/**
 ******************************************************************************
 * @file    sync_command_pattern.c
 * @brief   PubSub에서 동기적 명령-응답 패턴 구현
 * @details GPS 초기화처럼 응답을 기다려야 하는 경우
 ******************************************************************************
 */

#include "event_manager.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ============================================================================
 * 패턴 1: 직접 호출 (초기화용 - 가장 권장)
 * ============================================================================ */

/**
 * @brief GPS 초기화 - 직접 호출 방식
 * @note  초기화는 순서가 중요하므로 PubSub 사용 안 함
 */
void gps_init_direct_call(void) {
    // 1. 하드웨어 초기화 (직접 호출)
    gps_hw_reset();
    vTaskDelay(pdMS_TO_TICKS(100));

    gps_uart_init(9600);  // 초기 baudrate
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. GPS 설정 명령 전송 (UBX config)
    gps_send_ubx_config();
    vTaskDelay(pdMS_TO_TICKS(500));  // 설정 적용 대기

    // 3. Baudrate 변경
    gps_set_baudrate(115200);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. 메시지 설정
    gps_configure_output_messages();

    // 5. 초기화 완료 - 이벤트로 알림
    event_manager_publish(EVENT_GPS_INIT_COMPLETE, NULL, 0, (void*)"GPS");

    // 6. 이제 RTCM 데이터 구독 시작
    static event_subscriber_t rtcm_sub;
    event_manager_subscribe(&rtcm_sub, EVENT_RTCM_DATA_RECEIVED,
                           gps_rtcm_handler, NULL, 0, "GPS_RTCM");
}

/* ============================================================================
 * 패턴 2: 이벤트 + Semaphore (동기 대기)
 * ============================================================================ */

/**
 * @brief GPS 명령-응답 컨텍스트
 */
typedef struct {
    SemaphoreHandle_t response_sem;  // 응답 대기용
    bool success;                    // 성공 여부
    uint32_t timeout_ms;             // 타임아웃
    void *response_data;             // 응답 데이터
} gps_cmd_context_t;

static gps_cmd_context_t gps_cmd_ctx;

/**
 * @brief GPS 명령 이벤트 데이터
 */
typedef struct {
    uint8_t cmd_type;        // 명령 타입
    uint8_t *cmd_data;       // 명령 데이터
    size_t cmd_len;          // 데이터 길이
    gps_cmd_context_t *ctx;  // 컨텍스트 (응답용)
} gps_command_t;

// GPS 명령 이벤트 (새로 추가 필요)
// event_manager.h에 EVENT_GPS_COMMAND_REQUEST 추가

/**
 * @brief GPS 태스크에서 명령 처리
 */
static event_subscriber_t gps_cmd_sub;

static void gps_command_handler(const event_data_t *event, void *user_data) {
    if (event->topic != EVENT_GPS_COMMAND_REQUEST) {
        return;
    }

    gps_command_t *cmd = (gps_command_t *)event->data;

    // 명령 처리
    bool result = false;

    switch (cmd->cmd_type) {
        case GPS_CMD_SET_BAUDRATE:
            result = gps_send_ubx_cfg_prt(cmd->cmd_data, cmd->cmd_len);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case GPS_CMD_CONFIGURE_MSG:
            result = gps_send_ubx_cfg_msg(cmd->cmd_data, cmd->cmd_len);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case GPS_CMD_RESET:
            result = gps_send_reset();
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
    }

    // 응답 전달
    if (cmd->ctx) {
        cmd->ctx->success = result;
        xSemaphoreGive(cmd->ctx->response_sem);  // 대기 해제
    }
}

/**
 * @brief GPS 명령 전송 (동기 대기)
 * @param cmd_type 명령 타입
 * @param cmd_data 명령 데이터
 * @param cmd_len 데이터 길이
 * @param timeout_ms 타임아웃 (ms)
 * @retval true: 성공, false: 실패
 */
bool gps_send_command_sync(uint8_t cmd_type, uint8_t *cmd_data,
                           size_t cmd_len, uint32_t timeout_ms) {
    // 1. Semaphore 생성 (Binary Semaphore)
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    if (!sem) {
        return false;
    }

    // 2. 컨텍스트 설정
    gps_cmd_context_t ctx = {
        .response_sem = sem,
        .success = false,
        .timeout_ms = timeout_ms
    };

    // 3. 명령 데이터 준비
    gps_command_t cmd = {
        .cmd_type = cmd_type,
        .cmd_data = cmd_data,
        .cmd_len = cmd_len,
        .ctx = &ctx
    };

    // 4. 명령 이벤트 발행
    event_manager_publish(EVENT_GPS_COMMAND_REQUEST,
                         &cmd, sizeof(cmd),
                         (void*)"GPS_Init");

    // 5. 응답 대기 (동기)
    BaseType_t result = xSemaphoreTake(sem, pdMS_TO_TICKS(timeout_ms));

    // 6. Semaphore 정리
    vSemaphoreDelete(sem);

    // 7. 결과 반환
    return (result == pdTRUE) && ctx.success;
}

/**
 * @brief GPS 초기화 - 이벤트 + Semaphore 방식
 */
