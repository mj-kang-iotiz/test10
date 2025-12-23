## Event Manager 동기 요청-응답 API 가이드

중앙관리 앱에서 동기적 명령-응답을 처리하는 방법

---

## 🎯 개요

**문제**: GPS 초기화처럼 "명령 보내고 → 응답 대기" 하는 동기적 작업을 PubSub에서 처리하고 싶다

**해결**: Event Manager에 **Request-Response API** 추가!

---

## ✨ 특징

- ✅ **중앙 관리**: 모든 요청이 Event Manager를 통해 처리
- ✅ **동기 대기**: 응답이 올 때까지 blocking
- ✅ **타임아웃**: 자동 타임아웃 처리
- ✅ **독립성**: 모듈 간 직접 의존 없음
- ✅ **확장성**: 새 명령 추가가 쉬움

---

## 📚 사용 방법

### 1단계: 초기화

```c
void initThread(void *p) {
    // Event Manager 초기화
    event_manager_init();

    // 동기 API 초기화
    event_manager_sync_init();  // ← 추가!
}
```

### 2단계: 명령 핸들러 등록 (GPS 모듈)

```c
// GPS 모듈에서 핸들러 구현
static bool gps_command_handler(event_request_t *request) {
    gps_command_request_t *cmd = (gps_command_request_t *)request->request_data;
    gps_command_response_t response = {0};

    // 명령 처리
    switch (cmd->cmd_type) {
        case GPS_CMD_SET_BAUDRATE:
            gps_set_baudrate(cmd->baudrate);
            response.success = true;
            break;

        case GPS_CMD_CONFIGURE_MSG:
            gps_configure_messages();
            response.success = true;
            break;
    }

    // 응답 전송
    return event_manager_send_response(request, &response, sizeof(response));
}

// GPS 초기화 시 핸들러 등록
void gps_module_init(void) {
    event_manager_register_request_handler(
        EVENT_GPS_COMMAND_REQUEST,
        gps_command_handler
    );
}
```

### 3단계: 동기 요청 전송 (초기화 코드)

```c
bool gps_initialize(void) {
    gps_command_request_t cmd_req;
    gps_command_response_t cmd_resp;
    size_t resp_len;

    // Baudrate 설정 요청
    cmd_req.cmd_type = GPS_CMD_SET_BAUDRATE;
    cmd_req.baudrate = 115200;

    // 동기 요청 전송 (응답 대기)
    bool success = event_manager_send_request(
        EVENT_GPS_COMMAND_REQUEST,      // 토픽
        &cmd_req, sizeof(cmd_req),      // 요청 데이터
        &cmd_resp, sizeof(cmd_resp),    // 응답 버퍼
        &resp_len,                      // 응답 길이
        1000                            // 타임아웃 (ms)
    );

    if (!success) {
        printf("GPS command failed!\n");
        return false;
    }

    if (!cmd_resp.success) {
        printf("GPS returned error\n");
        return false;
    }

    printf("✓ GPS baudrate set!\n");
    return true;
}
```

---

## 🔥 전체 흐름

```
초기화 코드 (main.c)
    ↓
event_manager_send_request()  ← 요청 전송
    ↓
Event Manager (중앙)
    ↓
gps_command_handler()         ← GPS 모듈에서 처리
    ↓
event_manager_send_response() ← 응답 전송
    ↓
초기화 코드 복귀             ← 응답 받음 (동기)
```

---

## 💡 실전 예제: GPS F9P 초기화

```c
bool gps_f9p_init_with_sync_api(void) {
    gps_command_request_t cmd;
    gps_command_response_t resp;
    size_t resp_len;

    // 1. Baudrate 설정
    cmd.cmd_type = GPS_CMD_SET_BAUDRATE;
    cmd.baudrate = 115200;
    if (!event_manager_send_request(EVENT_GPS_COMMAND_REQUEST,
                                    &cmd, sizeof(cmd),
                                    &resp, sizeof(resp),
                                    &resp_len, 1000)) {
        return false;
    }

    // 2. 메시지 설정
    cmd.cmd_type = GPS_CMD_CONFIGURE_MESSAGES;
    if (!event_manager_send_request(EVENT_GPS_COMMAND_REQUEST,
                                    &cmd, sizeof(cmd),
                                    &resp, sizeof(resp),
                                    &resp_len, 1000)) {
        return false;
    }

    // 3. RTK 모드
    cmd.cmd_type = GPS_CMD_SET_RTK_MODE;
    cmd.rtk_mode = 1;  // Base
    if (!event_manager_send_request(EVENT_GPS_COMMAND_REQUEST,
                                    &cmd, sizeof(cmd),
                                    &resp, sizeof(resp),
                                    &resp_len, 1000)) {
        return false;
    }

    // 4. 설정 저장
    cmd.cmd_type = GPS_CMD_SAVE_CONFIG;
    if (!event_manager_send_request(EVENT_GPS_COMMAND_REQUEST,
                                    &cmd, sizeof(cmd),
                                    &resp, sizeof(resp),
                                    &resp_len, 2000)) {
        return false;
    }

    // 5. 완료 이벤트
    event_manager_publish(EVENT_GPS_INIT_COMPLETE, NULL, 0, NULL);

    return true;
}
```

