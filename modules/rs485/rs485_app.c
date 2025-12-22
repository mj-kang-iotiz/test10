#include "softuart.h"
#include "rs485_app.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "flash_params.h"
#include "board_type.h"
#include "board_config.h"
#include "gps_app.h"
#include "lora_app.h"
#include "gsm_app.h"
#include "gsm.h"
#include "lte_init.h"
#include "rs485.h"
#include "rs485_cmd.h"
#include "rs485_port.h"

#ifndef TAG
#define TAG "RS485_APP"
#endif

#include "log.h"

TimerHandle_t gps_send_timer = NULL;

static char gps_send_buf[140];

static void gps_send_timer_callback(TimerHandle_t xTimer)
{
    gps_format_position_data(gps_send_buf);
    rs485_send((uint8_t *)gps_send_buf, strlen(gps_send_buf));
}

void rs485_cmd_parse_process(rs485_instance_t *inst, const void *data, size_t len)
{
  const uint8_t *d = data;

  for (; len > 0; ++d, --len)
  {
    if(inst->parse_stae == RS485_CMD_PARSE_STATE_NONE)
    {
      if(*d == 'A')
      {
        inst->parser.pos = 0;
        inst->parser.data[inst->parser.pos++] = (char)(*d);
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = RS485_CMD_PARSE_STATE_GOT_A;
      }
    }
    else if(inst->parse_stae == RS485_CMD_PARSE_STATE_GOT_A)
    {
      if(*d == 'T')
      {
        inst->parser.data[inst->parser.pos++] = (char)(*d);
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = RS485_CMD_PARSE_STATE_DATA;
      }
      else
      {
        inst->parser.pos = 0;
        inst->parser.data[inst->parser.pos] = '\0';
        inst->parse_stae = RS485_CMD_PARSE_STATE_NONE;
      }
    }
    else if(inst->parse_stae == RS485_CMD_PARSE_STATE_DATA)
    {
        if(inst->parser.pos < sizeof(inst->parser.data) - 1)
        {
          inst->parser.data[inst->parser.pos++] = (char)(*d);
          inst->parser.data[inst->parser.pos] = '\0';
        }
        else
        {
          inst->parser.pos = 0;
          inst->parser.data[inst->parser.pos] = '\0';
          inst->parser.prev_char = '\0';
          inst->parse_stae = RS485_CMD_PARSE_STATE_NONE;
        }

        if(*d == '\r')
        {
          inst->parser.data[inst->parser.pos - 1] = '\0'; // \r 제거
          LOG_INFO("RS485 AT Command received: %s", inst->parser.data);

          rs485_at_cmd_handler(inst);

          inst->parser.prev_char = '\0';
          inst->parser.pos = 0;
          inst->parse_stae = RS485_CMD_PARSE_STATE_NONE;
        }
        inst->parser.prev_char = (char)(*d);
    }
  }
}

static rs485_instance_t rs485_instance = {0};

static void rs485_tx_task(void *pvParameter) {
  rs485_instance_t *inst = (rs485_instance_t *)pvParameter;
  rs485_tx_request_t tx_req;

  LOG_INFO("RS485 TX Task started");

  while (1) {
     if (xQueueReceive(inst->tx_queue, &tx_req, portMAX_DELAY) == pdTRUE) {
      LOG_DEBUG("RS485 Sending %d bytes", tx_req.len);

      xSemaphoreTake(inst->mutex, portMAX_DELAY);

      if (inst->rs485.ops && inst->rs485.ops->send) {
        if (inst->rs485.ops->tx_enable) {
          inst->rs485.ops->tx_enable();
        }

        inst->rs485.ops->send(tx_req.data, tx_req.len);

        if (inst->rs485.ops->rx_enable) {
          inst->rs485.ops->rx_enable();
        }

        LOG_DEBUG("RS485 TX complete");
      } else {
        LOG_ERR("RS485 send ops not available");
      }

      xSemaphoreGive(inst->mutex);
    }
  }

  vTaskDelete(NULL);
}

