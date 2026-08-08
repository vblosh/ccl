#include "test_support.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "ccl_internal.h"

static int test_lifecycle_and_empty_operations(void)
{
    PQueue *queue = NULL;
    int value = 99;
    size_t empty_size;

    TEST_REQUIRE(iPQueue.Size(NULL) == 0);
    TEST_REQUIRE(iPQueue.Front(NULL, &value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iPQueue.Pop(NULL, &value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iPQueue.Clear(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iPQueue.Finalize(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iPQueue.Sizeof(NULL) > 0);

    queue = iPQueue.Create(sizeof(value));
    TEST_REQUIRE(queue != NULL);
    empty_size = iPQueue.Sizeof(queue);
    TEST_REQUIRE(empty_size >= iPQueue.Sizeof(NULL));
    TEST_REQUIRE(iPQueue.Size(queue) == 0);
    TEST_REQUIRE(iPQueue.Front(queue, &value) == INT_MIN);
    TEST_REQUIRE(iPQueue.Pop(queue, &value) == INT_MAX);
    TEST_REQUIRE(iPQueue.Add(NULL, 1, &value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iPQueue.Add(queue, 1, &value) == 1);
    TEST_REQUIRE(iPQueue.Front(queue, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iPQueue.Pop(queue, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iPQueue.Pop(queue, &value) == 1 && value == 99);
    {
        PQueue *bytes = iPQueue.Create(0);
        TEST_REQUIRE(bytes != NULL);
        TEST_REQUIRE(iPQueue.Add(bytes, 8, NULL) == 1);
        TEST_REQUIRE(iPQueue.Front(bytes, NULL) == 8);
        TEST_REQUIRE(iPQueue.Pop(bytes, NULL) == 8);
        TEST_REQUIRE(iPQueue.Finalize(bytes) == 1);
    }
    TEST_REQUIRE(iPQueue.Finalize(queue) == 1);
    queue = NULL;
    return 0;

cleanup:
    if (queue != NULL)
        iPQueue.Finalize(queue);
    return -1;
}

static int test_priority_order_and_value_association(void)
{
    PQueue *queue = NULL;
    struct item {
        int id;
        int guard;
    } item;
    struct item output;
    intptr_t keys[] = { 9, 5, 7, 5, -20, 12 };
    int ids[] = { 90, 50, 70, 51, -20, 120 };
    size_t i;
    intptr_t previous = INTPTR_MIN;
    size_t count = sizeof(keys) / sizeof(keys[0]);

    queue = iPQueue.Create(sizeof(item));
    TEST_REQUIRE(queue != NULL);
    for (i = 0; i < count; ++i) {
        item.id = ids[i];
        item.guard = 0x12340000 + (int)i;
        TEST_REQUIRE(iPQueue.Add(queue, keys[i], &item) == 1);
    }
    TEST_REQUIRE(iPQueue.Size(queue) == count);
    TEST_REQUIRE(iPQueue.Front(queue, &output) == -20);
    TEST_REQUIRE(output.id == -20 && output.guard == 0x12340004);
    for (i = 0; i < count; ++i) {
        intptr_t key = iPQueue.Pop(queue, &output);
        TEST_REQUIRE(key >= previous);
        previous = key;
        TEST_REQUIRE(output.id == (key == 5 ? 50 : key == 7 ? 70 :
                                   key == 9 ? 90 : key == 12 ? 120 : -20) ||
                     (key == 5 && output.id == 51));
    }
    TEST_REQUIRE(iPQueue.Size(queue) == 0);
    TEST_REQUIRE(iPQueue.Pop(queue, NULL) == INT_MAX);
    TEST_REQUIRE(iPQueue.Finalize(queue) == 1);
    queue = NULL;
    return 0;

cleanup:
    if (queue != NULL)
        iPQueue.Finalize(queue);
    return -1;
}

static int test_clamping_and_reuse(void)
{
    PQueue *queue = NULL;
    intptr_t key;
    int value = 11;
    int output = 0;

    queue = iPQueue.Create(sizeof(value));
    TEST_REQUIRE(queue != NULL);
    TEST_REQUIRE(iPQueue.Add(queue, INTPTR_MIN, &value) == 1);
    TEST_REQUIRE(iPQueue.Add(queue, INTPTR_MAX, &value) == 1);
    TEST_REQUIRE(iPQueue.Add(queue, (intptr_t)CCL_PRIORITY_MIN, &value) == 1);
    TEST_REQUIRE(iPQueue.Add(queue, (intptr_t)CCL_PRIORITY_MAX, &value) == 1);
    key = iPQueue.Pop(queue, &output);
    TEST_REQUIRE(key == CCL_PRIORITY_MIN);
    TEST_REQUIRE(iPQueue.Pop(queue, &output) == CCL_PRIORITY_MIN);
    TEST_REQUIRE(iPQueue.Pop(queue, &output) == CCL_PRIORITY_MAX);
    TEST_REQUIRE(iPQueue.Pop(queue, &output) == CCL_PRIORITY_MAX);
    TEST_REQUIRE(iPQueue.Clear(queue) == 1);
    TEST_REQUIRE(iPQueue.Size(queue) == 0);
    TEST_REQUIRE(iPQueue.Front(queue, &output) == INT_MIN);
    value = 77;
    TEST_REQUIRE(iPQueue.Add(queue, 1, &value) == 1);
    value = 0;
    TEST_REQUIRE(iPQueue.Pop(queue, &value) == 1 && value == 77);
    TEST_REQUIRE(iPQueue.Finalize(queue) == 1);
    queue = NULL;
    return 0;

cleanup:
    if (queue != NULL)
        iPQueue.Finalize(queue);
    return -1;
}

static int test_consolidation_copy_and_equal(void)
{
    PQueue *queue = NULL;
    PQueue *copy = NULL;
    PQueue *same = NULL;
    int value;
    int output;
    int i;

    queue = iPQueue.Create(sizeof(value));
    TEST_REQUIRE(queue != NULL);
    for (i = 0; i < 48; ++i) {
        value = i * 3;
        TEST_REQUIRE(iPQueue.Add(queue, (i * 17) % 23 - 11, &value) == 1);
    }
    for (i = 0; i < 17; ++i)
        TEST_REQUIRE(iPQueue.Pop(queue, &output) != INT_MAX);
    copy = iPQueue.Copy(queue);
    TEST_REQUIRE(copy != NULL);
    TEST_REQUIRE(iPQueue.Equal(queue, copy) == 1);
    value = 777;
    TEST_REQUIRE(iPQueue.Add(copy, -100, &value) == 1);
    TEST_REQUIRE(iPQueue.Equal(queue, copy) == 0);

    same = iPQueue.Create(sizeof(value));
    TEST_REQUIRE(same != NULL);
    value = 1;
    TEST_REQUIRE(iPQueue.Add(same, 4, &value) == 1);
    value = 2;
    TEST_REQUIRE(iPQueue.Add(same, 4, &value) == 1);
    TEST_REQUIRE(iPQueue.Equal(same, same) == 1);
    TEST_REQUIRE(iPQueue.Equal(same, NULL) == 0);
    TEST_REQUIRE(iPQueue.Equal(NULL, NULL) == 1);
    TEST_REQUIRE(iPQueue.Finalize(same) == 1);
    same = NULL;
    TEST_REQUIRE(iPQueue.Finalize(copy) == 1);
    copy = NULL;
    TEST_REQUIRE(iPQueue.Finalize(queue) == 1);
    queue = NULL;
    return 0;

cleanup:
    if (same != NULL)
        iPQueue.Finalize(same);
    if (copy != NULL)
        iPQueue.Finalize(copy);
    if (queue != NULL)
        iPQueue.Finalize(queue);
    return -1;
}

static int test_randomized_reference_sequence(void)
{
    PQueue *queue = NULL;
    intptr_t keys[1024];
    int values[1024];
    size_t count = 0;
    unsigned state = 0x13579bdfU;
    int value;
    intptr_t key;
    int step;

    queue = iPQueue.Create(sizeof(value));
    TEST_REQUIRE(queue != NULL);
    for (step = 0; step < 700; ++step) {
        size_t i;
        size_t minimum;
        state = state * 1103515245U + 12345U;
        if (count == 0 || (state & 3U) != 0) {
            key = (intptr_t)((int)((state >> 8) % 101U) - 50);
            value = step * 17 + 3;
            TEST_REQUIRE(count < sizeof(keys) / sizeof(keys[0]));
            TEST_REQUIRE(iPQueue.Add(queue, key, &value) == 1);
            keys[count] = key;
            values[count] = value;
            ++count;
        } else {
            minimum = 0;
            for (i = 1; i < count; ++i)
                if (keys[i] < keys[minimum])
                    minimum = i;
            value = 0;
            key = iPQueue.Pop(queue, &value);
            TEST_REQUIRE(key == keys[minimum]);
            for (i = 0; i < count; ++i)
                if (keys[i] == key && values[i] == value)
                    break;
            TEST_REQUIRE(i < count);
            minimum = i;
            --count;
            keys[minimum] = keys[count];
            values[minimum] = values[count];
        }
        TEST_REQUIRE(iPQueue.Size(queue) == count);
    }
    while (count != 0) {
        size_t i;
        size_t minimum = 0;
        for (i = 1; i < count; ++i)
            if (keys[i] < keys[minimum])
                minimum = i;
        key = iPQueue.Pop(queue, &value);
        TEST_REQUIRE(key == keys[minimum]);
        for (i = 0; i < count; ++i)
            if (keys[i] == key && values[i] == value)
                break;
        TEST_REQUIRE(i < count);
        minimum = i;
        --count;
        keys[minimum] = keys[count];
        values[minimum] = values[count];
    }
    TEST_REQUIRE(iPQueue.Pop(queue, NULL) == INT_MAX);
    TEST_REQUIRE(iPQueue.Finalize(queue) == 1);
    queue = NULL;
    return 0;

cleanup:
    if (queue != NULL)
        iPQueue.Finalize(queue);
    return -1;
}

static int test_union_and_incompatible_inputs(void)
{
    PQueue *left = NULL;
    PQueue *right = NULL;
    PQueue *bad = NULL;
    PQueue *merged = NULL;
    int value;
    int output;

    left = iPQueue.Create(sizeof(value));
    right = iPQueue.Create(sizeof(value));
    bad = iPQueue.Create(sizeof(long));
    TEST_REQUIRE(left != NULL && right != NULL && bad != NULL);
    value = 10;
    TEST_REQUIRE(iPQueue.Add(left, 7, &value) == 1);
    value = 20;
    TEST_REQUIRE(iPQueue.Add(right, 2, &value) == 1);
    TEST_REQUIRE(iPQueue.Union(left, bad) == NULL);
    TEST_REQUIRE(iPQueue.Size(left) == 1 && iPQueue.Size(bad) == 0);
    merged = iPQueue.Union(left, right);
    TEST_REQUIRE(merged == left);
    left = NULL; /* ownership is now held by merged */
    right = NULL;
    TEST_REQUIRE(iPQueue.Pop(merged, &output) == 2 && output == 20);
    TEST_REQUIRE(iPQueue.Pop(merged, &output) == 7 && output == 10);
    TEST_REQUIRE(iPQueue.Finalize(merged) == 1);
    merged = NULL;
    /* Exercise the empty/nonempty ownership direction as well. */
    left = iPQueue.Create(sizeof(value));
    right = iPQueue.Create(sizeof(value));
    TEST_REQUIRE(left != NULL && right != NULL);
    value = 33;
    TEST_REQUIRE(iPQueue.Add(right, 3, &value) == 1);
    merged = iPQueue.Union(left, right);
    TEST_REQUIRE(merged == right);
    left = NULL;
    right = NULL;
    TEST_REQUIRE(iPQueue.Pop(merged, &output) == 3 && output == 33);
    TEST_REQUIRE(iPQueue.Finalize(merged) == 1);
    merged = NULL;
    TEST_REQUIRE(iPQueue.Finalize(bad) == 1);
    bad = NULL;
    return 0;

cleanup:
    if (left != NULL)
        iPQueue.Finalize(left);
    if (right != NULL)
        iPQueue.Finalize(right);
    if (merged != NULL)
        iPQueue.Finalize(merged);
    if (bad != NULL)
        iPQueue.Finalize(bad);
    return -1;
}

typedef struct {
    size_t malloc_calls;
    size_t calloc_calls;
    size_t realloc_calls;
    size_t free_calls;
    size_t fail_malloc_at;
    size_t fail_calloc_at;
    size_t fail_realloc_at;
} AllocationState;

static AllocationState *active_state;

static void *pq_malloc(size_t size)
{
    ++active_state->malloc_calls;
    if (active_state->fail_malloc_at != 0 &&
        active_state->malloc_calls == active_state->fail_malloc_at)
        return NULL;
    return malloc(size);
}

static void *pq_calloc(size_t count, size_t size)
{
    ++active_state->calloc_calls;
    if (active_state->fail_calloc_at != 0 &&
        active_state->calloc_calls == active_state->fail_calloc_at)
        return NULL;
    return calloc(count, size);
}

static void *pq_realloc(void *ptr, size_t size)
{
    ++active_state->realloc_calls;
    if (active_state->fail_realloc_at != 0 &&
        active_state->realloc_calls == active_state->fail_realloc_at)
        return NULL;
    return realloc(ptr, size);
}

static void pq_free(void *ptr)
{
    if (ptr != NULL)
        ++active_state->free_calls;
    free(ptr);
}

static int test_allocator_failures_preserve_state(void)
{
    AllocationState state = { 0, 0, 0, 0, 0, 0, 0 };
    ContainerAllocator allocator = { pq_malloc, pq_free, pq_realloc, pq_calloc };
    PQueue *queue = NULL;
    int value = 1;
    size_t before;

    active_state = &state;
    state.fail_malloc_at = 1;
    TEST_REQUIRE(iPQueue.CreateWithAllocator(sizeof(value), &allocator) == NULL);
    memset(&state, 0, sizeof(state));
    state.fail_calloc_at = 1;
    TEST_REQUIRE(iPQueue.CreateWithAllocator(sizeof(value), &allocator) == NULL);

    {
        ContainerAllocator invalid = { NULL, pq_free, pq_realloc, pq_calloc };
        TEST_REQUIRE(iPQueue.CreateWithAllocator(sizeof(value), &invalid) == NULL);
    }

    memset(&state, 0, sizeof(state));
    queue = iPQueue.CreateWithAllocator(sizeof(value), &allocator);
    TEST_REQUIRE(queue != NULL);
    state.fail_calloc_at = state.calloc_calls + 1;
    TEST_REQUIRE(iPQueue.Add(queue, 9, &value) == CONTAINER_ERROR_NOMEMORY);
    state.fail_calloc_at = 0;
    TEST_REQUIRE(iPQueue.Add(queue, 4, &value) == 1);
    TEST_REQUIRE(iPQueue.Add(queue, 3, &value) == 1);
    before = iPQueue.Size(queue);
    state.fail_realloc_at = state.realloc_calls + 1;
    TEST_REQUIRE(iPQueue.Pop(queue, &value) == INT_MAX);
    TEST_REQUIRE(iPQueue.Size(queue) == before);
    state.fail_realloc_at = 0;
    TEST_REQUIRE(iPQueue.Pop(queue, &value) == 3);
    TEST_REQUIRE(iPQueue.Size(queue) == 1);
    TEST_REQUIRE(iPQueue.Sizeof(queue) > iPQueue.Sizeof(NULL));
    state.fail_calloc_at = state.calloc_calls + 1;
    TEST_REQUIRE(iPQueue.Copy(queue) == NULL);
    state.fail_calloc_at = 0;
    TEST_REQUIRE(iPQueue.Finalize(queue) == 1);
    queue = NULL;
    active_state = NULL;
    return 0;

cleanup:
    if (queue != NULL)
        iPQueue.Finalize(queue);
    active_state = NULL;
    return -1;
}

static const TestCase tests[] = {
    { "lifecycle and empty operations", test_lifecycle_and_empty_operations },
    { "priority order and value association", test_priority_order_and_value_association },
    { "clamping and reuse", test_clamping_and_reuse },
    { "consolidation, copy and equal", test_consolidation_copy_and_equal },
    { "randomized reference sequence", test_randomized_reference_sequence },
    { "union and incompatible inputs", test_union_and_incompatible_inputs },
    { "allocator failures preserve state", test_allocator_failures_preserve_state },
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "priorityqueue",
        tests,
        sizeof(tests) / sizeof(tests[0]),
    };
    return &suite;
}
