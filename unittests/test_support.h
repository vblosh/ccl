#ifndef CCL_TEST_SUPPORT_H
#define CCL_TEST_SUPPORT_H

#include <stddef.h>
#include <stdio.h>

typedef struct {
    const char *name;
    int (*test_fn)(void);
} TestCase;

typedef struct {
    const char *name;
    const TestCase *tests;
    size_t num_tests;
} TestSuite;

/* Every unit-test executable contains one suite and exports this getter. */
const TestSuite *ccl_get_test_suite(void);

/* Shared diagnostics and cleanup-friendly assertions for new suites. */
void ccl_test_failure(const char *file, int line, const char *expression,
                      const char *description);
FILE *ccl_test_tmpfile(void);
int ccl_run_suite(const TestSuite *suite);

#define TEST_REQUIRE(condition)                                             \
    do {                                                                     \
        if (!(condition)) {                                                  \
            ccl_test_failure(__FILE__, __LINE__, #condition, NULL);          \
            goto cleanup;                                                    \
        }                                                                    \
    } while (0)

#define TEST_REQUIRE_MSG(condition, description)                             \
    do {                                                                     \
        if (!(condition)) {                                                  \
            ccl_test_failure(__FILE__, __LINE__, #condition, (description)); \
            goto cleanup;                                                    \
        }                                                                    \
    } while (0)

#endif /* CCL_TEST_SUPPORT_H */
