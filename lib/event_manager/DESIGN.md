# Event Manager 설계 문서

## 설계 목표

1. **모듈 간 느슨한 결합**: 각 모듈이 독립적으로 개발/테스트 가능
2. **확장성**: 새 모듈/이벤트 추가가 쉬워야 함
3. **성능**: 실시간 임베디드 시스템에 적합한 성능
4. **안전성**: Thread-safe, ISR-safe
5. **메모리 효율**: 동적 할당 없이 정적 메모리만 사용

---

## 핵심 설계 결정

### 1. FreeRTOS List 사용

**결정**: STL이나 커스텀 자료구조 대신 FreeRTOS List 사용

**이유**:
- ✅ 이미 FreeRTOS에 포함되어 있음 (추가 코드 불필요)
- ✅ 우선순위 기반 정렬 자동 지원
- ✅ 잘 테스트되고 안정적
- ✅ 메모리 오버헤드 최소

**구현**:
```c
// 각 토픽마다 하나의 List
typedef struct {
    List_t subscriber_list;  // FreeRTOS List
    SemaphoreHandle_t mutex; // Thread-safe
} event_topic_info_t;

// 구독자는 ListItem을 포함
typedef struct {
    ListItem_t list_item;    // List에 삽입될 항목
    event_callback_t callback;
    // ...
} event_subscriber_t;
```

### 2. 토픽별 독립 Mutex

**결정**: 전역 mutex 대신 토픽별 mutex

**이유**:
- ✅ 동시성 향상: 서로 다른 토픽은 병렬 처리 가능
- ✅ 데드락 위험 감소
- ✅ 성능 향상

**트레이드오프**:
- ❌ 메모리 사용량 증가 (토픽당 mutex)
- 결론: 성능 > 메모리 (mutex는 작음)

### 3. 정적 메모리 할당

**결정**: 모든 subscriber는 사용자가 정적 할당

**이유**:
- ✅ 동적 할당 오버헤드 없음
- ✅ 메모리 단편화 방지
- ✅ 예측 가능한 메모리 사용
- ✅ MISRA-C 호환

**사용자 책임**:
```c
// ✅ 올바름
static event_subscriber_t sub;
event_manager_subscribe(&sub, ...);

// ❌ 잘못됨
void func() {
    event_subscriber_t sub;  // 스택!
    event_manager_subscribe(&sub, ...);
}  // sub 사라짐!
```

### 4. 동기 vs 비동기 발행

**결정**: 두 가지 모드 모두 지원

**동기 모드** (`event_manager_publish`):
- 호출 즉시 모든 핸들러 실행
- 빠른 응답
- ISR에서 사용 불가

**비동기 모드** (`event_manager_publish_from_isr`):
- 큐에 저장 후 태스크에서 처리
- ISR-safe
- 약간의 지연

**선택 기준**:
- ISR에서 발행 → 반드시 비동기
- 일반 태스크 → 응답 속도 중요하면 동기, 아니면 비동기

### 5. 우선순위 기반 실행

**결정**: 구독자마다 우선순위 할당

**이유**:
- ✅ 중요한 핸들러 먼저 실행
- ✅ 실시간 시스템에 적합

**예시**:
```c
// BLE: 사용자 응답성 중요 (우선순위 5)
event_manager_subscribe(&ble_sub, topic, handler, NULL, 5, "BLE");

// LoRa: 약간 늦어도 됨 (우선순위 20)
event_manager_subscribe(&lora_sub, topic, handler, NULL, 20, "LoRa");
```

실행 순서: BLE → LoRa

---

## 데이터 구조

### 전체 구조

```
┌─────────────────────────────────────────────────┐
│ event_topic_info_t topics[EVENT_TOPIC_MAX]      │
├─────────────────────────────────────────────────┤
│ [0] EVENT_GPS_DATA_READY                        │
│     ├─ List_t subscriber_list                   │
│     │    └─ [Sub1:p=5] → [Sub2:p=10] → ...     │
│     ├─ SemaphoreHandle_t mutex                  │
│     └─ uint32_t subscriber_count = 2            │
├─────────────────────────────────────────────────┤
│ [1] EVENT_GPS_FIX_STATUS_CHANGED                │
│     ├─ List_t subscriber_list (empty)           │
│     ├─ SemaphoreHandle_t mutex                  │
│     └─ uint32_t subscriber_count = 0            │
├─────────────────────────────────────────────────┤
│ [2] EVENT_RTCM_DATA_RECEIVED                    │
│     ├─ List_t subscriber_list                   │
│     │    └─ [GPS:p=0] → [LoRa:p=20]            │
│     ├─ SemaphoreHandle_t mutex                  │
│     └─ uint32_t subscriber_count = 2            │
└─────────────────────────────────────────────────┘
```

