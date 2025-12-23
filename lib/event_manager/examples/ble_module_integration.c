/**
 ******************************************************************************
 * @file    ble_module_integration.c
 * @brief   BLE 모듈의 Event Manager 통합 예제
 ******************************************************************************
 */

#include "event_manager.h"
#include "ble_app.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private variables ---------------------------------------------------------*/

static event_subscriber_t ble_gps_subscriber;
static event_subscriber_t ble_gsm_subscriber;

/* Private functions ---------------------------------------------------------*/

/**
 * @brief GPS 데이터 수신 핸들러 (BLE notification 전송)
 */
static void ble_gps_event_handler(const event_data_t *event, void *user_data) {
    if (event->topic != EVENT_GPS_POSITION_UPDATED) {
        return;
    }

    // BLE가 연결되어 있으면 notification 전송
    if (ble_is_connected()) {
        ble_send_notification(BLE_CHAR_GPS_POSITION, event->data, event->data_len);
    }
}

/**
 * @brief GSM 연결 상태 변경 핸들러
 */
static void ble_gsm_event_handler(const event_data_t *event, void *user_data) {
    bool connected = (event->topic == EVENT_GSM_CONNECTED);

    // BLE Characteristic 업데이트
    uint8_t status = connected ? 1 : 0;
    ble_update_characteristic(BLE_CHAR_GSM_STATUS, &status, 1);
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief BLE 모듈 초기화 (Event Manager 연동)
 */
void ble_module_event_init(void) {
    // GPS 위치 이벤트 구독 (높은 우선순위 - 사용자에게 빠르게 전달)
    event_manager_subscribe(
        &ble_gps_subscriber,
        EVENT_GPS_POSITION_UPDATED,
        ble_gps_event_handler,
        NULL,
        5,  // 높은 우선순위
        "BLE_GPS_Handler"
    );

    // GSM 연결 상태 이벤트 구독
    event_manager_subscribe(
        &ble_gsm_subscriber,
        EVENT_GSM_CONNECTED,
        ble_gsm_event_handler,
        NULL,
        10,
        "BLE_GSM_Connected"
    );

    // GSM 연결 해제 이벤트도 같은 핸들러 사용
    static event_subscriber_t ble_gsm_disconnected_subscriber;
    event_manager_subscribe(
        &ble_gsm_disconnected_subscriber,
        EVENT_GSM_DISCONNECTED,
        ble_gsm_event_handler,
        NULL,
        10,
        "BLE_GSM_Disconnected"
    );
}

/**
 * @brief BLE 명령 수신 시 이벤트 발행
 */
void ble_publish_command(const uint8_t *cmd_data, size_t cmd_len) {
    event_manager_publish(
        EVENT_BLE_CMD_RECEIVED,
        (void*)cmd_data,
        cmd_len,
        (void*)"BLE_Module"
    );
}

/**
 * @brief BLE 연결/해제 이벤트 발행
 */
void ble_publish_connection_status(bool connected) {
    event_topic_t topic = connected ? EVENT_BLE_CONNECTED : EVENT_BLE_DISCONNECTED;

    event_manager_publish(
        topic,
        &connected,
        sizeof(bool),
        (void*)"BLE_Module"
    );
}
