#include "ble_port.h"
#include "ble_app.h"
#include "board_config.h"
#include "board_type.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>
#include "flash_params.h"

#ifndef TAG
    #define TAG "BLE_PORT"
#endif

#include "log.h"

static int ble_uart5_change_baudrate(uint32_t baudrate);
static int ble_send_at_command_sync(const char *at_cmd, const char *expected_response, uint32_t timeout_ms);
int ble_uart5_recv_line_poll(char *buf, size_t buf_size, uint32_t timeout_ms);
static int ble_send_uart_change_command(uint32_t baudrate, uint32_t timeout_ms);
static int ble_configure_module(void);

#define BLE_PORT_UART UART5
#define BLE_PORT_UART_DMA DMA1
#define BLE_PORT_UART_DMA_STREAM LL_DMA_STREAM_0

static char ble_recv_buf[1][1024];
static QueueHandle_t ble_queues[1] = {NULL};

int ble_set_at_cmd_mode(void);
int ble_set_bypass_mode(void);

static void ble_uart5_dma_init(void)
{
  /* DMA controller clock enable */
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

  /* DMA interrupt init - 우선순위만 설정, IRQ는 나중에 활성화 */
  NVIC_SetPriority(DMA1_Stream0_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
  /* NVIC_EnableIRQ는 comm_start에서 호출 */
}

static void ble_uart5_gpio_init(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);

  /* UART5 GPIO Configuration
     PC12   ------> UART5_TX
     PD2    ------> UART5_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_12;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_8;
  LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_8;
  LL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

static void ble_uart5_init(void)
{
  LL_USART_InitTypeDef USART_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_UART5);

  /* GPIO 초기화 */
  ble_uart5_gpio_init();

  /* DMA 설정 (스트림은 아직 활성화하지 않음) */
  LL_DMA_SetChannelSelection(DMA1, LL_DMA_STREAM_0, LL_DMA_CHANNEL_4);
  LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_STREAM_0, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  LL_DMA_SetStreamPriorityLevel(DMA1, LL_DMA_STREAM_0, LL_DMA_PRIORITY_LOW);
  LL_DMA_SetMode(DMA1, LL_DMA_STREAM_0, LL_DMA_MODE_CIRCULAR);
  LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_STREAM_0, LL_DMA_PERIPH_NOINCREMENT);
  LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_STREAM_0, LL_DMA_MEMORY_INCREMENT);
  LL_DMA_SetPeriphSize(DMA1, LL_DMA_STREAM_0, LL_DMA_PDATAALIGN_BYTE);
  LL_DMA_SetMemorySize(DMA1, LL_DMA_STREAM_0, LL_DMA_MDATAALIGN_BYTE);
  LL_DMA_DisableFifoMode(DMA1, LL_DMA_STREAM_0);

  /* ★★★ 중요: UART 인터럽트 NVIC는 여기서 설정만 하고, 활성화는 comm_start에서 ★★★ */
  NVIC_SetPriority(UART5_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
  /* NVIC_EnableIRQ(UART5_IRQn); ← 이것을 comm_start로 이동! */

  /* USART 설정 */
  USART_InitStruct.BaudRate = 9600;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(UART5, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(UART5);
}

int ble_uart5_comm_start(void) {
  /* ★★★ 태스크 컨텍스트에서 호출됨 - vTaskDelay 사용 가능 ★★★ */
  
  /* 1. BLE 모듈 설정 (baudrate 변경, advertising 시작 등) */
  ble_configure_module();
  
  /* 2. DMA 설정 */
  LL_DMA_SetPeriphAddress(DMA1, LL_DMA_STREAM_0, (uint32_t)&UART5->DR);
  LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_0, (uint32_t)&ble_recv_buf[0]);
  LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_0, sizeof(ble_recv_buf[0]));
  
  /* DMA 에러 인터럽트 활성화 */
  LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_0);
  LL_DMA_EnableIT_FE(DMA1, LL_DMA_STREAM_0);
  LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_0);

  /* 3. 인터럽트 활성화 (큐 설정 후이므로 안전) */
  NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  NVIC_EnableIRQ(UART5_IRQn);

  /* USART 인터럽트 및 DMA 활성화 */
  LL_USART_EnableIT_IDLE(UART5);
  LL_USART_EnableIT_PE(UART5);
  LL_USART_EnableIT_ERROR(UART5);
  LL_USART_EnableDMAReq_RX(UART5);

  /* DMA 스트림 및 USART 활성화 */
  LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_0);
  LL_USART_Enable(UART5);

  LOG_INFO("BLE UART5 DMA communication started");
  return 0;
}

