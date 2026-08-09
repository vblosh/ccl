#include "containers.h"
#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t malloc_calls;
    size_t calloc_calls;
    size_t free_calls;
    size_t fail_malloc_at;
    size_t fail_calloc_at;
} AllocatorState;

static AllocatorState state_a;
static AllocatorState state_b;

#define DEFINE_TRACKING_ALLOCATOR(prefix, state)                            \
    static void *prefix##_malloc(size_t size)                               \
    {                                                                        \
        ++(state).malloc_calls;                                              \
        if ((state).fail_malloc_at != 0 &&                                  \
            (state).malloc_calls >= (state).fail_malloc_at)                 \
            return NULL;                                                     \
        return malloc(size);                                                 \
    }                                                                        \
    static void prefix##_free(void *memory)                                  \
    {                                                                        \
        if (memory != NULL)                                                  \
            ++(state).free_calls;                                            \
        free(memory);                                                        \
    }                                                                        \
    static void *prefix##_realloc(void *memory, size_t size)                 \
    {                                                                        \
        return realloc(memory, size);                                        \
    }                                                                        \
    static void *prefix##_calloc(size_t count, size_t size)                  \
    {                                                                        \
        ++(state).calloc_calls;                                              \
        if ((state).fail_calloc_at != 0 &&                                   \
            (state).calloc_calls >= (state).fail_calloc_at)                 \
            return NULL;                                                     \
        if (size != 0 && count > SIZE_MAX / size)                            \
            return NULL;                                                     \
        return calloc(count, size);                                          \
    }

DEFINE_TRACKING_ALLOCATOR(allocator_a, state_a)
DEFINE_TRACKING_ALLOCATOR(allocator_b, state_b)

static ContainerAllocator allocator_a = {
    allocator_a_malloc,
    allocator_a_free,
    allocator_a_realloc,
    allocator_a_calloc,
};

static ContainerAllocator allocator_b = {
    allocator_b_malloc,
    allocator_b_free,
    allocator_b_realloc,
    allocator_b_calloc,
};

static void reset_state(AllocatorState *state)
{
    memset(state, 0, sizeof(*state));
}

static int test_empty_pool_lifecycle_is_balanced(void)
{
    Pool *pool = NULL;
    int result = -1;

    reset_state(&state_a);
    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    TEST_REQUIRE(state_a.calloc_calls == 1);
    TEST_REQUIRE(state_a.malloc_calls == 1);

    iPool.Finalize(pool);
    pool = NULL;
    TEST_REQUIRE(state_a.free_calls == 2);
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    return result;
}

static int test_creation_and_growth_failures_clean_up(void)
{
    Pool *pool = NULL;
    void *memory = NULL;
    size_t malloc_before;
    int result = -1;

    reset_state(&state_a);
    state_a.fail_calloc_at = 1;
    TEST_REQUIRE(iPool.Create(&allocator_a) == NULL);
    TEST_REQUIRE(state_a.calloc_calls == 1);
    TEST_REQUIRE(state_a.free_calls == 0);

    reset_state(&state_a);
    state_a.fail_malloc_at = 1;
    TEST_REQUIRE(iPool.Create(&allocator_a) == NULL);
    TEST_REQUIRE(state_a.calloc_calls == 1);
    TEST_REQUIRE(state_a.malloc_calls == 1);
    TEST_REQUIRE(state_a.free_calls == 1);

    reset_state(&state_a);
    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    memory = iPool.Alloc(pool, 4000);
    TEST_REQUIRE(memory != NULL);
    malloc_before = state_a.malloc_calls;
    state_a.fail_malloc_at = malloc_before + 1;
    TEST_REQUIRE(iPool.Alloc(pool, 8192) == NULL);
    TEST_REQUIRE(state_a.malloc_calls == malloc_before + 1);

    /* A failed growth attempt must not consume the active node's space. */
    state_a.fail_malloc_at = 0;
    TEST_REQUIRE(iPool.Alloc(pool, 16) != NULL);
    TEST_REQUIRE(state_a.malloc_calls == malloc_before + 1);

    iPool.Finalize(pool);
    pool = NULL;
    TEST_REQUIRE(state_a.free_calls == 2);
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    return result;
}

