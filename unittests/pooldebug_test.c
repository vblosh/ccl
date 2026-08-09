#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "test_support.h"

typedef struct {
    int count;
    int code;
    const char *operation;
} ErrorCapture;

static ErrorCapture captured;

static void *capture_error(const char *operation, int code, ...)
{
    captured.count++;
    captured.code = code;
    captured.operation = operation;
    return NULL;
}

static void clear_capture(void)
{
    captured.count = 0;
    captured.code = 0;
    captured.operation = NULL;
}

static void *fail_malloc(size_t size)
{
    (void)size;
    return NULL;
}

static int test_empty_and_allocated_lifecycles_are_clean(void)
{
    Pool *pool = NULL;
    unsigned char *memory = NULL;
    ErrorFunction old_error;

    old_error = iError.SetErrorFunction(capture_error);
    clear_capture();

    pool = iPoolDebug.Create("pooldebug_test:empty");
    TEST_REQUIRE(pool != NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 0);
    iPoolDebug.Finalize(pool, "pooldebug_test:empty-finalize");
    pool = NULL;

    pool = iPoolDebug.Create("pooldebug_test:allocated");
    TEST_REQUIRE(pool != NULL);
    memory = (unsigned char *)iPoolDebug.Alloc(pool, 32,
                                                "pooldebug_test:alloc");
    TEST_REQUIRE(memory != NULL);
    memory[0] = 0x5a;
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 32);
    iPoolDebug.Finalize(pool, "pooldebug_test:allocated-finalize");
    pool = NULL;
    memory = NULL;
    TEST_REQUIRE(captured.count == 0);
    iError.SetErrorFunction(old_error);
    return 0;

cleanup:
    if (pool != NULL)
        iPoolDebug.Finalize(pool, "pooldebug_test:cleanup");
    iError.SetErrorFunction(old_error);
    return -1;
}

static int test_tracking_nodes_clear_and_reuse(void)
{
    enum { allocation_count = 129 };
    Pool *pool = NULL;
    unsigned char *allocations[allocation_count];
    size_t expected = 0;
    size_t i;

    memset(allocations, 0, sizeof(allocations));
    pool = iPoolDebug.Create("pooldebug_test:tracking");
    TEST_REQUIRE(pool != NULL);

    for (i = 0; i < allocation_count; ++i) {
        size_t size = (i % 4) + 1;
        allocations[i] = (unsigned char *)iPoolDebug.Alloc(
            pool, size, "pooldebug_test:tracking-alloc");
        TEST_REQUIRE(allocations[i] != NULL);
        memset(allocations[i], (int)i, size);
        expected += size;
    }
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == expected);

    /* Exercise a second node explicitly with the documented boundary sizes. */
    for (i = 0; i < 4; ++i) {
        size_t size = (size_t[]){1, 64, 65, 129}[i];
        allocations[i] = (unsigned char *)iPoolDebug.Alloc(
            pool, size, "pooldebug_test:boundary-alloc");
        TEST_REQUIRE(allocations[i] != NULL);
        expected += size;
    }
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == expected);

    iPoolDebug.Clear(pool, "pooldebug_test:clear");
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 0);

    allocations[0] = (unsigned char *)iPoolDebug.Alloc(
        pool, 23, "pooldebug_test:reuse");
    TEST_REQUIRE(allocations[0] != NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 23);
    iPoolDebug.Finalize(pool, "pooldebug_test:tracking-finalize");
    return 0;

cleanup:
    if (pool != NULL)
        iPoolDebug.Finalize(pool, "pooldebug_test:tracking-cleanup");
    return -1;
}

static int test_user_allocator_failure_keeps_pool_usable(void)
{
    Pool *pool = NULL;
    void *memory = NULL;
    void *(*old_malloc)(size_t);

    old_malloc = iDebugMalloc.malloc;
    pool = iPoolDebug.Create("pooldebug_test:alloc-failure");
    TEST_REQUIRE(pool != NULL);
    iDebugMalloc.malloc = fail_malloc;
    TEST_REQUIRE(iPoolDebug.Alloc(pool, 32,
                                  "pooldebug_test:alloc-failure") == NULL);
    iDebugMalloc.malloc = old_malloc;
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 0);

    memory = iPoolDebug.Alloc(pool, 32, "pooldebug_test:alloc-recovery");
    TEST_REQUIRE(memory != NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 32);
    iPoolDebug.Finalize(pool, "pooldebug_test:alloc-failure-finalize");
    return 0;

cleanup:
    iDebugMalloc.malloc = old_malloc;
    if (pool != NULL)
        iPoolDebug.Finalize(pool, "pooldebug_test:alloc-failure-cleanup");
    return -1;
}

