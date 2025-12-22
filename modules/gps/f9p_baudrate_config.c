#include "f9p_baudrate_config.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_hal.h"

#ifndef TAG
    #define TAG "F9P_BAUD"
#endif

#include "log.h"


#define UBX_SYNC1           0xB5
#define UBX_SYNC2           0x62
#define UBX_CLASS_CFG       0x06
#define UBX_CFG_PRT         0x00
#define UBX_CLASS_ACK       0x05
#define UBX_ACK_ACK         0x01
#define UBX_ACK_NAK         0x00

 
#define UBX_PORT_UART1      1


static void ubx_send_ll(USART_TypeDef *USARTx, uint8_t msg_class, uint8_t msg_id,
                        const uint8_t *payload, uint16_t payload_len)
{
    uint8_t header[6];
    uint8_t ck_a = 0, ck_b = 0;

    header[0] = UBX_SYNC1;
    header[1] = UBX_SYNC2;
    header[2] = msg_class;
    header[3] = msg_id;
    header[4] = payload_len & 0xFF;
    header[5] = (payload_len >> 8) & 0xFF;

    ck_a += msg_class; ck_b += ck_a;
    ck_a += msg_id; ck_b += ck_a;
    ck_a += (payload_len & 0xFF); ck_b += ck_a;
    ck_a += ((payload_len >> 8) & 0xFF); ck_b += ck_a;

    for (uint16_t i = 0; i < payload_len; i++) {
        ck_a += payload[i];
        ck_b += ck_a;
    }

    for (int i = 0; i < 6; i++) {
        while (!LL_USART_IsActiveFlag_TXE(USARTx));
        LL_USART_TransmitData8(USARTx, header[i]);
    }

    if (payload_len > 0 && payload) {

        for (uint16_t i = 0; i < payload_len; i++) {

            while (!LL_USART_IsActiveFlag_TXE(USARTx));

            LL_USART_TransmitData8(USARTx, payload[i]);

        }

    }

 

    while (!LL_USART_IsActiveFlag_TXE(USARTx));

    LL_USART_TransmitData8(USARTx, ck_a);

 

    while (!LL_USART_IsActiveFlag_TXE(USARTx));

    LL_USART_TransmitData8(USARTx, ck_b);

 

    while (!LL_USART_IsActiveFlag_TC(USARTx));

}

 

/**

 * ACK/NAK 응답 대기 (LL 라이브러리)

 * @return 1: ACK, 0: NAK, -1: Timeout

 */

static int wait_for_ack_ll(USART_TypeDef *USARTx, uint8_t expected_class,

                           uint8_t expected_id, uint32_t timeout_ms)

{

    uint32_t start = HAL_GetTick();

    uint8_t byte;

    uint8_t state = 0;

    uint8_t msg_class, msg_id;

    uint16_t payload_len = 0;

    uint8_t payload[4];

    uint16_t payload_idx = 0;

    uint32_t byte_timeout = 10;

 

    while ((HAL_GetTick() - start) < timeout_ms) {

        uint32_t byte_start = HAL_GetTick();

 

        // 데이터 수신 대기

        while (!LL_USART_IsActiveFlag_RXNE(USARTx)) {

            if ((HAL_GetTick() - byte_start) > byte_timeout) {

                break;  // 바이트 타임아웃

            }

        }

 

        if (!LL_USART_IsActiveFlag_RXNE(USARTx)) {

            continue;

        }

 

        byte = LL_USART_ReceiveData8(USARTx);

 

        switch (state) {

            case 0: if (byte == UBX_SYNC1) state = 1; break;

            case 1: state = (byte == UBX_SYNC2) ? 2 : 0; break;

            case 2: msg_class = byte; state = 3; break;

            case 3: msg_id = byte; state = 4; break;

            case 4: payload_len = byte; state = 5; break;

            case 5:

                payload_len |= (byte << 8);

                payload_idx = 0;

                state = (payload_len > 0) ? 6 : 7;

                break;

            case 6:

                if (payload_idx < sizeof(payload)) {

                    payload[payload_idx] = byte;

                }

                payload_idx++;

                if (payload_idx >= payload_len) state = 7;

                break;

            case 7: state = 8; break;  // CK_A

            case 8:  // CK_B

                if (msg_class == UBX_CLASS_ACK && payload_len >= 2) {

                    if (payload[0] == expected_class && payload[1] == expected_id) {

                        return (msg_id == UBX_ACK_ACK) ? 1 : 0;

                    }

                }

                state = 0;

                break;

        }

    }

    return -1;  // Timeout

}

 

