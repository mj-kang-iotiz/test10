#include "lte_init.h"
#include "gsm.h"
#include <string.h>

#ifndef TAG
  #define TAG "GSM_INIT"
#endif

#include "log.h"

// LTE 초기화 상태 및 재시도 카운터
static lte_init_state_t lte_init_state = LTE_INIT_IDLE;
static uint8_t lte_init_retry_count = 0;
static uint8_t lte_network_check_count = 0;
static TimerHandle_t lte_network_check_timer = NULL;
static gsm_t *gsm_handle_ptr = NULL;

// 내부 콜백 함수 선언
static void lte_init_fail_with_retry(const char *error_msg);
static void lte_at_test_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                 bool is_ok);
static void lte_echo_off_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                  bool is_ok);
static void lte_cmee_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                  bool is_ok);
static void lte_qisde_off_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                   bool is_ok);
static void lte_airplane_ctrl_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                           bool is_ok);
static void lte_cpin_check_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                    bool is_ok);
static void lte_apn_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                 bool is_ok);
static void lte_apn_verify_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                    bool is_ok);
static void lte_network_check_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                       bool is_ok);
static void lte_keepalive_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                       bool is_ok);

/**
 * @brief GSM 핸들 설정
 */
void lte_set_gsm_handle(gsm_t *gsm) { gsm_handle_ptr = gsm; }

/**
 * @brief 네트워크 체크 타이머 설정
 */
void lte_set_network_check_timer(TimerHandle_t timer) {
  lte_network_check_timer = timer;
}

/**
 * @brief LTE 초기화 상태 조회
 */
lte_init_state_t lte_get_init_state(void) { return lte_init_state; }

/**
 * @brief LTE 초기화 재시도 카운터 조회
 */
uint8_t lte_get_retry_count(void) { return lte_init_retry_count; }

/**
 * @brief 초기화 실패 시 재시도 로직
 */
static void lte_init_fail_with_retry(const char *error_msg) {
  lte_init_retry_count++;

  if (lte_init_retry_count < LTE_INIT_MAX_RETRY) {
    // 소프트웨어 재시도 (1~3회)
    LOG_WARN("%s (소프트웨어 재시도 %d/%d)", error_msg, lte_init_retry_count,
             LTE_INIT_MAX_RETRY);

    // 1초 대기 후 재시작
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 초기화 재시작
    lte_init_state = LTE_INIT_IDLE;
    lte_init_start();
  } else if (lte_init_retry_count == LTE_INIT_MAX_RETRY) {
    // 하드웨어 리셋 시도 (4번째)
    LOG_ERR("%s (최대 재시도 초과)", error_msg);
    // EC25 모듈 하드웨어 리셋 (HAL ops 콜백 사용)
    if (gsm_handle_ptr && gsm_handle_ptr->ops && gsm_handle_ptr->ops->reset) {
      gsm_handle_ptr->ops->reset();
      LOG_INFO("EC25 모듈 리셋 완료");
    } else {
      LOG_ERR("리셋 함수가 설정되지 않음");
    }

    lte_init_retry_count++; // 4로 증가
    lte_init_state = LTE_INIT_IDLE;

    // 네트워크 체크 카운터도 리셋
    lte_network_check_count = 0;

    // RDY 이벤트가 올 때까지 대기하므로 여기서는 lte_init_start() 호출 안 함
    // RDY 이벤트 핸들러에서 자동으로 초기화 시작됨
  } else {
    // 모듈 리셋 후에도 실패 (최종 실패)
    LOG_ERR("%s (모듈 리셋 후에도 실패)", error_msg);
    lte_init_state = LTE_INIT_FAILED;

    if (gsm_handle_ptr && gsm_handle_ptr->evt_handler.handler) {
      gsm_handle_ptr->evt_handler.handler(GSM_EVT_INIT_FAIL, NULL);
    }
  }
}

/**
 * @brief LTE 초기화 시작
 */
void lte_init_start(void) {
  if (lte_init_state == LTE_INIT_IDLE) {
    LOG_INFO("LTE 초기화 시작 (시도 %d/%d)", lte_init_retry_count + 1,
             LTE_INIT_MAX_RETRY);
  }

  if (!gsm_handle_ptr) {
    LOG_ERR("GSM 핸들이 설정되지 않음");
    return;
  }

  lte_init_state = LTE_INIT_AT_TEST;

  gsm_send_at_cmd(gsm_handle_ptr, GSM_CMD_AT, GSM_AT_EXECUTE, NULL,
                  lte_at_test_callback);
}

/**
 * @brief AT 테스트 완료 콜백
 */
static void lte_at_test_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                 bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("AT 통신 실패");
    return;
  }

  LOG_INFO("AT 통신 성공");

  lte_init_state = LTE_INIT_ECHO_OFF;

  gsm_send_at_ate(gsm, 0, lte_echo_off_callback);
}