static int test_small_allocations_are_aligned_and_disjoint(void)
{
    static const size_t sizes[] = { 0, 1, 7, 8, 9 };
    unsigned char *blocks[sizeof(sizes) / sizeof(sizes[0])] = { NULL };
    Pool *pool = NULL;
    size_t i;
    size_t j;
    int result = -1;

    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        blocks[i] = iPool.Alloc(pool, sizes[i]);
        TEST_REQUIRE(blocks[i] != NULL);
        if (sizes[i] != 0) {
            TEST_REQUIRE(((uintptr_t)blocks[i] & (uintptr_t)7) == 0);
            memset(blocks[i], (int)(i + 1), sizes[i]);
        }
    }

    for (i = 1; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        uintptr_t left = (uintptr_t)blocks[i];
        uintptr_t right = left + sizes[i];
        TEST_REQUIRE(right >= left);
        for (j = 1; j < i; ++j) {
            uintptr_t other_left = (uintptr_t)blocks[j];
            uintptr_t other_right = other_left + sizes[j];
            TEST_REQUIRE(other_right <= left || right <= other_left);
        }
    }

    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    return result;
}

static int test_node_buckets_and_oversized_sink_reuse(void)
{
    void *nodes[6] = { NULL };
    void *reused[6] = { NULL };
    void *sink = NULL;
    void *reused_sink = NULL;
    Pool *pool = NULL;
    size_t before_reuse;
    size_t i;
    int result = -1;

    reset_state(&state_a);
    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);

    for (i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
        nodes[i] = iPool.Alloc(pool, 8192);
        TEST_REQUIRE(nodes[i] != NULL);
    }
    sink = iPool.Alloc(pool, 100000);
    TEST_REQUIRE(sink != NULL);
    before_reuse = state_a.malloc_calls;

    iPool.Clear(pool);
    for (i = 0; i < sizeof(reused) / sizeof(reused[0]); ++i) {
        reused[i] = iPool.Alloc(pool, 8192);
        TEST_REQUIRE(reused[i] != NULL);
    }
    reused_sink = iPool.Alloc(pool, 100000);
    TEST_REQUIRE(reused_sink != NULL);
    TEST_REQUIRE(state_a.malloc_calls == before_reuse);

    iPool.Finalize(pool);
    pool = NULL;
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    return result;
}

static int test_clear_reuses_memory_and_calloc_resets_it(void)
{
    unsigned char *memory = NULL;
    unsigned char *first = NULL;
    unsigned char *second = NULL;
    Pool *pool = NULL;
    size_t i;
    int result = -1;

    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    for (i = 0; i < 4; ++i) {
        memory = iPool.Alloc(pool, 64);
        TEST_REQUIRE(memory != NULL);
        memset(memory, 0xa5, 64);
        iPool.Clear(pool);
        memory = iPool.Calloc(pool, 64, 1);
        TEST_REQUIRE(memory != NULL);
        for (size_t j = 0; j < 64; ++j)
            TEST_REQUIRE(memory[j] == 0);
    }

    first = iPool.Alloc(pool, 8000);
    second = iPool.Alloc(pool, 8000);
    TEST_REQUIRE(first != NULL && second != NULL);
    memset(first, 0x11, 8000);
    memset(second, 0x22, 8000);
    iPool.Clear(pool);

    first = iPool.Calloc(pool, 8000, 1);
    second = iPool.Calloc(pool, 8000, 1);
    TEST_REQUIRE(first != NULL && second != NULL);
    for (i = 0; i < 8000; ++i) {
        TEST_REQUIRE(first[i] == 0);
        TEST_REQUIRE(second[i] == 0);
    }

    iPool.Clear(pool);
    TEST_REQUIRE(iPool.Alloc(pool, 32) != NULL);
    iPool.Clear(pool);
    TEST_REQUIRE(iPool.Alloc(pool, 32) != NULL);
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    return result;
}

static int test_calloc_zeroes_and_rejects_overflow(void)
{
    unsigned char *memory = NULL;
    Pool *pool = NULL;
    size_t malloc_before;
    size_t i;
    int result = -1;

    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    memory = iPool.Calloc(pool, 32, sizeof(uint32_t));
    TEST_REQUIRE(memory != NULL);
    for (i = 0; i < 32 * sizeof(uint32_t); ++i)
        TEST_REQUIRE(memory[i] == 0);

    malloc_before = state_a.malloc_calls;
    TEST_REQUIRE(iPool.Calloc(pool, SIZE_MAX / 2 + 1, 2) == NULL);
    TEST_REQUIRE(state_a.malloc_calls == malloc_before);
    TEST_REQUIRE(iPool.Calloc(NULL, 1, 1) == NULL);
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    return result;
}

