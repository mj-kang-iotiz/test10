#include "gsm_port.h"
#include "FreeRTOS.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_utils.h"
#include "task.h"

#define GSM_PORT_UART USART1
#define GSM_PORT_UART_DMA DMA2
#define GSM_PORT_UART_DMA_STREAM LL_DMA_STREAM_2

#define GSM_PORT_GPIO_PORT GPIOB
#define GSM_PORT_GPIO_PWR_PIN GPIO_PIN_3
#define GSM_PORT_GPIO_RST_PIN GPIO_PIN_4
#define GSM_PORT_GPIO_AIRPLANE_PIN GPIO_PIN_5
#define GSM_PORT_GPIO_WAKEUP_PIN GPIO_PIN_6

extern char gsm_mem[2048];

/**
 * Enable DMA controller clock
 */
void gsm_dma_init(void) {

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA2_Stream2_IRQn interrupt configuration */
  NVIC_SetPriority(DMA2_Stream2_IRQn,
                   NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
  NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}

void gsm_uart_init(void) {

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
  /**USART1 GPIO Configuration
  PA9   ------> USART1_TX
  PA10   ------> USART1_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9 | LL_GPIO_PIN_10;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_7;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USART1 DMA Init */

  /* USART1_RX Init */
  LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_2, LL_DMA_CHANNEL_4);

  LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_2,
                                  LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

  LL_DMA_SetStreamPriorityLevel(DMA2, LL_DMA_STREAM_2, LL_DMA_PRIORITY_LOW);

  LL_DMA_SetMode(DMA2, LL_DMA_STREAM_2, LL_DMA_MODE_CIRCULAR);

  LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_2, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_2, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA2, LL_DMA_STREAM_2, LL_DMA_PDATAALIGN_BYTE);

  LL_DMA_SetMemorySize(DMA2, LL_DMA_STREAM_2, LL_DMA_MDATAALIGN_BYTE);

  LL_DMA_DisableFifoMode(DMA2, LL_DMA_STREAM_2);

  /* USART1 interrupt Init */
  NVIC_SetPriority(USART1_IRQn,
                   NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
  NVIC_EnableIRQ(USART1_IRQn);

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(USART1);
  LL_USART_Enable(USART1);
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */
}

void gsm_port_comm_start(void) {
  LL_DMA_SetPeriphAddress(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM,
                          (uint32_t)&GSM_PORT_UART->DR);
  LL_DMA_SetMemoryAddress(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM,
                          (uint32_t)&gsm_mem);
  LL_DMA_SetDataLength(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM,
                       sizeof(gsm_mem));
  LL_DMA_EnableIT_HT(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM);
  LL_DMA_EnableIT_TC(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM);
  LL_DMA_EnableIT_TE(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM);
  LL_DMA_EnableIT_FE(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM);
  LL_DMA_EnableIT_DME(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM);

  LL_USART_EnableIT_IDLE(GSM_PORT_UART);
  LL_USART_EnableIT_PE(GSM_PORT_UART);
  LL_USART_EnableIT_ERROR(GSM_PORT_UART);
  LL_USART_EnableDMAReq_RX(GSM_PORT_UART);

  LL_DMA_EnableStream(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM);
  LL_USART_Enable(GSM_PORT_UART);
}

void gsm_port_gpio_start(void) {
  /* power on */
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_RST_PIN,
                    GPIO_PIN_RESET); // reset
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN,
                    GPIO_PIN_RESET); // pwr
  HAL_Delay(pdMS_TO_TICKS(5));
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN,
                    GPIO_PIN_SET); // pwr
  vTaskDelay(pdMS_TO_TICKS(1000));
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN,
                    GPIO_PIN_RESET); // pwr

  /* wake up mode & airplane mode setting */
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_AIRPLANE_PIN,
                    GPIO_PIN_RESET); // airplane mode
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_WAKEUP_PIN,
                    GPIO_PIN_RESET); // wakeup
}

int gsm_port_power_on(void) {

  // HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN, GPIO_PIN_RESET);
  // vTaskDelay(pdMS_TO_TICKS(10));
  // HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN, GPIO_PIN_SET);
  // vTaskDelay(pdMS_TO_TICKS(200));
  // HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN, GPIO_PIN_RESET);
  gsm_port_gpio_start();
  return 0;

}

/**
 * @brief dma 버퍼 위치 반환
 *
 * @return uint32_t
 */
