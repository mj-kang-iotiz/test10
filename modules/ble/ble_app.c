#include "ble_app.h"
#include "board_config.h"
#include "ble.h"
#include "ble_port.h"
#include <string.h>
#include "flash_params.h"

#ifndef TAG
#define TAG "BLE_APP"
#endif

#include "log.h"

static void ble_check_async_at_timeout(void);
bool ble_set_advon_async(uint32_t timeout_ms);

void ble_cmd_parse_process(ble_instance_t *inst, const void *data, size_t len)
{
  const uint8_t *d = data;

  for (; len > 0; ++d, --len)
  {
    if (inst->parse_stae == BLE_CMD_PARSE_STATE_NONE)
    {
      if (*d == '\r' || *d == '\n')
      {
        continue;
      }

      if (*d == '+')
      {
        inst->parser.pos = 0;
        inst->parser.data[inst->parser.pos++] = (char)(*d);
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = BLE_CMD_PARSE_STATE_DATA;
      }
      else if (*d == 'A')
      {
        inst->parser.pos = 0;
        inst->parser.data[inst->parser.pos++] = (char)(*d);
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = BLE_CMD_PARSE_STATE_GOT_A;
      }
      else
      {
        inst->parser.pos = 0;
        inst->parser.data[inst->parser.pos++] = (char)(*d);
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = BLE_CMD_PARSE_STATE_APP;
      }
    }
    else if (inst->parse_stae == BLE_CMD_PARSE_STATE_GOT_A)
    {
      if (*d == 'T')
      {
        inst->parser.data[inst->parser.pos++] = (char)(*d);
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = BLE_CMD_PARSE_STATE_DATA;
      }
      else
      {
        inst->parser.pos = 0;
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = BLE_CMD_PARSE_STATE_NONE;

        if (*d == '+')
        {
          inst->parser.data[inst->parser.pos++] = (char)(*d);
          inst->parse_stae = BLE_CMD_PARSE_STATE_DATA;
        }
      }
    }
    else if (inst->parse_stae == BLE_CMD_PARSE_STATE_DATA || inst->parse_stae == BLE_CMD_PARSE_STATE_APP)
    {
      if (inst->parser.pos < sizeof(inst->parser.data) - 1)
      {
        inst->parser.data[inst->parser.pos++] = (char)(*d);
        inst->parser.data[inst->parser.pos] = '\0';
      }
      else
      {
        inst->parser.pos = 0;
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parser.prev_char = '\0';
        inst->parse_stae = BLE_CMD_PARSE_STATE_NONE;
      }

      if (*d == '\r' || *d == '\n')
      {
        inst->parser.data[inst->parser.pos - 1] = '\0'; // \r 제거
        LOG_INFO("BLE AT Command received: %s", inst->parser.data);

        if (inst->parse_stae == BLE_CMD_PARSE_STATE_DATA)
        {
          ble_at_cmd_handler(inst);
        }
        else
        {
          ble_app_cmd_handler(inst);
        }

        inst->parser.prev_char = '\0';
        inst->parser.pos = 0;
        inst->parse_stae = BLE_CMD_PARSE_STATE_NONE;
      }
      inst->parser.prev_char = (char)(*d);
    }
  }
}

static ble_instance_t ble_instance = {0};

static void ble_tx_task(void *pvParameter)
{
  ble_instance_t *inst = (ble_instance_t *)pvParameter;
  ble_tx_request_t tx_req;

  LOG_INFO("BLE TX Task started");
  user_params_t *params = flash_params_get_current();

  while (1)
  {
    if (xQueueReceive(inst->tx_queue, &tx_req, portMAX_DELAY) == pdTRUE)
    {
      LOG_DEBUG("BLE Sending %d bytes", tx_req.len);

      xSemaphoreTake(inst->mutex, portMAX_DELAY);

      if (inst->ble.ops && inst->ble.ops->send)
      {
        if (tx_req.is_at == true)
        {
          inst->ble.ops->at_mode();
        }
        inst->ble.ops->send(tx_req.data, tx_req.len);

        if (tx_req.is_at == true)
        {
          vTaskDelay(pdMS_TO_TICKS(10));
          inst->ble.ops->bypass_mode();
        }
        LOG_DEBUG("BLE TX complete");
      }
      else
      {
        LOG_ERR("BLE send ops not available");
      }

      xSemaphoreGive(inst->mutex);
    }
  }

  vTaskDelete(NULL);
}