static int test_explicit_and_current_allocators_are_snapshotted(void)
{
    ContainerAllocator *saved_current = CurrentAllocator;
    Pool *pool = NULL;
    int result = -1;

    reset_state(&state_a);
    reset_state(&state_b);
    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    CurrentAllocator = &allocator_b;
    iPool.Finalize(pool);
    pool = NULL;
    TEST_REQUIRE(state_a.calloc_calls == 1 && state_a.malloc_calls == 1);
    TEST_REQUIRE(state_a.free_calls == 2);
    TEST_REQUIRE(state_b.calloc_calls == 0 && state_b.malloc_calls == 0);
    TEST_REQUIRE(state_b.free_calls == 0);

    reset_state(&state_a);
    reset_state(&state_b);
    CurrentAllocator = &allocator_b;
    pool = iPool.Create(NULL);
    TEST_REQUIRE(pool != NULL);
    CurrentAllocator = &allocator_a;
    iPool.Finalize(pool);
    pool = NULL;
    TEST_REQUIRE(state_b.calloc_calls == 1 && state_b.malloc_calls == 1);
    TEST_REQUIRE(state_b.free_calls == 2);
    TEST_REQUIRE(state_a.calloc_calls == 0 && state_a.malloc_calls == 0);
    TEST_REQUIRE(state_a.free_calls == 0);
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    CurrentAllocator = saved_current;
    return result;
}

static int test_near_size_max_requests_do_not_mutate_pool(void)
{
    Pool *pool = NULL;
    size_t malloc_before;
    int result = -1;

    reset_state(&state_a);
    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    malloc_before = state_a.malloc_calls;
    TEST_REQUIRE(iPool.Alloc(pool, SIZE_MAX) == NULL);
    TEST_REQUIRE(iPool.Alloc(pool, SIZE_MAX - 7) == NULL);
    TEST_REQUIRE(state_a.malloc_calls == malloc_before);
    TEST_REQUIRE(iPool.Alloc(pool, 1) != NULL);
    TEST_REQUIRE(state_a.malloc_calls == malloc_before);
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
    return result;
}

static int test_debug_allocator_lifecycle(void)
{
    Pool *pool = NULL;
    void *memory = NULL;
    int result = -1;

    pool = iPool.Create(&iDebugMalloc);
    TEST_REQUIRE(pool != NULL);
    memory = iPool.Alloc(pool, 9000);
    TEST_REQUIRE(memory != NULL);
    memory = iPool.Calloc(pool, 64, sizeof(uint32_t));
    TEST_REQUIRE(memory != NULL);
    iPool.Clear(pool);
    TEST_REQUIRE(iPool.Alloc(pool, 1) != NULL);
    iPool.Finalize(pool);
    pool = NULL;
    result = 0;

cleanup:
    if (pool != NULL)
        iPool.Finalize(pool);
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

    pool = iPool.Create(&allocator_a);
    TEST_REQUIRE(pool != NULL);
    for (iteration = 0; iteration < 400; ++iteration) {
        size_t size = (size_t)(next_random(&random_value) % 12000);
        unsigned char *memory;

        if ((next_random(&random_value) & 15u) == 0 || live == 256) {
            iPool.Clear(pool);
            live = 0;
        }
        memory = iPool.Alloc(pool, size);
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
        iPool.Finalize(pool);
    return result;
}

static const TestCase tests[] = {
    { "empty pool lifecycle is balanced", test_empty_pool_lifecycle_is_balanced },
    { "creation and growth failures clean up", test_creation_and_growth_failures_clean_up },
    { "small allocations are aligned and disjoint", test_small_allocations_are_aligned_and_disjoint },
    { "node buckets and oversized sink are reused", test_node_buckets_and_oversized_sink_reuse },
    { "clear reuses memory and calloc resets it", test_clear_reuses_memory_and_calloc_resets_it },
    { "calloc zeroes and rejects overflow", test_calloc_zeroes_and_rejects_overflow },
    { "explicit and current allocators are snapshotted", test_explicit_and_current_allocators_are_snapshotted },
    { "near SIZE_MAX requests do not mutate the pool", test_near_size_max_requests_do_not_mutate_pool },
    { "debug allocator supports the pool lifecycle", test_debug_allocator_lifecycle },
    { "randomized live ranges remain disjoint", test_randomized_live_ranges },
};

static const TestSuite suite = {
    "pool",
    tests,
    sizeof(tests) / sizeof(tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
