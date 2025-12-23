/**
 ******************************************************************************
 * @file    gps_f9p_init_example.c
 * @brief   실전: GPS F9P 초기화 예제
 ******************************************************************************
 */

#include "event_manager.h"
#include "gps.h"
#include "gps_app.h"
#include "FreeRTOS.h"
#include "task.h"

/* GPS 초기화 함수 (직접 호출 방식 - 권장) */

/**
 * @brief GPS F9P 초기화
 * @retval true: 성공, false: 실패
 */
bool gps_f9p_initialize(void) {
    // === 1. 하드웨어 리셋 ===
    HAL_GPIO_WritePin(GPS_RESET_GPIO_Port, GPS_RESET_Pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
    HAL_GPIO_WritePin(GPS_RESET_GPIO_Port, GPS_RESET_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(1000));  // 부팅 대기

    // === 2. Baudrate 변경 (9600 → 115200) ===
    const uint8_t ubx_cfg_prt_115200[] = {
        0xB5, 0x62,              // UBX Header
        0x06, 0x00,              // CFG-PRT
        0x14, 0x00,              // Length = 20 bytes
        0x01,                    // Port ID = 1 (UART1)
        0x00,                    // Reserved
        0x00, 0x00,              // TX Ready
        0xD0, 0x08, 0x00, 0x00,  // Mode: 8N1
        0x00, 0xC2, 0x01, 0x00,  // Baudrate: 115200
        0x07, 0x00,              // Input protocols: UBX+NMEA+RTCM3
        0x03, 0x00,              // Output protocols: UBX+NMEA
        0x00, 0x00,              // Flags
        0x00, 0x00,              // Reserved
        0xB0, 0x94               // Checksum
    };

    // 9600 baud로 명령 전송
    gps_uart_send(ubx_cfg_prt_115200, sizeof(ubx_cfg_prt_115200));
    vTaskDelay(pdMS_TO_TICKS(100));

    // UART를 115200으로 변경
    gps_uart_set_baudrate(115200);
    vTaskDelay(pdMS_TO_TICKS(100));

    // === 3. NMEA 메시지 비활성화 (UBX만 사용) ===
    const uint8_t disable_nmea_gga[] = {
        0xB5, 0x62, 0x06, 0x01, 0x08, 0x00,
        0xF0, 0x00,  // NMEA GGA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x23
    };
    gps_uart_send(disable_nmea_gga, sizeof(disable_nmea_gga));
    vTaskDelay(pdMS_TO_TICKS(50));

    // 다른 NMEA 메시지도 비활성화...
    // (GLL, GSA, GSV, RMC, VTG, ZDA 등)

    // === 4. UBX-NAV-PVT 활성화 (위치/속도/시간) ===
    const uint8_t enable_nav_pvt[] = {
        0xB5, 0x62, 0x06, 0x01, 0x08, 0x00,
        0x01, 0x07,  // NAV-PVT
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00,  // UART1: 1Hz
        0x18, 0xE1
    };
    gps_uart_send(enable_nav_pvt, sizeof(enable_nav_pvt));
    vTaskDelay(pdMS_TO_TICKS(50));

    // === 5. RTK 모드 설정 ===
    const uint8_t cfg_tmode3_base[] = {
        0xB5, 0x62, 0x06, 0x71, 0x28, 0x00,  // CFG-TMODE3
        0x02, 0x00,  // Mode: Survey-In
        0x00, 0x00,  // Flags
        // ... 나머지 파라미터 ...
    };

    const board_config_t *config = board_get_config();
    if (config->board == BOARD_TYPE_BASE_F9P) {
        // Base 모드
        gps_uart_send(cfg_tmode3_base, sizeof(cfg_tmode3_base));
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        // Rover 모드 (기본값 사용)
    }

    // === 6. Configuration 저장 (Flash) ===
    const uint8_t save_config[] = {
        0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00,
        0x00, 0x00, 0x00, 0x00,  // Clear mask
        0xFF, 0xFF, 0x00, 0x00,  // Save mask
        0x00, 0x00, 0x00, 0x00,  // Load mask
        0x17,                    // Device: BBR, Flash, EEPROM, SPI Flash
        0x5F, 0xDC               // Checksum
    };
    gps_uart_send(save_config, sizeof(save_config));
    vTaskDelay(pdMS_TO_TICKS(500));  // 저장 완료 대기

    // === 7. 초기화 완료 이벤트 발행 ===
    printf("[GPS] Initialization complete\n");
    event_manager_publish(EVENT_GPS_INIT_COMPLETE, NULL, 0, (void*)"GPS_F9P");

    // === 8. RTCM 데이터 구독 시작 ===
    static event_subscriber_t rtcm_sub;
    event_manager_subscribe(&rtcm_sub, EVENT_RTCM_DATA_RECEIVED,
                           gps_rtcm_handler, NULL, 0, "GPS_RTCM");

    return true;
}

/**
 * @brief RTCM 데이터 핸들러
 */
static void gps_rtcm_handler(const event_data_t *event, void *user_data) {
    if (event->data && event->data_len > 0) {
        // RTCM 데이터를 GPS UART로 전송
        gps_uart_send(event->data, event->data_len);
    }
}

/**
 * @brief initThread에서 호출
 */
void gps_init_in_init_thread(void) {
    // GPS 하드웨어 초기화
    gps_uart_init();

    // GPS F9P 설정
    if (!gps_f9p_initialize()) {
        printf("[GPS] Initialization failed!\n");
        // 에러 처리
    }

    // GPS 태스크 생성
    xTaskCreate(gps_task, "GPS", 512, NULL, tskIDLE_PRIORITY + 3, NULL);
}

/**
 * @brief GPS 태스크 (이벤트 발행)
 */
void gps_task(void *pvParameters) {
    gps_position_t position;
    uint8_t last_fix_type = 0;

    while (1) {
        // GPS 데이터 파싱
        if (gps_parse_ubx_nav_pvt(&position) == GPS_OK) {
            // 위치 업데이트 이벤트 발행
            event_manager_publish(EVENT_GPS_POSITION_UPDATED,
                                 &position, sizeof(position),
                                 (void*)"GPS");

            // Fix 상태 변경 시 이벤트 발행
            if (position.fix_type != last_fix_type) {
                typedef struct { uint8_t old; uint8_t new; } fix_t;
                static fix_t fix_status;
                fix_status.old = last_fix_type;
                fix_status.new = position.fix_type;

                event_manager_publish(EVENT_GPS_FIX_STATUS_CHANGED,
                                     &fix_status, sizeof(fix_status),
                                     (void*)"GPS");

                last_fix_type = position.fix_type;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