static void ble_rx_task(void *pvParameter)
{
  ble_instance_t *inst = (ble_instance_t *)pvParameter;

  size_t pos = 0;
  size_t old_pos = 0;
  uint8_t dummy = 0;
  size_t total_received = 0;

  LOG_INFO("BLE RX Task started");

  while (1)
  {
    xQueueReceive(inst->rx_queue, &dummy, pdMS_TO_TICKS(100));
    xSemaphoreTake(inst->mutex, portMAX_DELAY);
    ble_check_async_at_timeout();

    pos = ble_port_get_rx_pos();
    char *ble_recv = ble_port_get_recv_buf();

    if (pos != old_pos)
    {
      if (pos > old_pos)
      {
        size_t len = pos - old_pos;
        total_received = len;
        LOG_DEBUG_RAW("BLE RX: ", &ble_recv[old_pos], len);
        // 항상 파싱 (AT 응답 또는 앱 커맨드 처리)
        ble_cmd_parse_process(inst, &ble_recv[old_pos], len);

        // Bypass 모드에서는 콜백도 호출 (raw 데이터 전달)
        if (inst->current_mode == BLE_MODE_BYPASS && inst->bypass_rx_callback != NULL)
        {
          inst->bypass_rx_callback((const uint8_t *)&ble_recv[old_pos], len);
        }
      }
      else
      {
        size_t len1 = BLE_UART_MAX_RECV_SIZE - old_pos;
        size_t len2 = pos;
        total_received = len1 + len2;
        LOG_DEBUG_RAW("BLE RX: ", &ble_recv[old_pos], len1);
        // 항상 파싱 (AT 응답 또는 앱 커맨드 처리)
        ble_cmd_parse_process(inst, &ble_recv[old_pos],
                              BLE_UART_MAX_RECV_SIZE - old_pos);
        if (pos > 0)
        {
          LOG_DEBUG_RAW("BLE RX: ", ble_recv, len2);
          ble_cmd_parse_process(inst, ble_recv, pos);
        }

        // Bypass 모드에서는 콜백도 호출 (raw 데이터 전달)
        if (inst->current_mode == BLE_MODE_BYPASS && inst->bypass_rx_callback != NULL)
        {
          inst->bypass_rx_callback((const uint8_t *)&ble_recv[old_pos], len1);
          if (pos > 0)
          {
            inst->bypass_rx_callback((const uint8_t *)ble_recv, len2);
          }
        }
      }
      old_pos = pos;
      if (old_pos == BLE_UART_MAX_RECV_SIZE)
      {
        old_pos = 0;
      }
    }

    xSemaphoreGive(inst->mutex);
  }

  vTaskDelete(NULL);
}

void ble_init_all(void)
{
  const board_config_t *config = board_get_config();

  LOG_INFO("BLE 초기화 시작 - 보드: %d", config->board);

  if (!config->use_ble)
  {
    LOG_INFO("BLE 사용 안함");
    return;
  }

  ble_instance.enabled = true;
  ble_instance.current_mode = BLE_MODE_BYPASS;     // 초기 모드는 Bypass
  ble_instance.conn_state = BLE_CONN_DISCONNECTED; // 초기 연결 상태
  ble_instance.bypass_rx_callback = NULL;          // 콜백 초기화
  ble_instance.async_at_cmd.is_active = false;

  if (ble_port_init_instance(&ble_instance.ble) != 0)
  {
    LOG_ERR("BLE 포트 초기화 실패");
    ble_instance.enabled = false;
    return;
  }

  ble_instance.rx_queue = xQueueCreate(10, sizeof(uint8_t));
  if (ble_instance.rx_queue == NULL)
  {
    LOG_ERR("BLE RX 큐 생성 실패");
    ble_instance.enabled = false;
    return;
  }

  ble_port_set_queue(ble_instance.rx_queue);

  ble_instance.tx_queue = xQueueCreate(5, sizeof(ble_tx_request_t));
  if (ble_instance.tx_queue == NULL)
  {
    LOG_ERR("BLE TX 큐 생성 실패");
    ble_instance.enabled = false;
    return;
  }

  ble_instance.mutex = xSemaphoreCreateMutex();
  if (ble_instance.mutex == NULL)
  {
    LOG_ERR("BLE mutex 생성 실패");
    ble_instance.enabled = false;
    return;
  }

  ble_port_start(&ble_instance.ble);

  BaseType_t ret = xTaskCreate(ble_rx_task, "ble_rx", 512,
                               (void *)&ble_instance,
                               tskIDLE_PRIORITY + 1, &ble_instance.rx_task);

  if (ret != pdPASS)
  {
    LOG_ERR("BLE RX 태스크 생성 실패");
    ble_instance.enabled = false;
    return;
  }

  ret = xTaskCreate(ble_tx_task, "ble_tx", 512,
                    (void *)&ble_instance,
                    tskIDLE_PRIORITY + 1, &ble_instance.tx_task);

  if (ret != pdPASS)
  {
    LOG_ERR("BLE TX 태스크 생성 실패");
    ble_instance.enabled = false;
    return;
  }

  LOG_INFO("BLE 초기화 완료");
}

