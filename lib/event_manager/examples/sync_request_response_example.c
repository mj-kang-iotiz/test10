/**
 ******************************************************************************
 * @file    sync_request_response_example.c
 * @brief   중앙관리 앱(Event Manager)의 동기 요청-응답 예제
 * @details GPS 초기화를 PubSub 시스템 내에서 동기적으로 처리
 ******************************************************************************
 */

#include "event_manager.h"
#include "event_manager_sync.h"
#include "gps.h"
#include "FreeRTOS.h"
#include "task.h"

/* ============================================================================
 * GPS 명령 핸들러 (GPS 모듈에서 구현)
 * ============================================================================ */

/**
 * @brief GPS 명령 타입
 */
typedef enum {
    GPS_CMD_SET_BAUDRATE = 1,
    GPS_CMD_CONFIGURE_MESSAGES,
    GPS_CMD_SET_RTK_MODE,
    GPS_CMD_RESET,
    GPS_CMD_SAVE_CONFIG
} gps_command_type_t;

/**
 * @brief GPS 명령 요청 데이터
 */
typedef struct {
    gps_command_type_t cmd_type;
    uint8_t params[64];
    size_t params_len;
} gps_command_request_t;

/**
 * @brief GPS 명령 응답 데이터
 */
typedef struct {
    bool success;
    uint8_t error_code;
    char message[32];
} gps_command_response_t;

/**
 * @brief GPS 명령 핸들러
 * @note  GPS 모듈(gps_app.c)에서 등록
 */
static bool gps_command_handler(event_request_t *request) {
    gps_command_request_t *cmd_req = (gps_command_request_t *)request->request_data;
    gps_command_response_t response = {0};

    if (!cmd_req) {
        event_manager_send_error(request);
        return false;
    }

    // 명령 처리
    switch (cmd_req->cmd_type) {
        case GPS_CMD_SET_BAUDRATE: {
            uint32_t baudrate = *(uint32_t *)cmd_req->params;

            // UBX-CFG-PRT 전송
            const uint8_t ubx_cfg_prt[] = {
                0xB5, 0x62, 0x06, 0x00, 0x14, 0x00,
                // ... baudrate 설정 ...
            };

            gps_uart_send(ubx_cfg_prt, sizeof(ubx_cfg_prt));
            vTaskDelay(pdMS_TO_TICKS(100));

            // UART baudrate 변경
            gps_uart_set_baudrate(baudrate);
            vTaskDelay(pdMS_TO_TICKS(100));

            response.success = true;
            strcpy(response.message, "Baudrate changed");
            break;
        }

        case GPS_CMD_CONFIGURE_MESSAGES: {
            // NMEA 비활성화, UBX-NAV-PVT 활성화
            gps_configure_output_messages();
            vTaskDelay(pdMS_TO_TICKS(200));

            response.success = true;
            strcpy(response.message, "Messages configured");
            break;
        }

        case GPS_CMD_SET_RTK_MODE: {
            uint8_t mode = cmd_req->params[0];  // 0=Rover, 1=Base

            if (mode == 1) {
                // Base 모드 설정
                const uint8_t cfg_tmode3[] = {/* ... */};
                gps_uart_send(cfg_tmode3, sizeof(cfg_tmode3));
            }

            vTaskDelay(pdMS_TO_TICKS(100));
            response.success = true;
            strcpy(response.message, "RTK mode set");
            break;
        }

        case GPS_CMD_RESET: {
            const uint8_t reset_cmd[] = {0xB5, 0x62, 0x06, 0x04, /* ... */};
            gps_uart_send(reset_cmd, sizeof(reset_cmd));
            vTaskDelay(pdMS_TO_TICKS(500));

            response.success = true;
            strcpy(response.message, "GPS reset");
            break;
        }

        case GPS_CMD_SAVE_CONFIG: {
            const uint8_t save_cmd[] = {0xB5, 0x62, 0x06, 0x09, /* ... */};
            gps_uart_send(save_cmd, sizeof(save_cmd));
            vTaskDelay(pdMS_TO_TICKS(500));

            response.success = true;
            strcpy(response.message, "Config saved");
            break;
        }

        default:
            response.success = false;
            response.error_code = 1;
            strcpy(response.message, "Unknown command");
            break;
    }

    // 응답 전송
    return event_manager_send_response(request, &response, sizeof(response));
}

/* ============================================================================
 * GPS 모듈 초기화 (GPS 측에서 핸들러 등록)
 * ============================================================================ */

/**
 * @brief GPS 모듈 초기화
 */
