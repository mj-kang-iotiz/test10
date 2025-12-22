#ifndef LORA_APP_H
#define LORA_APP_H

#include "lora.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief LoRa P2P 수신 데이터 구조체
 */
typedef struct {
  int16_t rssi;           // 수신 신호 강도 (dBm)
  int16_t snr;            // Signal-to-Noise Ratio (dB)
  uint16_t data_len;      // 데이터 길이
  char data[256];
} lora_p2p_recv_data_t;

/**
 * @brief RTCM fragment 재조립 버퍼
 */
#define RTCM_REASSEMBLY_BUF_SIZE 512
#define RTCM_REASSEMBLY_TIMEOUT_MS 5000  // 5초 타임아웃

typedef struct {
  
  uint8_t buffer[RTCM_REASSEMBLY_BUF_SIZE];  // 재조립 버퍼
  uint16_t buffer_pos;                        // 현재 버퍼 위치
  uint16_t expected_len;                      // 예상 RTCM 패킷 전체 길이
  bool has_header;                            // 헤더 수신 완료 여부
  TickType_t last_recv_tick;                  // 마지막 수신 시간
} rtcm_reassembly_t;

void lora_start_tx_test(void);
/**
 * @brief LoRa P2P 수신 콜백
 *
 * @param recv_data 수신 데이터
 * @param user_data 사용자 데이터
 */
typedef void (*lora_p2p_recv_callback_t)(lora_p2p_recv_data_t *recv_data, void *user_data);

/**
 * @brief LoRa 명령어 응답 콜백
 *
 * @param success true: OK, false: ERROR/TIMEOUT
 * @param user_data 사용자 데이터
 */
typedef void (*lora_command_callback_t)(bool success, void *user_data);

/**
 * @brief LoRa 명령어 요청 구조체
 */
typedef struct {
  char cmd[256];                  // 전송할 AT 명령어
  uint32_t timeout_ms;            // 타임아웃 (ms)
  uint32_t toa_ms;                // Time on Air (ms) - OK 응답 후 추가 대기 시간
  bool is_async;                  // true: 비동기, false: 동기
  bool skip_response;             // true: 응답 파싱 건너뛰기 (명령어만 전송)

  SemaphoreHandle_t response_sem; // 응답 대기용 세마포어
  bool *result;                   // 동기 응답 결과 (true: OK, false: ERROR/TIMEOUT)

  lora_command_callback_t callback; // 비동기 완료 콜백
  void *user_data;                  // 사용자 데이터
  bool async_result;                // 비동기 결과 저장용
} lora_cmd_request_t;

/**
 * @brief LoRa 작동 모드
 */
typedef enum {
  LORA_WORK_MODE_LORAWAN = 0,  // LoRaWAN 모드
  LORA_WORK_MODE_P2P = 1       // P2P 모드
} lora_work_mode_t;

/**
 * @brief LoRa P2P 전송 모드
 */
typedef enum {
  LORA_P2P_TRANSFER_MODE_RECEIVER = 1,   // Receiver mode (수신)
  LORA_P2P_TRANSFER_MODE_SENDER = 2      // Sender mode (송신, 기본값)
} lora_p2p_transfer_mode_t;

/**
 * @brief LoRa 인스턴스 초기화
 */
void lora_instance_init(void);

/**
 * @brief LoRa 명령어 전송 (동기)
 *
 * @param cmd AT 명령어 (예: "AT+SET_CONFIG=lora:work_mode:0\r\n")
 * @param timeout_ms 타임아웃 (ms)
 * @return true: OK, false: ERROR/TIMEOUT
 */
bool lora_send_command_sync(const char *cmd, uint32_t timeout_ms);

/**
 * @brief LoRa 명령어 전송 (비동기)
 *
 * @param cmd AT 명령어
 * @param timeout_ms 타임아웃 (ms)
 * @param toa_ms Time on Air (ms) - OK 응답 후 추가 대기 시간
 * @param callback 완료 콜백
 * @param user_data 사용자 데이터
 * @param skip_response true: 응답 파싱 건너뛰기, false: 정상 응답 대기
 * @return true: 요청 성공, false: 요청 실패
 */