/**

 * @brief ATE0 에코 비활성화 완료 콜백

 */

static void lte_echo_off_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                  bool is_ok)

{

  if (!is_ok) {
    lte_init_fail_with_retry("ATE0 설정 실패");
    return;
  }

  LOG_INFO("ATE0 OK");
  lte_init_state = LTE_INIT_CMEE_SET;

  gsm_send_at_cmee(gsm, GSM_AT_WRITE, GSM_CMEE_ENABLE_VERBOSE,
                   lte_cmee_set_callback);
}

/**
 * @brief AT+CMEE=2 설정 완료 콜백
 */
static void lte_cmee_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                  bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("AT+CMEE=2 설정 실패");

    return;
  }

  LOG_INFO("AT+CMEE=2 OK");

  lte_init_state = LTE_INIT_QISDE_OFF;

  gsm_send_at_qisde(gsm, GSM_AT_WRITE, 0, lte_qisde_off_callback);
}

/**
 * @brief AT+QISDE=0 소켓 데이터 에코 비활성화 완료 콜백
 */
static void lte_qisde_off_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                   bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("AT+QISDE=0 설정 실패");
    return;
  }

  LOG_INFO("AT+QISDE=0 OK");
  lte_init_state = LTE_INIT_AIRPLANE_CTRL_SET;

  gsm_send_at_qcfg_airplanecontrol(gsm, 1, lte_airplane_ctrl_set_callback);
}

static void lte_airplane_ctrl_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                           bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("AT+QCFG=\"airplanecontrol\" 설정 실패");
    return;
  }

  LOG_INFO("Airplane mode GPIO control OK");
  lte_init_state = LTE_INIT_KEEPALIVE_SET;

  gsm_send_at_qicfg_keepalive(gsm, 1, 2, 30, 3, lte_keepalive_set_callback);

}

/**
 * @brief AT+CPIN? 확인 완료 콜백
 */
static void lte_cpin_check_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                    bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("AT+CPIN? 실패");
    return;
  }

  // SIM 상태 확인
  gsm_msg_t *m = (gsm_msg_t *)msg;
  if (m && strlen(m->cpin.code) > 0) {

    if (strcmp(m->cpin.code, "READY") != 0) {
      LOG_WARN("SIM 인식 불가: %s", m->cpin.code);
      // READY가 아니어도 계속 진행 (SIM PIN 입력 등은 별도 처리)
    }
  } else {
    LOG_INFO("SIM 상태 확인 완료");
  }

  lte_init_state = LTE_INIT_APN_SET;


  gsm_pdp_context_t ctx = {
      .cid = 1, .type = GSM_PDP_TYPE_IP, .apn = "m2m-router.lguplus.co.kr"};
  gsm_send_at_cgdcont(gsm, GSM_AT_WRITE, &ctx, lte_apn_set_callback); // internet.lguplus.co.kr
}

/**
 * @brief APN 설정 완료 콜백
 */
static void lte_apn_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                 bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("APN 설정 실패");
    return;
  }

  LOG_INFO("APN 설정 OK");
  lte_init_state = LTE_INIT_APN_VERIFY;

  gsm_send_at_cgdcont(gsm, GSM_AT_READ, NULL, lte_apn_verify_callback);
}

/**
 * @brief APN 확인 완료 콜백
 */
static void lte_apn_verify_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                    bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("APN 확인 실패");
    return;
  }

  // APN 설정 확인
  gsm_msg_t *m = (gsm_msg_t *)msg;
  if (m && m->cgdcont.count > 0) {
    for (size_t i = 0; i < m->cgdcont.count; i++) {
      gsm_pdp_context_t *ctx = &m->cgdcont.contexts[i];
      LOG_INFO("CID %d: type=%d, apn=%s", ctx->cid, ctx->type, ctx->apn);
    }

    // 설정한 APN이 맞는지 확인
    gsm_pdp_context_t *ctx = &m->cgdcont.contexts[0];
    if (ctx->cid == 1 && strcmp(ctx->apn, "m2m-router.lguplus.co.kr") == 0) { // internet.lguplus.co.kr
      LOG_INFO("APN 등록 완료: %s", ctx->apn);
      lte_init_state = LTE_INIT_NETWORK_CHECK;

      lte_network_check_count = 0;
      gsm_send_at_cmd(gsm, GSM_CMD_COPS, GSM_AT_READ, NULL,
                      lte_network_check_callback);
      return;
    } else {
      LOG_WARN("APN 불일치: cid=%d, apn=%s", ctx->cid, ctx->apn);
    }
  }

  // APN 확인 실패
  lte_init_fail_with_retry("APN 확인 실패");
}

