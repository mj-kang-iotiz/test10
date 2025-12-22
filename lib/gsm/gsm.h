#ifndef GSM_H
#define GSM_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define GSM_PAYLOAD_SIZE 128
#define GSM_AT_CMD_PARAM_SIZE 64

// TCP 관련 정의
#define GSM_TCP_MAX_SOCKETS 2       ///< EC25는 최대 12개 소켓 지원
#define GSM_TCP_RX_BUFFER_SIZE 1500 ///< TCP RX 버퍼 (1460 + 여유)
#define GSM_TCP_TX_BUFFER_SIZE 1500 ///< TCP TX 버퍼 (1460 + 여유)

#define GSM_TCP_PBUF_MAX_LEN (16 * 1024) // 소켓당 최대 16KB

typedef struct gsm_s gsm_t;

typedef enum {
  GSM_CMD_NONE = 0,

  GSM_CMD_AT,      ///< AT 명령어 동작 확인용
  GSM_CMD_ATE,     ///< 에코 설정 (ATE0/ATE1)
  GSM_CMD_CMEE,    ///< 에러코드 결과 설정
  GSM_CMD_CGDCONT, ///< APN 설정
  GSM_CMD_CPIN,    ///< SIM 장착 확인
  GSM_CMD_COPS,    ///< 선택된 네트워크 OPERATOR 확인
  GSM_CMD_QPOWD,   ///< 전원 OFF (AT+QPOWD)

  // TCP
  GSM_CMD_QIOPEN,  ///< 소켓 open
  GSM_CMD_QICLOSE, ///< 소켓 close
  GSM_CMD_QISEND,  ///< 소켓 전송
  GSM_CMD_QIRD,    ///< 소켓 읽기
  GSM_CMD_QISDE,   ///< 소켓 데이터 에코 설정
  GSM_CMD_QISTATE, ///< 소켓 상태 조회

  // 설정
  GSM_CMD_QICFG,
  GSM_CMD_QCFG,
  GSM_CMD_MAX
} gsm_cmd_t;

typedef enum {
  GSM_EVT_NONE = 0,
  GSM_EVT_POWERED_DOWN,
  GSM_EVT_RDY,
  GSM_EVT_INIT_OK,       ///< LTE 초기화 완료 (APN 설정 포함)
  GSM_EVT_INIT_FAIL,     ///< LTE 초기화 실패
  GSM_EVT_TCP_CONNECTED, ///< TCP 연결 완료
  GSM_EVT_TCP_CLOSED,    ///< TCP 연결 종료
  GSM_EVT_TCP_DATA_RECV, ///< TCP 데이터 수신
  GSM_EVT_TCP_SEND_OK,   ///< TCP 전송 완료
  GSM_EVT_PDP_DEACT,
} gsm_evt_t;

typedef void (*urc_handler_t)(gsm_t *gsm, const char *data,
                              size_t len); ///< GSM resposne 핸들러
typedef void (*evt_handler_t)(
    gsm_evt_t evt, void *args); ///< GSM 이벤트 발생시, 사용하는 콜백함수

// TCP 패킷 버퍼 (lwcell의 pbuf 방식)
typedef struct tcp_pbuf_s {
  uint8_t *payload;        ///< 데이터 포인터
  size_t len;              ///< 현재 pbuf 데이터 길이
  size_t tot_len;          ///< 전체 체인 길이 (다음 pbuf 포함)
  struct tcp_pbuf_s *next; ///< 다음 pbuf
} tcp_pbuf_t;

typedef struct {
  const char *prefix;    ///< GSM 명령어
  urc_handler_t handler; ///< GSM 명령어 처리 핸들러
} urc_handler_entry_t;

typedef struct {
  gsm_cmd_t cmd;
  const char *at_str;
  const char *expected_resp;
  uint32_t timeout_ms;
} gsm_at_cmd_entry_t;

typedef enum {
  GSM_WAIT_NONE,
  GSM_WAIT_EXPECTED,
  GSM_WAIT_PROMPT,
} gsm_wait_type_t;

typedef enum {
  GSM_AT_EXECUTE,
  GSM_AT_WRITE, ///< =
  GSM_AT_READ,  ///< ?
  GSM_AT_TEST   ///< =?
} gsm_at_mode_t;