---

## 🆚 비교: 직접 호출 vs 동기 API

| 항목 | 직접 호출 | 동기 API |
|------|----------|---------|
| **코드** | `gps_set_baudrate(115200);` | `event_manager_send_request(...)` |
| **결합도** | 높음 (GPS 직접 의존) | 낮음 (Event Manager만 의존) |
| **타임아웃** | 직접 구현 | 자동 처리 |
| **에러 처리** | 직접 구현 | 응답에 포함 |
| **확장성** | 낮음 | 높음 |
| **성능** | 최고 | 약간 오버헤드 |
| **권장 사용** | 간단한 프로젝트 | 복잡한/확장 가능한 시스템 |

---

## 📋 API 레퍼런스

### `event_manager_sync_init()`
```c
bool event_manager_sync_init(void);
```
- 동기 API 초기화
- `event_manager_init()` 이후 호출

### `event_manager_register_request_handler()`
```c
bool event_manager_register_request_handler(
    event_topic_t topic,
    request_handler_t handler
);
```
- 요청 핸들러 등록
- 토픽당 하나의 핸들러만

### `event_manager_send_request()`
```c
bool event_manager_send_request(
    event_topic_t topic,
    void *request_data,
    size_t request_len,
    void *response_data,
    size_t max_response_len,
    size_t *response_len,
    uint32_t timeout_ms
);
```
- 동기 요청 전송
- 응답 올 때까지 blocking
- 반환: `true` = 성공, `false` = 실패/타임아웃

### `event_manager_send_response()`
```c
bool event_manager_send_response(
    event_request_t *request,
    void *response_data,
    size_t response_len
);
```
- 핸들러에서 응답 전송

### `event_manager_send_error()`
```c
void event_manager_send_error(event_request_t *request);
```
- 핸들러에서 에러 응답

---

## ⚠️ 주의사항

### ✅ 해야 할 것
```c
// 핸들러 등록 (모듈 init에서)
event_manager_register_request_handler(topic, handler);

// 타임아웃 설정
event_manager_send_request(..., 1000);  // 1초

// 에러 처리
if (!event_manager_send_request(...)) {
    // 실패 처리
}
```

### ❌ 하지 말아야 할 것
```c
// ISR에서 호출 금지 (blocking)
void ISR() {
    event_manager_send_request(...);  // ❌
}

// 핸들러에서 다른 요청 전송 금지 (데드락)
bool handler(event_request_t *req) {
    event_manager_send_request(...);  // ❌
}
```

---

## 🎓 언제 사용할까?

### ✅ 동기 API 사용
- GPS 초기화 (설정 명령)
- 모듈 설정 변경 (응답 필요)
- 상태 조회 (현재 값 읽기)
- 런타임 재설정

### ❌ 일반 PubSub 사용
- 데이터 전송 (GPS → LoRa)
- 이벤트 알림 (연결/해제)
- 실시간 데이터 (센서 값)

---

## 📁 파일 구조

```
lib/event_manager/
├── event_manager.h              # 기본 PubSub API
├── event_manager.c
├── event_manager_sync.h         # ⭐ 동기 API
├── event_manager_sync.c         # ⭐ 동기 API 구현
├── README.md
├── SYNC_API_GUIDE.md           # 이 문서
└── examples/
    └── sync_request_response_example.c  # 사용 예제
```

---

## 🚀 빠른 시작

### 1. 초기화
```c
event_manager_init();
event_manager_sync_init();
```

### 2. 핸들러 등록 (GPS 모듈)
```c
event_manager_register_request_handler(EVENT_GPS_COMMAND_REQUEST, handler);
```

### 3. 요청 전송 (초기화 코드)
```c
event_manager_send_request(EVENT_GPS_COMMAND_REQUEST, &cmd, ..., 1000);
```

### 4. 완료!
```
요청 → Event Manager → GPS 핸들러 → 응답 → 완료
```

---

## 💡 팁

1. **타임아웃 설정**: 명령마다 적절한 타임아웃 (보통 1-2초)
2. **에러 처리**: 항상 반환값 확인
3. **응답 버퍼**: 충분한 크기로 할당
4. **핸들러 성능**: 빠르게 처리 (오래 걸리면 큐 사용)

---

**이제 GPS 초기화를 중앙에서 깔끔하게 관리하세요! 🎉**
