#include "containers.h"
#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The legacy pool predates the public pool interface.  Keep these
 * declarations private to this suite so that the test is linked against
 * smallpool.o rather than accidentally exercising iPool from pool.c. */
typedef struct MemoryNode_t MemoryNode_t;
typedef struct {
    uint32_t max_index;
    size_t max_free_index;
    size_t current_free_index;
    MemoryNode_t *free[20];
} SmallAllocator;

Pool *newPool(void);
void *PoolAlloc(Pool *pool, size_t size);
void *PoolCalloc(Pool *pool, size_t size);
void PoolClear(Pool *pool);
void PoolDestroy(Pool *pool);
void SetMaxFree(SmallAllocator *allocator, size_t size);

static int test_basic_lifecycle_and_alignment(void)
{
    static const size_t sizes[] = { 0, 1, 7, 8, 9 };
    unsigned char *blocks[sizeof(sizes) / sizeof(sizes[0])] = { NULL };
    Pool *pool = NULL;
    size_t i;
    int result = -1;

    pool = newPool();
    TEST_REQUIRE(pool != NULL);
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        blocks[i] = PoolAlloc(pool, sizes[i]);
        TEST_REQUIRE(blocks[i] != NULL);
        TEST_REQUIRE(((uintptr_t)blocks[i] & (uintptr_t)7) == 0);
        if (sizes[i] != 0)
            memset(blocks[i], (int)(i + 1), sizes[i]);
    }

    PoolClear(pool);
    blocks[0] = PoolAlloc(pool, 32);
    TEST_REQUIRE(blocks[0] != NULL);
    memset(blocks[0], 0xa5, 32);
    PoolDestroy(pool);
    pool = NULL;
    result = 0;

cleanup:
    if (pool != NULL)
        PoolDestroy(pool);
    return result;
}

static int test_calloc_zeroes_and_null_is_safe(void)
{
    unsigned char *memory = NULL;
    Pool *pool = NULL;
    size_t i;
    int result = -1;

    TEST_REQUIRE(PoolAlloc(NULL, 1) == NULL);
    TEST_REQUIRE(PoolCalloc(NULL, 1) == NULL);
    PoolClear(NULL);
    PoolDestroy(NULL);

    pool = newPool();
    TEST_REQUIRE(pool != NULL);
    memory = PoolAlloc(pool, 128);
    TEST_REQUIRE(memory != NULL);
    memset(memory, 0xcd, 128);
    memory = PoolCalloc(pool, 128);
    TEST_REQUIRE(memory != NULL);
    for (i = 0; i < 128; ++i)
        TEST_REQUIRE(memory[i] == 0);

    result = 0;

cleanup:
    if (pool != NULL)
        PoolDestroy(pool);
    return result;
}

static int test_indexed_nodes_and_sink_reuse_after_clear(void)
{
    void *indexed[6] = { NULL };
    void *reused_indexed[6] = { NULL };
    void *sink = NULL;
    void *reused_sink = NULL;
    Pool *pool = NULL;
    size_t i;
    int result = -1;

    pool = newPool();
    TEST_REQUIRE(pool != NULL);
    for (i = 0; i < sizeof(indexed) / sizeof(indexed[0]); ++i) {
        indexed[i] = PoolAlloc(pool, 8192);
        TEST_REQUIRE(indexed[i] != NULL);
    }
    sink = PoolAlloc(pool, 100000);
    TEST_REQUIRE(sink != NULL);
    PoolClear(pool);

    for (i = 0; i < sizeof(reused_indexed) / sizeof(reused_indexed[0]); ++i) {
        reused_indexed[i] = PoolAlloc(pool, 8192);
        TEST_REQUIRE(reused_indexed[i] != NULL);
    }
    reused_sink = PoolAlloc(pool, 100000);
    TEST_REQUIRE(reused_sink != NULL);

    /* Repeated clear cycles exercise both the active list reset and cached
     * nodes, including the oversized sink bucket. */
    for (i = 0; i < 3; ++i) {
        TEST_REQUIRE(PoolAlloc(pool, 9000) != NULL);
        TEST_REQUIRE(PoolAlloc(pool, 100000) != NULL);
        PoolClear(pool);
    }

    result = 0;

cleanup:
    if (pool != NULL)
        PoolDestroy(pool);
    return result;
}

static int test_near_size_max_does_not_mutate_pool(void)
{
    Pool *pool = NULL;
    void *memory = NULL;
    int result = -1;

    pool = newPool();
    TEST_REQUIRE(pool != NULL);
    TEST_REQUIRE(PoolAlloc(pool, SIZE_MAX) == NULL);
    TEST_REQUIRE(PoolAlloc(pool, SIZE_MAX - 7) == NULL);
    memory = PoolAlloc(pool, 16);
    TEST_REQUIRE(memory != NULL);
    memset(memory, 0x3c, 16);
    result = 0;

cleanup:
    if (pool != NULL)
        PoolDestroy(pool);
    return result;
}

