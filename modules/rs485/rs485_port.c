#include "rs485_port.h"
#include "board_config.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"
#include "FreeRTOS.h"
#include "queue.h"

#ifndef TAG
    #define TAG "RS485_PORT"
#endif

#include "log.h"

#define RS485_PORT_UART USART5
#define RS485_PORT_UART_DMA DMA1
#define RS485_PORT_UART_DMA_STREAM LL_DMA_STREAM_0

static char rs485_recv_buf[1][512];
static QueueHandle_t rs485_queues[1] = {NULL};

static void rs485_rx_enable();

static inline void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

static void rs485_uart5_dma_init(void)
{
  /* Init with LL driver */
  /* DMA controller clock enable */
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  NVIC_SetPriority(DMA1_Stream0_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),5, 0));
  NVIC_EnableIRQ(DMA1_Stream0_IRQn);

}

static void rs485_uart5_init(void)
{
 
  /* USER CODE BEGIN UART5_Init 0 */

  /* USER CODE END UART5_Init 0 */

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_UART5);

  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);
  /**UART5 GPIO Configuration
  PC12   ------> UART5_TX
  PD2   ------> UART5_RX
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

  /* UART5 DMA Init */

  /* UART5_RX Init */
  LL_DMA_SetChannelSelection(DMA1, LL_DMA_STREAM_0, LL_DMA_CHANNEL_4);

  LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_STREAM_0, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

  LL_DMA_SetStreamPriorityLevel(DMA1, LL_DMA_STREAM_0, LL_DMA_PRIORITY_LOW);

  LL_DMA_SetMode(DMA1, LL_DMA_STREAM_0, LL_DMA_MODE_CIRCULAR);

  LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_STREAM_0, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_STREAM_0, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA1, LL_DMA_STREAM_0, LL_DMA_PDATAALIGN_BYTE);

  LL_DMA_SetMemorySize(DMA1, LL_DMA_STREAM_0, LL_DMA_MDATAALIGN_BYTE);

  LL_DMA_DisableFifoMode(DMA1, LL_DMA_STREAM_0);

  /* UART5 interrupt Init */
  NVIC_SetPriority(UART5_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),5, 0));
  NVIC_EnableIRQ(UART5_IRQn);

  /* USER CODE BEGIN UART5_Init 1 */

  /* USER CODE END UART5_Init 1 */
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(UART5, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(UART5);
  /* USER CODE BEGIN UART5_Init 2 */

  /* USER CODE END UART5_Init 2 */
}

int rs485_uart5_comm_start(void) {
  LL_DMA_SetPeriphAddress(DMA1, LL_DMA_STREAM_0, (uint32_t)&UART5->DR);
  LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_0,
                          (uint32_t)&rs485_recv_buf[0]);
  LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_0,
                       sizeof(rs485_recv_buf[0]));
  LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_0);
  LL_DMA_EnableIT_FE(DMA1, LL_DMA_STREAM_0);
  LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_0);

  LL_USART_EnableIT_IDLE(UART5);
  LL_USART_EnableIT_PE(UART5);
  LL_USART_EnableIT_ERROR(UART5);
  LL_USART_EnableDMAReq_RX(UART5);

  LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_0);
  LL_USART_Enable(UART5);

  return 0;
}

int rs485_uart5_hw_init(void) {
  rs485_uart5_dma_init();
  rs485_uart5_init();
  rs485_rx_enable();

  return 0;
}

int rs485_uart5_send(const char *data, size_t len) {
  for (int i = 0; i < len; i++) {
    while (!LL_USART_IsActiveFlag_TXE(UART5))
      ;
    LL_USART_TransmitData8(UART5, *(data + i));
  }

  while (!LL_USART_IsActiveFlag_TC(UART5))
    ;

  return 0;
}

void rs485_tx_enable()
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET); // DE
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, GPIO_PIN_SET); // /RE

    delay_us(5);
}

void rs485_rx_enable()
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET); // DE
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, GPIO_PIN_RESET); // /RE

    delay_us(5);
}

static const rs485_hal_ops_t rs485_uart5_ops = {
    .init = rs485_uart5_hw_init,
    .reset = NULL,
    .start = rs485_uart5_comm_start,
    .stop = NULL,
    .send = rs485_uart5_send,
    .recv = NULL,
    .tx_enable = rs485_tx_enable,
    .rx_enable = rs485_rx_enable,
};


#if defined(BOARD_TYPE_ROVER_UNICORE) || defined(BOARD_TYPE_ROVER_UBLOX)
/**
 * @brief This function handles UART5 global interrupt.
 */
void UART5_IRQHandler(void) {
    /* USER CODE BEGIN USART3_IRQn 0 */
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (LL_USART_IsActiveFlag_IDLE(UART5)) {
    if (rs485_queues[0] != NULL) {
      uint8_t dummy = 0;
      xQueueSendFromISR(rs485_queues[0], &dummy, &xHigherPriorityTaskWoken);
    }
    LL_USART_ClearFlag_IDLE(UART5);
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
  /* USER CODE END USART3_IRQn 0 */
  /* USER CODE BEGIN USART3_IRQn 1 */

  /* USER CODE END USART3_IRQn 1 */
}

void DMA1_Stream0_IRQHandler(void)
{
  if (LL_DMA_IsActiveFlag_TE0(DMA1)) {

    LL_DMA_ClearFlag_TE0(DMA1);

    LOG_ERR("RS485 DMA Transfer Error");

  }

  if (LL_DMA_IsActiveFlag_FE0(DMA1)) {

    LL_DMA_ClearFlag_FE0(DMA1);

    LOG_ERR("RS485 DMA FIFO Error");

  }

  if (LL_DMA_IsActiveFlag_DME0(DMA1)) {

    LL_DMA_ClearFlag_DME0(DMA1);

    LOG_ERR("RS485 DMA Direct Mode Error");
  }
}

#endif

int rs485_port_init_instance(rs485_t *rs485_handle) {
  const board_config_t *config = board_get_config();

  LOG_INFO("RS485 Port 초기화 시작 (보드: %d)", config->board);

    if(config->use_rs485 == true)
    {
        rs485_handle->ops = &rs485_uart5_ops;
        if (rs485_handle->ops->init) {
            rs485_handle->ops->init();
        }
    }
    else
    {
        LOG_ERR("RS485 포트 초기화 실패: 잘못된 RS485 모드");
        return -1;
    }

    LOG_INFO("RS485 초기화 완료");

    return 0;
}


void rs485_port_start(rs485_t *rs485_handle) {
  if (!rs485_handle || !rs485_handle->ops || !rs485_handle->ops->start) {
    LOG_ERR("RS485 start failed: invalid handle or ops");
    return;
  }

  rs485_handle->ops->start();
}

void rs485_port_stop(rs485_t *rs485_handle) {
  if (!rs485_handle || !rs485_handle->ops || !rs485_handle->ops->stop) {
    LOG_ERR("RS485 stop failed: invalid handle or ops");
    return;
  }

  rs485_handle->ops->stop();
}

uint32_t rs485_port_get_rx_pos() {
  uint32_t pos = sizeof(rs485_recv_buf[0]) - LL_DMA_GetDataLength(RS485_PORT_UART_DMA, RS485_PORT_UART_DMA_STREAM);
  return pos;
}

char *rs485_port_get_recv_buf() 
{
  return rs485_recv_buf[0];
}

void rs485_port_set_queue(QueueHandle_t queue) {
    rs485_queues[0] = queue;
}
