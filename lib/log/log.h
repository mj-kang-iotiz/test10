#ifndef LOG_H
#define LOG_H

#include "stm32f4xx_hal.h"
#include <stdio.h>

#ifndef TAG
#define TAG "NONE"
#endif

// ANSI 색상 코드
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RESET "\033[0m"

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARNING 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
	#define LOG_LEVEL LOG_LEVEL_NONE
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...)                                                    \
  do {                                                                         \
    printf(COLOR_GREEN "[%u][D][%s]" fmt COLOR_RESET "\r\n",                   \
           (unsigned)HAL_GetTick(), TAG, ##__VA_ARGS__);                       \
  } while (0)

#define LOG_DEBUG_HEX(prefix, data, len)                                       \
  do {                                                                         \
    printf(COLOR_GREEN "[%u][D][%s]%s[%u]:", (unsigned)HAL_GetTick(), TAG,     \
           prefix, (unsigned)len);                                             \
    for (size_t i = 0; i < (len); i++) {                                       \
      printf(" %02X", ((uint8_t *)(data))[i]);                                 \
    }                                                                          \
    printf(COLOR_RESET "\r\n");                                                \
  } while (0)

#define LOG_DEBUG_RAW(prefix, data, len)                                       \
  do {                                                                         \
    printf(COLOR_GREEN "[%u][D][%s]%s[%u]", (unsigned)HAL_GetTick(), TAG,      \
           prefix, (unsigned)len);                                             \
    for (size_t i = 0; i < (len); i++) {                                       \
      uint8_t c = ((uint8_t *)(data))[i];                                      \
      if (c == '\r') {                                                         \
        printf("\r");                                                          \
        continue;                                                              \
      } else if (c == '\n') {                                                  \
        printf("\n");                                                          \
        continue;                                                              \
      } else if (c >= 0x20 && c < 0x7F)                                        \
        printf("%c", c);                                                       \
      else                                                                     \
        printf("<%02X>", c);                                                   \
    }                                                                          \
    printf(COLOR_RESET "\r\n");                                                \
  } while (0)

#else
#define LOG_DEBUG(fmt, ...)
#define LOG_DEBUG_HEX(prefix, data, len)
#define LOG_DEBUG_RAW(prefix, data, len)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...)                                                     \
  do {                                                                         \
    printf("[%u][I][%s]" fmt "\r\n", (unsigned)HAL_GetTick(), TAG,             \
           ##__VA_ARGS__);                                                     \
  } while (0)

#else
#define LOG_INFO(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARNING
#define LOG_WARN(fmt, ...)                                                     \
  do {                                                                         \
    printf(COLOR_YELLOW "[%u][W][%s]" fmt COLOR_RESET "\r\n",                  \
           (unsigned)HAL_GetTick(), TAG, ##__VA_ARGS__);                       \
  } while (0)

#else
#define LOG_WARN(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERR(fmt, ...)                                                      \
  do {                                                                         \
    printf(COLOR_RED "[%u][E][%s]" fmt COLOR_RESET "\r\n",                     \
           (unsigned)HAL_GetTick(), TAG, ##__VA_ARGS__);                       \
  } while (0)

#else
#define LOG_ERR(fmt, ...)
#endif

#endif