ble_t *ble_get_handle(void)
{
  if (!ble_instance.enabled)
  {
    return NULL;
  }

  return &ble_instance.ble;
}

ble_instance_t *ble_get_instance(void)
{
  return &ble_instance;
}

bool ble_send(const char *data, size_t len, bool is_at)
{
  if (!ble_instance.enabled)
  {
    LOG_ERR("BLE not enabled");
    return false;
  }

  if (!data || len == 0 || len > 512)
  {
    LOG_ERR("BLE invalid send parameters");
    return false;
  }

  // AT 커맨드 전송 중에는 일반 전송 차단 (is_at=false 요청)
  //  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  bool is_at_mode = (ble_instance.current_mode == BLE_MODE_AT);
  bool has_async_request = (ble_instance.async_request != NULL);
  //  xSemaphoreGive(ble_instance.mutex);

  if (is_at_mode && !is_at && has_async_request)
  {
    LOG_WARN("BLE in AT command mode - blocking normal transmission");
    return false;
  }

  ble_tx_request_t tx_req;
  memcpy(tx_req.data, data, len);
  tx_req.len = len;
  tx_req.is_at = is_at;

  if (xQueueSend(ble_instance.tx_queue, &tx_req, pdMS_TO_TICKS(1000)) != pdTRUE)
  {
    LOG_ERR("BLE TX queue full");
    return false;
  }

  return true;
}

