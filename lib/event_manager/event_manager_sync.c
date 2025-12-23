/**
 ******************************************************************************
 * @file    event_manager_sync.c
 * @brief   Event Manager 동기 요청-응답 구현
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "event_manager_sync.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

/* Private define ------------------------------------------------------------*/
#define REQUEST_QUEUE_SIZE  8
#define REQUEST_TASK_STACK_SIZE  512
#define REQUEST_TASK_PRIORITY  (tskIDLE_PRIORITY + 3)

/* Private typedef -----------------------------------------------------------*/

/**
 * @brief 요청 핸들러 정보
 */
typedef struct {
    event_topic_t topic;
    request_handler_t handler;
    bool registered;
} request_handler_info_t;

/* Private variables ---------------------------------------------------------*/

/**
 * @brief 요청 핸들러 테이블
 */
static request_handler_info_t handler_table[EVENT_TOPIC_MAX] = {0};

/**
 * @brief 요청 큐
 */
static QueueHandle_t request_queue = NULL;

/**
 * @brief 요청 처리 태스크
 */
static TaskHandle_t request_task_handle = NULL;

/**
 * @brief 초기화 여부
 */
static bool is_sync_initialized = false;

/**
 * @brief 요청 ID 카운터
 */
static request_id_t request_id_counter = 0;

/**
 * @brief 전역 mutex
 */
static SemaphoreHandle_t sync_mutex = NULL;

/* Private function prototypes -----------------------------------------------*/
static void request_handler_task(void *pvParameters);
static request_id_t generate_request_id(void);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief 요청 ID 생성
 */
static request_id_t generate_request_id(void) {
    request_id_t id;

    if (xSemaphoreTake(sync_mutex, portMAX_DELAY) == pdTRUE) {
        id = ++request_id_counter;
        xSemaphoreGive(sync_mutex);
    } else {
        id = 0;
    }

    return id;
}

/**
 * @brief 요청 처리 태스크
 */
static void request_handler_task(void *pvParameters) {
    event_request_t *request;

    while (1) {
        // 요청 큐에서 대기
        if (xQueueReceive(request_queue, &request, portMAX_DELAY) == pdTRUE) {
            // 핸들러 찾기
            if (request->topic < EVENT_TOPIC_MAX &&
                handler_table[request->topic].registered) {

                request->status = REQUEST_STATUS_PROCESSING;

                // 핸들러 실행
                bool success = handler_table[request->topic].handler(request);

                if (!success && request->status != REQUEST_STATUS_COMPLETED) {
                    request->status = REQUEST_STATUS_ERROR;
                }
            } else {
                request->status = REQUEST_STATUS_ERROR;
            }

            // 타임아웃이 아니면 Semaphore 해제
            if (request->status != REQUEST_STATUS_TIMEOUT) {
                xSemaphoreGive(request->response_sem);
            }
        }
    }
}

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 동기 요청-응답 기능 초기화
 */
bool event_manager_sync_init(void) {
    if (is_sync_initialized) {
        return true;
    }

    // Mutex 생성
    sync_mutex = xSemaphoreCreateMutex();
    if (sync_mutex == NULL) {
        return false;
    }

    // 요청 큐 생성
    request_queue = xQueueCreate(REQUEST_QUEUE_SIZE, sizeof(event_request_t*));
    if (request_queue == NULL) {
        vSemaphoreDelete(sync_mutex);
        return false;
    }

    // 요청 처리 태스크 생성
    BaseType_t result = xTaskCreate(
        request_handler_task,
        "ReqHandler",
        REQUEST_TASK_STACK_SIZE,
        NULL,
        REQUEST_TASK_PRIORITY,
        &request_task_handle
    );

    if (result != pdPASS) {
        vQueueDelete(request_queue);
        vSemaphoreDelete(sync_mutex);
        return false;
    }

    is_sync_initialized = true;
    return true;
}

/**
 * @brief 동기 요청-응답 기능 정리
 */