void gps_module_init_with_sync(void) {
    // GPS 명령 핸들러 등록
    event_manager_register_request_handler(
        EVENT_GPS_COMMAND_REQUEST,
        gps_command_handler
    );

    printf("[GPS] Command handler registered\n");
}

/* ============================================================================
 * 초기화 코드 (main.c initThread에서 호출)
 * ============================================================================ */

/**
 * @brief GPS 초기화 (동기 요청-응답 사용)
 * @retval true: 성공, false: 실패
 */
bool gps_initialize_with_sync_api(void) {
    gps_command_request_t cmd_req;
    gps_command_response_t cmd_resp;
    size_t resp_len;

    printf("[Init] Starting GPS initialization...\n");

    // === 1. Baudrate 설정 (9600 → 115200) ===
    cmd_req.cmd_type = GPS_CMD_SET_BAUDRATE;
    *(uint32_t *)cmd_req.params = 115200;
    cmd_req.params_len = 4;

    if (!event_manager_send_request(
            EVENT_GPS_COMMAND_REQUEST,
            &cmd_req, sizeof(cmd_req),
            &cmd_resp, sizeof(cmd_resp),
            &resp_len,
            1000)) {  // 1초 타임아웃
        printf("[Init] GPS baudrate setting failed!\n");
        return false;
    }

    if (!cmd_resp.success) {
        printf("[Init] GPS returned error: %s\n", cmd_resp.message);
        return false;
    }

    printf("[Init] ✓ Baudrate set to 115200\n");

    // === 2. 메시지 출력 설정 ===
    cmd_req.cmd_type = GPS_CMD_CONFIGURE_MESSAGES;
    cmd_req.params_len = 0;

    if (!event_manager_send_request(
            EVENT_GPS_COMMAND_REQUEST,
            &cmd_req, sizeof(cmd_req),
            &cmd_resp, sizeof(cmd_resp),
            &resp_len,
            1000)) {
        printf("[Init] GPS message config failed!\n");
        return false;
    }

    printf("[Init] ✓ Messages configured\n");

    // === 3. RTK 모드 설정 ===
    const board_config_t *config = board_get_config();
    if (config->board == BOARD_TYPE_BASE_F9P) {
        cmd_req.cmd_type = GPS_CMD_SET_RTK_MODE;
        cmd_req.params[0] = 1;  // Base mode
        cmd_req.params_len = 1;

        if (!event_manager_send_request(
                EVENT_GPS_COMMAND_REQUEST,
                &cmd_req, sizeof(cmd_req),
                &cmd_resp, sizeof(cmd_resp),
                &resp_len,
                1000)) {
            printf("[Init] GPS RTK mode setting failed!\n");
            return false;
        }

        printf("[Init] ✓ RTK Base mode set\n");
    }

    // === 4. 설정 저장 ===
    cmd_req.cmd_type = GPS_CMD_SAVE_CONFIG;
    cmd_req.params_len = 0;

    if (!event_manager_send_request(
            EVENT_GPS_COMMAND_REQUEST,
            &cmd_req, sizeof(cmd_req),
            &cmd_resp, sizeof(cmd_resp),
            &resp_len,
            2000)) {  // 저장은 2초 타임아웃
        printf("[Init] GPS config save failed!\n");
        return false;
    }

    printf("[Init] ✓ Configuration saved\n");

    // === 5. 초기화 완료 이벤트 발행 ===
    event_manager_publish(EVENT_GPS_INIT_COMPLETE, NULL, 0, (void*)"GPS_Init");

    printf("[Init] ✅ GPS initialization complete!\n");

    return true;
}

/* ============================================================================
 * main.c의 initThread 예제
 * ============================================================================ */

void initThread_with_sync_api(void *pvParameter) {
    const board_config_t *config = board_get_config();

    // === 1. Event Manager 초기화 ===
    if (!event_manager_init()) {
        Error_Handler();
    }

    // === 2. 동기 API 초기화 ===
    if (!event_manager_sync_init()) {
        Error_Handler();
    }

    printf("[System] Event Manager initialized\n");

    // === 3. LED 초기화 ===
    led_init();

    // === 4. GPS 모듈 초기화 (핸들러 등록) ===
    if (config->board == BOARD_TYPE_BASE_F9P ||
        config->board == BOARD_TYPE_BASE_UM982) {

        // GPS 하드웨어 초기화
        gps_uart_init();

        // GPS 명령 핸들러 등록
        gps_module_init_with_sync();

        // GPS 초기화 (동기 요청-응답 사용)
        if (!gps_initialize_with_sync_api()) {
            printf("[Error] GPS initialization failed!\n");
            Error_Handler();
        }

        // RTCM 데이터 구독 (초기화 완료 후)
        static event_subscriber_t rtcm_sub;
        event_manager_subscribe(&rtcm_sub, EVENT_RTCM_DATA_RECEIVED,
                               gps_rtcm_handler, NULL, 0, "GPS_RTCM");

        // LoRa 초기화
        lora_instance_init();
        lora_module_event_init();
    }

    // === 5. BLE 초기화 ===
    if (config->use_ble) {
        ble_init_all();
        ble_module_event_init();
    }

    vTaskDelete(NULL);
}