ble_at_status_t ble_send_at_command_async(const char *at_cmd, const char *expected_response,
                                          char *response_buf, size_t response_buf_size,
                                          uint32_t timeout_ms)
{
  if (!ble_instance.enabled)
  {
    LOG_ERR("BLE not enabled");
    return BLE_AT_STATUS_ERROR;
  }

  if (!at_cmd || !expected_response)
  {
    LOG_ERR("Invalid parameters");
    return BLE_AT_STATUS_ERROR;
  }

  // 이미 진행 중인 비동기 요청이 있는지 확인
  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  if (ble_instance.async_request != NULL)
  {
    xSemaphoreGive(ble_instance.mutex);
    LOG_ERR("Another async AT command is pending");
    return BLE_AT_STATUS_ERROR;
  }

  // 비동기 요청 구조체 생성 및 초기화
  ble_async_at_request_t request = {0};
  strncpy(request.expected_response, expected_response, sizeof(request.expected_response) - 1);
  request.response_len = 0;
  request.status = BLE_AT_STATUS_PENDING;
  request.timeout_ticks = pdMS_TO_TICKS(timeout_ms);
  request.wait_sem = xSemaphoreCreateBinary();

  if (request.wait_sem == NULL)
  {
    xSemaphoreGive(ble_instance.mutex);
    LOG_ERR("Failed to create semaphore");
    return BLE_AT_STATUS_ERROR;
  }

  // 전역 async_request 포인터 설정
  ble_instance.async_request = &request;
  xSemaphoreGive(ble_instance.mutex);

  // AT 모드로 전환
  LOG_INFO("Switching to AT command mode");
  if (ble_instance.ble.ops && ble_instance.ble.ops->at_mode)
  {
    ble_instance.ble.ops->at_mode();
  }

  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  ble_instance.current_mode = BLE_MODE_AT; // 모드 상태 업데이트
  xSemaphoreGive(ble_instance.mutex);

  // 모드 전환 대기 (BLE 모듈이 안정화될 시간)
  vTaskDelay(pdMS_TO_TICKS(100));

  // DMA 버퍼의 현재 위치를 업데이트하여 이전 데이터 무시
  // (Bypass 모드에서 받은 데이터가 남아있을 수 있음)
  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  // RX task의 old_pos를 현재 DMA 위치로 동기화하는 것이 이상적이지만,
  // 간단하게 잠시 대기하여 RX task가 버퍼를 처리하도록 함
  xSemaphoreGive(ble_instance.mutex);
  vTaskDelay(pdMS_TO_TICKS(10)); // RX task가 기존 데이터 처리하도록 여유 시간

  // AT 커맨드 전송
  LOG_INFO("Sending AT command: %s", at_cmd);
  if (!ble_send(at_cmd, strlen(at_cmd), true))
  {
    xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
    ble_instance.async_request = NULL;
    xSemaphoreGive(ble_instance.mutex);
    vSemaphoreDelete(request.wait_sem);
    LOG_ERR("Failed to send AT command");
    return BLE_AT_STATUS_ERROR;
  }

  // TX task가 실제로 UART로 전송할 시간 확보
  // ble_send()는 큐에만 추가하므로, TX task 실행까지 대기 필요
  vTaskDelay(pdMS_TO_TICKS(10));

  // 응답 대기 (타임아웃 포함)
  LOG_INFO("Waiting for response: %s (timeout: %lu ms)", expected_response, timeout_ms);
  BaseType_t result = xSemaphoreTake(request.wait_sem, request.timeout_ticks);

  // 세마포어 해제 및 정리
  vSemaphoreDelete(request.wait_sem);

  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  ble_at_status_t status = request.status;

  // 응답 버퍼 복사 (사용자가 제공한 경우)
  if (response_buf && response_buf_size > 0 && request.response_len > 0)
  {
    size_t copy_len = (request.response_len < response_buf_size - 1) ? request.response_len : (response_buf_size - 1);
    memcpy(response_buf, request.response_buf, copy_len);
    response_buf[copy_len] = '\0';
  }

  ble_instance.async_request = NULL;
  xSemaphoreGive(ble_instance.mutex);

  // Bypass 모드로 전환 (응답 수신 후)
  LOG_INFO("Switching to bypass mode");
  if (ble_instance.ble.ops && ble_instance.ble.ops->bypass_mode)
  {
    ble_instance.ble.ops->bypass_mode();
  }

  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  ble_instance.current_mode = BLE_MODE_BYPASS; // 모드 상태 업데이트
  xSemaphoreGive(ble_instance.mutex);

  if (result == pdFALSE)
  {
    LOG_ERR("AT command timeout");
    return BLE_AT_STATUS_TIMEOUT;
  }

  LOG_INFO("AT command completed with status: %d", status);
  return status;
}

// BLE 디바이스 이름 설정 (AT+MANUF=<name>)
bool ble_set_device_name_async(const char *device_name, uint32_t timeout_ms)
{
  if (!device_name)
  {
    LOG_ERR("Device name is NULL");
    return false;
  }

  // AT+MANUF=<name>\r\n 커맨드 생성
  char at_cmd[64];
  snprintf(at_cmd, sizeof(at_cmd), "AT+MANUF=%s\r", device_name);

  // 응답 버퍼
  char response[BLE_AT_RESPONSE_MAX_SIZE];

  // 비동기 AT 커맨드 전송 (+OK 응답 기대)
  ble_at_status_t status = ble_send_at_command_async(at_cmd, "+OK", response, sizeof(response), timeout_ms);

  if (status == BLE_AT_STATUS_COMPLETED)
  {
    LOG_INFO("Device name set successfully: %s", device_name);
    return true;
  }
  else if (status == BLE_AT_STATUS_TIMEOUT)
  {
    LOG_ERR("Device name setting timeout");
    return false;
  }
  else if (status == BLE_AT_STATUS_ERROR)
  {
    LOG_ERR("Device name setting error: %s", response);
    return false;
  }

  LOG_ERR("Device name setting failed with status: %d", status);
  return false;
}