// AT 명령 콜백 함수 타입
// cmd: 실행된 명령어 (어떤 msg 필드를 사용할지 구분)
// msg: 파싱된 결과 (union 포인터, OK일 때만 유효)
// is_ok: 명령 성공 여부
//
// 사용 예시:
//   void my_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg, bool is_ok) {
//       if(!is_ok) { /* 에러 처리 */ return; }
//
//       gsm_msg_t *m = (gsm_msg_t*)msg;
//       switch(cmd) {
//           case GSM_CMD_COPS:
//               printf("Network: %s\n", m->cops.oper);
//               break;
//           case GSM_CMD_CPIN:
//               printf("SIM: %s\n", m->cpin.code);
//               break;
//       }
//   }
typedef void (*at_cmd_handler)(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                               bool is_ok);

typedef enum {
  GSM_PDP_TYPE_IP = 0,
  GSM_PDP_TYPE_PPP,
  GSM_PDP_TYPE_IPV6,
  GSM_PDP_TYPE_IPV4V6,
} gsm_pdp_type_t;

typedef struct {
  uint8_t cid;
  gsm_pdp_type_t type;
  char apn[32];
} gsm_pdp_context_t;

// ★ lwcell 방식: 각 명령어별 파싱 결과 저장 (Union으로 메모리 절약)
typedef union {
  // AT+CGDCONT? 결과 (여러 줄 가능)
  struct {
    gsm_pdp_context_t contexts[5]; // 최대 5개 PDP context
    size_t count;
  } cgdcont;

  // AT+COPS? 결과 (단일 줄)
  struct {
    uint8_t mode;
    uint8_t format;
    char oper[32];
    uint8_t act;
  } cops;

  // AT+CPIN? 결과
  struct {
    char code[16]; // "READY", "SIM PIN", etc.
  } cpin;

  // AT+QIOPEN 결과
  struct {
    uint8_t connect_id;
    int32_t result; // 0: 성공, 기타: 에러 코드
  } qiopen;

  // AT+QICLOSE 결과
  struct {
    uint8_t connect_id;
    int32_t result;
  } qiclose;

  // AT+QISEND 결과
  struct {
    uint8_t connect_id;
    uint16_t sent_length;
    uint16_t acked_length;
  } qisend;

  // AT+QIRD 결과
  struct {
    uint8_t connect_id;
    uint16_t read_actual_length;
    uint8_t *data; // TCP 버퍼를 가리킴
  } qird;

  // AT+QISTATE 결과
  struct {
    uint8_t connect_id;
    char service_type[8]; // "TCP", "UDP" 등
    char remote_ip[64];
    uint16_t remote_port;
    uint16_t local_port;
    uint8_t socket_state; // 0: Initial, 1: Opening, 2: Connected, 3: Listening,
                          // 4: Closing
    uint8_t context_id;
    uint8_t server_id;
    uint8_t access_mode;
    char at_port[16]; // "usbmodem", "uart1" 등
  } qistate;

} gsm_msg_t;

typedef struct {
  gsm_cmd_t cmd;
  char params[GSM_AT_CMD_PARAM_SIZE];
  at_cmd_handler callback; ///< 비동기식 명령어 완료시 콜백 (NULL이면 동기식)
  gsm_wait_type_t wait_type; ///< expected response 수신받아야할지 여부
  gsm_at_mode_t at_mode;
  SemaphoreHandle_t sem;
  uint32_t timeout_ms;

  gsm_msg_t msg; ///< 명령어별 파싱 결과

  // ★ TCP 전송용 데이터 (QISEND 전용)
  tcp_pbuf_t *tx_pbuf; ///< 전송할 데이터 (pbuf로 관리, 전송 완료 후 해제)
} gsm_at_cmd_t;

typedef struct {
  char data[GSM_PAYLOAD_SIZE]; ///< 수신받은 GSM 명령어 패킷
  size_t len;                  ///< 수신받은 GSM 명령어 패킷 길이
  uint8_t data_offset; ///< URC 등 명령어를 제외한 실제 데이터가 있는 위치
} gsm_recv_t;

typedef struct {
  evt_handler_t handler;
  void *args;
} gsm_evt_handler_t;

