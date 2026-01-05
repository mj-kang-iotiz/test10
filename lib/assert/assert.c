/**
 * @file    assert.c
 * @brief   Application assert implementation
 */

#include "assert.h"
#include <stdio.h>

#ifdef STM32F4
#include "stm32f4xx.h"
#endif

static AssertAction s_action = ASSERT_ACTION_HALT;

void assert_set_action(AssertAction action)
{
    s_action = action;
}

void assert_failed(const char *file, int line, const char *expr, const char *msg)
{
    __disable_irq();

    printf("\r\n[ASSERT] %s:%d\r\n", file, line);
    if (expr) printf("  expr: %s\r\n", expr);
    if (msg)  printf("  msg:  %s\r\n", msg);

    if (s_action == ASSERT_ACTION_RESET) {
        NVIC_SystemReset();
    }

    while (1) {
        __NOP();
    }
}
