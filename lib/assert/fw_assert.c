/**
 * @file    fw_assert.c
 * @brief   Custom assert implementation for embedded firmware
 */

#include "fw_assert.h"
#include <stdio.h>

/* Optional: Include for NVIC_SystemReset */
#if defined(STM32F4xx) || defined(STM32F405xx)
#include "stm32f4xx.h"
#endif

/* Static configuration */
static FwAssertAction s_assert_action = FW_ASSERT_ACTION_HALT;
static FwAssertHandler s_custom_handler = NULL;

void fw_assert_set_action(FwAssertAction action)
{
    s_assert_action = action;
}

void fw_assert_set_handler(FwAssertHandler handler)
{
    s_custom_handler = handler;
}

void fw_assert_failed(const char *file, uint32_t line,
                      const char *expr, const char *msg)
{
    /* Disable interrupts to prevent further issues */
    __disable_irq();

    /* Print assert information */
    printf("\r\n");
    printf("========== ASSERT FAILED ==========\r\n");
    printf("File: %s\r\n", file ? file : "unknown");
    printf("Line: %lu\r\n", (unsigned long)line);
    if (expr) {
        printf("Expr: %s\r\n", expr);
    }
    if (msg) {
        printf("Msg:  %s\r\n", msg);
    }
    printf("====================================\r\n");

    /* Call custom handler if registered */
    if (s_custom_handler) {
        s_custom_handler(file, line, expr, msg);
    }

    /* Perform configured action */
    switch (s_assert_action) {
        case FW_ASSERT_ACTION_RESET:
            #if defined(NVIC_SystemReset)
            NVIC_SystemReset();
            #else
            /* Fallback: trigger watchdog or infinite loop */
            while (1) { }
            #endif
            break;

        case FW_ASSERT_ACTION_CONTINUE:
            __enable_irq();
            return;

        case FW_ASSERT_ACTION_HALT:
        default:
            /* Infinite loop with LED blink pattern (if available) */
            while (1) {
                /* Debugger can inspect: file, line, expr, msg */
                __NOP();
            }
            break;
    }
}
