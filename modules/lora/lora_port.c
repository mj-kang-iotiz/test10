#include "lora.h"
#include "lora_port.h"
#include "board_config.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"

#ifndef TAG
    #define TAG "LORA_PORT"
#endif

#include "log.h"

#define LORA_PORT_UART USART3
#define LORA_PORT_UART_DMA DMA1
#define LORA_PORT_UART_DMA_STREAM LL_DMA_STREAM_1

static char lora_recv_buf[1][1024];
static QueueHandle_t lora_queues[1] = {NULL};

static void lora_uart3_dma_init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream1_IRQn interrupt configuration */
  NVIC_SetPriority(DMA1_Stream1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),5, 0));
  NVIC_EnableIRQ(DMA1_Stream1_IRQn);

}

static void lora_uart3_init(void)
{
  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);

  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
  /**USART3 GPIO Configuration
  PB10   ------> USART3_TX
  PB11   ------> USART3_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_10|LL_GPIO_PIN_11;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USART3 DMA Init */

  /* USART3_RX Init */
  LL_DMA_SetChannelSelection(DMA1, LL_DMA_STREAM_1, LL_DMA_CHANNEL_4);

  LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_STREAM_1, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

  LL_DMA_SetStreamPriorityLevel(DMA1, LL_DMA_STREAM_1, LL_DMA_PRIORITY_LOW);

  LL_DMA_SetMode(DMA1, LL_DMA_STREAM_1, LL_DMA_MODE_CIRCULAR);

  LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_STREAM_1, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_STREAM_1, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA1, LL_DMA_STREAM_1, LL_DMA_PDATAALIGN_BYTE);

  LL_DMA_SetMemorySize(DMA1, LL_DMA_STREAM_1, LL_DMA_MDATAALIGN_BYTE);

  LL_DMA_DisableFifoMode(DMA1, LL_DMA_STREAM_1);

  /* USART3 interrupt Init */
  NVIC_SetPriority(USART3_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),5, 0));
  NVIC_EnableIRQ(USART3_IRQn);

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART3, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(USART3);
}

int lora_uart3_comm_start(void) {
  LL_DMA_SetPeriphAddress(DMA1, LL_DMA_STREAM_1, (uint32_t)&USART3->DR);
  LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_1,
                          (uint32_t)&lora_recv_buf[0]);
  LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_1,
                       sizeof(lora_recv_buf[0]));
  LL_DMA_EnableIT_TE(DMA1, LL_DMA_STREAM_1);
  LL_DMA_EnableIT_FE(DMA1, LL_DMA_STREAM_1);
  LL_DMA_EnableIT_DME(DMA1, LL_DMA_STREAM_1);

  LL_USART_EnableIT_IDLE(USART3);
  LL_USART_EnableIT_PE(USART3);
  LL_USART_EnableIT_ERROR(USART3);
  LL_USART_EnableDMAReq_RX(USART3);

  LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_1);
  LL_USART_Enable(USART3);

  return 0;
}

int lora_uart3_hw_init(void) {
  lora_uart3_dma_init();
  lora_uart3_init();

  return 0;
}

int lora_uart3_send(const char *data, size_t len) {
  for (int i = 0; i < len; i++) {
    while (!LL_USART_IsActiveFlag_TXE(LORA_PORT_UART))
      ;
    LL_USART_TransmitData8(LORA_PORT_UART, *(data + i));
  }

  while (!LL_USART_IsActiveFlag_TC(LORA_PORT_UART))
    ;

  return 0;
}
int lora_uart3_comm_stop(void) {

  // UART 인터럽트 비활성화

  LL_USART_DisableIT_IDLE(LORA_PORT_UART);

  LL_USART_DisableIT_PE(LORA_PORT_UART);

  LL_USART_DisableIT_ERROR(LORA_PORT_UART);

  LL_USART_DisableDMAReq_RX(LORA_PORT_UART);

 

  // DMA 인터럽트 비활성화

  LL_DMA_DisableIT_TE(LORA_PORT_UART_DMA, LORA_PORT_UART_DMA_STREAM);

  LL_DMA_DisableIT_FE(LORA_PORT_UART_DMA, LORA_PORT_UART_DMA_STREAM);

  LL_DMA_DisableIT_DME(LORA_PORT_UART_DMA, LORA_PORT_UART_DMA_STREAM);

 

  // DMA 스트림 비활성화

  LL_DMA_DisableStream(LORA_PORT_UART_DMA, LORA_PORT_UART_DMA_STREAM);

 

  // USART 비활성화

  LL_USART_Disable(LORA_PORT_UART);

 

  LOG_INFO("LoRa UART3 통신 중지 완료");

 

  return 0;

}