/**

 * UBX-CFG-PRT: F9P UART 보드레이트 변경

 */

static bool f9p_set_uart_baudrate(USART_TypeDef *USARTx, uint8_t f9p_port_id,

                                   uint32_t new_baudrate)

{

    uint8_t cfg_prt[20] = {0};

 

    cfg_prt[0] = f9p_port_id;  // portID (1: UART1, 2: UART2)

    cfg_prt[1] = 0x00;         // reserved1

    cfg_prt[2] = 0x00;         // txReady

    cfg_prt[3] = 0x00;

 

    // mode: 8N1

    uint32_t mode = 0x000008D0;

    cfg_prt[4] = (mode >> 0) & 0xFF;

    cfg_prt[5] = (mode >> 8) & 0xFF;

    cfg_prt[6] = (mode >> 16) & 0xFF;

    cfg_prt[7] = (mode >> 24) & 0xFF;

 

    // baudRate

    cfg_prt[8]  = (new_baudrate >> 0) & 0xFF;

    cfg_prt[9]  = (new_baudrate >> 8) & 0xFF;

    cfg_prt[10] = (new_baudrate >> 16) & 0xFF;

    cfg_prt[11] = (new_baudrate >> 24) & 0xFF;

 

    // inProtoMask: UBX + NMEA + RTCM3

    cfg_prt[12] = 0x07;

    cfg_prt[13] = 0x00;

 

    // outProtoMask: UBX + NMEA + RTCM3

    cfg_prt[14] = 0x07;

    cfg_prt[15] = 0x00;

 

    ubx_send_ll(USARTx, UBX_CLASS_CFG, UBX_CFG_PRT, cfg_prt, sizeof(cfg_prt));

 

    // ACK는 새 보드레이트로 올 수 있어서 timeout 나도 정상일 수 있음

    int result = wait_for_ack_ll(USARTx, UBX_CLASS_CFG, UBX_CFG_PRT, 500);

    return (result == 1 || result == -1);  // ACK 또는 Timeout 모두 OK

}

 

/**

 * F9P UART 현재 설정 읽기 (Poll)

 */

static bool f9p_poll_uart_config(USART_TypeDef *USARTx, uint8_t f9p_port_id,

                                  uint32_t *baudrate)

{

    uint8_t poll_payload[1] = { f9p_port_id };

    ubx_send_ll(USARTx, UBX_CLASS_CFG, UBX_CFG_PRT, poll_payload, 1);

 

    uint32_t start = HAL_GetTick();

    uint8_t byte;

    uint8_t state = 0;

    uint8_t msg_class, msg_id;

    uint16_t payload_len = 0;

    uint8_t payload[24];

    uint16_t payload_idx = 0;

    uint32_t byte_timeout = 10;

 

    while ((HAL_GetTick() - start) < 1000) {

        uint32_t byte_start = HAL_GetTick();

 

        // 데이터 수신 대기

        while (!LL_USART_IsActiveFlag_RXNE(USARTx)) {

            if ((HAL_GetTick() - byte_start) > byte_timeout) {

                break;

            }

        }

 

        if (!LL_USART_IsActiveFlag_RXNE(USARTx)) {

            continue;

        }

 

        byte = LL_USART_ReceiveData8(USARTx);

 

        switch (state) {

            case 0: if (byte == UBX_SYNC1) state = 1; break;

            case 1: state = (byte == UBX_SYNC2) ? 2 : 0; break;

            case 2: msg_class = byte; state = 3; break;

            case 3: msg_id = byte; state = 4; break;

            case 4: payload_len = byte; state = 5; break;

            case 5:

                payload_len |= (byte << 8);

                payload_idx = 0;

                state = 6;

                break;

            case 6:

                if (payload_idx < sizeof(payload)) {

                    payload[payload_idx] = byte;

                }

                payload_idx++;

                if (payload_idx >= payload_len) {

                    if (msg_class == UBX_CLASS_CFG && msg_id == UBX_CFG_PRT &&

                        payload_len >= 20) {

                        if (baudrate) {

                            *baudrate = payload[8] | (payload[9] << 8) |

                                       (payload[10] << 16) | (payload[11] << 24);

                        }

                        return true;

                    }

                    state = 0;

                }

                break;

        }

    }

    return false;

}

 

