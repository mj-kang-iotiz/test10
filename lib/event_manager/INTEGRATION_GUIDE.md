# Event Manager 통합 가이드

현재 프로젝트에 Event Manager를 통합하는 단계별 가이드

## 🎯 목표

기존 코드를 최소한으로 수정하면서 Event Manager를 도입하여 모듈 간 결합도를 낮춥니다.

---

## 📋 통합 단계

### 1단계: main.c 수정

`Core/Src/main.c`의 `initThread` 함수에 Event Manager 초기화 추가:

```c
// USER CODE BEGIN Includes
#include "FreeRTOS.h"
#include "SEGGER_SYSVIEW.h"
#include "event_manager.h"  // ← 추가
// ... 기존 includes ...
```

```c
void initThread(void *pvParameter) {
    const board_config_t *config = board_get_config();
    user_params_t* params = flash_params_get_current();

    flash_params_init();

    // ===== Event Manager 초기화 (가장 먼저!) =====
    if (!event_manager_init()) {
        // 초기화 실패 시 LED로 표시하거나 에러 처리
        while (1) {
            // Error indication
        }
    }
    // ============================================

    led_init();

    if(config->board == BOARD_TYPE_BASE_F9P || config->board == BOARD_TYPE_BASE_UM982) {
        gps_init_all();
        // GPS 이벤트 설정 (새로 추가)
        gps_module_event_init();

        if(params->use_manual_position == false) {
            gsm_task_create(NULL);
            // GSM 이벤트 설정 (새로 추가)
            gsm_module_event_init();
        }

        lora_instance_init();
        // LoRa 이벤트 설정 (새로 추가)
        lora_module_event_init();
    }

    if(config->use_rs485) {
        dwt_init();
        #if USE_SOFTUART
            rs485_app_init();
        #else
            rs485_init_all();
        #endif
    }

    if(config->use_ble) {
        ble_init_all();
        // BLE 이벤트 설정 (새로 추가)
        ble_module_event_init();
    }

    vTaskDelete(NULL);
}
```

---

### 2단계: GPS 모듈 수정

#### 2-1. `modules/gps/gps_app.h`에 추가

```c
#ifndef GPS_APP_H
#define GPS_APP_H

// 기존 코드...

// ===== Event Manager 연동 함수 추가 =====
void gps_module_event_init(void);
void gps_publish_position_update(const gps_position_t *position);
void gps_publish_fix_status_changed(uint8_t old_fix, uint8_t new_fix);
// ======================================

#endif
```

#### 2-2. `modules/gps/gps_app.c`에 구현 추가

```c
#include "gps_app.h"
#include "event_manager.h"

// ===== Event Manager용 전역 변수 =====
static event_subscriber_t gps_rtcm_subscriber;
static uint8_t last_fix_type = 0;
// ====================================

// RTCM 데이터 핸들러
static void gps_rtcm_event_handler(const event_data_t *event, void *user_data) {
    if (event->topic == EVENT_RTCM_DATA_RECEIVED && event->data && event->data_len > 0) {
        // GPS UART로 RTCM 데이터 전송
        // 예: uart_send(GPS_UART, event->data, event->data_len);
        // 또는 기존 gps_inject_rtcm() 함수 호출
    }
}

// GPS 모듈 이벤트 초기화
void gps_module_event_init(void) {
    // RTCM 데이터 구독
    event_manager_subscribe(
        &gps_rtcm_subscriber,
        EVENT_RTCM_DATA_RECEIVED,
        gps_rtcm_event_handler,
        NULL,
        0,  // 최고 우선순위
        "GPS_RTCM"
    );
}

// GPS 위치 업데이트 발행
void gps_publish_position_update(const gps_position_t *position) {
    if (position) {
        event_manager_publish(
            EVENT_GPS_POSITION_UPDATED,
            (void*)position,
            sizeof(gps_position_t),
            (void*)"GPS"
        );
    }
}

// GPS Fix 상태 변경 발행
void gps_publish_fix_status_changed(uint8_t old_fix, uint8_t new_fix) {
    typedef struct { uint8_t old_fix; uint8_t new_fix; } fix_status_t;
    static fix_status_t status;
    status.old_fix = old_fix;
    status.new_fix = new_fix;

    event_manager_publish(
        EVENT_GPS_FIX_STATUS_CHANGED,
        &status,
        sizeof(status),
        (void*)"GPS"
    );
}

// 기존 GPS 태스크 수정
void gps_task(void *pvParameters) {
    gps_position_t position;

    while (1) {
        // 기존 GPS 파싱 코드...
        // gps_parse() 등

        // GPS 데이터가 업데이트되면 이벤트 발행
        if (gps_get_position(&position) == GPS_OK) {
            gps_publish_position_update(&position);

            // Fix 상태 변경 체크
            if (position.fix_type != last_fix_type) {
                gps_publish_fix_status_changed(last_fix_type, position.fix_type);
                last_fix_type = position.fix_type;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

### 3단계: GSM/NTRIP 모듈 수정

#### 3-1. `modules/gsm/ntrip_app.h`에 추가

```c
void gsm_module_event_init(void);
void ntrip_publish_rtcm_data(const uint8_t *data, size_t len);
```

#### 3-2. `modules/gsm/ntrip_app.c`에 구현

```c
#include "ntrip_app.h"
#include "event_manager.h"

