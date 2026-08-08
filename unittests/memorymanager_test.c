#include <stddef.h>
#include <stdlib.h>

#include "containers.h"
#include "test_support.h"

static size_t counting_malloc_calls;
static size_t counting_free_calls;
static size_t counting_realloc_calls;
static size_t counting_calloc_calls;

static void *counting_malloc(size_t size)
{
    ++counting_malloc_calls;
    return malloc(size);
}

static void counting_free(void *memory)
{
    ++counting_free_calls;
    free(memory);
}

static void *counting_realloc(void *memory, size_t size)
{
    ++counting_realloc_calls;
    return realloc(memory, size);
}

static void *counting_calloc(size_t count, size_t size)
{
    ++counting_calloc_calls;
    return calloc(count, size);
}

static ContainerAllocator counting_allocator = {
    counting_malloc,
    counting_free,
    counting_realloc,
    counting_calloc,
};

static void reset_counting_allocator(void)
{
    counting_malloc_calls = 0;
    counting_free_calls = 0;
    counting_realloc_calls = 0;
    counting_calloc_calls = 0;
}

static int test_default_allocator_lifecycle(void)
{
    ContainerAllocator *allocator = NULL;
    unsigned char *memory = NULL;
    unsigned char *zeroed = NULL;
    unsigned char *resized = NULL;
    size_t i;
    int result = -1;

    allocator = iAllocator.GetCurrent();
    TEST_REQUIRE(allocator != NULL);
    TEST_REQUIRE(allocator == CurrentAllocator);
    TEST_REQUIRE(allocator->malloc != NULL);
    TEST_REQUIRE(allocator->free != NULL);
    TEST_REQUIRE(allocator->realloc != NULL);
    TEST_REQUIRE(allocator->calloc != NULL);

    memory = allocator->malloc(8);
    TEST_REQUIRE(memory != NULL);
    for (i = 0; i < 8; ++i)
        memory[i] = (unsigned char)(i + 1);

    zeroed = allocator->calloc(8, 1);
    TEST_REQUIRE(zeroed != NULL);
    for (i = 0; i < 8; ++i)
        TEST_REQUIRE(zeroed[i] == 0);

    resized = allocator->realloc(memory, 16);
    TEST_REQUIRE(resized != NULL);
    memory = resized;
    resized = NULL;
    for (i = 0; i < 8; ++i)
        TEST_REQUIRE(memory[i] == (unsigned char)(i + 1));

    allocator->free(zeroed);
    zeroed = NULL;
    allocator->free(memory);
    memory = NULL;
    result = 0;

cleanup:
    if (allocator != NULL && resized != NULL)
        allocator->free(resized);
    if (allocator != NULL && memory != NULL)
        allocator->free(memory);
    if (allocator != NULL && zeroed != NULL)
        allocator->free(zeroed);
    return result;
}

static int test_change_installs_allocator_and_returns_previous(void)
{
    ContainerAllocator *previous = iAllocator.GetCurrent();
    ContainerAllocator *returned;
    int result = -1;

    TEST_REQUIRE(previous != NULL);
    reset_counting_allocator();
    returned = iAllocator.Change(&counting_allocator);
    TEST_REQUIRE(returned == previous);
    TEST_REQUIRE(iAllocator.GetCurrent() == &counting_allocator);
    TEST_REQUIRE(CurrentAllocator == &counting_allocator);

    result = 0;

cleanup:
    if (previous != NULL && CurrentAllocator != previous)
        (void)iAllocator.Change(previous);
    return result;
}

static int test_change_null_queries_without_mutating(void)
{
    ContainerAllocator *previous = iAllocator.GetCurrent();
    ContainerAllocator *returned;
    int result = -1;

    TEST_REQUIRE(previous != NULL);
    TEST_REQUIRE(iAllocator.Change(&counting_allocator) == previous);
    returned = iAllocator.Change(NULL);
    TEST_REQUIRE(returned == &counting_allocator);
    TEST_REQUIRE(iAllocator.GetCurrent() == &counting_allocator);
    TEST_REQUIRE(CurrentAllocator == &counting_allocator);
    TEST_REQUIRE(counting_malloc_calls == 0);
    TEST_REQUIRE(counting_free_calls == 0);
    TEST_REQUIRE(counting_realloc_calls == 0);
    TEST_REQUIRE(counting_calloc_calls == 0);

    result = 0;

cleanup:
    if (previous != NULL && CurrentAllocator != previous)
        (void)iAllocator.Change(previous);
    return result;
}

static int test_change_restores_saved_allocator(void)
{
    ContainerAllocator *previous = iAllocator.GetCurrent();
    ContainerAllocator *returned;
    int result = -1;

    TEST_REQUIRE(previous != NULL);
    returned = iAllocator.Change(&counting_allocator);
    TEST_REQUIRE(returned == previous);
    returned = iAllocator.Change(previous);
    TEST_REQUIRE(returned == &counting_allocator);
    TEST_REQUIRE(iAllocator.GetCurrent() == previous);
    TEST_REQUIRE(CurrentAllocator == previous);

    result = 0;

cleanup:
    if (previous != NULL && CurrentAllocator != previous)
        (void)iAllocator.Change(previous);
    return result;
}

static const TestCase tests[] = {
    {"default allocator supports a complete lifecycle",
     test_default_allocator_lifecycle},
    {"changing allocator returns and installs exact pointers",
     test_change_installs_allocator_and_returns_previous},
    {"changing to NULL only queries the current allocator",
     test_change_null_queries_without_mutating},
    {"saved allocator can be restored",
     test_change_restores_saved_allocator},
};

static const TestSuite suite = {
    "memorymanager",
    tests,
    sizeof(tests) / sizeof(tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
