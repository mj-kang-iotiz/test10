/**
 ******************************************************************************
 * @file    event_manager.h
 * @brief   중앙 이벤트 관리 시스템 (PubSub 패턴)
 * @details FreeRTOS List 기반 이벤트 구독/발행 시스템
 ******************************************************************************
 */

#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "list.h"
#include "semphr.h"
#include <stdint.h>
#include <stdbool.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 이벤트 토픽 종류 정의
 * @note  새로운 이벤트를 추가할 때는 여기에 추가하고 EVENT_TOPIC_MAX를 업데이트
 */
typedef enum {
    /* GPS 관련 이벤트 */
    EVENT_GPS_DATA_READY = 0,           // GPS 데이터 수신 완료
    EVENT_GPS_FIX_STATUS_CHANGED,       // GPS Fix 상태 변경
    EVENT_GPS_POSITION_UPDATED,         // GPS 위치 업데이트

    /* RTCM 관련 이벤트 */
    EVENT_RTCM_DATA_RECEIVED,           // RTCM 데이터 수신
    EVENT_RTCM_PARSE_COMPLETE,          // RTCM 파싱 완료

    /* GSM 관련 이벤트 */
    EVENT_GSM_CONNECTED,                // GSM 연결 완료
    EVENT_GSM_DISCONNECTED,             // GSM 연결 끊김
    EVENT_NTRIP_DATA_RECEIVED,          // NTRIP 데이터 수신

    /* LoRa 관련 이벤트 */
    EVENT_LORA_TX_COMPLETE,             // LoRa 전송 완료
    EVENT_LORA_RX_COMPLETE,             // LoRa 수신 완료
    EVENT_LORA_ERROR,                   // LoRa 에러

    /* BLE 관련 이벤트 */
    EVENT_BLE_CONNECTED,                // BLE 연결
    EVENT_BLE_DISCONNECTED,             // BLE 연결 해제
    EVENT_BLE_CMD_RECEIVED,             // BLE 명령 수신

    /* RS485 관련 이벤트 */
    EVENT_RS485_DATA_RECEIVED,          // RS485 데이터 수신
    EVENT_RS485_TX_COMPLETE,            // RS485 전송 완료

    /* 시스템 관련 이벤트 */
    EVENT_SYSTEM_ERROR,                 // 시스템 에러
    EVENT_PARAM_CHANGED,                // 파라미터 변경
    EVENT_LOW_BATTERY,                  // 배터리 부족

    EVENT_TOPIC_MAX                     // 토픽 최대 개수 (자동 계산)
} event_topic_t;

/**
 * @brief 이벤트 데이터 구조체
 * @note  이벤트 발행 시 전달되는 데이터
 */
typedef struct {
    event_topic_t topic;                // 이벤트 토픽
    uint32_t timestamp;                 // 이벤트 발생 시간 (tick)
    void *data;                         // 이벤트 데이터 포인터
    size_t data_len;                    // 데이터 길이
    void *sender;                       // 발행자 식별자 (optional)
} event_data_t;

/**
 * @brief 이벤트 콜백 함수 타입
 * @param event 이벤트 데이터
 * @param user_data 사용자 데이터 (subscribe 시 등록한 데이터)
 */
typedef void (*event_callback_t)(const event_data_t *event, void *user_data);

/**
 * @brief 구독자(Subscriber) 구조체
 * @note  FreeRTOS ListItem과 함께 사용
 */
typedef struct {
    ListItem_t list_item;               // FreeRTOS List 항목 (반드시 첫 번째 멤버)
    event_callback_t callback;          // 이벤트 핸들러 콜백
    void *user_data;                    // 사용자 정의 데이터
    event_topic_t topic;                // 구독 중인 토픽
    uint32_t priority;                  // 우선순위 (낮을수록 먼저 실행)
    bool is_active;                     // 활성화 상태
    const char *name;                   // 디버깅용 이름
} event_subscriber_t;

/**
 * @brief 이벤트 토픽 정보 구조체
 */
typedef struct {
    List_t subscriber_list;             // 구독자 리스트 (FreeRTOS List)
    SemaphoreHandle_t mutex;            // 동기화용 뮤텍스
    uint32_t subscriber_count;          // 구독자 수
    uint32_t publish_count;             // 발행 횟수 (통계용)
} event_topic_info_t;

/**
 * @brief 이벤트 매니저 통계 구조체
 */
typedef struct {
    uint32_t total_published;           // 총 발행 횟수
    uint32_t total_delivered;           // 총 전달 횟수
    uint32_t total_subscribers;         // 총 구독자 수
    uint32_t max_subscribers_per_topic; // 토픽당 최대 구독자 수
} event_manager_stats_t;

