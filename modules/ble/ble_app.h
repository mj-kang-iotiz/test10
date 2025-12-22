#ifndef BLE_APP_H
#define BLE_APP_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "ble.h"
#include <stdbool.h>
#include <stdint.h>

#define BLE_UART_MAX_RECV_SIZE 1024
#define BLE_AT_RESPONSE_MAX_SIZE 256

typedef enum
{
  BLE_CMD_PARSE_STATE_NONE,
  BLE_CMD_PARSE_STATE_GOT_A,
  BLE_CMD_PARSE_STATE_DATA,
  BLE_CMD_PARSE_STATE_APP,
} ble_cmd_parse_state_t;
typedef enum
{
  BLE_AT_STATUS_IDLE,
  BLE_AT_STATUS_PENDING,
  BLE_AT_STATUS_COMPLETED,
  BLE_AT_STATUS_TIMEOUT,
  BLE_AT_STATUS_ERROR,
} ble_at_status_t;

typedef enum
{
  BLE_MODE_BYPASS, // 일반 UART 통신 모드 (파싱 비활성화)
  BLE_MODE_AT,     // AT 커맨드 모드 (파싱 활성화)
} ble_mode_t;

typedef enum
{
  BLE_CONN_DISCONNECTED,
  BLE_CONN_CONNECTED,
} ble_connection_state_t;

// Bypass 모드 RX 데이터 콜백
typedef void (*ble_bypass_rx_callback_t)(const uint8_t *data, size_t len);

typedef void (*ble_at_command_callback_t)(bool result, void *user_data);

typedef struct
{
  char command[128];                  // AT 명령어
  ble_at_command_callback_t callback; // 완료 콜백
  void *user_data;                    // 사용자 데이터
  TickType_t start_tick;              // 시작 시각
  TickType_t timeout_ticks;           // 타임아웃 (ticks)
  bool is_active;                     // 활성 상태
} ble_async_at_cmd_t;

typedef struct
{
  char expected_response[32];                  // 기대하는 응답 문자열 (예: "+OK", "+ERROR")
  char response_buf[BLE_AT_RESPONSE_MAX_SIZE]; // 실제 받은 응답
  size_t response_len;
  SemaphoreHandle_t wait_sem; // 응답 대기용 세마포어
  ble_at_status_t status;
  TickType_t timeout_ticks; // 타임아웃 (ticks)
} ble_async_at_request_t;

typedef struct
{
  char data[512];
  size_t len;
  bool is_at;
} ble_tx_request_t;

typedef struct
{
  char data[100];
  size_t pos;
  char prev_char;
} ble_cmd_parser_t;

typedef struct
{
  ble_t ble;
  ble_cmd_parse_state_t parse_stae;
  ble_cmd_parser_t parser;
  QueueHandle_t rx_queue;
  TaskHandle_t rx_task;
  bool enabled;

  QueueHandle_t tx_queue;
  TaskHandle_t tx_task;

  SemaphoreHandle_t mutex;

  // 비동기 AT 커맨드 요청
  ble_async_at_request_t *async_request;

  // 모드 및 연결 상태
  ble_mode_t current_mode;           // 현재 모드 (AT/Bypass)
  ble_connection_state_t conn_state; // BLE 연결 상태

  // Bypass 모드 데이터 수신 콜백
  ble_bypass_rx_callback_t bypass_rx_callback;
  ble_async_at_cmd_t async_at_cmd;
} ble_instance_t;

void ble_init_all(void);

ble_t *ble_get_handle(void);
ble_instance_t *ble_get_instance(void);
bool ble_send(const char *data, size_t len, bool is_at);

// 비동기 AT 커맨드 전송 (응답 대기)
ble_at_status_t ble_send_at_command_async(const char *at_cmd, const char *expected_response,
                                          char *response_buf, size_t response_buf_size,
                                          uint32_t timeout_ms);

// BLE 디바이스 이름 설정 (AT+MANUF=<name>)
bool ble_set_device_name_async(const char *device_name, uint32_t timeout_ms);

// BLE UART 통신 속도 설정 (AT+UART=<baudrate>)
bool ble_set_uart_baudrate_async(uint32_t baudrate, uint32_t timeout_ms);

// BLE 초기 설정 시퀀스 (디바이스 이름 + UART 속도 설정)
// 설정 완료 후 자동으로 Bypass 모드로 전환됨
bool ble_configure_async(const char *device_name, uint32_t baudrate);

// BLE 연결 상태 조회
ble_connection_state_t ble_get_connection_state(void);

// BLE 연결 상태 변경 (GPIO 인터럽트에서 호출)
void ble_set_connection_state(ble_connection_state_t state);

// 현재 모드 조회
ble_mode_t ble_get_current_mode(void);

// Bypass 모드 RX 콜백 등록 (Bypass 모드에서 수신된 데이터를 전달받음)
void ble_set_bypass_rx_callback(ble_bypass_rx_callback_t callback);
bool ble_get_device_name_async(char *device_name_buf, size_t buf_size, uint32_t timeout_ms);

bool ble_send_at_cmd_truly_async(const char *at_cmd, ble_at_command_callback_t callback,
                                 void *user_data, uint32_t timeout_ms);
bool ble_set_manuf_async(const char *device_name, ble_at_command_callback_t callback,
                         void *user_data, uint32_t timeout_ms);
// AT+DISCONNECT 비동기 전송

bool ble_disconnect_async(ble_at_command_callback_t callback, void *user_data, uint32_t timeout_ms);

#endif