static void rs485_rx_task(void *pvParameter) {
  rs485_instance_t *inst = (rs485_instance_t *)pvParameter;

  size_t pos = 0;
  size_t old_pos = 0;
  uint8_t dummy = 0;
  size_t total_received = 0;

  LOG_INFO("RS485 RX Task started");

  vTaskDelay(pdMS_TO_TICKS(2900));
  rs485_send("+READY\r", strlen("+READY\r"));

  while (1) {
    xQueueReceive(inst->rx_queue, &dummy, portMAX_DELAY);

    xSemaphoreTake(inst->mutex, portMAX_DELAY);

    pos = rs485_port_get_rx_pos();
    char *rs485_recv = rs485_port_get_recv_buf();

    if (pos != old_pos) {
      if (pos > old_pos) {
        size_t len = pos - old_pos;
        total_received = len;
        LOG_DEBUG_RAW("RS485 RX: ", &rs485_recv[old_pos], len);
        rs485_cmd_parse_process(inst, &rs485_recv[old_pos], pos - old_pos);
      } else {
        size_t len1 = RS485_UART_MAX_RECV_SIZE - old_pos;
        size_t len2 = pos;
        total_received = len1 + len2;
        LOG_DEBUG_RAW("RS485 RX: ", &rs485_recv[old_pos], len1);
        rs485_cmd_parse_process(inst, &rs485_recv[old_pos],
                                RS485_UART_MAX_RECV_SIZE - old_pos);
        if (pos > 0) {
          LOG_DEBUG_RAW("RS485 RX: ", rs485_recv, len2);
          rs485_cmd_parse_process(inst, rs485_recv, pos);
        }
      }
      old_pos = pos;
      if (old_pos == RS485_UART_MAX_RECV_SIZE) {
        old_pos = 0;
      }
    }

    xSemaphoreGive(inst->mutex);
  }

  vTaskDelete(NULL);
}

void rs485_init_all(void) {
  const board_config_t *config = board_get_config();

  LOG_INFO("RS485 초기화 시작 - 보드: %d", config->board);

  if (!config->use_rs485) {
    LOG_INFO("RS485 사용 안함");
    return;
  }

  rs485_instance.enabled = true;

  if (rs485_port_init_instance(&rs485_instance.rs485) != 0) {
    LOG_ERR("RS485 포트 초기화 실패");
    rs485_instance.enabled = false;
    return;
  }

  rs485_instance.rx_queue = xQueueCreate(10, sizeof(uint8_t));
  if (rs485_instance.rx_queue == NULL) {
    LOG_ERR("RS485 RX 큐 생성 실패");
    rs485_instance.enabled = false;
    return;
  }

  rs485_port_set_queue(rs485_instance.rx_queue);

  rs485_instance.tx_queue = xQueueCreate(5, sizeof(rs485_tx_request_t));
  if (rs485_instance.tx_queue == NULL) {
    LOG_ERR("RS485 TX 큐 생성 실패");
    rs485_instance.enabled = false;
    return;
  }

  rs485_instance.mutex = xSemaphoreCreateMutex();
  if (rs485_instance.mutex == NULL) {
    LOG_ERR("RS485 mutex 생성 실패");
    rs485_instance.enabled = false;
    return;
  }

  rs485_port_start(&rs485_instance.rs485);

  BaseType_t ret = xTaskCreate(rs485_rx_task, "rs485_rx", 512,
                                (void *)&rs485_instance,
                                tskIDLE_PRIORITY + 1, &rs485_instance.rx_task);

  if (ret != pdPASS) {
    LOG_ERR("RS485 RX 태스크 생성 실패");
    rs485_instance.enabled = false;
    return;
  }

  ret = xTaskCreate(rs485_tx_task, "rs485_tx", 512,
                    (void *)&rs485_instance,
                    tskIDLE_PRIORITY + 3, &rs485_instance.tx_task);

  if (ret != pdPASS) {
    LOG_ERR("RS485 TX 태스크 생성 실패");
    rs485_instance.enabled = false;
    return;
  }
    
  gps_send_timer = xTimerCreate(
    "GPS_Send",           // Timer name
    pdMS_TO_TICKS(50),          // Timer period (50ms)
    pdTRUE,                     // Auto-reload
    NULL,                       // Timer ID (not used)
    gps_send_timer_callback     // Callback function
  );

  if (gps_send_timer == NULL)
  {
    LOG_ERR("Failed to create GPS send timer");
  }

  LOG_INFO("RS485 초기화 완료");
}

