/**
 * @file    fw_assert.h
 * @brief   Custom assert for embedded firmware
 *
 * Usage:
 *   #define FW_ASSERT_ENABLED 1  // Enable assert (debug build)
 *   #include "fw_assert.h"
 *
 *   FW_ASSERT(ptr != NULL);
 *   FW_ASSERT_MSG(count > 0, "Count must be positive");
 */

#ifndef FW_ASSERT_H
#define FW_ASSERT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Assert action configuration */
typedef enum {
    FW_ASSERT_ACTION_HALT,      /* Infinite loop (default) */
    FW_ASSERT_ACTION_RESET,     /* System reset */
    FW_ASSERT_ACTION_CONTINUE   /* Log and continue (dangerous) */
} FwAssertAction;

/**
 * @brief  Assert failure handler
 * @param  file: Source file name
 * @param  line: Line number
 * @param  expr: Failed expression string (can be NULL)
 * @param  msg: Optional message (can be NULL)
 */
void fw_assert_failed(const char *file, uint32_t line,
                      const char *expr, const char *msg);

/**
 * @brief  Set assert action behavior
 * @param  action: What to do on assert failure
 */
void fw_assert_set_action(FwAssertAction action);

/**
 * @brief  Register custom assert handler callback
 * @param  handler: Function to call on assert (before action)
 *                  Parameters: file, line, expr, msg
 */
typedef void (*FwAssertHandler)(const char *file, uint32_t line,
                                 const char *expr, const char *msg);
void fw_assert_set_handler(FwAssertHandler handler);

/*
 * Assert macros
 */
#if defined(FW_ASSERT_ENABLED) && FW_ASSERT_ENABLED

    /* Basic assert */
    #define FW_ASSERT(expr)                                         \
        do {                                                        \
            if (!(expr)) {                                          \
                fw_assert_failed(__FILE__, __LINE__, #expr, NULL);  \
            }                                                       \
        } while (0)

    /* Assert with message */
    #define FW_ASSERT_MSG(expr, msg)                                \
        do {                                                        \
            if (!(expr)) {                                          \
                fw_assert_failed(__FILE__, __LINE__, #expr, msg);   \
            }                                                       \
        } while (0)

    /* Assert that always triggers (for unreachable code) */
    #define FW_ASSERT_FAIL(msg)                                     \
        fw_assert_failed(__FILE__, __LINE__, NULL, msg)

#else
    /* Release build - remove all asserts */
    #define FW_ASSERT(expr)           ((void)0)
    #define FW_ASSERT_MSG(expr, msg)  ((void)0)
    #define FW_ASSERT_FAIL(msg)       ((void)0)

#endif /* FW_ASSERT_ENABLED */

/*
 * Compile-time assert (always available)
 */
#define FW_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)

#ifdef __cplusplus
}
#endif

#endif /* FW_ASSERT_H */
