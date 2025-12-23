/**
 ******************************************************************************
 * @file    lora_module_integration.c
 * @brief   LoRa 모듈의 Event Manager 통합 예제
 * @details GPS 데이터와 RTCM 데이터를 LoRa로 전송
 ******************************************************************************
 */

#include "event_manager.h"
#include "lora.h"
#include "lora_app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Private typedef -----------------------------------------------------------*/

typedef struct {
    uint8_t data[256];
    size_t len;
    event_topic_t topic;
} lora_tx_queue_item_t;

/* Private variables ---------------------------------------------------------*/

// LoRa subscriber들
static event_subscriber_t lora_gps_subscriber;
static event_subscriber_t lora_rtcm_subscriber;

// LoRa 전송 큐
static QueueHandle_t lora_tx_queue = NULL;

/* Private functions ---------------------------------------------------------*/

/**
 * @brief GPS 데이터 수신 핸들러
 * @note  GPS 위치 데이터를 LoRa로 전송
 */
static void lora_gps_event_handler(const event_data_t *event, void *user_data) {
    if (event->topic != EVENT_GPS_POSITION_UPDATED) {
        return;
    }

    // GPS 데이터를 큐에 추가 (LoRa 전송은 별도 태스크에서)
    lora_tx_queue_item_t item;
    item.topic = event->topic;
    item.len = event->data_len < sizeof(item.data) ? event->data_len : sizeof(item.data);
    memcpy(item.data, event->data, item.len);

    xQueueSend(lora_tx_queue, &item, 0);
}

/**
 * @brief RTCM 데이터 수신 핸들러
 * @note  Base 스테이션인 경우 RTCM을 LoRa로 브로드캐스트
 */
static void lora_rtcm_event_handler(const event_data_t *event, void *user_data) {
    if (event->topic != EVENT_RTCM_DATA_RECEIVED) {
        return;
    }

    // Base 스테이션인 경우에만 처리
    const board_config_t *config = board_get_config();
    if (config->board != BOARD_TYPE_BASE_F9P && config->board != BOARD_TYPE_BASE_UM982) {
        return;
    }

    // RTCM 데이터를 큐에 추가
    lora_tx_queue_item_t item;
    item.topic = event->topic;
    item.len = event->data_len < sizeof(item.data) ? event->data_len : sizeof(item.data);
    memcpy(item.data, event->data, item.len);

    xQueueSend(lora_tx_queue, &item, 0);
}

/**
 * @brief LoRa 전송 태스크
 */
static void lora_tx_task(void *pvParameters) {
    lora_tx_queue_item_t item;

    while (1) {
        if (xQueueReceive(lora_tx_queue, &item, portMAX_DELAY) == pdTRUE) {
            // LoRa로 데이터 전송
            lora_send_data(item.data, item.len);

            // 전송 완료 이벤트 발행
            event_manager_publish(
                EVENT_LORA_TX_COMPLETE,
                &item.len,
                sizeof(size_t),
                (void*)"LoRa_Module"
            );
        }
    }
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief LoRa 모듈 초기화 (Event Manager 연동)
 */
void lora_module_event_init(void) {
    // 전송 큐 생성
    lora_tx_queue = xQueueCreate(10, sizeof(lora_tx_queue_item_t));
    if (lora_tx_queue == NULL) {
        return;
    }

    // GPS 위치 업데이트 이벤트 구독
    event_manager_subscribe(
        &lora_gps_subscriber,
        EVENT_GPS_POSITION_UPDATED,
        lora_gps_event_handler,
        NULL,
        10,
        "LoRa_GPS_Handler"
    );

    // RTCM 데이터 이벤트 구독 (Base 스테이션용)
    event_manager_subscribe(
        &lora_rtcm_subscriber,
        EVENT_RTCM_DATA_RECEIVED,
        lora_rtcm_event_handler,
        NULL,
        20,  // GPS보다 낮은 우선순위
        "LoRa_RTCM_Handler"
    );

    // LoRa 전송 태스크 생성
    xTaskCreate(lora_tx_task, "LoRa_TX", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
}

/**
 * @brief LoRa 수신 완료 시 이벤트 발행
 * @note  LoRa 인터럽트 핸들러에서 호출
 */
void lora_rx_complete_callback(uint8_t *data, size_t len) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // LoRa 수신 이벤트 발행 (ISR에서)
    event_manager_publish_from_isr(
        EVENT_LORA_RX_COMPLETE,
        data,
        len,
        (void*)"LoRa_Module",
        &xHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief 배터리 부족 시 LoRa 이벤트 비활성화
 */
void lora_low_power_mode(bool enable) {
    if (enable) {
        // 저전력 모드: GPS 이벤트만 비활성화 (RTCM은 유지)
        event_manager_set_active(&lora_gps_subscriber, false);
    } else {
        // 정상 모드: 모든 이벤트 활성화
        event_manager_set_active(&lora_gps_subscriber, true);
    }
}