void gsm_module_event_init(void) {
    // GSM은 주로 발행자 역할만 수행
    // 필요시 다른 이벤트 구독 가능
}

void ntrip_publish_rtcm_data(const uint8_t *data, size_t len) {
    if (data && len > 0) {
        // RTCM 데이터 이벤트 발행
        event_manager_publish(
            EVENT_RTCM_DATA_RECEIVED,
            (void*)data,
            len,
            (void*)"NTRIP"
        );
    }
}

// 기존 NTRIP 태스크 수정
void ntrip_task(void *pvParameters) {
    uint8_t buffer[512];
    size_t len;

    while (1) {
        // NTRIP 서버에서 데이터 수신
        if (ntrip_receive(buffer, sizeof(buffer), &len) == NTRIP_OK) {
            // ===== 기존 코드 제거 또는 주석 처리 =====
            // gps_inject_rtcm(buffer, len);  // 직접 호출 대신
            // lora_send_rtcm(buffer, len);    // 이벤트로 대체
            // ========================================

            // ===== 이벤트로 발행 =====
            ntrip_publish_rtcm_data(buffer, len);
            // ========================
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

### 4단계: LoRa 모듈 수정

#### 4-1. `modules/lora/lora_app.h`에 추가

```c
void lora_module_event_init(void);
```

#### 4-2. `modules/lora/lora_app.c`에 구현

```c
#include "lora_app.h"
#include "event_manager.h"
#include "board_config.h"

// Subscriber 변수
static event_subscriber_t lora_gps_subscriber;
static event_subscriber_t lora_rtcm_subscriber;

// GPS 데이터 핸들러
static void lora_gps_handler(const event_data_t *event, void *user_data) {
    if (event->topic == EVENT_GPS_POSITION_UPDATED && event->data) {
        // GPS 데이터를 LoRa로 전송
        lora_send_data(event->data, event->data_len);
    }
}

// RTCM 데이터 핸들러 (Base 스테이션용)
static void lora_rtcm_handler(const event_data_t *event, void *user_data) {
    const board_config_t *config = board_get_config();

    // Base 스테이션만 RTCM을 LoRa로 브로드캐스트
    if (config->board == BOARD_TYPE_BASE_F9P ||
        config->board == BOARD_TYPE_BASE_UM982) {

        if (event->data && event->data_len > 0) {
            lora_broadcast_rtcm(event->data, event->data_len);
        }
    }
}

void lora_module_event_init(void) {
    // GPS 위치 이벤트 구독
    event_manager_subscribe(
        &lora_gps_subscriber,
        EVENT_GPS_POSITION_UPDATED,
        lora_gps_handler,
        NULL,
        10,
        "LoRa_GPS"
    );

    // RTCM 데이터 이벤트 구독
    event_manager_subscribe(
        &lora_rtcm_subscriber,
        EVENT_RTCM_DATA_RECEIVED,
        lora_rtcm_handler,
        NULL,
        20,  // GPS보다 낮은 우선순위
        "LoRa_RTCM"
    );
}
```

---

### 5단계: BLE 모듈 수정

#### 5-1. `modules/ble/ble_app.h`에 추가

```c
void ble_module_event_init(void);
```

#### 5-2. `modules/ble/ble_app.c`에 구현

```c
#include "ble_app.h"
#include "event_manager.h"

static event_subscriber_t ble_gps_subscriber;

static void ble_gps_handler(const event_data_t *event, void *user_data) {
    // BLE가 연결되어 있으면 notification 전송
    if (ble_is_connected() && event->data) {
        ble_send_notification(BLE_CHAR_GPS, event->data, event->data_len);
    }
}

void ble_module_event_init(void) {
    event_manager_subscribe(
        &ble_gps_subscriber,
        EVENT_GPS_POSITION_UPDATED,
        ble_gps_handler,
        NULL,
        5,  // 높은 우선순위 (사용자 응답성)
        "BLE_GPS"
    );
}
```

---

## 🔧 Makefile 수정

프로젝트 Makefile에 Event Manager 추가:

```makefile
# Event Manager 소스 파일
C_SOURCES += \
lib/event_manager/event_manager.c

# Include 경로
C_INCLUDES += \
-Ilib/event_manager
```

---

## ✅ 테스트 체크리스트

통합 후 다음 항목들을 확인하세요:

- [ ] 시스템이 정상적으로 부팅되는가?
- [ ] GPS 데이터가 LoRa/BLE로 전송되는가?
- [ ] NTRIP RTCM 데이터가 GPS로 전달되는가?
- [ ] Base 스테이션에서 RTCM이 LoRa로 브로드캐스트되는가?
- [ ] 메모리 사용량이 허용 범위 내인가?
- [ ] CPU 사용률이 정상인가?

---

## 🐛 문제 해결

### Event Manager 초기화 실패

**증상**: `event_manager_init()` 반환값이 `false`

**원인**:
- 힙 메모리 부족
- FreeRTOS 설정 문제

**해결**:
1. `FreeRTOSConfig.h`에서 `configTOTAL_HEAP_SIZE` 증가
2. `EVENT_MANAGER_QUEUE_SIZE` 감소 (32 → 16)

### 이벤트가 전달되지 않음

**증상**: 이벤트를 발행했지만 핸들러가 호출되지 않음

**체크리스트**:
1. `event_manager_init()` 호출 확인
2. 구독 성공 여부 확인: `if (!event_manager_subscribe(...)) { /* 실패 */ }`
3. 토픽이 올바른지 확인
4. Subscriber가 활성화 상태인지 확인

### 데드락 발생

**증상**: 시스템이 멈춤

**원인**:
- ISR에서 `event_manager_publish()` 호출 (mutex)
- 콜백에서 같은 토픽 재귀 발행

**해결**:
1. ISR에서는 반드시 `event_manager_publish_from_isr()` 사용
2. 콜백에서 같은 토픽 발행 금지

---

## 📊 성능 모니터링

통계 확인 코드:

```c
void print_event_stats(void) {
    event_manager_stats_t stats;
    event_manager_get_stats(&stats);

    printf("=== Event Manager Stats ===\n");
    printf("Published: %lu\n", stats.total_published);
    printf("Delivered: %lu\n", stats.total_delivered);
    printf("Subscribers: %lu\n", stats.total_subscribers);
    printf("Max/Topic: %lu\n", stats.max_subscribers_per_topic);

    // 토픽별 구독자 수
    for (int i = 0; i < EVENT_TOPIC_MAX; i++) {
        uint32_t count = event_manager_get_subscriber_count(i);
        if (count > 0) {
            printf("%s: %lu subscribers\n",
                   event_manager_get_topic_name(i), count);
        }
    }
}

// 1분마다 통계 출력
void stats_task(void *p) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        print_event_stats();
    }
}
```

---

## 🚀 점진적 마이그레이션 전략

모든 모듈을 한 번에 바꾸지 말고 단계적으로:

### Phase 1: GPS → LoRa
1. GPS에서 위치 이벤트 발행
2. LoRa에서 구독
3. 기존 직접 호출 코드는 유지 (백업)
4. 테스트 후 기존 코드 제거

### Phase 2: NTRIP → GPS
1. NTRIP에서 RTCM 이벤트 발행
2. GPS에서 구독
3. 테스트

### Phase 3: 나머지 모듈
1. BLE, RS485 등 추가
2. 전체 통합 테스트

---

## 💡 권장 사항

1. **디버깅 활성화**: 처음에는 모든 핸들러에 printf/log 추가
2. **통계 모니터링**: 주기적으로 통계 확인
3. **에러 핸들링**: `EVENT_SYSTEM_ERROR` 적극 활용
4. **문서화**: 각 모듈이 어떤 이벤트를 구독/발행하는지 문서화

---

## 📞 지원

문제가 있으면:
1. `README.md`의 FAQ 확인
2. `examples/` 디렉토리의 예제 참조
3. 이슈 리포트: GitHub Issues

---

**통합 성공을 기원합니다! 🎉**
