/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
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

#include "stm32f4xx_ll_exti.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32f4xx_hal_tim.h"
#include "stm32f4xx_hal_tim_ex.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_D1_R_Pin GPIO_PIN_1
#define LED_D1_R_GPIO_Port GPIOC
#define LED_D1_G_Pin GPIO_PIN_2
#define LED_D1_G_GPIO_Port GPIOC
#define LED_D2_R_Pin GPIO_PIN_3
#define LED_D2_R_GPIO_Port GPIOC
#define RTK2_UART4_RX_Pin GPIO_PIN_0
#define RTK2_UART4_RX_GPIO_Port GPIOA
#define RTK2_UART4_TX_Pin GPIO_PIN_1
#define RTK2_UART4_TX_GPIO_Port GPIOA
#define RTK_UART2_RX_Pin GPIO_PIN_2
#define RTK_UART2_RX_GPIO_Port GPIOA
#define RTK_UART2_TX_Pin GPIO_PIN_3
#define RTK_UART2_TX_GPIO_Port GPIOA
#define RTK_INT_Pin GPIO_PIN_4
#define RTK_INT_GPIO_Port GPIOA
#define RTK_INT_EXTI_IRQn EXTI4_IRQn
#define RTK_RST_Pin GPIO_PIN_5
#define RTK_RST_GPIO_Port GPIOA
#define RTK_BAT_ADC1_IN6_Pin GPIO_PIN_6
#define RTK_BAT_ADC1_IN6_GPIO_Port GPIOA
#define RTK2_INT_Pin GPIO_PIN_7
#define RTK2_INT_GPIO_Port GPIOA
#define RTK2_INT_EXTI_IRQn EXTI9_5_IRQn
#define LED_D2_G_Pin GPIO_PIN_4
#define LED_D2_G_GPIO_Port GPIOC
#define LORA_UART3_TX_Pin GPIO_PIN_10
#define LORA_UART3_TX_GPIO_Port GPIOB
#define LORA_UART3_RX_Pin GPIO_PIN_11
#define LORA_UART3_RX_GPIO_Port GPIOB
#define LED_D3_R_Pin GPIO_PIN_13
#define LED_D3_R_GPIO_Port GPIOB
#define LED_D3_G_Pin GPIO_PIN_14
#define LED_D3_G_GPIO_Port GPIOB
#define DEBUG_UART6_TX_Pin GPIO_PIN_6
#define DEBUG_UART6_TX_GPIO_Port GPIOC
#define DEBUG_UART6_RX_Pin GPIO_PIN_7
#define DEBUG_UART6_RX_GPIO_Port GPIOC
#define RTK2_RST_Pin GPIO_PIN_8
#define RTK2_RST_GPIO_Port GPIOA
#define LTE_UART1_TX_Pin GPIO_PIN_9
#define LTE_UART1_TX_GPIO_Port GPIOA
#define LTE_UART1_RX_Pin GPIO_PIN_10
#define LTE_UART1_RX_GPIO_Port GPIOA
#define RS485_DE_Pin GPIO_PIN_10
#define RS485_DE_GPIO_Port GPIOC
#define RS485_RE_Pin GPIO_PIN_11
#define RS485_RE_GPIO_Port GPIOC
#define RS485_UART5_RX_Pin GPIO_PIN_12
#define RS485_UART5_RX_GPIO_Port GPIOC
#define RS485_UART5_TX_Pin GPIO_PIN_2
#define RS485_UART5_TX_GPIO_Port GPIOD
#define LTE_PWRKEY_Pin GPIO_PIN_3
#define LTE_PWRKEY_GPIO_Port GPIOB
#define LTE_RST_Pin GPIO_PIN_4
#define LTE_RST_GPIO_Port GPIOB
#define LTE_W_DISABLE_Pin GPIO_PIN_5
#define LTE_W_DISABLE_GPIO_Port GPIOB
#define LTE_WAKEUP_Pin GPIO_PIN_6
#define LTE_WAKEUP_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
