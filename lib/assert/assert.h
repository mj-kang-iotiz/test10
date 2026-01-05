/**
 * @file    assert.h
 * @brief   Application assert macros
 *
 * Usage:
 *   #define ASSERT_ENABLED 1
 *   #include "assert.h"
 *
 *   ASSERT(ptr != NULL);
 *   ASSERT_MSG(len > 0, "Invalid length");
 */

#ifndef APP_ASSERT_H
#define APP_ASSERT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Configuration                                                             */
/*---------------------------------------------------------------------------*/

typedef enum {
    ASSERT_ACTION_HALT,     /* Infinite loop (default) */
    ASSERT_ACTION_RESET,    /* System reset */
} AssertAction;

void assert_set_action(AssertAction action);

/*---------------------------------------------------------------------------*/
/* Internal handler (do not call directly)                                   */
/*---------------------------------------------------------------------------*/

void assert_failed(const char *file, int line, const char *expr, const char *msg);

/*---------------------------------------------------------------------------*/
/* Assert macros                                                             */
/*---------------------------------------------------------------------------*/

#if defined(ASSERT_ENABLED) && ASSERT_ENABLED

#define ASSERT(expr) \
    do { if (!(expr)) assert_failed(__FILE__, __LINE__, #expr, NULL); } while(0)

#define ASSERT_MSG(expr, msg) \
    do { if (!(expr)) assert_failed(__FILE__, __LINE__, #expr, msg); } while(0)

#define ASSERT_FAIL(msg) \
    assert_failed(__FILE__, __LINE__, NULL, msg)

#else

#define ASSERT(expr)          ((void)0)
#define ASSERT_MSG(expr, msg) ((void)0)
#define ASSERT_FAIL(msg)      ((void)0)

#endif

/* Compile-time assert (always active) */
#define STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)

#ifdef __cplusplus
}
#endif

#endif /* APP_ASSERT_H */