/* ============================================================================
 * 비교: 직접 호출 vs 동기 API
 * ============================================================================ */

/*
 * === 직접 호출 방식 ===
 * 장점:
 *   - 간단하고 직관적
 *   - 오버헤드 없음
 * 단점:
 *   - GPS 모듈과 직접 의존성
 *   - 모듈 교체 시 코드 수정 필요
 *
 * 코드:
 *   gps_set_baudrate(115200);
 *   gps_configure_messages();
 *
 * === 동기 API 방식 (이 예제) ===
 * 장점:
 *   - GPS 모듈과 독립적
 *   - 중앙에서 관리 (로깅, 재시도 등 추가 용이)
 *   - 모듈 교체 시 핸들러만 교체
 *   - 타임아웃 자동 처리
 * 단점:
 *   - 약간의 오버헤드
 *   - 구조가 복잡
 *
 * 코드:
 *   event_manager_send_request(EVENT_GPS_COMMAND_REQUEST, ...);
 *
 * === 결론 ===
 * - 간단한 프로젝트: 직접 호출
 * - 복잡한/확장 가능한 시스템: 동기 API
 */

/* ============================================================================
 * 추가 예제: 런타임 GPS 재설정
 * ============================================================================ */

/**
 * @brief 런타임 중 GPS baudrate 변경
 * @note  BLE 명령으로 받았을 때 등
 */
void change_gps_baudrate_runtime(uint32_t new_baudrate) {
    gps_command_request_t cmd_req;
    gps_command_response_t cmd_resp;
    size_t resp_len;

    cmd_req.cmd_type = GPS_CMD_SET_BAUDRATE;
    *(uint32_t *)cmd_req.params = new_baudrate;
    cmd_req.params_len = 4;

    if (event_manager_send_request(
            EVENT_GPS_COMMAND_REQUEST,
            &cmd_req, sizeof(cmd_req),
            &cmd_resp, sizeof(cmd_resp),
            &resp_len,
            1000)) {
        printf("[Runtime] GPS baudrate changed to %lu\n", new_baudrate);
    } else {
        printf("[Runtime] Failed to change GPS baudrate\n");
    }
}

/**
 * @brief BLE 명령 핸들러 예제
 */
static void ble_cmd_handler(const event_data_t *event, void *user_data) {
    if (event->topic != EVENT_BLE_CMD_RECEIVED) {
        return;
    }

    // BLE 명령 파싱
    uint8_t *cmd = (uint8_t *)event->data;

    if (cmd[0] == 0x01) {  // GPS Baudrate 변경 명령
        uint32_t baudrate = *(uint32_t *)&cmd[1];
        change_gps_baudrate_runtime(baudrate);
    }
}

/* ============================================================================
 * 응용: 여러 모듈 동기 요청 처리
 * ============================================================================ */

/**
 * @brief LoRa 모듈 명령 핸들러
 */
static bool lora_command_handler(event_request_t *request) {
    // LoRa 설정 처리
    // ...
    return true;
}

/**
 * @brief 시스템 전체 초기화 (모든 모듈 동기 처리)
 */
void system_full_init_with_sync(void) {
    // Event Manager 초기화
    event_manager_init();
    event_manager_sync_init();

    // 각 모듈 핸들러 등록
    event_manager_register_request_handler(EVENT_GPS_COMMAND_REQUEST,
                                          gps_command_handler);
    event_manager_register_request_handler(EVENT_LORA_COMMAND_REQUEST,
                                          lora_command_handler);

    // 모든 모듈 동기적으로 초기화
    printf("[System] Initializing all modules...\n");

    if (!gps_initialize_with_sync_api()) {
        printf("[Error] GPS init failed\n");
        return;
    }

    // LoRa, BLE 등도 동일한 방식으로...

    printf("[System] ✅ All modules initialized!\n");
}
