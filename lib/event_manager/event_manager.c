/**
 ******************************************************************************
 * @file    event_manager.c
 * @brief   중앙 이벤트 관리 시스템 구현
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "event_manager.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

/* Private define ------------------------------------------------------------*/
#define EVENT_MANAGER_LOCK_TIMEOUT_MS    100

/* Private typedef -----------------------------------------------------------*/

/**
 * @brief 비동기 이벤트 큐 항목 (ISR에서 발행된 이벤트)
 */
typedef struct {
    event_data_t event;
    uint8_t data_buffer[64];  // 작은 데이터는 여기에 복사
} event_queue_item_t;

/* Private variables ---------------------------------------------------------*/

/**
 * @brief 토픽별 정보 배열 (각 토픽마다 List와 mutex 유지)
 */
static event_topic_info_t topic_info[EVENT_TOPIC_MAX] = {0};

/**
 * @brief 비동기 이벤트 처리용 큐 (ISR에서 발행된 이벤트)
 */
static QueueHandle_t event_queue = NULL;

/**
 * @brief 이벤트 매니저 태스크 핸들
 */
static TaskHandle_t event_manager_task_handle = NULL;

/**
 * @brief 통계 정보
 */
static event_manager_stats_t stats = {0};

/**
 * @brief 전역 mutex (통계 접근용)
 */
static SemaphoreHandle_t global_mutex = NULL;

/**
 * @brief 초기화 여부
 */
static bool is_initialized = false;

/* Private function prototypes -----------------------------------------------*/
static void event_manager_task(void *pvParameters);
static inline bool is_valid_topic(event_topic_t topic);
static inline bool lock_topic(event_topic_t topic, uint32_t timeout_ms);
static inline void unlock_topic(event_topic_t topic);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 토픽 유효성 검사
 */
static inline bool is_valid_topic(event_topic_t topic) {
    return (topic < EVENT_TOPIC_MAX);
}

/**
 * @brief 토픽 뮤텍스 잠금
 */