bool ble_set_advon_async(uint32_t timeout_ms)
{
  // AT+MANUF=<name>\r\n 커맨드 생성
  char at_cmd[16];
  snprintf(at_cmd, sizeof(at_cmd), "AT+ADVON\r");

  // 응답 버퍼
  char response[BLE_AT_RESPONSE_MAX_SIZE];

  // 비동기 AT 커맨드 전송 (+OK 응답 기대)
  ble_at_status_t status = ble_send_at_command_async(at_cmd, "+OK", response, sizeof(response), timeout_ms);

  if (status == BLE_AT_STATUS_COMPLETED)
  {
    LOG_INFO("advon successfully");
    return true;
  }
  else if (status == BLE_AT_STATUS_TIMEOUT)
  {
    LOG_ERR("timeout");
    return false;
  }
  else if (status == BLE_AT_STATUS_ERROR)
  {
    LOG_ERR("setting error: %s", response);
    return false;
  }

  LOG_ERR("setting failed with status: %d", status);
  return false;
}

// BLE UART 통신 속도 설정 (AT+UART=<baudrate>)
bool ble_set_uart_baudrate_async(uint32_t baudrate, uint32_t timeout_ms)
{
  // AT+UART=<baudrate>\r\n 커맨드 생성
  char at_cmd[64];
  snprintf(at_cmd, sizeof(at_cmd), "AT+UART=%lu\r\n", baudrate);

  // 응답 버퍼
  char response[BLE_AT_RESPONSE_MAX_SIZE];

  // 비동기 AT 커맨드 전송 (+OK 응답 기대)
  ble_at_status_t status = ble_send_at_command_async(at_cmd, "+OK", response, sizeof(response), timeout_ms);

  if (status == BLE_AT_STATUS_COMPLETED)
  {
    LOG_INFO("UART baudrate set successfully: %lu", baudrate);
    return true;
  }
  else if (status == BLE_AT_STATUS_TIMEOUT)
  {
    LOG_ERR("UART baudrate setting timeout");
    return false;
  }
  else if (status == BLE_AT_STATUS_ERROR)
  {
    LOG_ERR("UART baudrate setting error: %s", response);
    return false;
  }

  LOG_ERR("UART baudrate setting failed with status: %d", status);
  return false;
}

// BLE 연결 상태 조회
ble_connection_state_t ble_get_connection_state(void)
{
  if (!ble_instance.enabled)
  {
    return BLE_CONN_DISCONNECTED;
  }

  //  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  ble_connection_state_t state = ble_instance.conn_state;
  // xSemaphoreGive(ble_instance.mutex);

  return state;
}

// BLE 연결 상태 변경 (GPIO 인터럽트에서 호출)
void ble_set_connection_state(ble_connection_state_t state)
{
  if (!ble_instance.enabled)
  {
    return;
  }

  //  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  ble_instance.conn_state = state;
  //  xSemaphoreGive(ble_instance.mutex);

  if (state == BLE_CONN_CONNECTED)
  {
    LOG_INFO("BLE Connected");
  }
  else
  {
    LOG_INFO("BLE Disconnected");
  }
}

// 현재 모드 조회
ble_mode_t ble_get_current_mode(void)
{
  if (!ble_instance.enabled)
  {
    return BLE_MODE_BYPASS;
  }

  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  ble_mode_t mode = ble_instance.current_mode;
  xSemaphoreGive(ble_instance.mutex);

  return mode;
}

// Bypass 모드 RX 콜백 등록
void ble_set_bypass_rx_callback(ble_bypass_rx_callback_t callback)
{
  if (!ble_instance.enabled)
  {
    return;
  }

  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);
  ble_instance.bypass_rx_callback = callback;
  xSemaphoreGive(ble_instance.mutex);

  LOG_INFO("BLE Bypass RX callback %s", callback ? "registered" : "unregistered");
}