bool lora_send_command_async(const char *cmd, uint32_t timeout_ms, uint32_t toa_ms,
                              lora_command_callback_t callback, void *user_data,
                              bool skip_response);

/**
 * @brief LoRa 작동 모드 설정
 *
 * @param mode 작동 모드 (P2P or LoRaWAN)
 * @param timeout_ms 타임아웃 (ms)
 * @return true: 성공, false: 실패
 */
bool lora_set_work_mode(lora_work_mode_t mode, uint32_t timeout_ms);

/**
 * @brief LoRa P2P 설정
 *
 * @param freq 주파수 (Hz) - 예: 868000000, 915000000
 * @param sf Spreading Factor (7~12)
 * @param bw Bandwidth (0:125kHz, 1:250kHz, 2:500kHz)
 * @param cr Coding Rate (1:4/5, 2:4/6, 3:4/7, 4:4/8)
 * @param preamlen Preamble Length (5~65535)
 * @param pwr TX Power (5~20 dBm)
 * @param timeout_ms 타임아웃 (ms)
 * @return true: 성공, false: 실패
 */
bool lora_set_p2p_config(uint32_t freq, uint8_t sf, uint8_t bw, uint8_t cr,
                         uint16_t preamlen, uint8_t pwr, uint32_t timeout_ms);

/**
 * @brief LoRa P2P 전송 모드 설정
 *
 * @param mode 전송 모드 (Event-driven or Continuous)
 * @param timeout_ms 타임아웃 (ms)
 * @return true: 성공, false: 실패
 */
bool lora_set_p2p_transfer_mode(lora_p2p_transfer_mode_t mode, uint32_t timeout_ms);

/**
 * @brief LoRa P2P 데이터 전송 (HEX string)
 *
 * @param data 전송할 데이터 (HEX string, 예: "48656C6C6F" = "Hello")
 * @param timeout_ms 타임아웃 (ms)
 * @return true: 성공, false: 실패
 */
bool lora_send_p2p_data(const char *data, uint32_t timeout_ms);

/**
 * @brief LoRa P2P Raw Binary 데이터 전송 (동기)
 *
 * Binary 데이터를 HEX ASCII로 변환하여 전송
 * 최대 118바이트까지 전송 가능 (HEX 변환 시 236 문자)
 *
 * @param data 전송할 raw binary 데이터
 * @param len 데이터 길이 (바이트, 최대 118)
 * @param timeout_ms 타임아웃 (ms)
 * @return true: 성공, false: 실패
 */
bool lora_send_p2p_raw(const uint8_t *data, size_t len, uint32_t timeout_ms);

/**
 * @brief LoRa P2P Raw Binary 데이터 전송 (비동기)
 *
 * Binary 데이터를 HEX ASCII로 변환하여 비동기 전송
 * 최대 118바이트까지 전송 가능 (HEX 변환 시 236 문자)
 *
 * @param data 전송할 raw binary 데이터
 * @param len 데이터 길이 (바이트, 최대 118)
 * @param timeout_ms 타임아웃 (ms)
 * @param callback 완료 콜백
 * @param user_data 사용자 데이터
 * @return true: 큐 추가 성공, false: 실패
 */
bool lora_send_p2p_raw_async(const uint8_t *data, size_t len, uint32_t timeout_ms,
                              lora_command_callback_t callback, void *user_data);

/**
 * @brief LoRa P2P 수신 콜백 등록
 *
 * @param callback 수신 콜백 (NULL이면 GPS로 자동 전송)
 * @param user_data 사용자 데이터
 */
void lora_set_p2p_recv_callback(lora_p2p_recv_callback_t callback, void *user_data);

/**
 * @brief LoRa 핸들 가져오기
 *
 * @return lora_t* LoRa 핸들
 */
lora_t *lora_get_handle(void);
void lora_instance_deinit(void);
void lora_start_rover(void);
#endif