typedef struct {
  /* 일반 */
  bool is_ok;
  bool is_err;
  bool is_timeout;
  bool is_powerd;

  /* USIM */
  bool is_sim_rdy;

  /* 네트워크 */
  bool is_network_registered;

  /* TCP */
  bool is_send_rdy;
} gsm_status_t;

typedef struct {
  int (*init)(void);
  int (*reset)(void);
  int (*send)(const char *data, size_t len);
  int (*recv)(char *buf, size_t len);
} gsm_hal_ops_t;

typedef enum {
  GSM_CMEE_DISABLE = 0,
  GSM_CMEE_ENABLE_NUMERIC = 1,
  GSM_CMEE_ENABLE_VERBOSE = 2,
} gsm_cmee_mode_t;

// TCP 관련 콜백 타입
typedef void (*tcp_recv_callback_t)(uint8_t connect_id);
typedef void (*tcp_close_callback_t)(uint8_t connect_id);

// TCP 이벤트 타입
typedef enum {
  TCP_EVT_RECV_NOTIFY = 0, ///< +QIURC: "recv" 수신 알림
  TCP_EVT_CLOSED_NOTIFY,   ///< +QIURC: "closed" 종료 알림
  TCP_EVT_CONTINUE_READ, ///< EC25 버퍼 드레인 계속 (QIURC 방지용) ⭐ 추가
} tcp_event_type_t;

// TCP 이벤트 구조체
typedef struct {
  tcp_event_type_t type;
  uint8_t connect_id;
} tcp_event_t;

// TCP 소켓 상태
typedef enum {
  GSM_TCP_STATE_CLOSED = 0,
  GSM_TCP_STATE_OPENING,
  GSM_TCP_STATE_CONNECTED,
  GSM_TCP_STATE_CLOSING,
} gsm_tcp_state_t;

// TCP 소켓 구조체
typedef struct {
  uint8_t connect_id;    ///< 소켓 ID (0-11)
  gsm_tcp_state_t state; ///< 연결 상태
  char remote_ip[64];    ///< 원격 IP 주소
  uint16_t remote_port;  ///< 원격 포트
  uint16_t local_port;   ///< 로컬 포트

  // 수신 버퍼 큐 (lwcell 방식)
  tcp_pbuf_t *pbuf_head; ///< 수신 pbuf 체인 헤드
  tcp_pbuf_t *pbuf_tail; ///< 수신 pbuf 체인 테일
  size_t pbuf_total_len; ///< 큐에 쌓인 총 바이트

  // 콜백
  tcp_recv_callback_t on_recv;   ///< 데이터 수신 콜백
  tcp_close_callback_t on_close; ///< 연결 종료 콜백

  SemaphoreHandle_t open_sem;
  SemaphoreHandle_t close_sem;
} gsm_tcp_socket_t;

// TCP 버퍼 구조체
typedef struct {
  uint8_t rx_buf[GSM_TCP_RX_BUFFER_SIZE]; ///< RX 버퍼
  uint8_t tx_buf[GSM_TCP_TX_BUFFER_SIZE]; ///< TX 버퍼
  size_t rx_len;                          ///< RX 버퍼 사용 길이
  size_t tx_len;                          ///< TX 버퍼 사용 길이

  // QIRD 응답 파싱 중 상태
  volatile bool is_reading_data; ///< TCP 데이터 읽기 중 플래그
  size_t expected_data_len;      ///< 예상 데이터 길이
  size_t read_data_len;          ///< 읽은 데이터 길이
  uint8_t current_connect_id;    ///< 현재 읽기 중인 소켓 ID
} gsm_tcp_buffer_t;

// TCP 관리 구조체
typedef struct {
  gsm_tcp_socket_t sockets[GSM_TCP_MAX_SOCKETS]; ///< 소켓 배열
  gsm_tcp_buffer_t buffer;                       ///< TCP 전용 버퍼
  SemaphoreHandle_t tcp_mutex;                   ///< TCP 접근 뮤텍스

  // TCP 전용 태스크
  QueueHandle_t event_queue; ///< TCP 이벤트 큐
  TaskHandle_t task_handle;  ///< TCP 태스크 핸들
} gsm_tcp_t;