rs485_t *rs485_get_handle(void) {
  if (!rs485_instance.enabled) {
    return NULL;
  }

  return &rs485_instance.rs485;
}

bool rs485_send(const char *data, size_t len) {
  if (!rs485_instance.enabled) {
    LOG_ERR("RS485 not enabled");
    return false;
  }

  if (!data || len == 0 || len > 256) {
    LOG_ERR("RS485 invalid send parameters");
    return false;
  }

  rs485_tx_request_t tx_req;
  memcpy(tx_req.data, data, len);
  tx_req.len = len;

  if (xQueueSend(rs485_instance.tx_queue, &tx_req, pdMS_TO_TICKS(1000)) != pdTRUE) {
    LOG_ERR("RS485 TX queue full");
    return false;
  }

  return true;
}


rs485_instance_t* rs485_get_instance(void) {
  return &rs485_instance;
}

#if USE_SOFTUART
char *INIT_Notify = "+READY\r";
char *GPS_Notify = "+GPS,\r";
char *F_Version_Response = "+V0.0.1\r";
char *AT_Response = "+OK\r";
char *ATZ_Response = "+RESET\r";
char *ATnF_Response = "+CONFIGINIT\r";
char *GPSMANUF_UM982_Response = "+Unicore\r";
char *GPSMANUF_F9P_Response = "+Ublox\r";
char *CONFIG_Response = "+CONFIG=Hello1234567890abcdefghijklmnopqrstuvwyzABCDEFGHIJKLMNOPQRSTUVWXYZ\r"; // need make CONFIG Variable
char *SETBASELINE_Response = "+SETBASELINE=\r";                                                         // need make setbaseline variable
char *CASTER_Response = "+CASTER=\r";                                                                   // need make caster variable
char *ID_Response = "+ID=\r";                                                                           // need make ID variable
char *MOUNTPOINT_Response = "+MOUNTPOINT=\r";                                                           // need make MOUNTPOINT variable
char *PASSWORD_Response = "+PASSWORD=\r";                                                               // need make PASSWORD variable
char *START_Response = "+GUGUSTART\r";
char *STOP_Response = "+GUGUSTOP\r";

char *ERROR_Response = "+ERROR\r"; // ETC
char *ERROR1_Response = "+E01\r";  // DO NOT KNOW ERROR
char *ERROR2_Response = "+E02\r";  // Parameter ERROR
char *ERROR3_Response = "+E03\r";  // NO ready device ERROR

static SemaphoreHandle_t rs485_tx_mutex;

volatile bool is_gugu_started = false;

void delay_170ns(void);

// 약 900ns 지연
void delay_170ns(void)
{
  __asm volatile(
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t");
}

void RS485_SetTransmitMode(void)
{
  HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET); // DE=1, RE=1 (송신)
  HAL_GPIO_WritePin(RS485_RE_PORT, RS485_RE_PIN, GPIO_PIN_SET); // DE=1, RE=1 (송신)
  delay_170ns();
  delay_170ns();
  delay_170ns();
  delay_170ns();
  delay_170ns();
}

void RS485_SetReceiveMode(void)
{
  delay_170ns();
  HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET); // DE=0, RE=0 (수신)
  HAL_GPIO_WritePin(RS485_RE_PORT, RS485_RE_PIN, GPIO_PIN_RESET); // DE=0, RE=0 (수신)
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1)
  {
    SoftUartHandler();
  }
}

int get_line(uint8_t SoftUartNumber, char *buffer, int maxlen)
{
  int index = 0;
  uint8_t ch;

  while (index < maxlen - 1)
  {
    while (SoftUartRxAlavailable(SoftUartNumber) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(3));
    }
    SoftUartReadRxBuffer(SoftUartNumber, &ch, 1);

    buffer[index++] = ch;

    if (ch == '\r') // 종료 문자
      break;
  }

  buffer[index] = '\0'; // 문자열 종료
  return index;
}

