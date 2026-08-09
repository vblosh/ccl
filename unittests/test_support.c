#include "test_support.h"

#include <stdlib.h>

void ccl_test_failure(const char *file, int line, const char *expression,
                      const char *description)
{
    if (description != NULL) {
        fprintf(stderr, "%s:%d: assertion failed: %s (%s)\n",
                file, line, expression, description);
    } else {
        fprintf(stderr, "%s:%d: assertion failed: %s\n",
                file, line, expression);
    }
}

FILE *ccl_test_tmpfile(void)
{
    return tmpfile();
}

int ccl_run_suite(const TestSuite *suite)
{
    size_t passed = 0;
    size_t failed = 0;
    size_t i;

    if (suite == NULL || suite->tests == NULL) {
        fprintf(stderr, "unit test suite is not initialized\n");
        return 1;
    }

    printf("\n========== Running %s ==========\n\n", suite->name);
    for (i = 0; i < suite->num_tests; ++i) {
        const TestCase *test = &suite->tests[i];
        int result;

        printf("[%lu/%lu] %s: ",
               (unsigned long)(i + 1),
               (unsigned long)suite->num_tests,
               test->name);
        fflush(stdout);
        result = test->test_fn();
        if (result == 0) {
            ++passed;
            printf("PASS\n");
        } else {
            ++failed;
            printf("FAIL\n");
        }
    }

    printf("\n========== %s Summary ==========\n"
           "Total: %lu, Passed: %lu, Failed: %lu\n\n",
           suite->name,
           (unsigned long)suite->num_tests,
           (unsigned long)passed,
           (unsigned long)failed);
    return failed == 0 ? 0 : 1;
}
