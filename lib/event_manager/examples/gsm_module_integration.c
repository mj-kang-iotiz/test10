/**
 ******************************************************************************
 * @file    gsm_module_integration.c
 * @brief   GSM/NTRIP 모듈의 Event Manager 통합 예제
 ******************************************************************************
 */

#include "event_manager.h"
#include "gsm.h"
#include "gsm_app.h"
#include "ntrip_app.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private variables ---------------------------------------------------------*/

// GSM 연결 상태 추적
static bool gsm_is_connected = false;

/* Public functions ----------------------------------------------------------*/

/**
 * @brief GSM 연결 상태 변경 시 이벤트 발행
 */
void gsm_publish_connection_status(bool connected) {
    // 상태 변경이 있을 때만 발행
    if (gsm_is_connected != connected) {
        gsm_is_connected = connected;

        event_topic_t topic = connected ? EVENT_GSM_CONNECTED : EVENT_GSM_DISCONNECTED;

        event_manager_publish(
            topic,
            &connected,
            sizeof(bool),
            (void*)"GSM_Module"
        );
    }
}

/**
 * @brief NTRIP 데이터 수신 시 이벤트 발행
 * @note  ntrip_app.c의 NTRIP 태스크에서 호출
 */
void ntrip_publish_rtcm_data(const uint8_t *rtcm_data, size_t data_len) {
    if (!rtcm_data || data_len == 0) {
        return;
    }

    // RTCM 데이터 수신 이벤트 발행
    // 모든 구독자(GPS, LoRa 등)에게 전달됨
    uint32_t delivered = event_manager_publish(
        EVENT_RTCM_DATA_RECEIVED,
        (void*)rtcm_data,
        data_len,
        (void*)"NTRIP"
    );

    // 디버깅: 몇 개의 모듈에게 전달되었는지 확인
    // printf("[NTRIP] RTCM data delivered to %lu modules\n", delivered);
}

/**
 * @brief GSM UART 수신 ISR에서 RTCM 데이터 발행
 * @note  실제 UART ISR에서 호출
 */
void gsm_uart_rx_complete_callback(uint8_t *data, size_t len) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // ISR에서 이벤트 발행 (비동기)
    event_manager_publish_from_isr(
        EVENT_RTCM_DATA_RECEIVED,
        data,
        len,
        (void*)"GSM_UART",
        &xHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief NTRIP 태스크 예제 (실제 ntrip_app.c에 통합 가능)
 */
void ntrip_task_with_events(void *pvParameters) {
    uint8_t rtcm_buffer[512];
    size_t rtcm_len;

    while (1) {
        // NTRIP 서버에서 데이터 수신
        if (ntrip_receive_data(rtcm_buffer, sizeof(rtcm_buffer), &rtcm_len) == NTRIP_OK) {
            // RTCM 데이터 이벤트 발행
            ntrip_publish_rtcm_data(rtcm_buffer, rtcm_len);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief GSM 모듈 초기화 (Event Manager 연동)
 */
void gsm_module_event_init(void) {
    // GSM 모듈은 주로 발행자 역할
    // 필요시 다른 이벤트 구독 가능
}