typedef struct gsm_s {
  gsm_recv_t recv;
  gsm_status_t status;

  gsm_evt_handler_t evt_handler;

  gsm_at_cmd_t *current_cmd; ///< 처리중인 AT 커맨드 (Producer Task 스택 변수를
                             ///< 직접 가리킴)
  SemaphoreHandle_t cmd_mutex; ///< current_cmd 접근 보호를 위한 뮤텍스
  SemaphoreHandle_t producer_sem; ///< Producer Task 응답 대기용 세마포어

  const gsm_hal_ops_t *ops;
  const gsm_at_cmd_entry_t *at_tbl;
  const urc_handler_entry_t *urc_stat_tbl;
  const urc_handler_entry_t *urc_info_tbl;
  QueueHandle_t at_cmd_queue;

  gsm_tcp_t tcp; ///< TCP 관리 구조체
} gsm_t;

void gsm_init(gsm_t *gsm, evt_handler_t handler, void *args);
void gsm_parse_process(gsm_t *gsm, const void *data, size_t len);

// TCP API 함수들
/**
 * @brief TCP 소켓 초기화
 *
 * @param gsm GSM 핸들
 */
void gsm_tcp_init(gsm_t *gsm);

/**
 * @brief TCP 소켓 오픈
 *
 * @param gsm GSM 핸들
 * @param connect_id 소켓 ID (0-11)
 * @param context_id PDP context ID
 * @param remote_ip 원격 IP 주소
 * @param remote_port 원격 포트
 * @param local_port 로컬 포트 (0이면 자동 할당)
 * @param on_recv 데이터 수신 콜백
 * @param on_close 연결 종료 콜백
 * @param callback AT 명령 완료 콜백
 * @return int 0: 성공, -1: 실패
 */
int gsm_tcp_open(gsm_t *gsm, uint8_t connect_id, uint8_t context_id,
                 const char *remote_ip, uint16_t remote_port,
                 uint16_t local_port, tcp_recv_callback_t on_recv,
                 tcp_close_callback_t on_close, at_cmd_handler callback);

/**
 * @brief TCP 소켓 닫기
 *
 * @param gsm GSM 핸들
 * @param connect_id 소켓 ID
 * @param callback AT 명령 완료 콜백
 * @return int 0: 성공, -1: 실패
 */
int gsm_tcp_close(gsm_t *gsm, uint8_t connect_id, at_cmd_handler callback);

int gsm_tcp_close_force(gsm_t *gsm, uint8_t connect_id);

/**
 * @brief TCP 데이터 전송
 *
 * @param gsm GSM 핸들
 * @param connect_id 소켓 ID
 * @param data 전송할 데이터
 * @param len 데이터 길이
 * @param callback AT 명령 완료 콜백
 * @return int 0: 성공, -1: 실패
 */
int gsm_tcp_send(gsm_t *gsm, uint8_t connect_id, const uint8_t *data,
                 size_t len, at_cmd_handler callback);

/**
 * @brief TCP 데이터 읽기
 *
 * @param gsm GSM 핸들
 * @param connect_id 소켓 ID
 * @param max_len 최대 읽기 길이
 * @param callback AT 명령 완료 콜백
 * @return int 0: 성공, -1: 실패
 */
int gsm_tcp_read(gsm_t *gsm, uint8_t connect_id, size_t max_len,
                 at_cmd_handler callback);

/**
 * @brief TCP 소켓 상태 조회
 *
 * @param gsm GSM 핸들
 * @param connect_id 소켓 ID
 * @return gsm_tcp_socket_t* 소켓 포인터 (NULL이면 유효하지 않은 ID)
 */
gsm_tcp_socket_t *gsm_tcp_get_socket(gsm_t *gsm, uint8_t connect_id);

// TCP pbuf 관리 함수들
/**
 * @brief pbuf 할당
 *
 * @param len 데이터 길이
 * @return tcp_pbuf_t* 할당된 pbuf (NULL이면 실패)
 */
tcp_pbuf_t *tcp_pbuf_alloc(size_t len);

/**
 * @brief pbuf 해제
 *
 * @param pbuf 해제할 pbuf
 */
void tcp_pbuf_free(tcp_pbuf_t *pbuf);

/**
 * @brief pbuf 체인 전체 해제
 *
 * @param pbuf 체인 헤드
 */
void tcp_pbuf_free_chain(tcp_pbuf_t *pbuf);