/**
 * @brief 네트워크 등록 확인 콜백
 */
static void lte_network_check_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                       bool is_ok) {
  lte_network_check_count++;

  if (!is_ok) {
    if (lte_network_check_count >= LTE_NETWORK_CHECK_MAX_RETRY) {
      lte_init_fail_with_retry("apn 등록 확인 실패");
      return;
    }

    if (lte_network_check_timer != NULL) {
      xTimerStart(lte_network_check_timer, 0);
    }

    return;
  }

  // 네트워크 상태 확인
  gsm_msg_t *m = (gsm_msg_t *)msg;
  if (m && strlen(m->cops.oper) > 0) {
    LOG_INFO("apn 등록: %s (mode=%d, act=%d)", m->cops.oper,
             m->cops.mode, m->cops.act);

    lte_init_state = LTE_INIT_DONE;
    lte_init_retry_count = 0;
    lte_network_check_count = 0;

    if (lte_network_check_timer != NULL) {
      xTimerStop(lte_network_check_timer, 0);
    }

    LOG_INFO("LTE 초기화 완료, 네트워크: %s", m->cops.oper);

    // 초기화 완료 이벤트
    if (gsm && gsm->evt_handler.handler) {
      gsm->evt_handler.handler(GSM_EVT_INIT_OK, NULL);
    }
    return;
  }

  // 네트워크 미등록 (재시도)
  LOG_INFO("네트워크 미등록 (%d/%d) 재시도...",
           lte_network_check_count, LTE_NETWORK_CHECK_MAX_RETRY);

  // 최대 재시도 횟수 확인
  if (lte_network_check_count >= LTE_NETWORK_CHECK_MAX_RETRY) {
    lte_init_fail_with_retry("네트워크 등록 실패");
    return;
  }

  // 타이머로 재시도 (FreeRTOS 타이머는 스레드 안전)
  if (lte_network_check_timer != NULL) {
    xTimerStart(lte_network_check_timer, 0);
  }
}

/**
 * @brief 네트워크 등록 체크 타이머 콜백
 */
void lte_network_check_timer_callback(TimerHandle_t xTimer) {
  if (!gsm_handle_ptr) {
    LOG_ERR("GSM 핸들이 설정되지 않음");
    return;
  }

  gsm_send_at_cmd(gsm_handle_ptr, GSM_CMD_COPS, GSM_AT_READ, NULL,
                  lte_network_check_callback);
}

static void lte_keepalive_set_callback(gsm_t *gsm, gsm_cmd_t cmd, void *msg,
                                       bool is_ok) {
  if (!is_ok) {
    lte_init_fail_with_retry("AT+QICFG keep-alive 설정 실패");
    return;
  }

  LOG_INFO("TCP keep-alive OK");
  lte_init_state = LTE_INIT_CPIN_CHECK;

  gsm_send_at_cmd(gsm, GSM_CMD_CPIN, GSM_AT_READ, NULL,
                  lte_cpin_check_callback);
}

void lte_reinit_from_apn(void) {
  if (!gsm_handle_ptr) {
    LOG_ERR("GSM 핸들이 설정되지 않음");
    return;
  }

  // 재시도 카운터 초기화 (pdpdeact는 네트워크 이슈이므로 별도 관리)
  lte_init_retry_count = 0;
  lte_network_check_count = 0;

  // APN 설정 단계로 이동
  lte_init_state = LTE_INIT_APN_SET;

  // APN 설정 시작
  LOG_INFO("AT+CGDCONT APN 설정");
  gsm_pdp_context_t ctx = {
      .cid = 1, .type = GSM_PDP_TYPE_IP, .apn = "m2m-router.lguplus.co.kr"};
  gsm_send_at_cgdcont(gsm_handle_ptr, GSM_AT_WRITE, &ctx, lte_apn_set_callback);
}

void lte_reset_state(void) {

  LOG_INFO("LTE 상태 리셋");

  if (lte_network_check_timer != NULL) {
    xTimerStop(lte_network_check_timer, 0);
    LOG_INFO("네트워크 체크 타이머 중지");
  }

  if (gsm_handle_ptr && gsm_handle_ptr->at_cmd_queue) {
    gsm_at_cmd_t dummy_cmd;
    while (xQueueReceive(gsm_handle_ptr->at_cmd_queue, &dummy_cmd, 0) == pdTRUE) {
      LOG_DEBUG("AT 명령 큐 비우기");
    }
  }

  if (gsm_handle_ptr && gsm_handle_ptr->producer_sem) {
    xSemaphoreGive(gsm_handle_ptr->producer_sem);
  }

  lte_init_state = LTE_INIT_IDLE;
  lte_init_retry_count = 0;
  lte_network_check_count = 0;
}
