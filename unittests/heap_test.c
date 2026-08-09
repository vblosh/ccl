#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>

#include "containers.h"
#include "ccl_internal.h"

#define HEAP_TEST_CHUNK 1000U

static int test_alignment_and_reuse(void)
{
    const size_t sizes[] = { 0, 1, 3, sizeof(void *), sizeof(max_align_t) + 3 };
    size_t s;
    ContainerHeap *heap = NULL;
    void *objects[8] = { 0 };
    size_t i, j;
    int result = 1;

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        heap = iHeap.Create(sizes[s], NULL);
        TEST_REQUIRE(heap != NULL);
        for (i = 0; i < 8; ++i) {
            objects[i] = iHeap.NewObject(heap);
            TEST_REQUIRE(objects[i] != NULL);
            TEST_REQUIRE((uintptr_t)objects[i] % _Alignof(max_align_t) == 0);
            for (j = 0; j < i; ++j)
                TEST_REQUIRE(objects[i] != objects[j]);
        }
        TEST_REQUIRE(iHeap.Sizeof(heap) > 0);
        TEST_REQUIRE(iHeap.FreeObject(heap, objects[2]) == 1);
        TEST_REQUIRE(iHeap.FreeObject(heap, objects[5]) == 1);
        TEST_REQUIRE(iHeap.NewObject(heap) == objects[5]);
        TEST_REQUIRE(iHeap.NewObject(heap) == objects[2]);
        iHeap.Finalize(heap);
        heap = NULL;
    }
    result = 0;

cleanup:
    if (heap != NULL)
        iHeap.Finalize(heap);
    return result;
}

static int test_free_iteration_and_mutation(void)
{
    ContainerHeap *heap = NULL;
    Iterator *iterator = NULL;
    void *objects[4] = { 0 };
    int result = 1;

    heap = iHeap.Create(sizeof(int), NULL);
    TEST_REQUIRE(heap != NULL);
    objects[0] = iHeap.NewObject(heap);
    objects[1] = iHeap.NewObject(heap);
    objects[2] = iHeap.NewObject(heap);
    objects[3] = iHeap.NewObject(heap);
    TEST_REQUIRE(objects[0] && objects[1] && objects[2] && objects[3]);
    *(int *)objects[0] = 10;
    *(int *)objects[1] = 11;
    *(int *)objects[2] = 12;
    *(int *)objects[3] = 13;
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[1]) == 1);
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[3]) == 1);
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[3]) < 0);
    TEST_REQUIRE(iHeap.FreeObject(heap, (char *)objects[0] + 1) < 0);

    iterator = iHeap.NewIterator(heap);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) == objects[0]);
    TEST_REQUIRE(iterator->GetPosition(iterator) == 0);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == objects[0]);
    TEST_REQUIRE(iterator->GetNext(iterator) == objects[2]);
    TEST_REQUIRE(iterator->GetPosition(iterator) == 2);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == objects[2]);
    TEST_REQUIRE(iterator->GetLast(iterator) == objects[2]);
    TEST_REQUIRE(iterator->GetPrevious(iterator) == objects[0]);
    TEST_REQUIRE(iterator->GetPrevious(iterator) == NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == objects[0]);

    TEST_REQUIRE(iHeap.NewObject(heap) == objects[3]);
    TEST_REQUIRE(iterator->GetFirst(iterator) == NULL);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    TEST_REQUIRE(iterator->GetPrevious(iterator) == NULL);
    TEST_REQUIRE(iterator->GetLast(iterator) == NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(iterator->GetPosition(iterator) == SIZE_MAX);
    TEST_REQUIRE(iHeap.DeleteIterator(iterator) == 1);
    iterator = NULL;

    TEST_REQUIRE(iHeap.FreeObject(heap, objects[2]) == 1);
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[3]) == 1);
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[0]) == 1);
    iterator = iHeap.NewIterator(heap);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetLast(iterator) == NULL);
    TEST_REQUIRE(iHeap.DeleteIterator(iterator) == 1);
    iterator = NULL;

    result = 0;
cleanup:
    if (iterator != NULL)
        iHeap.DeleteIterator(iterator);
    if (heap != NULL)
        iHeap.Finalize(heap);
    return result;
}

