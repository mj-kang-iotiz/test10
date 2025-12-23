/**
 ******************************************************************************
 * @file    example_usage.c
 * @brief   Event Manager 사용 예제
 ******************************************************************************
 */

#include "event_manager.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

/* ============================================================================
 * 예제 1: GPS 모듈에서 이벤트 발행
 * ============================================================================ */

/**
 * @brief GPS 데이터 구조체
 */
typedef struct {
    double latitude;
    double longitude;
    float altitude;
    uint8_t fix_type;
} gps_data_t;

/**
 * @brief GPS 태스크 (발행자 역할)
 */
void gps_task_example(void *pvParameters) {
    gps_data_t gps_data;

    while (1) {
        // GPS 데이터 수신 대기...
        vTaskDelay(pdMS_TO_TICKS(1000));

        // GPS 데이터 업데이트
        gps_data.latitude = 37.5665;
        gps_data.longitude = 126.9780;
        gps_data.altitude = 38.5f;
        gps_data.fix_type = 3; // RTK Fixed

        // 이벤트 발행 (동기 방식)
        uint32_t delivered = event_manager_publish(
            EVENT_GPS_DATA_READY,
            &gps_data,
            sizeof(gps_data),
            (void*)"GPS_Task"
        );

        printf("[GPS] Event published to %lu subscribers\n", delivered);
    }
}

/* ============================================================================
 * 예제 2: LoRa 모듈에서 GPS 이벤트 구독
 * ============================================================================ */

/**
 * @brief LoRa GPS 이벤트 핸들러 (구독자)
 */
void lora_gps_event_handler(const event_data_t *event, void *user_data) {
    if (event->topic == EVENT_GPS_DATA_READY) {
        gps_data_t *gps = (gps_data_t *)event->data;

        printf("[LoRa] GPS event received: lat=%.6f, lon=%.6f, alt=%.2f\n",
               gps->latitude, gps->longitude, gps->altitude);

        // LoRa로 GPS 데이터 전송
        // lora_send_gps_data(gps);
    }
}

/**
 * @brief LoRa 초기화 (구독자 등록)
 */
static event_subscriber_t lora_gps_subscriber;

void lora_init_example(void) {
    // GPS 이벤트 구독 (우선순위 10)
    bool success = event_manager_subscribe(
        &lora_gps_subscriber,
        EVENT_GPS_DATA_READY,
        lora_gps_event_handler,
        NULL,  // user_data
        10,    // priority
        "LoRa_GPS_Handler"
    );

    if (success) {
        printf("[LoRa] Subscribed to GPS events\n");
    }
}

/* ============================================================================
 * 예제 3: BLE 모듈에서 GPS 이벤트 구독 (높은 우선순위)
 * ============================================================================ */

/**
 * @brief BLE GPS 이벤트 핸들러
 */
void ble_gps_event_handler(const event_data_t *event, void *user_data) {
    gps_data_t *gps = (gps_data_t *)event->data;

    printf("[BLE] GPS event received (high priority!)\n");

    // BLE로 GPS 데이터 전송
    // ble_send_notification(gps);
}

static event_subscriber_t ble_gps_subscriber;

void ble_init_example(void) {
    // GPS 이벤트 구독 (우선순위 5 - LoRa보다 먼저 실행)
    event_manager_subscribe(
        &ble_gps_subscriber,
        EVENT_GPS_DATA_READY,
        ble_gps_event_handler,
        NULL,
        5,  // LoRa(10)보다 우선순위 높음
        "BLE_GPS_Handler"
    );
}

/* ============================================================================
 * 예제 4: GSM 모듈에서 RTCM 데이터 발행 (ISR에서)
 * ============================================================================ */

/**
 * @brief UART ISR 핸들러 예제 (RTCM 데이터 수신)
 */