uint32_t gsm_get_rx_pos(void) {
  return sizeof(gsm_mem) -
         LL_DMA_GetDataLength(GSM_PORT_UART_DMA, GSM_PORT_UART_DMA_STREAM);
}

void gsm_port_init(void) {
  gsm_dma_init();
  gsm_uart_init();
}

void gsm_start(void) {
  gsm_port_comm_start();
  gsm_port_gpio_start();
}

/**
 * @brief EC25 모듈 하드웨어 리셋 (HAL ops 콜백)
 *
 * RST 핀을 이용한 하드웨어 리셋 수행
 * EC25 데이터시트에 따르면 RESET_N 핀을 최소 100ms LOW 유지 시 리셋
 * 리셋 후 부팅 완료까지 약 13초 소요 (RDY URC는 자동 수신)
 *
 * @return int 0: 성공
 */
int gsm_port_reset(void) {
  // RST 핀 HIGH: 리셋 시작
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_RST_PIN, GPIO_PIN_SET);
  vTaskDelay(pdMS_TO_TICKS(150)); // 150ms 리셋 유지 (최소 100ms)

  // RST 핀 LOW: 정상 동작 모드로 전환
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_RST_PIN, GPIO_PIN_RESET);

  // 부팅 초기 대기 (3초)
  // 참고: RDY URC는 약 13초 후 자동 수신됨
  vTaskDelay(pdMS_TO_TICKS(3000));

  return 0;
}

/**
 * @brief This function handles USART1 global interrupt.
 */
void USART1_IRQHandler(void) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (LL_USART_IsActiveFlag_IDLE(GSM_PORT_UART)) {
    if (gsm_queue != NULL) {
      uint8_t dummy = 0;
      xQueueSendFromISR(gsm_queue, &dummy, &xHigherPriorityTaskWoken);
    }
    LL_USART_ClearFlag_IDLE(GSM_PORT_UART);
  }
  /* USER CODE BEGIN USART1_IRQn 0 */

  if (LL_USART_IsActiveFlag_PE(GSM_PORT_UART)) {
    LL_USART_ClearFlag_PE(GSM_PORT_UART);
  }
  if (LL_USART_IsActiveFlag_FE(GSM_PORT_UART)) {
    LL_USART_ClearFlag_FE(GSM_PORT_UART);
  }
  if (LL_USART_IsActiveFlag_ORE(GSM_PORT_UART)) {
    LL_USART_ClearFlag_ORE(GSM_PORT_UART);
  }
  if (LL_USART_IsActiveFlag_NE(GSM_PORT_UART)) {
    LL_USART_ClearFlag_NE(GSM_PORT_UART);
  }

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  /* USER CODE END USART1_IRQn 0 */
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
 * @brief This function handles DMA2 stream2 global interrupt.
 */
void DMA2_Stream2_IRQHandler(void) {
  /* USER CODE BEGIN DMA2_Stream2_IRQn 0 */
  LL_DMA_ClearFlag_TC2(GSM_PORT_UART_DMA);
  LL_DMA_ClearFlag_HT2(GSM_PORT_UART_DMA);

  if (gsm_queue != NULL) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t dummy = 0;
    xQueueSendFromISR(gsm_queue, &dummy, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
  /* USER CODE END DMA2_Stream2_IRQn 0 */
  /* USER CODE BEGIN DMA2_Stream2_IRQn 1 */

  /* USER CODE END DMA2_Stream2_IRQn 1 */
}

void gsm_port_power_off(void) {
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN, GPIO_PIN_SET);
  vTaskDelay(pdMS_TO_TICKS(800));
  HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_PWR_PIN, GPIO_PIN_RESET);
  vTaskDelay(pdMS_TO_TICKS(200));
}

void gsm_port_set_airplane_mode(uint8_t enable) {

  if (enable) {
    // W_DISABLE 핀 HIGH -> Airplane 모드 활성화 (무선 통신 차단)
    HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_AIRPLANE_PIN, GPIO_PIN_SET);
  } else {
    // W_DISABLE 핀 LOW -> Airplane 모드 비활성화 (정상 동작)
    HAL_GPIO_WritePin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_AIRPLANE_PIN, GPIO_PIN_RESET);
  }
}

bool gsm_port_get_airplane_mode(void) {

  // W_DISABLE 핀이 HIGH이면 airplane 모드 활성화

  return HAL_GPIO_ReadPin(GSM_PORT_GPIO_PORT, GSM_PORT_GPIO_AIRPLANE_PIN) == GPIO_PIN_SET;

}
