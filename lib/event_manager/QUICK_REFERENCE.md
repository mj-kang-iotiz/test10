# Event Manager 빠른 참조

## 🚀 3분 안에 시작하기

### 1. 초기화 (main.c)
```c
#include "event_manager.h"

void initThread(void *p) {
    event_manager_init();  // 가장 먼저!
    // ... 나머지 초기화 ...
}
```

### 2. 구독 (subscriber.c)
```c
static event_subscriber_t my_sub;

void my_handler(const event_data_t *event, void *user_data) {
    // 이벤트 처리
}

void init() {
    event_manager_subscribe(&my_sub, EVENT_GPS_DATA_READY,
                           my_handler, NULL, 10, "MySub");
}
```

### 3. 발행 (publisher.c)
```c
void my_task(void *p) {
    gps_data_t data;
    event_manager_publish(EVENT_GPS_DATA_READY,
                         &data, sizeof(data), NULL);
}
```

---

## 📋 주요 함수 치트시트

| 함수 | 용도 | 사용 위치 |
|------|------|----------|
| `event_manager_init()` | 초기화 | main.c (최초 1회) |
| `event_manager_subscribe()` | 이벤트 구독 | 모듈 init |
| `event_manager_publish()` | 이벤트 발행 | 태스크 |
| `event_manager_publish_from_isr()` | ISR에서 발행 | ISR 핸들러 |
| `event_manager_unsubscribe()` | 구독 해제 | 정리 코드 |
| `event_manager_set_active()` | 활성화/비활성화 | 런타임 |

---

## 🎯 이벤트 목록

### GPS
- `EVENT_GPS_DATA_READY` - GPS 데이터 준비
- `EVENT_GPS_FIX_STATUS_CHANGED` - Fix 상태 변경
- `EVENT_GPS_POSITION_UPDATED` - 위치 업데이트

### RTCM
- `EVENT_RTCM_DATA_RECEIVED` - RTCM 데이터 수신
- `EVENT_RTCM_PARSE_COMPLETE` - 파싱 완료

### 통신 (GSM, LoRa, BLE, RS485)
- `EVENT_XXX_CONNECTED` / `DISCONNECTED`
- `EVENT_XXX_DATA_RECEIVED`
- `EVENT_XXX_TX_COMPLETE` / `RX_COMPLETE`

### 시스템
- `EVENT_SYSTEM_ERROR` - 에러 발생
- `EVENT_PARAM_CHANGED` - 설정 변경
- `EVENT_LOW_BATTERY` - 배터리 부족

---

## ⚡ 자주 사용하는 패턴

### 패턴 1: 단순 구독/발행
```c
// 발행자
event_manager_publish(EVENT_GPS_DATA_READY, &data, sizeof(data), NULL);

// 구독자
static event_subscriber_t sub;
event_manager_subscribe(&sub, EVENT_GPS_DATA_READY, handler, NULL, 10, "Name");
```

### 패턴 2: 우선순위 제어
```c
// 높은 우선순위 (먼저 실행)
event_manager_subscribe(&sub1, topic, handler1, NULL, 5, "High");

// 낮은 우선순위 (나중에 실행)
event_manager_subscribe(&sub2, topic, handler2, NULL, 20, "Low");
```

### 패턴 3: ISR에서 발행
```c
void UART_IRQHandler(void) {
    BaseType_t woken = pdFALSE;
    event_manager_publish_from_isr(EVENT_RTCM_DATA_RECEIVED,
                                   data, len, NULL, &woken);
    portYIELD_FROM_ISR(woken);
}
```

### 패턴 4: 사용자 데이터 활용
```c
typedef struct { int id; } context_t;
static context_t ctx = {.id = 1};

void handler(const event_data_t *event, void *user_data) {
    context_t *ctx = (context_t *)user_data;
    printf("ID: %d\n", ctx->id);
}

event_manager_subscribe(&sub, topic, handler, &ctx, 10, "Name");
```

### 패턴 5: 조건부 처리
```c
void handler(const event_data_t *event, void *user_data) {
    if (some_condition) {
        // 처리
    } else {
        // 무시
    }
}
```

