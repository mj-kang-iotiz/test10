/**
 ******************************************************************************
 * @file    event_manager_sync.h
 * @brief   Event Manager 동기 요청-응답 API
 * @details PubSub 시스템에서 동기적 명령-응답 처리
 ******************************************************************************
 */

#ifndef EVENT_MANAGER_SYNC_H
#define EVENT_MANAGER_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "event_manager.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 요청 ID 타입
 */
typedef uint32_t request_id_t;

/**
 * @brief 요청 상태
 */
typedef enum {
    REQUEST_STATUS_PENDING,      // 대기 중
    REQUEST_STATUS_PROCESSING,   // 처리 중
    REQUEST_STATUS_COMPLETED,    // 완료
    REQUEST_STATUS_TIMEOUT,      // 타임아웃
    REQUEST_STATUS_ERROR         // 에러
} request_status_t;

/**
 * @brief 요청 데이터
 */
typedef struct {
    request_id_t request_id;     // 요청 ID (자동 생성)
    event_topic_t topic;         // 요청 토픽
    void *request_data;          // 요청 데이터
    size_t request_len;          // 요청 데이터 길이
    void *response_data;         // 응답 데이터 버퍼 (사용자 제공)
    size_t response_len;         // 응답 데이터 길이
    size_t max_response_len;     // 응답 버퍼 최대 크기
    uint32_t timeout_ms;         // 타임아웃 (ms)
    request_status_t status;     // 요청 상태
    SemaphoreHandle_t response_sem;  // 응답 대기 Semaphore
} event_request_t;

/**
 * @brief 요청 핸들러 콜백 타입
 * @param request 요청 데이터
 * @retval true: 성공, false: 실패
 */
typedef bool (*request_handler_t)(event_request_t *request);

/* Exported constants --------------------------------------------------------*/

#define EVENT_MANAGER_MAX_PENDING_REQUESTS  8   // 최대 동시 요청 수
#define EVENT_MANAGER_DEFAULT_TIMEOUT_MS    1000

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 동기 요청-응답 기능 초기화
 * @note  event_manager_init() 이후에 호출
 * @retval true: 성공, false: 실패
 */
bool event_manager_sync_init(void);

/**
 * @brief 동기 요청-응답 기능 정리
 */
void event_manager_sync_deinit(void);

/**
 * @brief 요청 핸들러 등록
 * @param topic 처리할 요청 토픽
 * @param handler 요청 핸들러 함수
 * @retval true: 성공, false: 실패
 *
 * @note 각 토픽당 하나의 핸들러만 등록 가능
 * @note 핸들러는 별도 태스크에서 실행됨
 */
bool event_manager_register_request_handler(event_topic_t topic,
                                            request_handler_t handler);

/**
 * @brief 동기 요청 전송 (응답 대기)
 * @param topic 요청 토픽
 * @param request_data 요청 데이터
 * @param request_len 요청 데이터 길이
 * @param response_data 응답 데이터 버퍼
 * @param max_response_len 응답 버퍼 최대 크기
 * @param response_len 실제 응답 데이터 길이 (출력)
 * @param timeout_ms 타임아웃 (0 = 기본값)
 * @retval true: 성공, false: 실패/타임아웃
 *
 * @note 응답이 올 때까지 blocking
 * @note ISR에서 호출 금지
 *
 * @example
 * uint8_t request[] = {0xB5, 0x62, ...};
 * uint8_t response[64];
 * size_t response_len;
 *
 * bool success = event_manager_send_request(
 *     EVENT_GPS_COMMAND_REQUEST,
 *     request, sizeof(request),
 *     response, sizeof(response),
 *     &response_len,
 *     1000
 * );
 */
bool event_manager_send_request(event_topic_t topic,
                                void *request_data,
                                size_t request_len,
                                void *response_data,
                                size_t max_response_len,
                                size_t *response_len,
                                uint32_t timeout_ms);

/**
 * @brief 응답 전송 (핸들러에서 호출)
 * @param request 요청 구조체
 * @param response_data 응답 데이터
 * @param response_len 응답 데이터 길이
 * @retval true: 성공, false: 실패
 *
 * @note 요청 핸들러 내에서만 호출
 */
bool event_manager_send_response(event_request_t *request,
                                 void *response_data,
                                 size_t response_len);

/**
 * @brief 에러 응답 전송 (핸들러에서 호출)
 * @param request 요청 구조체
 */
void event_manager_send_error(event_request_t *request);

/**
 * @brief 대기 중인 요청 수 조회
 * @retval 대기 중인 요청 수
 */
uint32_t event_manager_get_pending_requests(void);

/* Exported macros -----------------------------------------------------------*/

/**
 * @brief 간단한 요청 전송 (응답 불필요)
 */
#define event_manager_send_simple_request(topic, data, len, timeout) \
    event_manager_send_request(topic, data, len, NULL, 0, NULL, timeout)

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MANAGER_SYNC_H */
