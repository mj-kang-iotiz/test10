#ifndef LTE_INIT_H
#define LTE_INIT_H

#include "FreeRTOS.h"
#include "gsm.h"
#include "timers.h"

/**
 * @brief LTE 초기화 상태
 */
typedef enum {
  LTE_INIT_IDLE = 0,
  LTE_INIT_AT_TEST,       // AT 테스트
  LTE_INIT_ECHO_OFF,      // ATE0 에코 비활성화
  LTE_INIT_CMEE_SET,      // AT+CMEE=2 설정
  LTE_INIT_QISDE_OFF,     // AT+QISDE=0 소켓 데이터 에코 비활성화
  LTE_INIT_AIRPLANE_CTRL_SET,
  LTE_INIT_KEEPALIVE_SET, // AT+QICFG keep-alive 설정
  LTE_INIT_CPIN_CHECK,    // AT+CPIN? SIM 확인
  LTE_INIT_APN_SET,       // AT+CGDCONT APN 설정
  LTE_INIT_APN_VERIFY,    // AT+CGDCONT? APN 확인
  LTE_INIT_NETWORK_CHECK, // AT+COPS? 네트워크 등록 확인
  LTE_INIT_DONE,          // 초기화 완료
  LTE_INIT_FAILED,        // 초기화 실패
} lte_init_state_t;

/**
 * @brief LTE 초기화 재시도 설정
 */
#define LTE_INIT_MAX_RETRY 3
#define LTE_NETWORK_CHECK_MAX_RETRY 20 // 네트워크 등록 최대 20회 (약 2분)
#define LTE_NETWORK_CHECK_INTERVAL_MS 6000 // 6초마다 체크

/**
 * @brief LTE 초기화 시작
 *
 * GSM 모듈이 RDY 상태가 되면 호출하여 LTE 초기화 시작
 */
void lte_init_start(void);

/**
 * @brief LTE 초기화 상태 조회
 *
 * @return lte_init_state_t 현재 초기화 상태
 */
lte_init_state_t lte_get_init_state(void);

/**
 * @brief LTE 초기화 재시도 카운터 조회
 *
 * @return uint8_t 현재 재시도 카운터
 */
uint8_t lte_get_retry_count(void);

/**
 * @brief 네트워크 체크 타이머 생성
 *
 * gsm_process_task에서 타이머를 생성하고 이 함수로 설정
 *
 * @param timer 생성된 타이머 핸들
 */
void lte_set_network_check_timer(TimerHandle_t timer);

/**
 * @brief GSM 핸들 설정
 *
 * LTE 초기화 모듈에서 사용할 GSM 핸들 설정
 *
 * @param gsm GSM 핸들 포인터
 */
void lte_set_gsm_handle(gsm_t *gsm);

/**
 * @brief 네트워크 체크 타이머 콜백
 *
 * FreeRTOS 타이머 콜백으로 사용 (gsm_process_task에서 타이머 생성 시 지정)
 *
 * @param xTimer 타이머 핸들
 */
void lte_network_check_timer_callback(TimerHandle_t xTimer);

void lte_reinit_from_apn(void);

void lte_reset_state(void);

lte_init_state_t lte_get_init_state();

#endif // LTE_INIT_H
