/**
 * @file    assert.c
 * @brief   Development assert implementation
 */

#include "assert.h"
#include "cmsis_compiler.h"  /* __disable_irq, __NOP (ARM Cortex-M) */
#include <stdio.h>

void dev_assert_failed(const char *file, int line, const char *expr, const char *msg)
{
    __disable_irq();

    printf("\r\n[ASSERT] %s:%d\r\n", file, line);
    if (expr) printf("  expr: %s\r\n", expr);
    if (msg)  printf("  msg:  %s\r\n", msg);

    while (1) {
        __NOP();
    }
}