/* Exported constants --------------------------------------------------------*/

/* 설정 상수 */
#define EVENT_MANAGER_MAX_SUBSCRIBERS_PER_TOPIC  16  // 토픽당 최대 구독자 수
#define EVENT_MANAGER_QUEUE_SIZE                 32  // 비동기 이벤트 큐 크기
#define EVENT_MANAGER_TASK_PRIORITY              (tskIDLE_PRIORITY + 2)
#define EVENT_MANAGER_TASK_STACK_SIZE            512

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 이벤트 매니저 초기화
 * @retval true: 성공, false: 실패
 */
bool event_manager_init(void);

/**
 * @brief 이벤트 매니저 초기화 해제
 */
void event_manager_deinit(void);

/**
 * @brief 이벤트 구독
 * @param subscriber 구독자 구조체 포인터 (정적 할당 필요)
 * @param topic 구독할 토픽
 * @param callback 이벤트 핸들러 함수
 * @param user_data 사용자 데이터 (optional)
 * @param priority 우선순위 (0이 가장 높음)
 * @param name 디버깅용 이름 (optional)
 * @retval true: 성공, false: 실패
 *
 * @note subscriber는 반드시 정적으로 할당되어야 하며, unsubscribe 전까지 유효해야 함
 * @note 우선순위가 낮을수록(숫자가 작을수록) 먼저 호출됨
 *
 * @example
 * static event_subscriber_t gps_subscriber;
 * event_manager_subscribe(&gps_subscriber, EVENT_GPS_DATA_READY,
 *                         gps_event_handler, NULL, 10, "GPS_Handler");
 */
bool event_manager_subscribe(event_subscriber_t *subscriber,
                             event_topic_t topic,
                             event_callback_t callback,
                             void *user_data,
                             uint32_t priority,
                             const char *name);

/**
 * @brief 이벤트 구독 해제
 * @param subscriber 구독자 구조체 포인터
 * @retval true: 성공, false: 실패
 */
bool event_manager_unsubscribe(event_subscriber_t *subscriber);

/**
 * @brief 이벤트 발행 (동기 방식)
 * @param topic 토픽
 * @param data 데이터 포인터
 * @param data_len 데이터 길이
 * @param sender 발행자 식별자 (optional)
 * @retval 이벤트를 전달받은 구독자 수
 *
 * @note 호출 즉시 모든 구독자에게 이벤트 전달 (동기 방식)
 * @note ISR에서 호출하면 안됨 (event_manager_publish_from_isr 사용)
 */
uint32_t event_manager_publish(event_topic_t topic,
                               void *data,
                               size_t data_len,
                               void *sender);

/**
 * @brief 이벤트 발행 (ISR에서 호출)
 * @param topic 토픽
 * @param data 데이터 포인터
 * @param data_len 데이터 길이
 * @param sender 발행자 식별자 (optional)
 * @param pxHigherPriorityTaskWoken FreeRTOS 우선순위 태스크 플래그
 * @retval true: 성공, false: 실패 (큐가 가득 참)
 *
 * @note ISR에서만 사용해야 함
 * @note 이벤트는 큐에 저장되고 event_manager 태스크에서 처리됨
 */
bool event_manager_publish_from_isr(event_topic_t topic,
                                    void *data,
                                    size_t data_len,
                                    void *sender,
                                    BaseType_t *pxHigherPriorityTaskWoken);

/**
 * @brief 구독자 활성화/비활성화
 * @param subscriber 구독자 구조체 포인터
 * @param active true: 활성화, false: 비활성화
 * @note 비활성화된 구독자는 이벤트를 받지 않음 (리스트에는 남아있음)
 */
void event_manager_set_active(event_subscriber_t *subscriber, bool active);

/**
 * @brief 특정 토픽의 구독자 수 조회
 * @param topic 토픽
 * @retval 구독자 수
 */
uint32_t event_manager_get_subscriber_count(event_topic_t topic);

/**
 * @brief 이벤트 매니저 통계 조회
 * @param stats 통계 구조체 포인터
 */
void event_manager_get_stats(event_manager_stats_t *stats);

/**
 * @brief 디버그 정보 출력
 * @note SEGGER_RTT나 UART로 출력 가능
 */
void event_manager_print_info(void);

/**
 * @brief 토픽 이름 문자열 반환 (디버깅용)
 * @param topic 토픽
 * @retval 토픽 이름 문자열
 */
const char* event_manager_get_topic_name(event_topic_t topic);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MANAGER_H */