bool gps_init_with_events(void) {
    // GPS 명령 핸들러 구독
    event_manager_subscribe(&gps_cmd_sub, EVENT_GPS_COMMAND_REQUEST,
                           gps_command_handler, NULL, 0, "GPS_CMD");

    // 1. Baudrate 설정
    uint8_t baudrate_cfg[] = {0xB5, 0x62, ...};  // UBX-CFG-PRT
    if (!gps_send_command_sync(GPS_CMD_SET_BAUDRATE,
                               baudrate_cfg, sizeof(baudrate_cfg), 1000)) {
        return false;  // 실패
    }

    // 2. 메시지 설정
    uint8_t msg_cfg[] = {0xB5, 0x62, ...};  // UBX-CFG-MSG
    if (!gps_send_command_sync(GPS_CMD_CONFIGURE_MSG,
                               msg_cfg, sizeof(msg_cfg), 1000)) {
        return false;
    }

    // 3. 초기화 완료 이벤트
    event_manager_publish(EVENT_GPS_INIT_COMPLETE, NULL, 0, NULL);

    return true;
}

/* ============================================================================
 * 패턴 3: 플래그 + 타임아웃 (간단한 동기화)
 * ============================================================================ */

/**
 * @brief GPS 상태 플래그
 */
typedef struct {
    volatile bool config_done;
    volatile bool baudrate_changed;
    volatile bool msg_configured;
} gps_init_flags_t;

static gps_init_flags_t gps_flags = {0};

/**
 * @brief GPS 설정 완료 핸들러
 */
static void gps_config_done_handler(const event_data_t *event, void *user_data) {
    uint8_t *config_type = (uint8_t *)event->data;

    switch (*config_type) {
        case GPS_CFG_BAUDRATE:
            gps_flags.baudrate_changed = true;
            break;
        case GPS_CFG_MESSAGES:
            gps_flags.msg_configured = true;
            break;
        case GPS_CFG_COMPLETE:
            gps_flags.config_done = true;
            break;
    }
}

/**
 * @brief GPS 초기화 - 플래그 대기 방식
 */
bool gps_init_with_flags(void) {
    static event_subscriber_t config_sub;

    // 설정 완료 이벤트 구독
    event_manager_subscribe(&config_sub, EVENT_GPS_CONFIG_DONE,
                           gps_config_done_handler, NULL, 0, "GPS_CFG");

    // 1. Baudrate 설정 명령 발행
    uint8_t cfg_type = GPS_CFG_BAUDRATE;
    event_manager_publish(EVENT_GPS_COMMAND_REQUEST, &cfg_type, 1, NULL);

    // 2. 완료 플래그 대기 (타임아웃 포함)
    uint32_t timeout = 100;  // 100 * 10ms = 1초
    while (!gps_flags.baudrate_changed && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout--;
    }

    if (!gps_flags.baudrate_changed) {
        return false;  // 타임아웃
    }

    // 3. 메시지 설정
    cfg_type = GPS_CFG_MESSAGES;
    event_manager_publish(EVENT_GPS_COMMAND_REQUEST, &cfg_type, 1, NULL);

    timeout = 100;
    while (!gps_flags.msg_configured && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout--;
    }

    return gps_flags.msg_configured;
}

/* ============================================================================
 * 패턴 4: 상태 머신 (비동기, 복잡한 초기화용)
 * ============================================================================ */

typedef enum {
    GPS_INIT_STATE_IDLE,
    GPS_INIT_STATE_HW_RESET,
    GPS_INIT_STATE_WAIT_RESET,
    GPS_INIT_STATE_SET_BAUDRATE,
    GPS_INIT_STATE_WAIT_BAUDRATE,
    GPS_INIT_STATE_CONFIG_MESSAGES,
    GPS_INIT_STATE_WAIT_CONFIG,
    GPS_INIT_STATE_DONE,
    GPS_INIT_STATE_ERROR
} gps_init_state_t;

static gps_init_state_t gps_init_state = GPS_INIT_STATE_IDLE;
static uint32_t state_timeout_ms = 0;

/**
 * @brief GPS 초기화 상태 머신 (태스크에서 실행)
 */