void UART_IRQHandler_Example(void) {
    static uint8_t rtcm_buffer[256];
    static size_t rtcm_len = 0;

    // UART에서 데이터 수신
    // ... RTCM 데이터를 rtcm_buffer에 저장 ...

    if (rtcm_len > 0) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // ISR에서 이벤트 발행
        event_manager_publish_from_isr(
            EVENT_RTCM_DATA_RECEIVED,
            rtcm_buffer,
            rtcm_len,
            (void*)"GSM_UART",
            &xHigherPriorityTaskWoken
        );

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* ============================================================================
 * 예제 5: 여러 모듈이 RTCM 데이터 구독
 * ============================================================================ */

/**
 * @brief GPS 모듈 - RTCM 데이터 핸들러
 */
void gps_rtcm_handler(const event_data_t *event, void *user_data) {
    printf("[GPS] RTCM data received: %zu bytes\n", event->data_len);
    // GPS 모듈로 RTCM 데이터 전달
    // gps_inject_rtcm(event->data, event->data_len);
}

/**
 * @brief LoRa 모듈 - RTCM 데이터 핸들러 (Base 스테이션용)
 */
void lora_rtcm_handler(const event_data_t *event, void *user_data) {
    printf("[LoRa] RTCM data received, broadcasting...\n");
    // LoRa로 RTCM 데이터 브로드캐스트
    // lora_broadcast_rtcm(event->data, event->data_len);
}

static event_subscriber_t gps_rtcm_subscriber;
static event_subscriber_t lora_rtcm_subscriber;

void multi_subscriber_example(void) {
    // GPS 모듈이 RTCM 구독 (최고 우선순위)
    event_manager_subscribe(
        &gps_rtcm_subscriber,
        EVENT_RTCM_DATA_RECEIVED,
        gps_rtcm_handler,
        NULL,
        0,  // 최고 우선순위
        "GPS_RTCM_Handler"
    );

    // LoRa 모듈도 RTCM 구독 (낮은 우선순위)
    event_manager_subscribe(
        &lora_rtcm_subscriber,
        EVENT_RTCM_DATA_RECEIVED,
        lora_rtcm_handler,
        NULL,
        20,  // GPS 처리 후 실행
        "LoRa_RTCM_Handler"
    );
}

/* ============================================================================
 * 예제 6: 동적 구독 활성화/비활성화
 * ============================================================================ */

void dynamic_subscription_example(void) {
    // 특정 조건에서 구독자 비활성화
    if (/* 배터리 부족 조건 */ false) {
        printf("[System] Low battery, disabling LoRa events\n");
        event_manager_set_active(&lora_gps_subscriber, false);
    }

    // 나중에 다시 활성화
    if (/* 배터리 회복 */ true) {
        printf("[System] Battery recovered, enabling LoRa events\n");
        event_manager_set_active(&lora_gps_subscriber, true);
    }

    // 완전히 구독 해제
    event_manager_unsubscribe(&lora_gps_subscriber);
}

/* ============================================================================
 * 예제 7: 사용자 데이터 활용
 * ============================================================================ */

/**
 * @brief 여러 LoRa 인스턴스를 위한 핸들러
 */
typedef struct {
    uint8_t lora_id;
    uint32_t packet_count;
} lora_context_t;

void lora_multi_instance_handler(const event_data_t *event, void *user_data) {
    lora_context_t *ctx = (lora_context_t *)user_data;
    ctx->packet_count++;

    printf("[LoRa-%d] Event received, total packets: %lu\n",
           ctx->lora_id, ctx->packet_count);
}

static event_subscriber_t lora1_subscriber;
static event_subscriber_t lora2_subscriber;
static lora_context_t lora1_ctx = {.lora_id = 1, .packet_count = 0};
static lora_context_t lora2_ctx = {.lora_id = 2, .packet_count = 0};

void multi_instance_example(void) {
    // LoRa 인스턴스 1
    event_manager_subscribe(
        &lora1_subscriber,
        EVENT_GPS_DATA_READY,
        lora_multi_instance_handler,
        &lora1_ctx,  // user_data로 컨텍스트 전달
        10,
        "LoRa1_Handler"
    );

    // LoRa 인스턴스 2
    event_manager_subscribe(
        &lora2_subscriber,
        EVENT_GPS_DATA_READY,
        lora_multi_instance_handler,
        &lora2_ctx,  // 각 인스턴스마다 다른 컨텍스트
        15,
        "LoRa2_Handler"
    );
}

/* ============================================================================
 * 예제 8: 통계 조회
 * ============================================================================ */

void print_statistics_example(void) {
    event_manager_stats_t stats;
    event_manager_get_stats(&stats);

    printf("\n=== Event Manager Statistics ===\n");
    printf("Total Published: %lu\n", stats.total_published);
    printf("Total Delivered: %lu\n", stats.total_delivered);
    printf("Total Subscribers: %lu\n", stats.total_subscribers);
    printf("Max Subscribers/Topic: %lu\n", stats.max_subscribers_per_topic);

    // 특정 토픽의 구독자 수
    uint32_t gps_subscribers = event_manager_get_subscriber_count(EVENT_GPS_DATA_READY);
    printf("GPS event subscribers: %lu\n", gps_subscribers);
}

/* ============================================================================
 * 예제 9: 에러 처리 이벤트
 * ============================================================================ */

typedef struct {
    const char *module_name;
    uint32_t error_code;
    const char *error_msg;
} system_error_t;

void system_error_handler(const event_data_t *event, void *user_data) {
    system_error_t *error = (system_error_t *)event->data;

    printf("[ERROR] Module: %s, Code: %lu, Msg: %s\n",
           error->module_name,
           error->error_code,
           error->error_msg);

    // 에러 로그 저장, LED 표시 등
}

static event_subscriber_t error_subscriber;

void error_handling_example(void) {
    // 시스템 에러 이벤트 구독
    event_manager_subscribe(
        &error_subscriber,
        EVENT_SYSTEM_ERROR,
        system_error_handler,
        NULL,
        0,
        "Error_Handler"
    );

    // 에러 발생 시 발행
    system_error_t error = {
        .module_name = "GPS",
        .error_code = 0x1001,
        .error_msg = "Communication timeout"
    };

    event_manager_publish(
        EVENT_SYSTEM_ERROR,
        &error,
        sizeof(error),
        (void*)"GPS_Module"
    );
}

/* ============================================================================
 * 예제 10: 전체 시스템 초기화 예제
 * ============================================================================ */

void system_init_example(void) {
    // 1. Event Manager 초기화
    if (!event_manager_init()) {
        printf("Failed to initialize Event Manager!\n");
        return;
    }

    // 2. 각 모듈 초기화 및 이벤트 구독
    lora_init_example();
    ble_init_example();
    multi_subscriber_example();

    // 3. 에러 핸들러 등록
    event_manager_subscribe(
        &error_subscriber,
        EVENT_SYSTEM_ERROR,
        system_error_handler,
        NULL,
        0,
        "System_Error_Handler"
    );

    // 4. 태스크 생성
    xTaskCreate(gps_task_example, "GPS", 512, NULL, tskIDLE_PRIORITY + 2, NULL);

    printf("System initialized with Event Manager!\n");
}

/* ============================================================================
 * 예제 11: 체이닝 (한 이벤트가 다른 이벤트를 발행)
 * ============================================================================ */

void gps_fix_changed_handler(const event_data_t *event, void *user_data) {
    uint8_t *fix_type = (uint8_t *)event->data;

    printf("[System] GPS Fix changed to: %d\n", *fix_type);

    // GPS Fix 상태가 변경되면 파라미터 변경 이벤트 발행
    event_manager_publish(
        EVENT_PARAM_CHANGED,
        "gps_fix_type",
        13,  // strlen("gps_fix_type")
        (void*)"GPS_Fix_Handler"
    );
}

/* ============================================================================
 * 완전한 사용 예제 - GPS + LoRa + BLE 통합
 * ============================================================================ */

/**
 * @brief 실제 프로젝트에서 사용할 수 있는 통합 예제
 */

// 전역 subscriber 선언
static event_subscriber_t g_gps_rtcm_sub;
static event_subscriber_t g_lora_gps_sub;
static event_subscriber_t g_lora_rtcm_sub;
static event_subscriber_t g_ble_gps_sub;
static event_subscriber_t g_system_error_sub;

void real_world_example_init(void) {
    // Event Manager 초기화
    event_manager_init();

    /* GPS 모듈 구독 설정 */
    event_manager_subscribe(&g_gps_rtcm_sub, EVENT_RTCM_DATA_RECEIVED,
                           gps_rtcm_handler, NULL, 0, "GPS_RTCM");

    /* LoRa 모듈 구독 설정 */
    event_manager_subscribe(&g_lora_gps_sub, EVENT_GPS_DATA_READY,
                           lora_gps_event_handler, NULL, 10, "LoRa_GPS");
    event_manager_subscribe(&g_lora_rtcm_sub, EVENT_RTCM_DATA_RECEIVED,
                           lora_rtcm_handler, NULL, 20, "LoRa_RTCM");

    /* BLE 모듈 구독 설정 */
    event_manager_subscribe(&g_ble_gps_sub, EVENT_GPS_DATA_READY,
                           ble_gps_event_handler, NULL, 5, "BLE_GPS");

    /* 시스템 에러 핸들러 */
    event_manager_subscribe(&g_system_error_sub, EVENT_SYSTEM_ERROR,
                           system_error_handler, NULL, 0, "SYS_ERR");
}