static int test_calloc_zeroes_product_and_rejects_overflow(void)
{
    Pool *pool = NULL;
    unsigned char *memory = NULL;
    size_t i;
    const size_t element_count = 8;
    const size_t element_size = 17;
    const size_t total_size = element_count * element_size;

    pool = iPoolDebug.Create("pooldebug_test:calloc");
    TEST_REQUIRE(pool != NULL);
    memory = (unsigned char *)iPoolDebug.Calloc(
        pool, element_count, element_size, "pooldebug_test:calloc-normal");
    TEST_REQUIRE(memory != NULL);
    for (i = 0; i < total_size; ++i)
        TEST_REQUIRE(memory[i] == 0);
    memset(memory, 0xa5, total_size);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == total_size);

    TEST_REQUIRE(iPoolDebug.Calloc(pool, SIZE_MAX / 2 + 1, 2,
                                   "pooldebug_test:calloc-overflow") == NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == total_size);

    memory = (unsigned char *)iPoolDebug.Calloc(
        pool, 0, SIZE_MAX, "pooldebug_test:calloc-zero-count");
    TEST_REQUIRE(memory != NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == total_size);

    iPoolDebug.Clear(pool, "pooldebug_test:calloc-clear");
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 0);
    iPoolDebug.Finalize(pool, "pooldebug_test:calloc-finalize");
    return 0;

cleanup:
    if (pool != NULL)
        iPoolDebug.Finalize(pool, "pooldebug_test:calloc-cleanup");
    return -1;
}

static int test_find_pool_from_data_checks_half_open_ranges(void)
{
    Pool *pool = NULL;
    unsigned char *memory = NULL;
    unsigned char foreign = 0;
    void *candidate;
    void *original;

    pool = iPoolDebug.Create("pooldebug_test:find");
    TEST_REQUIRE(pool != NULL);
    memory = (unsigned char *)iPoolDebug.Alloc(
        pool, 16, "pooldebug_test:find-alloc");
    TEST_REQUIRE(memory != NULL);

    candidate = memory;
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(pool, &candidate) == 1);
    TEST_REQUIRE(candidate == pool);

    candidate = memory + 7;
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(pool, &candidate) == 1);
    TEST_REQUIRE(candidate == pool);

    candidate = memory + 15;
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(pool, &candidate) == 1);
    TEST_REQUIRE(candidate == pool);

    candidate = memory + 16;
    original = candidate;
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(pool, &candidate) == 0);
    TEST_REQUIRE(candidate == original);

    candidate = &foreign;
    original = candidate;
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(pool, &candidate) == 0);
    TEST_REQUIRE(candidate == original);

    candidate = NULL;
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(pool, &candidate) == 0);
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(pool, NULL) == 0);
    TEST_REQUIRE(iPoolDebug.FindPoolFromData(NULL, &candidate) == 0);

    iPoolDebug.Finalize(pool, "pooldebug_test:find-finalize");
    return 0;

cleanup:
    if (pool != NULL)
        iPoolDebug.Finalize(pool, "pooldebug_test:find-cleanup");
    return -1;
}

static int test_sizeof_and_max_free_states(void)
{
    Pool *pool = NULL;
    void *first = NULL;
    void *second = NULL;

    TEST_REQUIRE(iPoolDebug.Sizeof(NULL) > 0);

    pool = iPoolDebug.Create("pooldebug_test:sizeof");
    TEST_REQUIRE(pool != NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 0);
    iPoolDebug.SetMaxFree(pool, 0);
    iPoolDebug.SetMaxFree(pool, 8192);

    first = iPoolDebug.Alloc(pool, 11, "pooldebug_test:sizeof-first");
    second = iPoolDebug.Calloc(pool, 3, 7, "pooldebug_test:sizeof-second");
    TEST_REQUIRE(first != NULL);
    TEST_REQUIRE(second != NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 32);

    iPoolDebug.Clear(pool, "pooldebug_test:sizeof-clear");
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 0);
    second = iPoolDebug.Alloc(pool, 5, "pooldebug_test:sizeof-reuse");
    TEST_REQUIRE(second != NULL);
    TEST_REQUIRE(iPoolDebug.Sizeof(pool) == 5);
    iPoolDebug.Finalize(pool, "pooldebug_test:sizeof-finalize");
    return 0;

cleanup:
    if (pool != NULL)
        iPoolDebug.Finalize(pool, "pooldebug_test:sizeof-cleanup");
    return -1;
}

static const TestCase tests[] = {
    {"empty and allocated lifecycles are clean",
     test_empty_and_allocated_lifecycles_are_clean},
    {"tracking nodes clear and reuse",
     test_tracking_nodes_clear_and_reuse},
    {"user allocator failure keeps pool usable",
     test_user_allocator_failure_keeps_pool_usable},
    {"calloc zeroes product and rejects overflow",
     test_calloc_zeroes_product_and_rejects_overflow},
    {"find pool checks half-open ranges",
     test_find_pool_from_data_checks_half_open_ranges},
    {"sizeof and max-free states",
     test_sizeof_and_max_free_states},
};

static const TestSuite suite = {
    "pooldebug",
    tests,
    sizeof(tests) / sizeof(tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
