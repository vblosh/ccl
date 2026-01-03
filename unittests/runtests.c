#include <stdio.h>
#include "test_suite.h"

/* Main test runner - runs all test suites */
int main(void)
{
    int total_tests = 0;
    int total_passed = 0;
    int total_failed = 0;

    /* Array of all test suites */
    TestSuite *test_suites[] = {
        &ValArrayInt_Tests,
    };
    
    int num_suites = sizeof(test_suites) / sizeof(test_suites[0]);
    int suite_idx;

    printf("\n========== Running All Test Suites ==========\n\n");

    /* Run each test suite */
    for (suite_idx = 0; suite_idx < num_suites; suite_idx++) {
        TestSuite *suite = test_suites[suite_idx];
        int suite_passed = 0;
        int suite_failed = 0;
        int i;

        printf("========== Running %s ==========\n", suite->name);

        for (i = 0; i < suite->num_tests; i++) {
            printf("[%d/%d] %s: ", i + 1, suite->num_tests, suite->tests[i].name);
            if (suite->tests[i].test_fn() == 0) {
                suite_passed++;
                total_passed++;
            } else {
                suite_failed++;
                total_failed++;
            }
        }

        total_tests += suite->num_tests;
        printf("\n========== %s Summary ==========\n", suite->name);
        printf("Total: %d, Passed: %d, Failed: %d\n\n", 
               suite->num_tests, suite_passed, suite_failed);
    }

    printf("========== Overall Summary ==========\n");
    printf("Total Suites: %d\n", num_suites);
    printf("Total Tests: %d\n", total_tests);
    printf("Total Passed: %d\n", total_passed);
    printf("Total Failed: %d\n\n", total_failed);

    return total_failed == 0 ? 0 : 1;
}
