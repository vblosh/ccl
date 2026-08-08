#include "containers.h"
#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t malloc_calls;
    size_t free_calls;
    int fail_malloc;
} AllocatorStats;

static AllocatorStats allocator_a_stats;
static AllocatorStats allocator_b_stats;
static AllocatorStats failing_allocator_stats;

static void *tracked_malloc(AllocatorStats *stats,size_t size)
{
    ++stats->malloc_calls;
    if (stats->fail_malloc)
        return NULL;
    return malloc(size);
}

static void tracked_free(AllocatorStats *stats,void *ptr)
{
    if (ptr != NULL)
        ++stats->free_calls;
    free(ptr);
}

static void *a_malloc(size_t size)
{
    return tracked_malloc(&allocator_a_stats,size);
}

static void a_free(void *ptr)
{
    tracked_free(&allocator_a_stats,ptr);
}

static void *a_realloc(void *ptr,size_t size)
{
    return realloc(ptr,size);
}

static void *a_calloc(size_t count,size_t size)
{
    return calloc(count,size);
}

static void *b_malloc(size_t size)
{
    return tracked_malloc(&allocator_b_stats,size);
}

static void b_free(void *ptr)
{
    tracked_free(&allocator_b_stats,ptr);
}

static void *b_realloc(void *ptr,size_t size)
{
    return realloc(ptr,size);
}

static void *b_calloc(size_t count,size_t size)
{
    return calloc(count,size);
}

static void *failing_malloc(size_t size)
{
    return tracked_malloc(&failing_allocator_stats,size);
}

static void failing_free(void *ptr)
{
    tracked_free(&failing_allocator_stats,ptr);
}

static void *failing_realloc(void *ptr,size_t size)
{
    return realloc(ptr,size);
}

static void *failing_calloc(size_t count,size_t size)
{
    return calloc(count,size);
}

static ContainerAllocator allocator_a = {
    a_malloc, a_free, a_realloc, a_calloc
};
static ContainerAllocator allocator_b = {
    b_malloc, b_free, b_realloc, b_calloc
};
static ContainerAllocator failing_allocator = {
    failing_malloc, failing_free, failing_realloc, failing_calloc
};

static struct {
    const char *operation;
    int code;
} last_error;

static void *capture_error(const char *operation,int code,...)
{
    last_error.operation = operation;
    last_error.code = code;
    return NULL;
}

static void reset_error(void)
{
    last_error.operation = NULL;
    last_error.code = 0;
}

static int error_is(const char *operation,int code)
{
    return last_error.operation != NULL &&
           strcmp(last_error.operation,operation) == 0 &&
           last_error.code == code;
}

static int test_create_values_and_copy(void)
{
    const char values[] = { 0, 2, -1, 0 };
    Mask *empty = NULL;
    Mask *mask = NULL;
    Mask *copy = NULL;
    size_t i;
    int result = 1;

    empty = iMask.Create(0);
    mask = iMask.CreateFromMask(sizeof(values),values);
    TEST_REQUIRE(empty != NULL && mask != NULL);
    TEST_REQUIRE(iMask.Size(empty) == 0);
    TEST_REQUIRE(iMask.Sizeof(empty) == iMask.Sizeof(NULL));
    TEST_REQUIRE(iMask.Size(mask) == sizeof(values));
    TEST_REQUIRE(iMask.Sizeof(mask) == iMask.Sizeof(NULL) + sizeof(values));
    TEST_REQUIRE(iMask.PopulationCount(mask) == 2);
    for (i = 0; i < sizeof(values); ++i)
        TEST_REQUIRE(iMask.GetElement(mask,i) == values[i]);

    copy = iMask.Copy(mask);
    TEST_REQUIRE(copy != NULL && iMask.Size(copy) == iMask.Size(mask));
    TEST_REQUIRE(iMask.GetElement(copy,1) == 2);
    TEST_REQUIRE(iMask.SetElement(copy,1,0) == 1);
    TEST_REQUIRE(iMask.GetElement(mask,1) == 2);
    TEST_REQUIRE(iMask.Copy(NULL) == NULL);
    result = 0;

cleanup:
    if (copy != NULL) iMask.Finalize(copy);
    if (mask != NULL) iMask.Finalize(mask);
    if (empty != NULL) iMask.Finalize(empty);
    return result;
}