static const lora_hal_ops_t lora_uart3_ops = {
    .init = lora_uart3_hw_init,
    .reset = NULL,
    .start = lora_uart3_comm_start,
    .stop = lora_uart3_comm_stop,
    .send = lora_uart3_send,
    .recv = NULL,
};


/**
 * @brief This function handles USART3 global interrupt.
 */
void USART3_IRQHandler(void) {
    /* USER CODE BEGIN USART3_IRQn 0 */
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (LL_USART_IsActiveFlag_IDLE(LORA_PORT_UART)) {
    if (lora_queues[0] != NULL) {
      uint8_t dummy = 0;
      xQueueSendFromISR(lora_queues[0], &dummy, &xHigherPriorityTaskWoken);
    }
    LL_USART_ClearFlag_IDLE(LORA_PORT_UART);
  }


  if (LL_USART_IsActiveFlag_PE(LORA_PORT_UART)) {
    LL_USART_ClearFlag_PE(LORA_PORT_UART);
  }
  if (LL_USART_IsActiveFlag_FE(LORA_PORT_UART)) {
    LL_USART_ClearFlag_FE(LORA_PORT_UART);
  }
  if (LL_USART_IsActiveFlag_ORE(LORA_PORT_UART)) {
    LL_USART_ClearFlag_ORE(LORA_PORT_UART);
  }
  if (LL_USART_IsActiveFlag_NE(LORA_PORT_UART)) {
    LL_USART_ClearFlag_NE(LORA_PORT_UART);
  }

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  /* USER CODE END USART3_IRQn 0 */
  /* USER CODE BEGIN USART3_IRQn 1 */

  /* USER CODE END USART3_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream1 global interrupt.
  */
void DMA1_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream1_IRQn 0 */
//   LL_DMA_ClearFlag_TC1(LORA_PORT_UART_DMA);
//   LL_DMA_ClearFlag_HT1(LORA_PORT_UART_DMA);

//   if (&lora_queues[0] != NULL) {
//     BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//     uint8_t dummy = 0;
//     xQueueSendFromISR(&lora_queues[0], &dummy, &xHigherPriorityTaskWoken);
//     portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
//   }
  /* USER CODE END DMA1_Stream1_IRQn 0 */
  /* USER CODE BEGIN DMA1_Stream1_IRQn 1 */

  /* USER CODE END DMA1_Stream1_IRQn 1 */
}


int lora_port_init_instance(lora_t *lora_handle) {
  const board_config_t *config = board_get_config();

  LOG_INFO("LORA Port 초기화 시작 (보드: %d, GPS 타입: %s)",
           config->board, config->lora_mode == LORA_MODE_BASE ? "BASE" :
                        (config->lora_mode == LORA_MODE_ROVER? "ROVER" : "NONE"));

    if(config->lora_mode == LORA_MODE_BASE)
    {
        lora_handle->ops = &lora_uart3_ops;
        if (lora_handle->ops->init) {
            lora_handle->ops->init();
        }
    }
    else if(config->lora_mode == LORA_MODE_ROVER)
    {
        lora_handle->ops = &lora_uart3_ops;
        if (lora_handle->ops->init) {
            lora_handle->ops->init();
        }
    }
    else
    {
        LOG_ERR("LORA 포트 초기화 실패: 잘못된 LORA 모드");
        return -1;
    }

    LOG_INFO("LORA 초기화 완료");

    return 0;
}


void lora_port_start(lora_t *lora_handle) {
  if (!lora_handle || !lora_handle->ops || !lora_handle->ops->start) {
    LOG_ERR("LORA start failed: invalid handle or ops");
    return;
  }

  lora_handle->ops->start();
}

void lora_port_stop(lora_t *lora_handle) {
  if (!lora_handle || !lora_handle->ops || !lora_handle->ops->stop) {
    LOG_ERR("LORA start failed: invalid handle or ops");
    return;
  }

  lora_handle->ops->stop();
  LOG_INFO("LoRa 포트 중지 완료");
}

uint32_t lora_port_get_rx_pos() {
  uint32_t pos = sizeof(lora_recv_buf[0]) - LL_DMA_GetDataLength(LORA_PORT_UART_DMA, LORA_PORT_UART_DMA_STREAM);
  return pos;
}

char *lora_port_get_recv_buf() 
{
  return lora_recv_buf[0];
}

void lora_port_set_queue(QueueHandle_t queue) {
    lora_queues[0] = queue;
}