static void ble_check_async_at_timeout(void)
{

  if (!ble_instance.async_at_cmd.is_active)
  {

    return;
  }

  TickType_t current_tick = xTaskGetTickCount();

  TickType_t elapsed_ticks = current_tick - ble_instance.async_at_cmd.start_tick;

  if (elapsed_ticks >= ble_instance.async_at_cmd.timeout_ticks)
  {

    LOG_ERR("Async AT command timeout: %s", ble_instance.async_at_cmd.command);

    // 콜백 호출 (타임아웃 = 실패)

    if (ble_instance.async_at_cmd.callback)
    {

      ble_instance.async_at_cmd.callback(false, ble_instance.async_at_cmd.user_data);
    }

    // Bypass 모드로 복귀

    if (ble_instance.ble.ops && ble_instance.ble.ops->bypass_mode)
    {

      ble_instance.ble.ops->bypass_mode();
    }

    ble_instance.current_mode = BLE_MODE_BYPASS;

    // 비활성화

    ble_instance.async_at_cmd.is_active = false;
  }
}

// 비동기 AT 명령어 전송 (콜백 기반, 즉시 반환)

bool ble_send_at_cmd_truly_async(const char *at_cmd, ble_at_command_callback_t callback,

                                 void *user_data, uint32_t timeout_ms)
{

  if (!ble_instance.enabled)
  {

    LOG_ERR("BLE not enabled");

    return false;
  }

  if (!at_cmd || !callback)
  {

    LOG_ERR("Invalid parameters");

    return false;
  }

  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);

  // 이미 진행 중인 비동기 명령어가 있는지 확인

  if (ble_instance.async_at_cmd.is_active)
  {

    xSemaphoreGive(ble_instance.mutex);

    LOG_ERR("Another async AT command is active");

    return false;
  }

  // 비동기 요청 설정

  strncpy(ble_instance.async_at_cmd.command, at_cmd, sizeof(ble_instance.async_at_cmd.command) - 1);

  ble_instance.async_at_cmd.callback = callback;

  ble_instance.async_at_cmd.user_data = user_data;

  ble_instance.async_at_cmd.start_tick = xTaskGetTickCount();

  ble_instance.async_at_cmd.timeout_ticks = pdMS_TO_TICKS(timeout_ms);

  ble_instance.async_at_cmd.is_active = true;

  xSemaphoreGive(ble_instance.mutex);

  // AT 모드로 전환

  LOG_INFO("Switching to AT mode for async command");

  if (ble_instance.ble.ops && ble_instance.ble.ops->at_mode)
  {

    ble_instance.ble.ops->at_mode();
  }

  xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);

  ble_instance.current_mode = BLE_MODE_AT;

  xSemaphoreGive(ble_instance.mutex);

  // 모드 전환 대기

  vTaskDelay(pdMS_TO_TICKS(100));

  // AT 커맨드 전송

  LOG_INFO("Sending async AT command: %s", at_cmd);

  if (!ble_send(at_cmd, strlen(at_cmd), true))
  {

    xSemaphoreTake(ble_instance.mutex, portMAX_DELAY);

    ble_instance.async_at_cmd.is_active = false;

    xSemaphoreGive(ble_instance.mutex);

    // Bypass 모드로 복귀

    if (ble_instance.ble.ops && ble_instance.ble.ops->bypass_mode)
    {

      ble_instance.ble.ops->bypass_mode();
    }

    ble_instance.current_mode = BLE_MODE_BYPASS;

    LOG_ERR("Failed to send async AT command");

    return false;
  }

  LOG_INFO("Async AT command sent successfully, waiting for response");

  return true;
}

// AT+MANUF=디바이스명 비동기 전송

bool ble_set_manuf_async(const char *device_name, ble_at_command_callback_t callback,

                         void *user_data, uint32_t timeout_ms)
{

  if (!device_name)
  {

    LOG_ERR("Device name is NULL");

    return false;
  }

  // AT+MANUF=<name>\r 커맨드 생성

  char at_cmd[128];

  snprintf(at_cmd, sizeof(at_cmd), "AT+MANUF=%s\r", device_name);

  return ble_send_at_cmd_truly_async(at_cmd, callback, user_data, timeout_ms);
}

// AT+DISCONNECT 비동기 전송

bool ble_disconnect_async(ble_at_command_callback_t callback, void *user_data, uint32_t timeout_ms)
{

  const char *at_cmd = "AT+DISCONNECT\r";

  return ble_send_at_cmd_truly_async(at_cmd, callback, user_data, timeout_ms);
}