void gps_init_state_machine_task(void *pvParameters) {
    while (1) {
        switch (gps_init_state) {
            case GPS_INIT_STATE_IDLE:
                // 초기화 시작 이벤트 대기
                break;

            case GPS_INIT_STATE_HW_RESET:
                // HW 리셋 명령 발행
                event_manager_publish(EVENT_GPS_COMMAND_REQUEST,
                                     &(uint8_t){GPS_CMD_RESET}, 1, NULL);
                gps_init_state = GPS_INIT_STATE_WAIT_RESET;
                state_timeout_ms = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
                break;

            case GPS_INIT_STATE_WAIT_RESET:
                // 리셋 완료 대기
                if (gps_flags.config_done ||
                    xTaskGetTickCount() > state_timeout_ms) {
                    gps_init_state = GPS_INIT_STATE_SET_BAUDRATE;
                }
                break;

            case GPS_INIT_STATE_SET_BAUDRATE:
                // Baudrate 설정
                event_manager_publish(EVENT_GPS_COMMAND_REQUEST,
                                     &(uint8_t){GPS_CMD_SET_BAUDRATE}, 1, NULL);
                gps_init_state = GPS_INIT_STATE_WAIT_BAUDRATE;
                state_timeout_ms = xTaskGetTickCount() + pdMS_TO_TICKS(500);
                break;

            case GPS_INIT_STATE_WAIT_BAUDRATE:
                if (gps_flags.baudrate_changed ||
                    xTaskGetTickCount() > state_timeout_ms) {
                    gps_init_state = GPS_INIT_STATE_CONFIG_MESSAGES;
                }
                break;

            case GPS_INIT_STATE_CONFIG_MESSAGES:
                // 메시지 설정
                event_manager_publish(EVENT_GPS_COMMAND_REQUEST,
                                     &(uint8_t){GPS_CMD_CONFIGURE_MSG}, 1, NULL);
                gps_init_state = GPS_INIT_STATE_WAIT_CONFIG;
                state_timeout_ms = xTaskGetTickCount() + pdMS_TO_TICKS(500);
                break;

            case GPS_INIT_STATE_WAIT_CONFIG:
                if (gps_flags.msg_configured ||
                    xTaskGetTickCount() > state_timeout_ms) {
                    gps_init_state = GPS_INIT_STATE_DONE;
                }
                break;

            case GPS_INIT_STATE_DONE:
                // 초기화 완료 이벤트 발행
                event_manager_publish(EVENT_GPS_INIT_COMPLETE, NULL, 0, NULL);
                gps_init_state = GPS_INIT_STATE_IDLE;
                break;

            case GPS_INIT_STATE_ERROR:
                // 에러 처리
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ============================================================================
 * 실전 예제: GPS F9P 초기화
 * ============================================================================ */

/**
 * @brief 실제 GPS F9P 초기화 (권장 방법)
 */
bool gps_f9p_init_recommended(void) {
    // === 방법 1: 직접 호출 (가장 간단하고 안정적) ===

    // 1. 하드웨어 리셋
    HAL_GPIO_WritePin(GPS_RESET_GPIO_Port, GPS_RESET_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
    HAL_GPIO_WritePin(GPS_RESET_GPIO_Port, GPS_RESET_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Baudrate 설정 (9600 → 115200)
    const uint8_t ubx_cfg_prt[] = {
        0xB5, 0x62,  // Header
        0x06, 0x00,  // CFG-PRT
        0x14, 0x00,  // Length
        0x01,        // Port ID (UART1)
        0x00,        // Reserved
        0x00, 0x00,  // TX Ready
        0xD0, 0x08, 0x00, 0x00,  // Mode (8N1)
        0x00, 0xC2, 0x01, 0x00,  // Baudrate (115200)
        0x07, 0x00,  // Input protocols
        0x03, 0x00,  // Output protocols
        0x00, 0x00,  // Flags
        0x00, 0x00,  // Reserved
        // Checksum
        0xXX, 0xXX
    };

    gps_send_ubx_command(ubx_cfg_prt, sizeof(ubx_cfg_prt));
    vTaskDelay(pdMS_TO_TICKS(100));

    // UART baudrate 변경
    gps_uart_set_baudrate(115200);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. 메시지 출력 설정
    gps_configure_output_messages();
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. RTK 모드 설정 (F9P)
    gps_configure_rtk_mode();
    vTaskDelay(pdMS_TO_TICKS(100));

    // 5. 초기화 완료 - 이벤트 발행
    event_manager_publish(EVENT_GPS_INIT_COMPLETE, NULL, 0, (void*)"GPS_F9P");

    // 6. RTCM 데이터 구독 시작
    static event_subscriber_t rtcm_sub;
    event_manager_subscribe(&rtcm_sub, EVENT_RTCM_DATA_RECEIVED,
                           gps_rtcm_handler, NULL, 0, "GPS_RTCM");

    return true;
}

/* ============================================================================
 * 요약: 언제 어떤 방식을 사용할까?
 * ============================================================================ */

/*
 * 1. 직접 호출 (패턴 1) - ⭐ 가장 권장
 *    - 초기화처럼 순서가 중요한 경우
 *    - 간단하고 명확
 *    - 초기화 완료 후 이벤트로 알림
 *
 * 2. 이벤트 + Semaphore (패턴 2)
 *    - 런타임 중 GPS 재설정이 필요한 경우
 *    - 다른 모듈에서 GPS 설정 변경 요청
 *    - 복잡하지만 유연함
 *
 * 3. 플래그 + 타임아웃 (패턴 3)
 *    - 간단한 동기화만 필요한 경우
 *    - Semaphore 오버헤드가 부담스러운 경우
 *
 * 4. 상태 머신 (패턴 4)
 *    - 복잡한 다단계 초기화
 *    - 재시도 로직이 필요한 경우
 *    - 비동기 처리 필요
 */

// ===== 결론 =====
// GPS 초기화는 패턴 1 (직접 호출)을 사용하고,
// 초기화 완료 후 PubSub으로 RTCM 데이터 등을 처리하는 것이 가장 좋습니다!
