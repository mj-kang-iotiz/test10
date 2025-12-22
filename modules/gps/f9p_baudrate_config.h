#ifndef F9P_BAUDRATE_CONFIG_H
#define F9P_BAUDRATE_CONFIG_H


#include <stdbool.h>

 

/**

 * Base F9P UART1 보드레이트 초기화 (38400 → 115200)

 * gps_rtk_uart2_init()에서 자동 호출됨 (DMA 활성화 전)

 * STM32 UART2도 함께 115200으로 변경

 * @return true if success

 */

bool f9p_init_uart1_baudrate_115200(void);

 

/**

 * Rover F9P UART1 보드레이트 초기화 (38400 → 115200)

 * gps_rtk_uart4_init()에서 자동 호출됨 (DMA 활성화 전)

 * STM32 UART4도 함께 115200으로 변경

 * @return true if success

 */

bool f9p_init_rover_uart1_baudrate_115200(void);

 

#endif