static size_t threshold_units(size_t size)
{
    size_t units = size / 4096;

    if (size % 4096 != 0)
        ++units;
    return units;
}

static int test_set_max_free_rounds_without_narrowing(void)
{
    SmallAllocator allocator;
    size_t expected;
    int result = -1;

    memset(&allocator, 0, sizeof(allocator));
    SetMaxFree(&allocator, 0);
    TEST_REQUIRE(allocator.max_free_index == 0);
    TEST_REQUIRE(allocator.current_free_index == 0);

    SetMaxFree(&allocator, 1);
    TEST_REQUIRE(allocator.max_free_index == 1);
    TEST_REQUIRE(allocator.current_free_index == 1);
    SetMaxFree(&allocator, 4095);
    TEST_REQUIRE(allocator.max_free_index == 1);
    SetMaxFree(&allocator, 4096);
    TEST_REQUIRE(allocator.max_free_index == 1);
    SetMaxFree(&allocator, 4097);
    TEST_REQUIRE(allocator.max_free_index == 2);
    TEST_REQUIRE(allocator.current_free_index == 2);

    expected = threshold_units((size_t)UINT32_MAX);
    SetMaxFree(&allocator, (size_t)UINT32_MAX);
    TEST_REQUIRE(allocator.max_free_index == expected);

    if (SIZE_MAX > UINT32_MAX) {
        size_t above_uint32 = (size_t)UINT32_MAX + 1;

        expected = threshold_units(above_uint32);
        SetMaxFree(&allocator, above_uint32);
        TEST_REQUIRE(allocator.max_free_index == expected);

        expected = threshold_units(SIZE_MAX);
        SetMaxFree(&allocator, SIZE_MAX);
        TEST_REQUIRE(allocator.max_free_index == expected);
        TEST_REQUIRE(allocator.max_free_index > (size_t)UINT32_MAX);
    }

    SetMaxFree(&allocator, 0);
    TEST_REQUIRE(allocator.max_free_index == 0);
    TEST_REQUIRE(allocator.current_free_index == 0);
    SetMaxFree(NULL, SIZE_MAX);
    result = 0;

cleanup:
    return result;
}

static uint32_t next_random(uint32_t *value)
{
    *value = *value * UINT32_C(1664525) + UINT32_C(1013904223);
    return *value;
}

static int test_randomized_live_ranges(void)
{
    unsigned char *blocks[256] = { NULL };
    size_t lengths[256] = { 0 };
    Pool *pool = NULL;
    uint32_t random_value = UINT32_C(0x12345678);
    size_t live = 0;
    size_t iteration;
    size_t i;
    int result = -1;

    pool = newPool();
    TEST_REQUIRE(pool != NULL);
    for (iteration = 0; iteration < 400; ++iteration) {
        size_t size = (size_t)(next_random(&random_value) % 12000);
        unsigned char *memory;

        if ((next_random(&random_value) & 15u) == 0 || live == 256) {
            PoolClear(pool);
            live = 0;
        }
        memory = PoolAlloc(pool, size);
        TEST_REQUIRE(memory != NULL);
        if (size != 0) {
            uintptr_t left = (uintptr_t)memory;
            uintptr_t right = left + size;

            TEST_REQUIRE(right >= left);
            TEST_REQUIRE((left & (uintptr_t)7) == 0);
            for (i = 0; i < live; ++i) {
                uintptr_t other_left = (uintptr_t)blocks[i];
                uintptr_t other_right = other_left + lengths[i];

                TEST_REQUIRE(other_right <= left || right <= other_left);
            }
            memory[0] = (unsigned char)iteration;
            memory[size - 1] = (unsigned char)(iteration ^ 0xa5u);
            blocks[live] = memory;
            lengths[live] = size;
            ++live;
        }
    }

    result = 0;

cleanup:
    if (pool != NULL)
        PoolDestroy(pool);
    return result;
}

static const TestCase tests[] = {
    { "basic lifecycle and alignment", test_basic_lifecycle_and_alignment },
    { "calloc zeroes and null is safe", test_calloc_zeroes_and_null_is_safe },
    { "indexed nodes and sink are reused", test_indexed_nodes_and_sink_reuse_after_clear },
    { "near SIZE_MAX requests do not mutate the pool", test_near_size_max_does_not_mutate_pool },
    { "SetMaxFree rounds without narrowing", test_set_max_free_rounds_without_narrowing },
    { "randomized live ranges remain disjoint", test_randomized_live_ranges },
};

static const TestSuite suite = {
    "smallpool",
    tests,
    sizeof(tests) / sizeof(tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