/**
 * @brief 소켓 pbuf 큐에 추가
 *
 * @param socket 소켓
 * @param pbuf 추가할 pbuf
 */
int tcp_pbuf_enqueue(gsm_tcp_socket_t *socket, tcp_pbuf_t *pbuf);

/**
 * @brief 소켓 pbuf 큐에서 꺼내기
 *
 * @param socket 소켓
 * @return tcp_pbuf_t* pbuf (NULL이면 큐가 비어있음)
 */
tcp_pbuf_t *tcp_pbuf_dequeue(gsm_tcp_socket_t *socket);

// AT 명령어 전송 함수들
/**
 * @brief AT 커맨드 전송 (범용)
 *
 * @param gsm GSM 핸들
 * @param cmd 명령어 타입
 * @param at_mode 명령어 모드
 * @param params 파라미터 문자열 (NULL이면 없음)
 * @param callback 완료 콜백 (NULL이면 동기식)
 */
void gsm_send_at_cmd(gsm_t *gsm, gsm_cmd_t cmd, gsm_at_mode_t at_mode,
                     const char *params, at_cmd_handler callback);

/**
 * @brief AT+CMEE 전송 (에러 보고 모드 설정)
 *
 * @param gsm GSM 핸들
 * @param at_mode 명령어 모드
 * @param cmee 에러 모드 (0: 비활성, 1: 숫자, 2: 상세)
 * @param callback 완료 콜백 (NULL이면 동기식)
 */
void gsm_send_at_cmee(gsm_t *gsm, gsm_at_mode_t at_mode, gsm_cmee_mode_t cmee,
                      at_cmd_handler callback);

/**
 * @brief AT+CGDCONT 전송 (APN 설정/조회)
 *
 * @param gsm GSM 핸들
 * @param at_mode 명령어 모드
 * @param ctx PDP context (WRITE 모드일 때만 필요)
 * @param callback 완료 콜백 (NULL이면 동기식)
 */
void gsm_send_at_cgdcont(gsm_t *gsm, gsm_at_mode_t at_mode,
                         gsm_pdp_context_t *ctx, at_cmd_handler callback);

/**
 * @brief ATE 전송 (에코 설정)
 *
 * @param gsm GSM 핸들
 * @param echo_on 에코 활성화 여부 (0: 비활성, 1: 활성)
 * @param callback 완료 콜백 (NULL이면 동기식)
 */

void gsm_send_at_ate(gsm_t *gsm, uint8_t echo_on, at_cmd_handler callback);

/**
 * @brief AT+QISDE 전송 (소켓 데이터 에코 설정)
 *
 * @param gsm GSM 핸들
 * @param at_mode 명령어 모드
 * @param echo_on 에코 활성화 여부 (0: 비활성, 1: 활성) - WRITE 모드일 때만 사용
 * @param callback 완료 콜백 (NULL이면 동기식)
 */

void gsm_send_at_qisde(gsm_t *gsm, gsm_at_mode_t at_mode, uint8_t echo_on,
                       at_cmd_handler callback);

/**
 * @brief AT+QISTATE 전송 (소켓 상태 조회)
 *
 * @param gsm GSM 핸들
 * @param query_type 0: 모든 소켓 조회, 1: 특정 소켓 조회
 * @param connect_id 소켓 ID (query_type=1일 때만 사용)
 * @param callback 완료 콜백 (NULL이면 동기식)
 */

void gsm_send_at_qistate(gsm_t *gsm, uint8_t query_type, uint8_t connect_id,
                         at_cmd_handler callback);

void gsm_send_at_qicfg_keepalive(gsm_t *gsm, uint8_t enable, uint16_t keepidle,
                                 uint16_t keepinterval, uint8_t keepcount,
                                 at_cmd_handler callback);

/**
 * @brief AT+QPOWD 전송 (전원 OFF)
 *
 * @param gsm GSM 핸들
 * @param mode 종료 모드 (0: 정상 종료, 1: 긴급 종료)
 * @param callback 완료 콜백 (NULL이면 동기식)
 */
void gsm_send_at_qpowd(gsm_t *gsm, uint8_t mode, at_cmd_handler callback);
void gsm_send_at_qcfg_airplanecontrol(gsm_t *gsm, uint8_t mode, at_cmd_handler callback);

#endif
