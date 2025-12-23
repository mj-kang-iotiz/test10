# Event Manager - PubSub 시스템

FreeRTOS List 기반 중앙 이벤트 관리 시스템 (Publisher-Subscriber 패턴)

## 📋 목차

- [개요](#개요)
- [주요 기능](#주요-기능)
- [아키텍처](#아키텍처)
- [빠른 시작](#빠른-시작)
- [API 레퍼런스](#api-레퍼런스)
- [사용 예제](#사용-예제)
- [성능 및 메모리](#성능-및-메모리)
- [주의사항](#주의사항)
- [FAQ](#faq)

---

## 개요

Event Manager는 STM32 + FreeRTOS 임베디드 시스템을 위한 **경량 PubSub 시스템**입니다.

### 왜 필요한가?

기존 방식의 문제점:
```c
// ❌ 기존: 모듈 간 강결합
void gps_task() {
    gps_data_t data;
    // GPS 데이터 수신

    lora_send_gps(&data);      // LoRa에 직접 의존
    ble_notify_gps(&data);      // BLE에 직접 의존
    rs485_send_gps(&data);      // RS485에 직접 의존
}
```

Event Manager 사용:
```c
// ✅ 개선: 느슨한 결합, 확장 가능
void gps_task() {
    gps_data_t data;
    // GPS 데이터 수신

    // 누가 사용하든 상관없이 발행만
    event_manager_publish(EVENT_GPS_DATA_READY, &data, sizeof(data), NULL);
}

// LoRa, BLE, RS485 각각 필요한 이벤트만 구독
```

### 핵심 장점

1. **모듈 간 독립성** - 각 모듈이 서로 의존하지 않음
2. **확장성** - 새 모듈 추가가 쉬움 (기존 코드 수정 불필요)
3. **우선순위 제어** - 중요한 핸들러 먼저 실행
4. **동기/비동기 지원** - 일반 태스크와 ISR 모두 지원
5. **메모리 효율** - FreeRTOS List 사용, 동적 할당 없음

---

## 주요 기능

### ✅ 구현된 기능

- [x] **FreeRTOS List 기반** 구독자 관리
- [x] **우선순위 기반** 이벤트 전달
- [x] **동기/비동기** 이벤트 발행
- [x] **ISR 안전** (publish_from_isr)
- [x] **구독자 활성화/비활성화** (런타임)
- [x] **Thread-safe** (Mutex 보호)
- [x] **통계 수집** (디버깅용)
- [x] **사용자 데이터** (context 전달)

### 🎯 지원 이벤트

| 카테고리 | 이벤트 | 설명 |
|---------|--------|------|
| GPS | `EVENT_GPS_DATA_READY` | GPS 데이터 수신 완료 |
| | `EVENT_GPS_FIX_STATUS_CHANGED` | Fix 상태 변경 |
| | `EVENT_GPS_POSITION_UPDATED` | 위치 업데이트 |
| RTCM | `EVENT_RTCM_DATA_RECEIVED` | RTCM 데이터 수신 |
| | `EVENT_RTCM_PARSE_COMPLETE` | RTCM 파싱 완료 |
| GSM | `EVENT_GSM_CONNECTED` | GSM 연결 |
| | `EVENT_GSM_DISCONNECTED` | GSM 연결 해제 |
| | `EVENT_NTRIP_DATA_RECEIVED` | NTRIP 데이터 수신 |
| LoRa | `EVENT_LORA_TX_COMPLETE` | LoRa 전송 완료 |
| | `EVENT_LORA_RX_COMPLETE` | LoRa 수신 완료 |
| | `EVENT_LORA_ERROR` | LoRa 에러 |
| BLE | `EVENT_BLE_CONNECTED` | BLE 연결 |
| | `EVENT_BLE_DISCONNECTED` | BLE 연결 해제 |
| | `EVENT_BLE_CMD_RECEIVED` | BLE 명령 수신 |
| RS485 | `EVENT_RS485_DATA_RECEIVED` | RS485 데이터 수신 |
| | `EVENT_RS485_TX_COMPLETE` | RS485 전송 완료 |
| System | `EVENT_SYSTEM_ERROR` | 시스템 에러 |
| | `EVENT_PARAM_CHANGED` | 파라미터 변경 |
| | `EVENT_LOW_BATTERY` | 배터리 부족 |

---

## 아키텍처

### 시스템 구조

```
┌─────────────────────────────────────────────────────────┐
│              Event Manager (중앙 관리자)                 │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │ Topic: EVENT_GPS_DATA_READY                     │   │
│  │ Subscribers (FreeRTOS List):                    │   │
│  │   [BLE:Priority=5] → [LoRa:10] → [RS485:15]    │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │ Topic: EVENT_RTCM_DATA_RECEIVED                 │   │
│  │ Subscribers:                                    │   │
│  │   [GPS:Priority=0] → [LoRa:20]                  │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  각 토픽마다 독립적인 Mutex와 List 유지                  │
└─────────────────────────────────────────────────────────┘
            ↑ subscribe          ↓ publish
┌───────────────────────────────────────────────────────┐
│    GPS    GSM    LoRa    BLE    RS485   (모듈들)      │
└───────────────────────────────────────────────────────┘
```

### FreeRTOS List 활용

```c
// 각 토픽마다 하나의 List 유지
typedef struct {
    List_t subscriber_list;      // FreeRTOS List
    SemaphoreHandle_t mutex;     // Thread-safe 보장
    uint32_t subscriber_count;
} event_topic_info_t;

// 토픽별 배열
event_topic_info_t topics[EVENT_TOPIC_MAX];

// Subscriber는 ListItem을 포함
typedef struct {
    ListItem_t list_item;        // FreeRTOS List 항목
    event_callback_t callback;   // 핸들러 함수
    void *user_data;             // 사용자 데이터
    uint32_t priority;           // 우선순위
    bool is_active;              // 활성화 상태
} event_subscriber_t;
```

### 데이터 흐름

1. **구독 (Subscribe)**
   ```
   Module → event_manager_subscribe()
         → vListInsert() [FreeRTOS]
         → subscriber_list에 우선순위 순으로 삽입
   ```

2. **발행 (Publish)**
   ```
   Module → event_manager_publish()
         → lock_topic() [Mutex]
         → List 순회 (listGET_HEAD_ENTRY → listGET_NEXT)
         → 각 subscriber의 callback 호출
         → unlock_topic()
   ```

3. **ISR에서 발행**
   ```
   ISR → event_manager_publish_from_isr()
      → xQueueSendFromISR()
      → Event Manager Task
      → 일반 publish 로직 실행
   ```

---

## 빠른 시작

### 1단계: 초기화

```c
#include "event_manager.h"

void system_init(void) {
    // FreeRTOS 스케줄러 시작 전에 초기화
    if (!event_manager_init()) {
        // 초기화 실패 처리
        Error_Handler();
    }
}
```

### 2단계: 이벤트 구독

```c
// 전역 또는 정적 변수로 subscriber 선언 (스택 X)
static event_subscriber_t my_subscriber;

void my_event_handler(const event_data_t *event, void *user_data) {
    // 이벤트 처리
    printf("Event received: topic=%d, data_len=%zu\n",
           event->topic, event->data_len);
}

void my_module_init(void) {
    // 이벤트 구독
    event_manager_subscribe(
        &my_subscriber,              // subscriber (정적 변수)
        EVENT_GPS_DATA_READY,        // 토픽
        my_event_handler,            // 콜백 함수
        NULL,                        // user_data (optional)
        10,                          // 우선순위 (낮을수록 먼저)
        "MyModule_GPS_Handler"       // 이름 (디버깅용)
    );
}
```

### 3단계: 이벤트 발행

```c
void my_task(void *pvParameters) {
    gps_data_t gps_data;

    while (1) {
        // 데이터 준비
        gps_data.latitude = 37.5665;
        gps_data.longitude = 126.9780;

        // 이벤트 발행 (동기)
        event_manager_publish(
            EVENT_GPS_DATA_READY,
            &gps_data,
            sizeof(gps_data),
            (void*)"GPS_Task"
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 4단계: ISR에서 발행

```c
void UART_RX_IRQHandler(void) {
    uint8_t data[64];
    size_t len = uart_read(data, sizeof(data));

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // ISR에서 안전하게 발행
    event_manager_publish_from_isr(
        EVENT_RTCM_DATA_RECEIVED,
        data,
        len,
        (void*)"UART_ISR",
        &xHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

---

## API 레퍼런스

### 초기화/정리

#### `event_manager_init()`
```c
bool event_manager_init(void);
```
- **설명**: Event Manager 초기화
- **반환**: 성공 시 `true`, 실패 시 `false`
- **주의**: FreeRTOS 스케줄러 시작 전에 호출

#### `event_manager_deinit()`
```c
void event_manager_deinit(void);
```
- **설명**: Event Manager 정리 (모든 리소스 해제)

### 구독 관리

#### `event_manager_subscribe()`
```c
bool event_manager_subscribe(
    event_subscriber_t *subscriber,
    event_topic_t topic,
    event_callback_t callback,
    void *user_data,
    uint32_t priority,
    const char *name
);
```
- **파라미터**:
  - `subscriber`: 구독자 구조체 (정적 할당 필수)
  - `topic`: 구독할 토픽
  - `callback`: 이벤트 핸들러 함수
  - `user_data`: 사용자 데이터 (optional)
  - `priority`: 우선순위 (0이 가장 높음)
  - `name`: 디버깅용 이름
- **반환**: 성공 시 `true`

#### `event_manager_unsubscribe()`
```c
bool event_manager_unsubscribe(event_subscriber_t *subscriber);
```
- **설명**: 구독 해제

#### `event_manager_set_active()`
```c
void event_manager_set_active(event_subscriber_t *subscriber, bool active);
```
- **설명**: 구독자 활성화/비활성화 (리스트에는 유지)

### 이벤트 발행

#### `event_manager_publish()`
```c
uint32_t event_manager_publish(
    event_topic_t topic,
    void *data,
    size_t data_len,
    void *sender
);
```
- **설명**: 동기 방식으로 이벤트 발행
- **반환**: 이벤트를 받은 구독자 수
- **주의**: ISR에서 호출 금지

#### `event_manager_publish_from_isr()`
```c
bool event_manager_publish_from_isr(
    event_topic_t topic,
    void *data,
    size_t data_len,
    void *sender,
    BaseType_t *pxHigherPriorityTaskWoken
);
```
- **설명**: ISR에서 안전하게 이벤트 발행
- **반환**: 성공 시 `true`

### 유틸리티

#### `event_manager_get_subscriber_count()`
```c
uint32_t event_manager_get_subscriber_count(event_topic_t topic);
```
- **설명**: 특정 토픽의 구독자 수 조회

#### `event_manager_get_stats()`
```c
void event_manager_get_stats(event_manager_stats_t *stats);
```
- **설명**: 통계 정보 조회

#### `event_manager_get_topic_name()`
```c
const char* event_manager_get_topic_name(event_topic_t topic);
```
- **설명**: 토픽 이름 문자열 반환

---

## 사용 예제

### 예제 1: GPS → LoRa 전송

```c
// LoRa 모듈
static event_subscriber_t lora_subscriber;

void lora_gps_handler(const event_data_t *event, void *user_data) {
    gps_data_t *gps = (gps_data_t *)event->data;
    lora_send_data(gps, sizeof(gps_data_t));
}

void lora_init(void) {
    event_manager_subscribe(&lora_subscriber, EVENT_GPS_DATA_READY,
                           lora_gps_handler, NULL, 10, "LoRa");
}

// GPS 모듈
void gps_task(void *p) {
    gps_data_t data;
    while (1) {
        // GPS 데이터 업데이트
        event_manager_publish(EVENT_GPS_DATA_READY, &data, sizeof(data), NULL);
        vTaskDelay(1000);
    }
}
```

### 예제 2: 우선순위 제어

```c
// BLE는 빠른 응답 필요 (우선순위 5)
event_manager_subscribe(&ble_sub, EVENT_GPS_DATA_READY,
                       ble_handler, NULL, 5, "BLE");

// LoRa는 느려도 됨 (우선순위 20)
event_manager_subscribe(&lora_sub, EVENT_GPS_DATA_READY,
                       lora_handler, NULL, 20, "LoRa");

// 이벤트 발행 시 실행 순서: BLE → LoRa
```

### 예제 3: 여러 토픽 구독

```c
static event_subscriber_t gps_sub;
static event_subscriber_t rtcm_sub;
static event_subscriber_t error_sub;

void my_event_handler(const event_data_t *event, void *user_data) {
    switch (event->topic) {
        case EVENT_GPS_DATA_READY:
            // GPS 처리
            break;
        case EVENT_RTCM_DATA_RECEIVED:
            // RTCM 처리
            break;
        case EVENT_SYSTEM_ERROR:
            // 에러 처리
            break;
    }
}

void init(void) {
    event_manager_subscribe(&gps_sub, EVENT_GPS_DATA_READY,
                           my_event_handler, NULL, 10, "Handler");
    event_manager_subscribe(&rtcm_sub, EVENT_RTCM_DATA_RECEIVED,
                           my_event_handler, NULL, 10, "Handler");
    event_manager_subscribe(&error_sub, EVENT_SYSTEM_ERROR,
                           my_event_handler, NULL, 0, "Handler");
}
```

### 예제 4: 사용자 데이터 활용

```c
typedef struct {
    uint8_t instance_id;
    uint32_t packet_count;
} my_context_t;

static my_context_t ctx1 = {.instance_id = 1};
static my_context_t ctx2 = {.instance_id = 2};

void handler(const event_data_t *event, void *user_data) {
    my_context_t *ctx = (my_context_t *)user_data;
    ctx->packet_count++;
    printf("Instance %d: count=%lu\n", ctx->instance_id, ctx->packet_count);
}

void init(void) {
    static event_subscriber_t sub1, sub2;
    event_manager_subscribe(&sub1, EVENT_GPS_DATA_READY, handler, &ctx1, 10, "Inst1");
    event_manager_subscribe(&sub2, EVENT_GPS_DATA_READY, handler, &ctx2, 10, "Inst2");
}
```

---

## 성능 및 메모리

### 메모리 사용량

| 항목 | 크기 | 개수 | 총합 |
|------|------|------|------|
| `event_topic_info_t` | ~32 bytes | 19 (토픽 수) | ~608 bytes |
| `event_subscriber_t` | ~48 bytes | 사용자 정의 | N × 48 bytes |
| Event Queue | ~96 bytes/item | 32 | ~3 KB |
| Task Stack | 512 words | 1 | 2 KB |
| **총계** | | | ~6 KB + (N × 48) |

### 성능 특성

- **구독**: O(log N) - FreeRTOS List가 정렬된 삽입 사용
- **발행**: O(N) - N은 해당 토픽의 구독자 수
- **우선순위 순회**: O(N) - 이미 정렬되어 있음

### 최대 제한

```c
#define EVENT_MANAGER_MAX_SUBSCRIBERS_PER_TOPIC  16  // 토픽당 최대 구독자
#define EVENT_MANAGER_QUEUE_SIZE                 32  // ISR 이벤트 큐 크기
```

---

## 주의사항

### ⚠️ 중요한 주의사항

1. **Subscriber는 정적 할당 필수**
   ```c
   // ❌ 잘못됨: 스택에 할당
   void func() {
       event_subscriber_t sub;  // 함수 종료 시 사라짐!
       event_manager_subscribe(&sub, ...);
   }

   // ✅ 올바름: 정적 또는 전역 변수
   static event_subscriber_t sub;  // OK
   ```

2. **콜백 함수는 빠르게 실행**
   - 콜백에서 긴 작업 금지
   - 필요시 큐에 넣고 태스크에서 처리

3. **ISR에서는 반드시 _from_isr 사용**
   ```c
   // ❌ ISR에서 일반 publish 호출 금지
   event_manager_publish(...);  // 데드락 가능!

   // ✅ ISR 전용 함수 사용
   event_manager_publish_from_isr(...);
   ```

4. **데이터 수명 주의**
   ```c
   // ❌ 위험: 로컬 변수 포인터 전달
   void func() {
       uint8_t data[64];
       event_manager_publish(topic, data, 64, NULL);
       // data가 사라지기 전에 모든 핸들러 실행 보장?
   }

   // ✅ 안전: 정적 버퍼 또는 복사
   static uint8_t data[64];
   ```

5. **Mutex 타임아웃**
   - 기본 타임아웃: 100ms
   - 타임아웃 발생 시 이벤트 손실 가능

---

## FAQ

### Q1: 새 이벤트 토픽을 어떻게 추가하나요?

`event_manager.h`에서 `event_topic_t` enum에 추가:
```c
typedef enum {
    // ... 기존 이벤트들 ...
    EVENT_MY_NEW_EVENT,    // 새 이벤트 추가
    EVENT_TOPIC_MAX
} event_topic_t;
```

그리고 `event_manager.c`의 `topic_names` 배열에도 추가:
```c
static const char *topic_names[] = {
    // ... 기존 이름들 ...
    "MY_NEW_EVENT",  // 같은 순서로 추가
};
```

### Q2: 같은 모듈에서 여러 토픽을 구독하려면?

각 토픽마다 별도의 subscriber 필요:
```c
static event_subscriber_t sub1, sub2, sub3;

event_manager_subscribe(&sub1, EVENT_GPS_DATA_READY, handler, NULL, 10, "Sub1");
event_manager_subscribe(&sub2, EVENT_RTCM_DATA_RECEIVED, handler, NULL, 10, "Sub2");
event_manager_subscribe(&sub3, EVENT_SYSTEM_ERROR, handler, NULL, 0, "Sub3");
```

또는 하나의 핸들러에서 토픽 분기:
```c
void unified_handler(const event_data_t *event, void *user_data) {
    switch (event->topic) {
        case EVENT_GPS_DATA_READY: /* ... */ break;
        case EVENT_RTCM_DATA_RECEIVED: /* ... */ break;
        case EVENT_SYSTEM_ERROR: /* ... */ break;
    }
}
```

### Q3: 이벤트 발행 시 구독자가 없으면?

문제없음. 단순히 0을 반환:
```c
uint32_t count = event_manager_publish(...);
if (count == 0) {
    // 아무도 구독하지 않음
}
```

### Q4: 콜백 함수에서 다른 이벤트를 발행할 수 있나요?

가능하지만 주의:
```c
void handler(const event_data_t *event, void *user_data) {
    // OK: 다른 토픽 발행
    event_manager_publish(EVENT_PARAM_CHANGED, ...);

    // 주의: 같은 토픽 발행 시 재귀 가능!
    // event_manager_publish(event->topic, ...);  // 위험!
}
```

### Q5: 메모리가 부족한데 큐 크기를 줄일 수 있나요?

`event_manager.h`에서 설정:
```c
#define EVENT_MANAGER_QUEUE_SIZE  16  // 32 → 16으로 줄임
```

---

## 빌드 설정

### Makefile에 추가

```makefile
# Event Manager 소스
C_SOURCES += \
lib/event_manager/event_manager.c

# Include 경로
C_INCLUDES += \
-Ilib/event_manager
```

### STM32CubeIDE에서

1. 프로젝트 우클릭 → Properties
2. C/C++ Build → Settings → Include paths
3. `lib/event_manager` 추가
4. Source Locations에 `lib/event_manager/event_manager.c` 추가

---

## 라이선스

이 코드는 프로젝트의 라이선스를 따릅니다.

---

## 지원

- 이슈: GitHub Issues
- 문의: mj.kang@iotiz.kr
- 예제: `lib/event_manager/examples/`

---

**Happy Coding! 🚀**