### Subscriber 구조

```c
typedef struct {
    // FreeRTOS ListItem (반드시 첫 번째 멤버)
    ListItem_t list_item;
        ├─ xItemValue: priority (우선순위)
        ├─ pxNext: 다음 subscriber
        ├─ pxPrevious: 이전 subscriber
        ├─ pvOwner: 이 subscriber 자신을 가리킴
        └─ pxContainer: 속한 List

    // Event Manager 데이터
    event_callback_t callback;    // 핸들러 함수
    void *user_data;              // 사용자 컨텍스트
    event_topic_t topic;          // 구독 중인 토픽
    uint32_t priority;            // 우선순위 (복사본)
    bool is_active;               // 활성화 상태
    const char *name;             // 디버깅용
} event_subscriber_t;
```

---

## 알고리즘

### 구독 (Subscribe)

```
1. 입력 검증
   - subscriber != NULL
   - callback != NULL
   - topic < EVENT_TOPIC_MAX

2. Subscriber 초기화
   - vListInitialiseItem(&subscriber->list_item)
   - listSET_LIST_ITEM_OWNER(&list_item, subscriber)
   - listSET_LIST_ITEM_VALUE(&list_item, priority)

3. 토픽 잠금
   - xSemaphoreTake(topic_mutex)

4. 최대 구독자 수 체크
   - if (count >= MAX) → fail

5. List에 삽입 (우선순위 순)
   - vListInsert(&topic_list, &subscriber->list_item)
   - FreeRTOS가 자동으로 정렬

6. 토픽 해제
   - xSemaphoreGive(topic_mutex)

복잡도: O(N) (정렬된 삽입)
```

### 발행 (Publish)

```
1. 입력 검증
   - topic < EVENT_TOPIC_MAX

2. 이벤트 데이터 생성
   - event_data_t { topic, timestamp, data, len, sender }

3. 토픽 잠금
   - xSemaphoreTake(topic_mutex)

4. List 순회 (우선순위 순)
   item = listGET_HEAD_ENTRY(list)
   end = listGET_END_MARKER(list)

   while (item != end) {
       subscriber = listGET_LIST_ITEM_OWNER(item)

       if (subscriber->is_active && subscriber->callback) {
           subscriber->callback(&event, subscriber->user_data)
       }

       item = listGET_NEXT(item)
   }

5. 토픽 해제
   - xSemaphoreGive(topic_mutex)

복잡도: O(N) (N = 해당 토픽의 구독자 수)
```

### ISR에서 발행

```
1. 이벤트를 큐 항목으로 변환
   - event_queue_item_t
   - 작은 데이터는 복사, 큰 데이터는 포인터

2. 큐에 전송 (ISR-safe)
   - xQueueSendFromISR(event_queue, &item, &woken)

3. Event Manager Task에서 처리
   - xQueueReceive(event_queue, &item, portMAX_DELAY)
   - 일반 publish 로직 실행

복잡도: O(1) (큐 삽입만)
```

---

## 동기화 전략

### Mutex 계층

```
Level 1: global_mutex (통계 접근)
         ├─ 매우 짧은 시간만 잠금
         └─ 다른 mutex와 동시 잠금 금지

Level 2: topic_mutex[i] (토픽별)
         ├─ 토픽 독립적으로 잠금 가능
         └─ 서로 다른 토픽은 병렬 처리
```

### 데드락 방지

1. **Mutex 순서 규칙**: global → topic (역순 금지)
2. **타임아웃 사용**: 모든 mutex는 100ms 타임아웃
3. **재귀 금지**: 콜백에서 같은 토픽 발행 금지
4. **ISR 분리**: ISR은 mutex 사용 안함 (큐만)

---

## 메모리 맵

### 정적 메모리 (컴파일 타임)

```
event_topic_info_t topics[19]:
  - 19 topics × ~32 bytes = ~608 bytes

event_queue:
  - 32 items × 96 bytes = ~3 KB

Event Manager Task:
  - Stack: 512 words = 2 KB
```

### 동적 메모리 (런타임, 힙)

