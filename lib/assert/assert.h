/**
 * @file    assert.h
 * @brief   Development assert macros (disabled in release builds)
 */

#ifndef DEV_ASSERT_H
#define DEV_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

void dev_assert_failed(const char *file, int line, const char *expr, const char *msg);

#if defined(DEV_ASSERT_ENABLED) && DEV_ASSERT_ENABLED

#define DEV_ASSERT(expr) \
    do { if (!(expr)) dev_assert_failed(__FILE__, __LINE__, #expr, NULL); } while(0)

#define DEV_ASSERT_MSG(expr, msg) \
    do { if (!(expr)) dev_assert_failed(__FILE__, __LINE__, #expr, msg); } while(0)

#define DEV_ASSERT_FAIL(msg) \
    dev_assert_failed(__FILE__, __LINE__, NULL, msg)

#else

#define DEV_ASSERT(expr)          ((void)0)
#define DEV_ASSERT_MSG(expr, msg) ((void)0)
#define DEV_ASSERT_FAIL(msg)      ((void)0)

#endif

#define STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)

#ifdef __cplusplus
}
#endif

#endif /* DEV_ASSERT_H */