### 패턴 6: 여러 토픽 구독
```c
static event_subscriber_t sub1, sub2, sub3;

void init() {
    event_manager_subscribe(&sub1, EVENT_GPS_DATA_READY, handler, NULL, 10, "S1");
    event_manager_subscribe(&sub2, EVENT_RTCM_DATA_RECEIVED, handler, NULL, 10, "S2");
    event_manager_subscribe(&sub3, EVENT_SYSTEM_ERROR, handler, NULL, 0, "S3");
}

void handler(const event_data_t *event, void *user_data) {
    switch (event->topic) {
        case EVENT_GPS_DATA_READY: /* ... */ break;
        case EVENT_RTCM_DATA_RECEIVED: /* ... */ break;
        case EVENT_SYSTEM_ERROR: /* ... */ break;
    }
}
```

---

## ⚠️ 금지사항

| ❌ 하지 마세요 | ✅ 대신 이렇게 하세요 |
|--------------|-------------------|
| 스택에 subscriber 할당 | 정적/전역 변수 사용 |
| ISR에서 `publish()` 호출 | `publish_from_isr()` 사용 |
| 콜백에서 긴 작업 | 큐에 넣고 태스크에서 처리 |
| 콜백에서 같은 토픽 재발행 | 다른 토픽으로 발행 |
| 초기화 없이 사용 | `event_manager_init()` 먼저 |

---

## 🔧 설정 변경

`event_manager.h`에서 수정:
```c
// 토픽당 최대 구독자 수
#define EVENT_MANAGER_MAX_SUBSCRIBERS_PER_TOPIC  16

// ISR 이벤트 큐 크기
#define EVENT_MANAGER_QUEUE_SIZE  32

// 태스크 우선순위
#define EVENT_MANAGER_TASK_PRIORITY  (tskIDLE_PRIORITY + 2)

// 태스크 스택 크기
#define EVENT_MANAGER_TASK_STACK_SIZE  512
```

---

## 🐛 디버깅

### 통계 확인
```c
event_manager_stats_t stats;
event_manager_get_stats(&stats);
printf("Published: %lu, Delivered: %lu\n",
       stats.total_published, stats.total_delivered);
```

### 구독자 수 확인
```c
uint32_t count = event_manager_get_subscriber_count(EVENT_GPS_DATA_READY);
printf("GPS subscribers: %lu\n", count);
```

### 토픽 이름 확인
```c
const char *name = event_manager_get_topic_name(EVENT_GPS_DATA_READY);
printf("Topic: %s\n", name);
```

---

## 📁 파일 구조

```
lib/event_manager/
├── event_manager.h         # API 헤더
├── event_manager.c         # 구현
├── README.md              # 전체 문서
├── INTEGRATION_GUIDE.md   # 통합 가이드
├── DESIGN.md              # 설계 문서
├── QUICK_REFERENCE.md     # 이 파일
└── examples/
    ├── example_usage.c           # 기본 예제
    ├── gps_module_integration.c  # GPS 통합
    ├── gsm_module_integration.c  # GSM 통합
    ├── lora_module_integration.c # LoRa 통합
    └── ble_module_integration.c  # BLE 통합
```

---

## 💡 팁

1. **subscriber는 항상 static 또는 전역 변수로**
   ```c
   static event_subscriber_t sub;  // ✅
   ```

2. **우선순위는 0이 가장 높음**
   - 0-9: 긴급 (시스템, 에러)
   - 10-19: 보통 (GPS, 센서)
   - 20-29: 낮음 (로깅, 통계)

3. **콜백은 빠르게**
   - 목표: <1ms
   - 긴 작업은 큐에 넣고 태스크에서

4. **이벤트 데이터 수명 주의**
   - 동기 발행: 함수 종료까지만 유효하면 OK
   - 비동기 발행: 정적 버퍼 또는 복사 필요

5. **디버깅은 name 활용**
   ```c
   event_manager_subscribe(..., "GPS_Main_Handler");
   ```

---

## 🆘 문제 해결

| 증상 | 원인 | 해결 |
|------|------|------|
| 초기화 실패 | 메모리 부족 | 힙 크기 증가 |
| 이벤트 안 옴 | 구독 실패 | 반환값 확인 |
| 시스템 멈춤 | ISR에서 publish | publish_from_isr 사용 |
| 순서 이상 | 우선순위 잘못 | 값 확인 (낮을수록 먼저) |

---

## 📞 더 많은 정보

- 전체 문서: `README.md`
- 통합 가이드: `INTEGRATION_GUIDE.md`
- 설계 문서: `DESIGN.md`
- 예제 코드: `examples/`

---

**즐거운 코딩 되세요! 🎉**