void event_manager_sync_deinit(void) {
    if (!is_sync_initialized) {
        return;
    }

    if (request_task_handle != NULL) {
        vTaskDelete(request_task_handle);
        request_task_handle = NULL;
    }

    if (request_queue != NULL) {
        vQueueDelete(request_queue);
        request_queue = NULL;
    }

    if (sync_mutex != NULL) {
        vSemaphoreDelete(sync_mutex);
        sync_mutex = NULL;
    }

    memset(handler_table, 0, sizeof(handler_table));
    is_sync_initialized = false;
}

/**
 * @brief 요청 핸들러 등록
 */
bool event_manager_register_request_handler(event_topic_t topic,
                                            request_handler_t handler) {
    if (!is_sync_initialized || topic >= EVENT_TOPIC_MAX || !handler) {
        return false;
    }

    if (xSemaphoreTake(sync_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        handler_table[topic].topic = topic;
        handler_table[topic].handler = handler;
        handler_table[topic].registered = true;
        xSemaphoreGive(sync_mutex);
        return true;
    }

    return false;
}

/**
 * @brief 동기 요청 전송
 */
bool event_manager_send_request(event_topic_t topic,
                                void *request_data,
                                size_t request_len,
                                void *response_data,
                                size_t max_response_len,
                                size_t *response_len,
                                uint32_t timeout_ms) {
    if (!is_sync_initialized || topic >= EVENT_TOPIC_MAX) {
        return false;
    }

    // 기본 타임아웃
    if (timeout_ms == 0) {
        timeout_ms = EVENT_MANAGER_DEFAULT_TIMEOUT_MS;
    }

    // 요청 구조체 생성 (스택)
    event_request_t request = {0};
    request.request_id = generate_request_id();
    request.topic = topic;
    request.request_data = request_data;
    request.request_len = request_len;
    request.response_data = response_data;
    request.max_response_len = max_response_len;
    request.timeout_ms = timeout_ms;
    request.status = REQUEST_STATUS_PENDING;

    // Binary Semaphore 생성
    request.response_sem = xSemaphoreCreateBinary();
    if (request.response_sem == NULL) {
        return false;
    }

    // 요청 큐에 전송 (포인터 전달)
    event_request_t *req_ptr = &request;
    if (xQueueSend(request_queue, &req_ptr, pdMS_TO_TICKS(100)) != pdTRUE) {
        vSemaphoreDelete(request.response_sem);
        return false;
    }

    // 응답 대기 (동기)
    BaseType_t result = xSemaphoreTake(request.response_sem,
                                       pdMS_TO_TICKS(timeout_ms));

    // Semaphore 정리
    vSemaphoreDelete(request.response_sem);

    // 타임아웃 체크
    if (result != pdTRUE) {
        request.status = REQUEST_STATUS_TIMEOUT;
        return false;
    }

    // 응답 길이 반환
    if (response_len != NULL) {
        *response_len = request.response_len;
    }

    return (request.status == REQUEST_STATUS_COMPLETED);
}

/**
 * @brief 응답 전송
 */
bool event_manager_send_response(event_request_t *request,
                                 void *response_data,
                                 size_t response_len) {
    if (!request || !response_data) {
        return false;
    }

    // 응답 데이터 복사
    if (request->response_data && request->max_response_len > 0) {
        size_t copy_len = (response_len < request->max_response_len) ?
                          response_len : request->max_response_len;
        memcpy(request->response_data, response_data, copy_len);
        request->response_len = copy_len;
    } else {
        request->response_len = 0;
    }

    request->status = REQUEST_STATUS_COMPLETED;
    return true;
}

/**
 * @brief 에러 응답 전송
 */
void event_manager_send_error(event_request_t *request) {
    if (request) {
        request->status = REQUEST_STATUS_ERROR;
        request->response_len = 0;
    }
}

/**
 * @brief 대기 중인 요청 수 조회
 */
uint32_t event_manager_get_pending_requests(void) {
    if (!is_sync_initialized || !request_queue) {
        return 0;
    }

    return (uint32_t)uxQueueMessagesWaiting(request_queue);
}