static int test_clear_and_block_boundaries(void)
{
    ContainerHeap *heap = NULL;
    Iterator *iterator = NULL;
    void **objects = NULL;
    size_t count = HEAP_TEST_CHUNK + 3;
    size_t i;
    int result = 1;

    heap = iHeap.Create(1, NULL);
    TEST_REQUIRE(heap != NULL);
    objects = calloc(count, sizeof(*objects));
    TEST_REQUIRE(objects != NULL);
    for (i = 0; i < count; ++i) {
        objects[i] = iHeap.NewObject(heap);
        TEST_REQUIRE(objects[i] != NULL);
    }
    TEST_REQUIRE(objects[998] != objects[999]);
    TEST_REQUIRE(objects[999] != objects[1000]);
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[998]) == 1);
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[999]) == 1);
    TEST_REQUIRE(iHeap.FreeObject(heap, objects[1000]) == 1);
    iterator = iHeap.NewIterator(heap);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) == objects[0]);
    TEST_REQUIRE(iterator->GetLast(iterator) == objects[count - 1]);
    TEST_REQUIRE(iterator->GetPosition(iterator) == count - 1);
    TEST_REQUIRE(iHeap.DeleteIterator(iterator) == 1);
    iterator = NULL;
    TEST_REQUIRE(iHeap.Sizeof(heap) > 0);
    iHeap.Clear(heap);
    TEST_REQUIRE(iHeap.Sizeof(heap) == 0);
    TEST_REQUIRE(iHeap.NewObject(heap) != NULL);
    iHeap.Clear(heap);
    iHeap.Clear(heap);
    result = 0;

cleanup:
    if (iterator != NULL)
        iHeap.DeleteIterator(iterator);
    free(objects);
    if (heap != NULL)
        iHeap.Finalize(heap);
    return result;
}

static int allocation_calls;
static int allocation_live;
static int allocation_fail_at;
static void *counting_malloc(size_t size);
static void *counting_calloc(size_t count, size_t size);
static void *counting_realloc(void *old, size_t size);
static void counting_free(void *object);

static int test_pointer_table_growth(void)
{
    ContainerAllocator allocator = {
        counting_malloc, counting_free, counting_realloc, counting_calloc
    };
    ContainerHeap *heap = NULL;
    Iterator *iterator = NULL;
    size_t count = (size_t)HEAP_TEST_CHUNK * (size_t)HEAP_TEST_CHUNK + 1;
    size_t i;
    int result = 1;

    allocation_calls = 0;
    allocation_live = 0;
    allocation_fail_at = -1;
    heap = iHeap.Create(1, &allocator);
    TEST_REQUIRE(heap != NULL);
    for (i = 0; i < count - 1; ++i)
        TEST_REQUIRE(iHeap.NewObject(heap) != NULL);
    allocation_fail_at = allocation_calls;
    TEST_REQUIRE(iHeap.NewObject(heap) == NULL);
    TEST_REQUIRE(heap->CurrentBlock == HEAP_TEST_CHUNK - 1);
    TEST_REQUIRE(heap->BlockIndex == HEAP_TEST_CHUNK);
    allocation_fail_at = -1;
    TEST_REQUIRE(iHeap.NewObject(heap) != NULL);
    TEST_REQUIRE(heap->BlockCount > HEAP_TEST_CHUNK);
    TEST_REQUIRE(heap->CurrentBlock == HEAP_TEST_CHUNK);
    TEST_REQUIRE(heap->BlockIndex == 1);
    iterator = iHeap.NewIterator(heap);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(iterator->GetLast(iterator) != NULL);
    TEST_REQUIRE(iterator->GetPosition(iterator) == count - 1);
    iHeap.DeleteIterator(iterator);
    iterator = NULL;
    result = 0;

cleanup:
    if (iterator != NULL)
        iHeap.DeleteIterator(iterator);
    if (heap != NULL)
        iHeap.Finalize(heap);
    allocation_fail_at = -1;
    if (allocation_live != 0)
        result = 1;
    return result;
}

static int should_fail_allocation(void)
{
    int call = allocation_calls++;
    return allocation_fail_at >= 0 && call == allocation_fail_at;
}

static void *counting_malloc(size_t size)
{
    void *result;
    if (should_fail_allocation())
        return NULL;
    result = malloc(size);
    if (result != NULL)
        ++allocation_live;
    return result;
}

static void *counting_calloc(size_t count, size_t size)
{
    void *result;
    if (should_fail_allocation())
        return NULL;
    result = calloc(count, size);
    if (result != NULL)
        ++allocation_live;
    return result;
}

static void *counting_realloc(void *old, size_t size)
{
    return should_fail_allocation() ? NULL : realloc(old, size);
}

static void counting_free(void *object)
{
    if (object != NULL)
        --allocation_live;
    free(object);
}