/*
 * hw_init: 하드웨어 초기화만 수행 (스케줄러 시작 전 호출 가능)
 * - GPIO, UART, DMA 설정
 * - 폴링 통신이나 vTaskDelay 사용 금지!
 */
int ble_uart5_hw_init(void) {
  // 1. DMA 클럭 활성화
  ble_uart5_dma_init();

  // 2. UART 초기화 (NVIC 설정만, 활성화는 comm_start에서)
  ble_uart5_init();
  
  // 3. GPIO 초기 상태 설정 (Bypass 모드)
  ble_set_bypass_mode();

  LOG_INFO("BLE hardware initialized (UART5 @ 9600 bps default)");
  
  // ★★★ 중요: 여기서는 폴링/딜레이 사용하지 않음! ★★★
  // BLE 모듈 설정은 comm_start()에서 수행 (태스크 컨텍스트)
  
  return 0;
}

/*
 * 모듈 설정 (태스크 컨텍스트에서만 호출!)
 * - baudrate 변경, AT+ADVON 등
 */
static int ble_configure_module(void) {
  int ret;
  uint32_t current_baudrate = 9600;

  LOG_INFO("BLE module configuration started (task context)");

  // 1. UART 활성화 (폴링 모드로 사용)
  LL_USART_Enable(UART5);

  // 2. AT 커맨드 모드로 전환
  ble_set_bypass_mode();
  vTaskDelay(pdMS_TO_TICKS(100));  // 이제 안전하게 사용 가능
  ble_set_at_cmd_mode();
  vTaskDelay(pdMS_TO_TICKS(300));

  // 3. 부팅 메시지 버퍼 클리어
  while (LL_USART_IsActiveFlag_RXNE(UART5)) {
    (void)LL_USART_ReceiveData8(UART5);
  }
  LOG_INFO("UART RX buffer cleared");

  // 4. 자동 baudrate 감지 및 설정
  LOG_INFO("Auto-detecting BLE module baudrate...");
  LOG_INFO("Testing 9600 bps...");

  char buffer[20];
  int buffer_len = 0;
  ble_uart5_send("AT\r", strlen("AT\r"));
  buffer_len = ble_uart5_recv_line_poll(buffer, sizeof(buffer), 20);
  ble_uart5_send("AT\r", strlen("AT\r"));
  buffer_len = ble_uart5_recv_line_poll(buffer, sizeof(buffer), 20);

  if(strncmp(buffer, "+OK", 3) == 0)
  {
    ret = 0;
  }
  else
  {
    ret = 1;
  }

  if (ret == 0) {
    LOG_INFO("9600 bps communication OK, changing to 115200 bps...");
    vTaskDelay(pdMS_TO_TICKS(50));
      ble_uart5_send("AT\r", strlen("AT\r"));
  buffer_len = ble_uart5_recv_line_poll(buffer, sizeof(buffer), 2000);
  vTaskDelay(pdMS_TO_TICKS(50));
    ret = ble_send_uart_change_command(115200, 100);

    if (ret == 0) {
      LOG_INFO("Baudrate changed to 115200 successfully");
      current_baudrate = 115200;
    } else {
      LOG_ERR("Failed to change BLE module baudrate, staying at 9600");
      current_baudrate = 9600;
    }
  } else {
    LOG_INFO("9600 bps no response, assuming 115200 bps...");
    ble_uart5_change_baudrate(115200);
    current_baudrate = 115200;
    ble_uart5_send("AT\r", strlen("AT\r"));
    buffer_len = ble_uart5_recv_line_poll(buffer, sizeof(buffer), 50);
  }

  LOG_INFO("BLE module configured at %lu bps", current_baudrate);

  LOG_INFO("Reading BLE device name...");

  char device_name[64];

  vTaskDelay(pdMS_TO_TICKS(500));
 ble_uart5_send("AT\r", strlen("AT\r"));
 int name_len = ble_uart5_recv_line_poll(device_name, sizeof(device_name), 200);
  // char temp_buffer1[20];
  // ble_uart5_send("AT+DISCONNECT\r", strlen("AT+DISCONNECT\r"));
  // name_len = ble_uart5_recv_line_poll(temp_buffer1, sizeof(temp_buffer1), 100);
  vTaskDelay(pdMS_TO_TICKS(300));
  ble_uart5_send("AT+MANUF?\r", strlen("AT+MANUF?\r"));
  name_len = ble_uart5_recv_line_poll(device_name, sizeof(device_name), 200);

  if (name_len > 0) {
    if(strncmp(device_name, "+ERR", 4) == 0)
    {
    }
    else
    {
        LOG_INFO("BLE Device Name: %s", device_name);
        vTaskDelay(pdMS_TO_TICKS(200));
        user_params_t* params = flash_params_get_current();
        if(strncmp(params->ble_device_name, device_name, strlen(params->ble_device_name))!=0)
        {
          char temp_buf[100];
          sprintf(temp_buf, "AT+MANUF=%s\r", params->ble_device_name);
          ble_uart5_send(temp_buf, strlen(temp_buf));
          name_len = ble_uart5_recv_line_poll(device_name, sizeof(device_name), 200);
          vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

  } else {
    LOG_WARN("Failed to read BLE device name (timeout)");
  }


  // ble_uart5_send("AT+MANUF=TEST\r", strlen("AT+MANUF=TEST\r"));

  // vTaskDelay(pdMS_TO_TICKS(10));

  // 6. UART 비활성화 (comm_start에서 DMA 모드로 다시 활성화)
  LL_USART_Disable(UART5);
  ble_set_bypass_mode();

  return 0;
}

int ble_uart5_send(const char *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    while (!LL_USART_IsActiveFlag_TXE(UART5))
      ;
    LL_USART_TransmitData8(UART5, *(data + i));
  }

  while (!LL_USART_IsActiveFlag_TC(UART5))
    ;

  return 0;
}

static int ble_uart5_recv_poll(uint8_t *byte, uint32_t timeout_ms) {
  uint32_t start = HAL_GetTick();

  while (!LL_USART_IsActiveFlag_RXNE(UART5)) {
    if ((HAL_GetTick() - start) > timeout_ms) {
      return -1;
    }
    // 와치도그 방지: 긴 대기 시 yield
    if ((HAL_GetTick() - start) > 10) {
      taskYIELD();
    }
  }

  *byte = LL_USART_ReceiveData8(UART5);
  return 0;
}

int ble_uart5_recv_line_poll(char *buf, size_t buf_size, uint32_t timeout_ms) {
  size_t pos = 0;
  uint32_t start = HAL_GetTick();

  while (pos < buf_size - 1) {
    uint8_t byte;
    uint32_t elapsed = HAL_GetTick() - start;
    
    if (elapsed > timeout_ms) {
      buf[pos] = '\0';
      return -1;
    }

    if (ble_uart5_recv_poll(&byte, timeout_ms - elapsed) != 0) {
      buf[pos] = '\0';
      return -1;
    }

    buf[pos++] = (char)byte;

    if (byte == '\r') {
      buf[pos] = '\0';
      return pos;
    }
  }

  buf[buf_size - 1] = '\0';
  return pos;
}

static int ble_uart5_change_baudrate(uint32_t baudrate) {
  LL_USART_Disable(UART5);
  LL_USART_SetBaudRate(UART5, HAL_RCC_GetPCLK1Freq(), LL_USART_OVERSAMPLING_16, baudrate);
  LL_USART_Enable(UART5);

  LOG_INFO("UART5 baudrate changed to %lu", baudrate);
  return 0;
}

static int ble_send_at_command_sync(const char *at_cmd, const char *expected_response, uint32_t timeout_ms) {
  char response[128];

  LOG_INFO("Sending AT command (sync): %s", at_cmd);
  ble_uart5_send(at_cmd, strlen(at_cmd));

  uint32_t start = HAL_GetTick();

  while ((HAL_GetTick() - start) < timeout_ms) {
    int len = ble_uart5_recv_line_poll(response, sizeof(response), 100);

    if (len > 0) {
      LOG_INFO("Received response: %s", response);

      if (strstr(response, expected_response) != NULL) {
        LOG_INFO("AT command succeeded");
        return 0;
      }

      if (strstr(response, "+ERROR") != NULL) {
        LOG_ERR("AT command failed: %s", response);
        return -1;
      }
    }
    
    // 와치도그 방지
    taskYIELD();
  }

  LOG_ERR("AT command timeout");
  return -1;
}

static int ble_send_uart_change_command(uint32_t baudrate, uint32_t timeout_ms) {
  char at_cmd[32];
  char response[128];

  snprintf(at_cmd, sizeof(at_cmd), "AT+UART=%lu\r", baudrate);
  LOG_INFO("Sending UART change command: %s", at_cmd);

  ble_uart5_send(at_cmd, strlen(at_cmd));

  uint32_t start = HAL_GetTick();
  bool ok_received = false;

  while ((HAL_GetTick() - start) < timeout_ms) {
    int len = ble_uart5_recv_line_poll(response, sizeof(response), 100);

    if (len > 0) {
      LOG_INFO("Received response: %s", response);

      if (strstr(response, "+OK") != NULL) {
        LOG_INFO("UART change: +OK received");
        ok_received = true;
        break;
      }

      if (strstr(response, "+ERROR") != NULL) {
        LOG_ERR("UART change failed: %s", response);
        return -1;
      }
    }
    taskYIELD();
  }

  if (!ok_received) {
    LOG_ERR("UART change: +OK timeout");
    return -1;
  }

  LOG_INFO("Changing MCU UART to %lu bps...", baudrate);
  ble_uart5_change_baudrate(baudrate);

  vTaskDelay(pdMS_TO_TICKS(100));
  while (LL_USART_IsActiveFlag_RXNE(UART5)) {
    (void)LL_USART_ReceiveData8(UART5);
  }

  LOG_INFO("Waiting 2 seconds for BLE module reset...");
  vTaskDelay(pdMS_TO_TICKS(2000));

  LOG_INFO("Waiting for +READY at new baudrate...");
  start = HAL_GetTick();

  while ((HAL_GetTick() - start) < timeout_ms) {
    int len = ble_uart5_recv_line_poll(response, sizeof(response), 100);

    if (len > 0) {
      LOG_INFO("Received response: %s", response);

      if (strstr(response, "+READY") != NULL) {
        LOG_INFO("UART change: +READY received");
        return 0;
      }
    }
    taskYIELD();
  }

  LOG_WARN("UART change: +READY timeout (proceeding anyway)");
  return 0;
}

int ble_set_at_cmd_mode(void)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET);
  return 0;
}