/**

 * STM32 UART 보드레이트 런타임 변경 (LL 라이브러리)

 */

static void change_stm32_baudrate_ll(USART_TypeDef *USARTx, uint32_t new_baudrate)

{

    // UART 비활성화

    LL_USART_Disable(USARTx);

 

    // 보드레이트 변경

    LL_USART_SetBaudRate(USARTx, HAL_RCC_GetPCLK1Freq(),

                         LL_USART_OVERSAMPLING_16, new_baudrate);

 

    // UART 재활성화

    LL_USART_Enable(USARTx);

}

 

/**

 * Base F9P UART1 보드레이트 초기화 (DMA 활성화 전)

 * gps_rtk_uart2_init()에서 호출됨

 */

bool f9p_init_uart1_baudrate_115200(void)

{

    uint32_t current_baud = 0;

 

    LOG_INFO("=== F9P UART1 Init Baudrate to 115200 ===");

 

    // Step 1: 현재 설정 확인 (38400)

    LOG_INFO("[1] Current baudrate check...");

    if (f9p_poll_uart_config(USART2, UBX_PORT_UART1, &current_baud)) {

        LOG_INFO("    Current: %lu bps", current_baud);

        if (current_baud == 115200) {

            LOG_INFO("    Already 115200, skipping...");

            return true;

        }

    } else {

        LOG_WARN("    No response at 38400");

    }

 

    // Step 2: F9P UART1을 115200으로 변경 요청

    LOG_INFO("[2] Setting F9P UART1 to 115200...");

    f9p_set_uart_baudrate(USART2, UBX_PORT_UART1, 115200);

    HAL_Delay(100);

 

    // Step 3: STM32 UART2도 115200으로 변경

    LOG_INFO("[3] Switching STM32 UART2 to 115200...");

    change_stm32_baudrate_ll(USART2, 115200);

    HAL_Delay(200);

 

    // Step 4: 검증

    LOG_INFO("[4] Verifying...");

    if (f9p_poll_uart_config(USART2, UBX_PORT_UART1, &current_baud)) {

        if (current_baud == 115200) {

            LOG_INFO("    SUCCESS! Baudrate: %lu bps", current_baud);

            return true;

        } else {

            LOG_ERR("    Wrong baudrate: %lu bps", current_baud);

        }

    } else {

        LOG_ERR("    Verification failed, reverting...");

        change_stm32_baudrate_ll(USART2, 38400);

        HAL_Delay(100);

    }

 

    return false;

}

 

/**

 * Rover F9P UART1 보드레이트 초기화 (DMA 활성화 전)

 * gps_rtk_uart4_init()에서 호출됨

 */

bool f9p_init_rover_uart1_baudrate_115200(void)

{

    uint32_t current_baud = 0;

 

    LOG_INFO("=== Rover F9P UART1 Init Baudrate to 115200 ===");

 

    // Step 1: 현재 설정 확인 (38400)

    LOG_INFO("[1] Current baudrate check...");

    if (f9p_poll_uart_config(UART4, UBX_PORT_UART1, &current_baud)) {

        LOG_INFO("    Current: %lu bps", current_baud);

        if (current_baud == 115200) {

            LOG_INFO("    Already 115200, skipping...");

            return true;

        }

    } else {

        LOG_WARN("    No response at 38400");

    }

 

    // Step 2: Rover F9P UART1을 115200으로 변경 요청

    LOG_INFO("[2] Setting Rover F9P UART1 to 115200...");

    f9p_set_uart_baudrate(UART4, UBX_PORT_UART1, 115200);

    HAL_Delay(100);

 

    // Step 3: STM32 UART4도 115200으로 변경

    LOG_INFO("[3] Switching STM32 UART4 to 115200...");

    change_stm32_baudrate_ll(UART4, 115200);

    HAL_Delay(200);

 

    // Step 4: 검증

    LOG_INFO("[4] Verifying...");

    if (f9p_poll_uart_config(UART4, UBX_PORT_UART1, &current_baud)) {

        if (current_baud == 115200) {

            LOG_INFO("    SUCCESS! Baudrate: %lu bps", current_baud);

            return true;

        } else {

            LOG_ERR("    Wrong baudrate: %lu bps", current_baud);

        }

    } else {

        LOG_ERR("    Verification failed, reverting...");

        change_stm32_baudrate_ll(UART4, 38400);

        HAL_Delay(100);

    }

 

    return false;

}