void RS485_Send(uint8_t *data, uint8_t len)
{
  xSemaphoreTake(rs485_tx_mutex, portMAX_DELAY);

  RS485_SetTransmitMode();
  SoftUartPuts(0, data, len);
  SoftUartWaitUntilTxComplate(0);
  RS485_SetReceiveMode();

  xSemaphoreGive(rs485_tx_mutex);
}

volatile bool base_init_finished = false;
static void base_config_complete(bool success, void *user_data)
{
  gps_id_t id = (gps_id_t)(uintptr_t)user_data;
  LOG_INFO("GPS[%d] Fix mode init %s", id, success ? "succeeded" : "failed");

  base_init_finished = true;
}

static void send_gps_task(void *pvParameters)
{
  char buf[120];
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1)
  {
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
    if (is_gugu_started)
    {
      gps_format_position_data(buf);
      RS485_Send((uint8_t *)buf, strlen(buf));
    }
  }
}

static void rs485_task(void *pvParameter)
{
  char ch;
  char rx_buffer[64];
  uint8_t init_f = 0;
  char buf[64];
  TickType_t current_tick = xTaskGetTickCount();

  const board_config_t *config = board_get_config();
  user_params_t *params = flash_params_get_current();

  uint8_t dummy_buf[64];

  rtk_active_status_t active_status = RTK_ACTIVE_STATUS_NONE;

  // 시스템 부팅 후 3초 대기 (초기화 완료 대기)

  vTaskDelay(pdMS_TO_TICKS(2900));

  // RX 버퍼 완전히 비우기 (리셋 후 남아있을 수 있는 쓰레기 데이터 제거)
  while (SoftUartRxAlavailable(0) > 0)
  {
    uint8_t available = SoftUartRxAlavailable(0);
    SoftUartReadRxBuffer(0, dummy_buf, available > 64 ? 64 : available);
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  // TX 경로 클리어를 위한 더미 전송 (첫 바이트 손실 방지)
  RS485_SetTransmitMode();
  vTaskDelay(pdMS_TO_TICKS(10)); // RS485 DE/RE 핀 안정화
  uint8_t dummy_tx = '\0';
  SoftUartPuts(0, &dummy_tx, 1);
  SoftUartWaitUntilTxComplate(0);
  vTaskDelay(pdMS_TO_TICKS(10)); // 전송 완료 대기
  RS485_SetReceiveMode();

  // 추가 안정화 대기
  vTaskDelay(pdMS_TO_TICKS(50));
  RS485_Send((uint8_t *)INIT_Notify, strlen(INIT_Notify));

  while (1)
  {
    int len = get_line(0, rx_buffer, sizeof(rx_buffer)); // 문자열 수신

    if (strcmp(rx_buffer, "AT\r") == 0)
    {
      RS485_Send((uint8_t *)AT_Response, strlen(AT_Response));
    }
    else if (strcmp(rx_buffer, "ATZ\r") == 0)
    {
      user_params_t *current_params = flash_params_get_current();
      // if(flash_params_save(current_params) != HAL_OK)
      // {
      //     RS485_Send((uint8_t*)ERROR_Response, strlen(ERROR_Response));
      // }
      // else
      // {
      RS485_Send((uint8_t *)ATZ_Response, strlen(ATZ_Response));
      vTaskDelay(pdMS_TO_TICKS(5));
      NVIC_SystemReset();
      // }
    }
    else if (strcmp(rx_buffer, "AT&F\r") == 0)
    {
      if (flash_params_erase() == HAL_OK)
      {
        RS485_Send((uint8_t *)ATnF_Response, strlen(ATnF_Response));
        vTaskDelay(pdMS_TO_TICKS(5));
        NVIC_SystemReset();
      }
      else
      {
        RS485_Send(ERROR_Response, strlen(ERROR_Response));
      }
    }
    else if (strcmp(rx_buffer, "AT+VER?\r") == 0)
    {
      sprintf(buf, "+%s\r", BOARD_VERSION);
      RS485_Send((uint8_t *)buf, strlen(buf));
    }
    else if (strcmp(rx_buffer, "AT+GPSMANUF?\r") == 0)
    {
      if (config->board == BOARD_TYPE_BASE_UM982 || config->board == BOARD_TYPE_ROVER_UM982)
      {
        RS485_Send((uint8_t *)GPSMANUF_UM982_Response, strlen(GPSMANUF_UM982_Response));
      }
      else if (config->board == BOARD_TYPE_BASE_F9P || config->board == BOARD_TYPE_ROVER_F9P)
      {
        RS485_Send((uint8_t *)GPSMANUF_F9P_Response, strlen(GPSMANUF_F9P_Response));
      }
    }
    else if (strcmp(rx_buffer, "AT+CONFIG?\r") == 0)
    {
      // RS485_Send((uint8_t*)CONFIG_Response, strlen(CONFIG_Response));
    }
    else if (strncmp(rx_buffer, "AT+SETBASELINE:", 15) == 0)
    {
      float baseline_value = strtof(rx_buffer + 15, NULL);

      if (baseline_value != 0)
      {
        flash_params_set_baseline_len(baseline_value);
        gps_set_heading_length();

        RS485_Send((uint8_t *)AT_Response, strlen(AT_Response));
      }
      else
      {
        RS485_Send((uint8_t *)ERROR2_Response, strlen(ERROR2_Response));
      }
    }
    else if (strncmp(rx_buffer, "AT+CASTER:", 10) == 0)
    {
      char caster_host[64];
      char caster_port[8];

      char *param = (char *)rx_buffer + 10;
      char *port_sep = strrchr(param, ':'); // 마지막 ':' 찾기
      if (port_sep)
      {
        *port_sep = '\0'; // host와 port 분리
        strncpy(caster_host, param, sizeof(caster_host) - 1);

        char *end = strchr(port_sep + 1, '\r');
        if (end)
          *end = '\0';
        strncpy(caster_port, port_sep + 1, sizeof(caster_port) - 1);
      }

      int host_len = strlen(caster_host);
      int port_len = strlen(caster_port);

      if (host_len == 0 || port_len == 0 || host_len >= 63 || port_len >= 7)
      {
        RS485_Send(ERROR2_Response, strlen(ERROR2_Response));
      }
      else
      {
        flash_params_set_ntrip_url((const char *)caster_host);
        flash_params_set_ntrip_port((const char *)caster_port);

        sprintf(buf, "+CASTER=%s:%s\r", params->ntrip_url, params->ntrip_port);
        RS485_Send((uint8_t *)buf, strlen(buf));
      }
    }
    else if (strncmp(rx_buffer, "AT+ID=", 6) == 0)
    {
      char ntrip_id[32];
      char *param = (char *)rx_buffer + 6;
      char *end = strchr(param, '\r');
      if (end)
        *end = '\0';
      strncpy(ntrip_id, param, sizeof(ntrip_id) - 1);

      int id_len = strlen(ntrip_id);
      if (id_len == 0 || id_len >= 31)
      {
        RS485_Send(ERROR2_Response, strlen(ERROR2_Response));
      }
      else
      {
        flash_params_set_ntrip_id(ntrip_id);

        sprintf(buf, "+ID=%s\r", params->ntrip_id);
        RS485_Send((uint8_t *)buf, strlen(buf));
      }
    }
    else if (strncmp(rx_buffer, "AT+MOUNTPOINT=", 14) == 0)
    {
      char mountpoint[32];
      char *param = (char *)rx_buffer + 14;
      char *end = strchr(param, '\r');
      if (end)
        *end = '\0';
      strncpy(mountpoint, param, sizeof(mountpoint) - 1);

      int mountpoint_len = strlen(mountpoint);

      if (mountpoint_len == 0 || mountpoint_len >= 31)
      {
        RS485_Send(ERROR2_Response, strlen(ERROR2_Response));
      }
      else
      {
        flash_params_set_ntrip_mountpoint(mountpoint);

        sprintf(buf, "+MOUNTPOINT=%s\r", params->ntrip_mountpoint);
        RS485_Send((uint8_t *)buf, strlen(buf));
      }
    }
    else if (strncmp(rx_buffer, "AT+PASSWORD=", 12) == 0)
    {
      char password[32];
      char *param = (char *)rx_buffer + 12;
      char *end = strchr(param, '\r');
      if (end)
        *end = '\0';
      strncpy(password, param, sizeof(password) - 1);

      int password_len = strlen(password);

      if (password_len == 0 || password_len >= 31)
      {
        RS485_Send(ERROR2_Response, strlen(ERROR2_Response));
      }
      else
      {
        flash_params_set_ntrip_pw(password);

        sprintf(buf, "+PASSWORD=%s\r", params->ntrip_pw);
        RS485_Send((uint8_t *)buf, strlen(buf));
      }
    }
    else if (strncmp(rx_buffer, "AT+GUGUSTART:", 13) == 0)
    {
      char *param = (char *)rx_buffer + 13;
      char *end = strchr(param, '\r');
      if (end)
        *end = '\0';

      char cmd[10];
      strncpy(cmd, param, sizeof(cmd) - 1);

      if (strncmp(cmd, "LTE", 3) == 0)
      {
        if(active_status == RTK_ACTIVE_STATUS_LORA)
        {
          lora_instance_deinit();
        }
        
        gps_init_all();
        gsm_start_rover();
        RS485_Send("+GUGUSTART-LTE", strlen("+GUGUSTART-LTE"));
        active_status = RTK_ACTIVE_STATUS_GSM;
        is_gugu_started = true;
      }
      else if (strncmp(cmd, "LORA", 4) == 0)
      {
        if(active_status == RTK_ACTIVE_STATUS_GSM)
        {
          ntrip_stop();
          vTaskDelay(pdMS_TO_TICKS(100));
          gsm_port_set_airplane_mode(true);
        }
        
        gps_init_all();
        lora_start_rover();
        RS485_Send("+GUGUSTART-LORA", strlen("+GUGUSTART-LORA"));
        active_status = RTK_ACTIVE_STATUS_LORA;
        is_gugu_started = true;
      }
      else
      {
        RS485_Send((uint8_t *)ERROR2_Response, strlen(ERROR2_Response));
      }
    }
    else if (strncmp(rx_buffer, "AT+GUGUSTOP", 11) == 0)
    {
      if(active_status == RTK_ACTIVE_STATUS_LORA)
      {
        lora_instance_deinit();
        gps_cleanup_all();
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms 대기 권장
        is_gugu_started = false;
        active_status = RTK_ACTIVE_STATUS_NONE;
        RS485_Send((uint8_t *)STOP_Response, strlen(STOP_Response));
      }
      else if(active_status == RTK_ACTIVE_STATUS_GSM)
      {
        if(lte_get_init_state() == LTE_INIT_DONE)
        {
          // gsm 초기화
          is_gugu_started = false;
          ntrip_stop();
          vTaskDelay(pdMS_TO_TICKS(100));
          gsm_port_set_airplane_mode(true);

          gps_cleanup_all();
          vTaskDelay(pdMS_TO_TICKS(100));  // 100ms 대기 권장;
          active_status = RTK_ACTIVE_STATUS_NONE;
          RS485_Send((uint8_t *)STOP_Response, strlen(STOP_Response));
        }
        else
        {
          RS485_Send(ERROR3_Response, strlen(ERROR3_Response));
        }
      }
      else
      {
        is_gugu_started = false;
        RS485_Send((uint8_t *)STOP_Response, strlen(STOP_Response));
      }
    }
    else if (strncmp(rx_buffer, "AT+SAVE\r", 8) == 0)
    {
      user_params_t *params = flash_params_get_current();
      if (flash_params_erase() != HAL_OK)
      {
        RS485_Send(ERROR_Response, strlen(ERROR_Response));
      }
      else
      {
        if (flash_params_write(params) != HAL_OK)
        {
          RS485_Send(ERROR_Response, strlen(ERROR_Response));
        }
        else
        {
          RS485_Send((uint8_t *)"+SAVE\r", strlen("+SAVE\r"));
          NVIC_SystemReset();
        }
      }
    }
    else
    {
      RS485_Send((uint8_t *)ERROR1_Response, strlen(ERROR1_Response));
    }
  }
}

void rs485_app_init(void)
{
  rs485_tx_mutex = xSemaphoreCreateMutex();
  xTaskCreate(rs485_task, "RS485_Task", 512, NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(send_gps_task, "send_gps", 512, NULL, tskIDLE_PRIORITY + 1, NULL);
}

#endif
