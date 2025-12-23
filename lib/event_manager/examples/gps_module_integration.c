/**
 ******************************************************************************
 * @file    gps_module_integration.c
 * @brief   GPS 모듈의 Event Manager 통합 예제
 * @details 실제 GPS 모듈에서 이벤트를 발행하는 방법
 ******************************************************************************
 */

#include "event_manager.h"
#include "gps.h"
#include "gps_app.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private variables ---------------------------------------------------------*/

// GPS 모듈에서 RTCM 데이터를 받기 위한 subscriber
static event_subscriber_t gps_rtcm_subscriber;

/* Private functions ---------------------------------------------------------*/

/**
 * @brief RTCM 데이터 수신 핸들러
 * @note  GSM/NTRIP에서 받은 RTCM 데이터를 GPS로 전달
 */
static void gps_rtcm_event_handler(const event_data_t *event, void *user_data) {
    if (event->topic != EVENT_RTCM_DATA_RECEIVED) {
        return;
    }

    // RTCM 데이터를 GPS 모듈로 전달
    if (event->data && event->data_len > 0) {
        // 실제 GPS UART로 RTCM 데이터 전송
        gps_inject_rtcm_data(event->data, event->data_len);

        // 또는 gps.h의 함수 사용
        // gps_parse_rtcm(event->data, event->data_len);
    }
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief GPS 모듈 초기화 (Event Manager 연동)
 */
void gps_module_event_init(void) {
    // RTCM 데이터 이벤트 구독 (최고 우선순위)
    event_manager_subscribe(
        &gps_rtcm_subscriber,
        EVENT_RTCM_DATA_RECEIVED,
        gps_rtcm_event_handler,
        NULL,
        0,  // 최고 우선순위 (RTCM은 GPS가 먼저 받아야 함)
        "GPS_RTCM_Handler"
    );
}

/**
 * @brief GPS 데이터 파싱 완료 시 이벤트 발행
 * @note  gps_app.c의 GPS 태스크에서 호출
 */
void gps_publish_position_update(const gps_position_t *position) {
    if (!position) {
        return;
    }

    // GPS 위치 업데이트 이벤트 발행
    event_manager_publish(
        EVENT_GPS_POSITION_UPDATED,
        (void*)position,
        sizeof(gps_position_t),
        (void*)"GPS_Module"
    );
}

/**
 * @brief GPS Fix 상태 변경 시 이벤트 발행
 */
void gps_publish_fix_status_changed(uint8_t old_fix, uint8_t new_fix) {
    typedef struct {
        uint8_t old_fix;
        uint8_t new_fix;
    } fix_status_t;

    static fix_status_t fix_status;
    fix_status.old_fix = old_fix;
    fix_status.new_fix = new_fix;

    event_manager_publish(
        EVENT_GPS_FIX_STATUS_CHANGED,
        &fix_status,
        sizeof(fix_status),
        (void*)"GPS_Module"
    );
}

/**
 * @brief GPS 태스크 예제 (실제 gps_app.c에 통합 가능)
 */
void gps_task_with_events(void *pvParameters) {
    gps_position_t position;
    uint8_t last_fix_type = 0;

    while (1) {
        // GPS 데이터 파싱 (기존 코드)
        // ... gps_parse() 등의 함수 호출 ...

        // GPS 데이터가 업데이트되면 이벤트 발행
        if (gps_get_position(&position) == GPS_OK) {
            gps_publish_position_update(&position);

            // Fix 상태가 변경되었으면 이벤트 발행
            if (position.fix_type != last_fix_type) {
                gps_publish_fix_status_changed(last_fix_type, position.fix_type);
                last_fix_type = position.fix_type;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief GPS 에러 발생 시 시스템 에러 이벤트 발행
 */
void gps_publish_error(uint32_t error_code, const char *error_msg) {
    typedef struct {
        const char *module_name;
        uint32_t error_code;
        const char *error_msg;
    } system_error_t;

    static system_error_t error;
    error.module_name = "GPS";
    error.error_code = error_code;
    error.error_msg = error_msg;

    event_manager_publish(
        EVENT_SYSTEM_ERROR,
        &error,
        sizeof(error),
        (void*)"GPS_Module"
    );
}
