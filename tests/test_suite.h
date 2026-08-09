#ifndef TEST_SUITE_H
#define TEST_SUITE_H

#include <stdio.h>

typedef struct {
    const char *name;
    int (*test_fn)(void);
} TestCase;

typedef struct {
    const char *name;
    TestCase *tests;
    int num_tests;
} TestSuite;

/* Declare test suites */
extern TestSuite ValArrayInt_Tests;

#endif /* TEST_SUITE_H */