int ble_set_bypass_mode(void)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET);
  return 0;
}

static const ble_hal_ops_t ble_uart5_ops = {
    .init = ble_uart5_hw_init,
    .reset = NULL,
    .start = ble_uart5_comm_start,
    .stop = NULL,
    .send = ble_uart5_send,
    .recv = NULL,
    .at_mode = ble_set_at_cmd_mode,
    .bypass_mode = ble_set_bypass_mode,
};

#if defined(BOARD_TYPE_BASE_UNICORE) || defined(BOARD_TYPE_BASE_UBLOX)

void UART5_IRQHandler(void) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (LL_USART_IsActiveFlag_IDLE(UART5)) {
    LL_USART_ClearFlag_IDLE(UART5);
    
    /* ★★★ NULL 체크 추가 ★★★ */
    if (ble_queues[0] != NULL) {
      uint8_t dummy = 0;
      xQueueSendFromISR(ble_queues[0], &dummy, &xHigherPriorityTaskWoken);
    }
  }

  if (LL_USART_IsActiveFlag_PE(UART5)) {
    LL_USART_ClearFlag_PE(UART5);
  }
  if (LL_USART_IsActiveFlag_FE(UART5)) {
    LL_USART_ClearFlag_FE(UART5);
  }
  if (LL_USART_IsActiveFlag_ORE(UART5)) {
    LL_USART_ClearFlag_ORE(UART5);
  }
  if (LL_USART_IsActiveFlag_NE(UART5)) {
    LL_USART_ClearFlag_NE(UART5);
  }

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void DMA1_Stream0_IRQHandler(void)
{
  /* DMA 에러 처리 */
  if (LL_DMA_IsActiveFlag_TE0(DMA1)) {
    LL_DMA_ClearFlag_TE0(DMA1);
    LOG_ERR("DMA Transfer Error");
  }
  if (LL_DMA_IsActiveFlag_FE0(DMA1)) {
    LL_DMA_ClearFlag_FE0(DMA1);
  }
  if (LL_DMA_IsActiveFlag_DME0(DMA1)) {
    LL_DMA_ClearFlag_DME0(DMA1);
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if(GPIO_Pin == GPIO_PIN_11)
  {
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11);

    if(pin_state == GPIO_PIN_RESET)
    {
      ble_set_connection_state(BLE_CONN_DISCONNECTED);
    }
    else
    {
      ble_set_connection_state(BLE_CONN_CONNECTED);
    }
  }
}