static int test_allocator_failures_and_invalid_arguments(void)
{
    ContainerAllocator allocator = {
        counting_malloc, counting_free, counting_realloc, counting_calloc
    };
    ContainerHeap *heap = NULL;
    ContainerHeap *default_heap = NULL;
    Iterator *iterator = NULL;
    int result = 1;

    allocation_calls = 0;
    allocation_live = 0;
    allocation_fail_at = -1;
    heap = iHeap.Create(sizeof(int), &allocator);
    TEST_REQUIRE(heap != NULL);
    allocation_fail_at = allocation_calls; /* pointer table */
    TEST_REQUIRE(iHeap.NewObject(heap) == NULL);
    allocation_fail_at = -1;
    TEST_REQUIRE(iHeap.NewObject(heap) != NULL);
    iHeap.Clear(heap);
    allocation_fail_at = allocation_calls + 1; /* block after table */
    TEST_REQUIRE(iHeap.NewObject(heap) == NULL);
    allocation_fail_at = -1;
    TEST_REQUIRE(iHeap.NewObject(heap) != NULL);
    iterator = iHeap.NewIterator(heap);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(iHeap.NewObject(heap) != NULL);
    TEST_REQUIRE(iterator->GetPosition(iterator) == SIZE_MAX);
    TEST_REQUIRE(iHeap.DeleteIterator(iterator) == 1);
    iterator = NULL;
    iHeap.Finalize(heap);
    heap = NULL;
    TEST_REQUIRE(allocation_live == 0);

    default_heap = iHeap.Create(sizeof(int), NULL);
    TEST_REQUIRE(default_heap != NULL);
    iHeap.Finalize(default_heap);
    default_heap = NULL;

    result = 0;
cleanup:
    if (iterator != NULL)
        iHeap.DeleteIterator(iterator);
    if (heap != NULL)
        iHeap.Finalize(heap);
    if (default_heap != NULL)
        iHeap.Finalize(default_heap);
    allocation_fail_at = -1;
    return result;
}

static int test_bad_iterators_and_empty_heap(void)
{
    ContainerHeap storage;
    ContainerAllocator bad_allocator = { 0 };
    ContainerHeap *heap = NULL;
    Iterator *iterator = NULL;
    struct HeapIterator *internal;
    int result = 1;

    TEST_REQUIRE(iHeap.InitHeap(NULL, 1, NULL) == NULL);
    TEST_REQUIRE(iHeap.InitHeap(&storage, 1, &bad_allocator) == NULL);
    TEST_REQUIRE(iHeap.Create(1, &bad_allocator) == NULL);
    TEST_REQUIRE(iHeap.Create(SIZE_MAX, NULL) == NULL);
    TEST_REQUIRE(iHeap.Sizeof(NULL) == 0);
    TEST_REQUIRE(iHeap.NewObject(NULL) == NULL);
    TEST_REQUIRE(iHeap.FreeObject(NULL, NULL) < 0);
    iHeap.Clear(NULL);
    TEST_REQUIRE(iHeap.DeleteIterator(NULL) < 0);
    heap = iHeap.InitHeap(&storage, 0, NULL);
    TEST_REQUIRE(heap == &storage);
    iterator = iHeap.NewIterator(heap);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    TEST_REQUIRE(iterator->GetPrevious(iterator) == NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) == NULL);
    TEST_REQUIRE(iterator->GetLast(iterator) == NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(iterator->GetPosition(iterator) == SIZE_MAX);
    internal = (struct HeapIterator *)iterator;
    internal->Magic = 0;
    TEST_REQUIRE(iterator->GetFirst(iterator) == NULL);
    TEST_REQUIRE(iHeap.DeleteIterator(iterator) < 0);
    internal->Magic = HEAP_MAGIC_NUMBER;
    internal->BlockNumber = (SIZE_MAX - 1) / HEAP_TEST_CHUNK;
    internal->BlockPosition = (SIZE_MAX - 1) % HEAP_TEST_CHUNK;
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    internal->BlockNumber = SIZE_MAX;
    internal->BlockPosition = SIZE_MAX;
    TEST_REQUIRE(iHeap.DeleteIterator(iterator) == 1);
    iterator = NULL;
    iHeap.Clear(heap);
    heap = NULL;

    result = 0;

cleanup:
    if (iterator != NULL)
        iHeap.DeleteIterator(iterator);
    if (heap != NULL)
        iHeap.Clear(heap);
    return result;
}

static const TestCase tests[] = {
    { "alignment and reuse", test_alignment_and_reuse },
    { "free iteration and mutation", test_free_iteration_and_mutation },
    { "clear and block boundaries", test_clear_and_block_boundaries },
    { "pointer table growth", test_pointer_table_growth },
    { "allocator failures and invalid arguments",
      test_allocator_failures_and_invalid_arguments },
    { "bad iterators and empty heap", test_bad_iterators_and_empty_heap }
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "heap",
        tests,
        sizeof(tests) / sizeof(tests[0])
    };
    return &suite;
}