static int test_bounds_and_clear(void)
{
    Mask *mask = NULL;
    ErrorFunction old_error;
    size_t i;
    int result = 1;

    reset_error();
    old_error = iError.SetErrorFunction(capture_error);
    mask = iMask.Create(3);
    TEST_REQUIRE(mask != NULL);
    TEST_REQUIRE(iMask.SetElement(mask,0,1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask,2,7) == 1);
    TEST_REQUIRE(iMask.GetElement(mask,0) == 1);
    TEST_REQUIRE(iMask.GetElement(mask,2) == 7);
    TEST_REQUIRE(iMask.SetElement(mask,3,1) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(error_is("iMask.Set",CONTAINER_ERROR_INDEX));
    TEST_REQUIRE(iMask.GetElement(mask,3) == 0);
    TEST_REQUIRE(error_is("iMask.GetElement",CONTAINER_ERROR_INDEX));
    TEST_REQUIRE(iMask.GetElement(mask,SIZE_MAX) == 0);
    TEST_REQUIRE(error_is("iMask.GetElement",CONTAINER_ERROR_INDEX));
    TEST_REQUIRE(iMask.Clear(mask) == 1);
    TEST_REQUIRE(iMask.Size(mask) == 3);
    for (i = 0; i < 3; ++i)
        TEST_REQUIRE(iMask.GetElement(mask,i) == 0);
    TEST_REQUIRE(iMask.SetElement(mask,2,1) == 1);
    TEST_REQUIRE(iMask.GetElement(mask,2) == 1);
    result = 0;

cleanup:
    if (mask != NULL) iMask.Finalize(mask);
    iError.SetErrorFunction(old_error);
    return result;
}

static int test_boolean_operations(void)
{
    const char left_data[] = { 0, 1, 2, 0 };
    const char right_data[] = { 0, 2, 0, 3 };
    const char and_expected[] = { 0, 0, 0, 0 };
    const char or_expected[] = { 0, 2, 0, 3 };
    Mask *left = NULL;
    Mask *right = NULL;
    Mask *short_mask = NULL;
    ErrorFunction old_error;
    size_t i;
    int result = 1;

    reset_error();
    old_error = iError.SetErrorFunction(capture_error);
    left = iMask.CreateFromMask(sizeof(left_data),left_data);
    right = iMask.CreateFromMask(sizeof(right_data),right_data);
    short_mask = iMask.Create(1);
    TEST_REQUIRE(left != NULL && right != NULL && short_mask != NULL);

    TEST_REQUIRE(iMask.And(left,right) == 1);
    for (i = 0; i < sizeof(and_expected); ++i)
        TEST_REQUIRE(iMask.GetElement(left,i) == and_expected[i]);
    TEST_REQUIRE(iMask.Or(left,right) == 1);
    for (i = 0; i < sizeof(or_expected); ++i)
        TEST_REQUIRE(iMask.GetElement(left,i) == or_expected[i]);
    TEST_REQUIRE(iMask.And(left,left) == 1);
    for (i = 0; i < sizeof(or_expected); ++i)
        TEST_REQUIRE(iMask.GetElement(left,i) == or_expected[i]);
    TEST_REQUIRE(iMask.Not(left) == 1);
    for (i = 0; i < sizeof(or_expected); ++i)
        TEST_REQUIRE(iMask.GetElement(left,i) == (or_expected[i] ? 0 : 1));

    TEST_REQUIRE(iMask.And(left,short_mask) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(error_is("iMask.And",CONTAINER_ERROR_INCOMPATIBLE));
    TEST_REQUIRE(iMask.Or(left,short_mask) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(error_is("iMask.Or",CONTAINER_ERROR_INCOMPATIBLE));
    TEST_REQUIRE(iMask.And(NULL,left) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iMask.And",CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iMask.Or(left,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iMask.Or",CONTAINER_ERROR_BADARG));
    result = 0;

cleanup:
    if (short_mask != NULL) iMask.Finalize(short_mask);
    if (right != NULL) iMask.Finalize(right);
    if (left != NULL) iMask.Finalize(left);
    iError.SetErrorFunction(old_error);
    return result;
}

static int test_allocator_ownership(void)
{
    ContainerAllocator *old_allocator;
    Mask *source = NULL;
    Mask *copy = NULL;
    const char values[] = { 1, 0, 1 };
    int result = 1;

    memset(&allocator_a_stats,0,sizeof(allocator_a_stats));
    memset(&allocator_b_stats,0,sizeof(allocator_b_stats));
    old_allocator = iAllocator.Change(&allocator_a);
    source = iMask.CreateFromMask(sizeof(values),values);
    TEST_REQUIRE(source != NULL);
    TEST_REQUIRE(allocator_a_stats.malloc_calls == 1);
    iAllocator.Change(&allocator_b);
    copy = iMask.Copy(source);
    TEST_REQUIRE(copy != NULL);
    TEST_REQUIRE(allocator_a_stats.malloc_calls == 2);
    TEST_REQUIRE(allocator_b_stats.malloc_calls == 0);
    TEST_REQUIRE(iMask.SetElement(copy,0,0) == 1);
    TEST_REQUIRE(iMask.GetElement(source,0) == 1);
    TEST_REQUIRE(iMask.Finalize(copy) == 1);
    copy = NULL;
    TEST_REQUIRE(allocator_a_stats.free_calls == 1);
    TEST_REQUIRE(allocator_b_stats.free_calls == 0);
    TEST_REQUIRE(iMask.Finalize(source) == 1);
    source = NULL;
    TEST_REQUIRE(allocator_a_stats.free_calls == 2);
    result = 0;

cleanup:
    if (copy != NULL) iMask.Finalize(copy);
    if (source != NULL) iMask.Finalize(source);
    iAllocator.Change(old_allocator);
    return result;
}

static int test_allocation_failures(void)
{
    ContainerAllocator *old_allocator;
    ErrorFunction old_error;
    Mask *mask = NULL;
    Mask *source = NULL;
    Mask *copy = NULL;
    size_t before_calls;
    int result = 1;

    reset_error();
    old_error = iError.SetErrorFunction(capture_error);
    memset(&failing_allocator_stats,0,sizeof(failing_allocator_stats));
    failing_allocator_stats.fail_malloc = 1;
    old_allocator = iAllocator.Change(&failing_allocator);
    mask = iMask.Create(8);
    TEST_REQUIRE(mask == NULL);
    TEST_REQUIRE(failing_allocator_stats.malloc_calls == 1);
    TEST_REQUIRE(error_is("iMask.CreateFromMask",CONTAINER_ERROR_NOMEMORY));
    iAllocator.Change(old_allocator);

    before_calls = failing_allocator_stats.malloc_calls;
    TEST_REQUIRE(iMask.Create(SIZE_MAX) == NULL);
    TEST_REQUIRE(failing_allocator_stats.malloc_calls == before_calls);
    TEST_REQUIRE(error_is("iMask.CreateFromMask",CONTAINER_ERROR_NOMEMORY));

    failing_allocator_stats.fail_malloc = 0;
    old_allocator = iAllocator.Change(&failing_allocator);
    source = iMask.Create(8);
    TEST_REQUIRE(source != NULL);
    failing_allocator_stats.fail_malloc = 1;
    copy = iMask.Copy(source);
    TEST_REQUIRE(copy == NULL);
    TEST_REQUIRE(error_is("iMask.Copy",CONTAINER_ERROR_NOMEMORY));
    iMask.Finalize(source);
    source = NULL;
    iAllocator.Change(old_allocator);
    result = 0;

cleanup:
    if (copy != NULL) iMask.Finalize(copy);
    if (source != NULL) iMask.Finalize(source);
    iAllocator.Change(old_allocator);
    iError.SetErrorFunction(old_error);
    return result;
}

static int test_null_arguments(void)
{
    ErrorFunction old_error;
    int result = 1;

    reset_error();
    old_error = iError.SetErrorFunction(capture_error);
    TEST_REQUIRE(iMask.SetElement(NULL,0,1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iMask.Set",CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iMask.GetElement(NULL,0) == 0);
    TEST_REQUIRE(error_is("iMask.GetElement",CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iMask.Clear(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iMask.Clear",CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iMask.Finalize(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iMask.Finalize",CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iMask.Not(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iMask.Not",CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iMask.PopulationCount(NULL) == 0);
    TEST_REQUIRE(error_is("iMask.PopulationCount",CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iMask.Size(NULL) == 0);
    TEST_REQUIRE(iMask.Sizeof(NULL) == iMask.Sizeof(NULL));
    result = 0;

cleanup:
    iError.SetErrorFunction(old_error);
    return result;
}

static int test_comparison_reuse(void)
{
    int left_values[] = { 1, 2 };
    int right_values[] = { 1, 3 };
    strCollection *strings = NULL;
    strCollection *other_strings = NULL;
    Vector *left = NULL;
    Vector *right = NULL;
    Mask *mask = NULL;
    Mask *result_mask;
    int result = 1;

    left = iVector.Create(sizeof(int),2);
    right = iVector.Create(sizeof(int),2);
    strings = istrCollection.Create(2);
    other_strings = istrCollection.Create(2);
    mask = iMask.Create(2);
    TEST_REQUIRE(left != NULL && right != NULL && strings != NULL &&
                 other_strings != NULL && mask != NULL);
    TEST_REQUIRE(iVector.AddRange(left,2,left_values) == 1);
    TEST_REQUIRE(iVector.AddRange(right,2,right_values) == 1);

    result_mask = iVector.CompareEqual(left,right,mask);
    TEST_REQUIRE(result_mask == mask && iMask.Size(mask) == 2);
    TEST_REQUIRE(iMask.GetElement(mask,0) == 1 && iMask.GetElement(mask,1) == 0);
    result_mask = iVector.CompareEqual(left,left,mask);
    TEST_REQUIRE(result_mask == mask && iMask.Size(mask) == 2);
    TEST_REQUIRE(iMask.GetElement(mask,0) == 1 && iMask.GetElement(mask,1) == 1);

    TEST_REQUIRE(istrCollection.Add(strings,"one") == 1);
    TEST_REQUIRE(istrCollection.Add(strings,"two") == 1);
    TEST_REQUIRE(istrCollection.Add(other_strings,"one") == 1);
    TEST_REQUIRE(istrCollection.Add(other_strings,"different") == 1);
    result_mask = istrCollection.CompareEqual(strings,other_strings,mask);
    TEST_REQUIRE(result_mask == mask && iMask.Size(mask) == 2);
    TEST_REQUIRE(iMask.GetElement(mask,0) == 1 && iMask.GetElement(mask,1) == 0);
    result_mask = istrCollection.CompareEqual(strings,strings,mask);
    TEST_REQUIRE(result_mask == mask && iMask.Size(mask) == 2);
    TEST_REQUIRE(iMask.GetElement(mask,0) == 1 && iMask.GetElement(mask,1) == 1);
    result = 0;

cleanup:
    if (mask != NULL) iMask.Finalize(mask);
    if (other_strings != NULL) istrCollection.Finalize(other_strings);
    if (strings != NULL) istrCollection.Finalize(strings);
    if (right != NULL) iVector.Finalize(right);
    if (left != NULL) iVector.Finalize(left);
    return result;
}

static const TestCase tests[] = {
    { "create values and copy", test_create_values_and_copy },
    { "bounds and clear", test_bounds_and_clear },
    { "boolean operations", test_boolean_operations },
    { "allocator ownership", test_allocator_ownership },
    { "allocation failures", test_allocation_failures },
    { "NULL arguments", test_null_arguments },
    { "comparison reuse", test_comparison_reuse },
};

static const TestSuite suite = {
    "iMask",
    tests,
    sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