void EXTI15_10_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
}

#endif

int ble_port_init_instance(ble_t *ble_handle) {
  const board_config_t *config = board_get_config();

  LOG_INFO("BLE Port 초기화 시작 (보드: %d)", config->board);

  if(config->use_ble == true) {
    ble_handle->ops = &ble_uart5_ops;
    if (ble_handle->ops->init) {
      ble_handle->ops->init();
    }
  } else {
    LOG_ERR("BLE 포트 초기화 실패: BLE 비활성화");
    return -1;
  }

  LOG_INFO("BLE 초기화 완료");
  return 0;
}

void ble_port_start(ble_t *ble_handle) {
  if (!ble_handle || !ble_handle->ops || !ble_handle->ops->start) {
    LOG_ERR("BLE start failed: invalid handle or ops");
    return;
  }

  ble_handle->ops->start();
}

void ble_port_stop(ble_t *ble_handle) {
  if (!ble_handle || !ble_handle->ops || !ble_handle->ops->stop) {
    LOG_ERR("BLE stop failed: invalid handle or ops");
    return;
  }

  ble_handle->ops->stop();
}

uint32_t ble_port_get_rx_pos(void) {
  uint32_t pos = sizeof(ble_recv_buf[0]) - LL_DMA_GetDataLength(BLE_PORT_UART_DMA, BLE_PORT_UART_DMA_STREAM);
  return pos;
}

char *ble_port_get_recv_buf(void) {
  return ble_recv_buf[0];
}

void ble_port_set_queue(QueueHandle_t queue) {
  ble_queues[0] = queue;
}