```
Mutexes:
  - 19 topic mutexes + 1 global = 20 × ~88 bytes = ~1.76 KB

Queue:
  - ~200 bytes (FreeRTOS 오버헤드)
```

### 사용자 메모리 (정적)

```
event_subscriber_t per subscriber:
  - ~48 bytes

예시: 10개 subscriber = 480 bytes
```

**총 메모리**: ~6 KB + (N × 48 bytes)

---

## 성능 분석

### CPU 사용률

일반적인 시나리오:
- GPS 1Hz 업데이트
- 3개 구독자 (BLE, LoRa, RS485)
- 각 핸들러 100μs

계산:
- 1초당 1번 발행
- 3개 핸들러 × 100μs = 300μs
- CPU 사용률: 300μs / 1s = 0.03%

**결론**: 무시할 수 있는 수준

### 지연 시간

**동기 발행**:
- Mutex 획득: ~10μs
- List 순회: ~5μs per subscriber
- 핸들러 실행: 사용자 코드에 의존
- 총: ~20μs + 핸들러 시간

**비동기 발행** (ISR):
- 큐 삽입: ~5μs
- 태스크 전환: ~50μs
- 동기 발행 지연
- 총: ~70μs + 핸들러 시간

---

## 확장성

### 새 이벤트 추가

```c
// 1. enum에 추가
typedef enum {
    // ... 기존 ...
    EVENT_MY_NEW_EVENT,
    EVENT_TOPIC_MAX
} event_topic_t;

// 2. 이름 배열에 추가
static const char *topic_names[] = {
    // ... 기존 ...
    "MY_NEW_EVENT",
};

// 완료! 나머지는 자동
```

### 새 모듈 추가

```c
// 새 모듈의 subscriber 선언
static event_subscriber_t new_module_sub;

// 기존 이벤트 구독
void new_module_init(void) {
    event_manager_subscribe(&new_module_sub,
                           EVENT_GPS_DATA_READY,
                           new_module_handler,
                           NULL, 10, "NewModule");
}

// 완료! 기존 코드 수정 불필요
```

---

## 안전성

### Thread Safety

- ✅ 모든 공유 데이터는 mutex로 보호
- ✅ 토픽별 독립 mutex → 동시성 향상
- ✅ Critical section 최소화

### ISR Safety

- ✅ ISR 전용 함수 제공 (`_from_isr`)
- ✅ Mutex 대신 큐 사용
- ✅ FreeRTOS API만 사용

### 메모리 Safety

- ✅ 동적 할당 없음
- ✅ 정적 검증 가능
- ✅ 버퍼 오버플로우 방지

---

## 테스트 전략

### 단위 테스트

1. **구독/구독 해제**
   - 정상 케이스
   - NULL 파라미터
   - 중복 구독
   - 최대 구독자 초과

2. **발행**
   - 구독자 0명
   - 단일 구독자
   - 다수 구독자
   - 우선순위 순서

3. **동시성**
   - 여러 태스크에서 동시 구독
   - 여러 태스크에서 동시 발행
   - ISR + 태스크 동시

### 통합 테스트

1. **실제 시나리오**
   - GPS → LoRa/BLE 전송
   - NTRIP → GPS/LoRa 전송
   - 에러 핸들링

2. **스트레스 테스트**
   - 고속 발행 (>100Hz)
   - 많은 구독자 (>10개)
   - 장시간 실행 (>24시간)

---

## 제약사항

1. **최대 토픽 수**: 19개 (확장 가능)
2. **토픽당 최대 구독자**: 16개 (설정 가능)
3. **ISR 큐 크기**: 32개 (설정 가능)
4. **데이터 크기**: 제한 없음 (포인터 전달)
5. **콜백 실행 시간**: 짧아야 함 (<1ms 권장)

---

## 미래 개선 사항

### v1.1
- [ ] 이벤트 필터링 (조건부 전달)
- [ ] 이벤트 로깅 (디버깅)
- [ ] 성능 프로파일링 도구

### v2.0
- [ ] 동적 토픽 생성
- [ ] 이벤트 체이닝
- [ ] 와일드카드 구독 (topic*)

---

## 참고 자료

- FreeRTOS List API: https://www.freertos.org/a00111.html
- Observer Pattern: GoF Design Patterns
- Event-Driven Architecture: Martin Fowler

---

**작성일**: 2025-12-23
**버전**: 1.0
**작성자**: Event Manager Team