static inline bool lock_topic(event_topic_t topic, uint32_t timeout_ms) {
    if (!is_valid_topic(topic) || topic_info[topic].mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(topic_info[topic].mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/**
 * @brief 토픽 뮤텍스 해제
 */
static inline void unlock_topic(event_topic_t topic) {
    if (is_valid_topic(topic) && topic_info[topic].mutex != NULL) {
        xSemaphoreGive(topic_info[topic].mutex);
    }
}

/**
 * @brief 이벤트 매니저 태스크 (비동기 이벤트 처리)
 */
static void event_manager_task(void *pvParameters) {
    event_queue_item_t queue_item;

    while (1) {
        // 큐에서 이벤트 대기
        if (xQueueReceive(event_queue, &queue_item, portMAX_DELAY) == pdTRUE) {
            event_topic_t topic = queue_item.event.topic;

            if (!is_valid_topic(topic)) {
                continue;
            }

            if (!lock_topic(topic, EVENT_MANAGER_LOCK_TIMEOUT_MS)) {
                continue;
            }

            // 구독자 리스트 순회
            List_t *list = &topic_info[topic].subscriber_list;
            ListItem_t *item = listGET_HEAD_ENTRY(list);
            ListItem_t *end = listGET_END_MARKER(list);

            uint32_t delivered = 0;

            while (item != end) {
                event_subscriber_t *subscriber = (event_subscriber_t *)listGET_LIST_ITEM_OWNER(item);

                if (subscriber && subscriber->is_active && subscriber->callback) {
                    // 콜백 호출
                    subscriber->callback(&queue_item.event, subscriber->user_data);
                    delivered++;
                }

                item = listGET_NEXT(item);
            }

            // 통계 업데이트
            if (xSemaphoreTake(global_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                stats.total_delivered += delivered;
                xSemaphoreGive(global_mutex);
            }

            unlock_topic(topic);
        }
    }
}

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 이벤트 매니저 초기화
 */
bool event_manager_init(void) {
    if (is_initialized) {
        return true;
    }

    // 전역 mutex 생성
    global_mutex = xSemaphoreCreateMutex();
    if (global_mutex == NULL) {
        return false;
    }

    // 각 토픽별 초기화
    for (uint32_t i = 0; i < EVENT_TOPIC_MAX; i++) {
        // List 초기화
        vListInitialise(&topic_info[i].subscriber_list);

        // Mutex 생성
        topic_info[i].mutex = xSemaphoreCreateMutex();
        if (topic_info[i].mutex == NULL) {
            // 실패 시 이미 생성된 mutex 정리
            for (uint32_t j = 0; j < i; j++) {
                vSemaphoreDelete(topic_info[j].mutex);
            }
            vSemaphoreDelete(global_mutex);
            return false;
        }

        topic_info[i].subscriber_count = 0;
        topic_info[i].publish_count = 0;
    }

    // 비동기 이벤트 큐 생성
    event_queue = xQueueCreate(EVENT_MANAGER_QUEUE_SIZE, sizeof(event_queue_item_t));
    if (event_queue == NULL) {
        for (uint32_t i = 0; i < EVENT_TOPIC_MAX; i++) {
            vSemaphoreDelete(topic_info[i].mutex);
        }
        vSemaphoreDelete(global_mutex);
        return false;
    }

    // 이벤트 매니저 태스크 생성
    BaseType_t result = xTaskCreate(
        event_manager_task,
        "EventMgr",
        EVENT_MANAGER_TASK_STACK_SIZE,
        NULL,
        EVENT_MANAGER_TASK_PRIORITY,
        &event_manager_task_handle
    );

    if (result != pdPASS) {
        vQueueDelete(event_queue);
        for (uint32_t i = 0; i < EVENT_TOPIC_MAX; i++) {
            vSemaphoreDelete(topic_info[i].mutex);
        }
        vSemaphoreDelete(global_mutex);
        return false;
    }

    // 통계 초기화
    memset(&stats, 0, sizeof(stats));

    is_initialized = true;
    return true;
}

/**
 * @brief 이벤트 매니저 초기화 해제
 */
void event_manager_deinit(void) {
    if (!is_initialized) {
        return;
    }

    // 태스크 삭제
    if (event_manager_task_handle != NULL) {
        vTaskDelete(event_manager_task_handle);
        event_manager_task_handle = NULL;
    }

    // 큐 삭제
    if (event_queue != NULL) {
        vQueueDelete(event_queue);
        event_queue = NULL;
    }

    // 각 토픽 정리
    for (uint32_t i = 0; i < EVENT_TOPIC_MAX; i++) {
        if (topic_info[i].mutex != NULL) {
            vSemaphoreDelete(topic_info[i].mutex);
            topic_info[i].mutex = NULL;
        }
    }

    // 전역 mutex 삭제
    if (global_mutex != NULL) {
        vSemaphoreDelete(global_mutex);
        global_mutex = NULL;
    }

    is_initialized = false;
}

/**
 * @brief 이벤트 구독
 */
bool event_manager_subscribe(event_subscriber_t *subscriber,
                             event_topic_t topic,
                             event_callback_t callback,
                             void *user_data,
                             uint32_t priority,
                             const char *name) {
    if (!is_initialized || !subscriber || !callback || !is_valid_topic(topic)) {
        return false;
    }

    // 구독자 구조체 초기화
    vListInitialiseItem(&subscriber->list_item);
    subscriber->callback = callback;
    subscriber->user_data = user_data;
    subscriber->topic = topic;
    subscriber->priority = priority;
    subscriber->is_active = true;
    subscriber->name = name;

    // ListItem에 owner 설정 (구독자 자신을 가리킴)
    listSET_LIST_ITEM_OWNER(&subscriber->list_item, subscriber);

    // 우선순위 설정 (낮은 값이 먼저 실행되도록)
    listSET_LIST_ITEM_VALUE(&subscriber->list_item, priority);

    // 토픽 잠금
    if (!lock_topic(topic, EVENT_MANAGER_LOCK_TIMEOUT_MS)) {
        return false;
    }

    // 최대 구독자 수 체크
    if (topic_info[topic].subscriber_count >= EVENT_MANAGER_MAX_SUBSCRIBERS_PER_TOPIC) {
        unlock_topic(topic);
        return false;
    }

    // List에 우선순위 순서로 삽입 (FreeRTOS List는 자동으로 정렬)
    vListInsert(&topic_info[topic].subscriber_list, &subscriber->list_item);
    topic_info[topic].subscriber_count++;

    // 통계 업데이트
    if (xSemaphoreTake(global_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.total_subscribers++;
        if (topic_info[topic].subscriber_count > stats.max_subscribers_per_topic) {
            stats.max_subscribers_per_topic = topic_info[topic].subscriber_count;
        }
        xSemaphoreGive(global_mutex);
    }

    unlock_topic(topic);
    return true;
}

/**
 * @brief 이벤트 구독 해제
 */
bool event_manager_unsubscribe(event_subscriber_t *subscriber) {
    if (!is_initialized || !subscriber) {
        return false;
    }

    event_topic_t topic = subscriber->topic;
    if (!is_valid_topic(topic)) {
        return false;
    }

    // 토픽 잠금
    if (!lock_topic(topic, EVENT_MANAGER_LOCK_TIMEOUT_MS)) {
        return false;
    }

    // List에서 제거
    if (listIS_CONTAINED_WITHIN(&topic_info[topic].subscriber_list, &subscriber->list_item)) {
        uxListRemove(&subscriber->list_item);
        topic_info[topic].subscriber_count--;

        // 통계 업데이트
        if (xSemaphoreTake(global_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            stats.total_subscribers--;
            xSemaphoreGive(global_mutex);
        }
    }

    unlock_topic(topic);
    return true;
}

/**
 * @brief 이벤트 발행 (동기 방식)
 */
uint32_t event_manager_publish(event_topic_t topic,
                               void *data,
                               size_t data_len,
                               void *sender) {
    if (!is_initialized || !is_valid_topic(topic)) {
        return 0;
    }

    // 이벤트 데이터 생성
    event_data_t event = {
        .topic = topic,
        .timestamp = xTaskGetTickCount(),
        .data = data,
        .data_len = data_len,
        .sender = sender
    };

    // 토픽 잠금
    if (!lock_topic(topic, EVENT_MANAGER_LOCK_TIMEOUT_MS)) {
        return 0;
    }

    // 발행 횟수 증가
    topic_info[topic].publish_count++;

    // 구독자 리스트 순회
    List_t *list = &topic_info[topic].subscriber_list;
    ListItem_t *item = listGET_HEAD_ENTRY(list);
    ListItem_t *end = listGET_END_MARKER(list);

    uint32_t delivered = 0;

    while (item != end) {
        event_subscriber_t *subscriber = (event_subscriber_t *)listGET_LIST_ITEM_OWNER(item);

        if (subscriber && subscriber->is_active && subscriber->callback) {
            // 콜백 호출 (동기 방식)
            subscriber->callback(&event, subscriber->user_data);
            delivered++;
        }

        item = listGET_NEXT(item);
    }

    // 통계 업데이트
    if (xSemaphoreTake(global_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.total_published++;
        stats.total_delivered += delivered;
        xSemaphoreGive(global_mutex);
    }

    unlock_topic(topic);
    return delivered;
}

/**
 * @brief 이벤트 발행 (ISR에서 호출)
 */
bool event_manager_publish_from_isr(event_topic_t topic,
                                    void *data,
                                    size_t data_len,
                                    void *sender,
                                    BaseType_t *pxHigherPriorityTaskWoken) {
    if (!is_initialized || !is_valid_topic(topic)) {
        return false;
    }

    event_queue_item_t queue_item = {0};

    // 이벤트 데이터 설정
    queue_item.event.topic = topic;
    queue_item.event.timestamp = xTaskGetTickCountFromISR();
    queue_item.event.sender = sender;
    queue_item.event.data_len = data_len;

    // 작은 데이터는 큐 항목에 복사 (큰 데이터는 포인터만 전달)
    if (data && data_len > 0 && data_len <= sizeof(queue_item.data_buffer)) {
        memcpy(queue_item.data_buffer, data, data_len);
        queue_item.event.data = queue_item.data_buffer;
    } else {
        queue_item.event.data = data;
    }

    // 큐에 전송
    BaseType_t result = xQueueSendFromISR(event_queue, &queue_item, pxHigherPriorityTaskWoken);

    return (result == pdTRUE);
}

/**
 * @brief 구독자 활성화/비활성화
 */
void event_manager_set_active(event_subscriber_t *subscriber, bool active) {
    if (subscriber) {
        subscriber->is_active = active;
    }
}

/**
 * @brief 특정 토픽의 구독자 수 조회
 */
uint32_t event_manager_get_subscriber_count(event_topic_t topic) {
    if (!is_initialized || !is_valid_topic(topic)) {
        return 0;
    }

    uint32_t count = 0;
    if (lock_topic(topic, EVENT_MANAGER_LOCK_TIMEOUT_MS)) {
        count = topic_info[topic].subscriber_count;
        unlock_topic(topic);
    }

    return count;
}

/**
 * @brief 이벤트 매니저 통계 조회
 */
void event_manager_get_stats(event_manager_stats_t *stats_out) {
    if (!is_initialized || !stats_out) {
        return;
    }

    if (xSemaphoreTake(global_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats_out, &stats, sizeof(event_manager_stats_t));
        xSemaphoreGive(global_mutex);
    }
}

/**
 * @brief 토픽 이름 문자열 반환
 */
const char* event_manager_get_topic_name(event_topic_t topic) {
    static const char *topic_names[] = {
        "GPS_DATA_READY",
        "GPS_FIX_STATUS_CHANGED",
        "GPS_POSITION_UPDATED",
        "RTCM_DATA_RECEIVED",
        "RTCM_PARSE_COMPLETE",
        "GSM_CONNECTED",
        "GSM_DISCONNECTED",
        "NTRIP_DATA_RECEIVED",
        "LORA_TX_COMPLETE",
        "LORA_RX_COMPLETE",
        "LORA_ERROR",
        "BLE_CONNECTED",
        "BLE_DISCONNECTED",
        "BLE_CMD_RECEIVED",
        "RS485_DATA_RECEIVED",
        "RS485_TX_COMPLETE",
        "SYSTEM_ERROR",
        "PARAM_CHANGED",
        "LOW_BATTERY"
    };

    if (topic < EVENT_TOPIC_MAX) {
        return topic_names[topic];
    }
    return "UNKNOWN";
}

/**
 * @brief 디버그 정보 출력
 */
void event_manager_print_info(void) {
    if (!is_initialized) {
        return;
    }

    // 여기에 SEGGER_RTT나 UART 출력 코드 추가 가능
    // 예시는 주석으로 처리
    /*
    printf("\n=== Event Manager Info ===\n");
    printf("Total Published: %lu\n", stats.total_published);
    printf("Total Delivered: %lu\n", stats.total_delivered);
    printf("Total Subscribers: %lu\n", stats.total_subscribers);
    printf("Max Subscribers/Topic: %lu\n\n", stats.max_subscribers_per_topic);

    for (uint32_t i = 0; i < EVENT_TOPIC_MAX; i++) {
        if (topic_info[i].subscriber_count > 0) {
            printf("[%s] Subscribers: %lu, Published: %lu\n",
                   event_manager_get_topic_name(i),
                   topic_info[i].subscriber_count,
                   topic_info[i].publish_count);
        }
    }
    */
}
