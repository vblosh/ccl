#include "test_support.h"

#include "containers.h"

#include <stdint.h>
#include <stdlib.h>

static int captured_errors;
static int captured_code;
static int fail_malloc;
static int fail_calloc;

static void *capture_error(const char *name, int code, ...)
{
    (void)name;
    ++captured_errors;
    captured_code = code;
    return NULL;
}

static void *range_test_malloc(size_t size)
{
    return fail_malloc ? NULL : malloc(size);
}

static void range_test_free(void *memory)
{
    free(memory);
}

static void *range_test_realloc(void *memory, size_t size)
{
    return realloc(memory, size);
}

static void *range_test_calloc(size_t count, size_t size)
{
    return fail_calloc ? NULL : calloc(count, size);
}

static ContainerAllocator range_test_allocator = {
    range_test_malloc,
    range_test_free,
    range_test_realloc,
    range_test_calloc,
};

static int is_even(const void *element, void *arg)
{
    (void)arg;
    return (*(const int *)element % 2) == 0;
}

static int is_positive(const void *element, void *arg)
{
    (void)arg;
    return *(const int *)element > 0;
}

static int is_zero(const void *element, void *arg)
{
    (void)arg;
    return *(const int *)element == 0;
}

static int is_less_than(const void *element, void *arg)
{
    return *(const int *)element < *(const int *)arg;
}

static int is_equal_to(const void *element, void *arg)
{
    return *(const int *)element == *(const int *)arg;
}

static int predicate_error(const void *element, void *arg)
{
    (void)element;
    (void)arg;
    return CONTAINER_ERROR_BADMASK;
}

static int square(const void *input, void *output, void *arg)
{
    int value = *(const int *)input;

    (void)arg;
    *(int *)output = value * value;
    return 1;
}

static int zero_transform(const void *input, void *output, void *arg)
{
    (void)input;
    (void)output;
    (void)arg;
    return 0;
}

static int sum_fold(void *accumulator, const void *element, void *arg)
{
    (void)arg;
    *(int *)accumulator += *(const int *)element;
    return 1;
}

static int zero_fold(void *accumulator, const void *element, void *arg)
{
    (void)accumulator;
    (void)element;
    (void)arg;
    return 0;
}

typedef struct VisitState {
    int Values[8];
    size_t Count;
    size_t StopAfter;
} VisitState;

static int collect_values(const void *element, void *arg)
{
    VisitState *state = arg;

    state->Values[state->Count++] = *(const int *)element;
    return state->StopAfter == 0 || state->Count < state->StopAfter;
}

static int test_lazy_pipeline_and_reuse(void)
{
    int values[] = {1, 2, 3, 4, 5, 6, 7, 8};
    Range *range = NULL;
    RangeCursor *cursor = NULL;
    Vector *materialized = NULL;
    const void *element = NULL;

    TEST_REQUIRE(iRange.FromArray(values, 8, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Filter(&range, is_even, NULL) == 1);
    TEST_REQUIRE(iRange.Transform(&range, sizeof(int), square, NULL) == 1);
    TEST_REQUIRE(iRange.Drop(&range, 1) == 1);
    TEST_REQUIRE(iRange.Take(&range, 2) == 1);
    TEST_REQUIRE(iRange.GetElementSize(range) == sizeof(int));

    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1 &&
                 *(const int *)element == 16);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1 &&
                 *(const int *)element == 36);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 0 && element == NULL);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 0);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;

    /* A range is a reusable recipe; a terminal opens an independent cursor. */
    TEST_REQUIRE(iRange.ToVector(range, &materialized) == 1);
    TEST_REQUIRE(materialized != NULL && iVector.Size(materialized) == 2);
    TEST_REQUIRE(*(int *)iVector.GetElement(materialized, 0) == 16);
    TEST_REQUIRE(*(int *)iVector.GetElement(materialized, 1) == 36);

    iVector.Finalize(materialized);
    materialized = NULL;
    TEST_REQUIRE(iRange.Finalize(range) == 1);
    return 0;

cleanup:
    if (cursor) iRange.DeleteCursor(cursor);
    if (materialized) iVector.Finalize(materialized);
    if (range) iRange.Finalize(range);
    return -1;
}

static int test_while_concat_and_terminals(void)
{
    int leftValues[] = {1, 2, 3, 4};
    int rightValues[] = {0, 0, 5, 6};
    int limit = 3;
    int needle = 5;
    int found = -1;
    int sum = 0;
    size_t count = 99;
    Range *left = NULL;
    Range *right = NULL;
    Vector *vector = NULL;
    VisitState visited = {{0}, 0, 0};

    TEST_REQUIRE(iRange.FromArray(leftValues, 4, sizeof(int), &left) == 1);
    TEST_REQUIRE(iRange.TakeWhile(&left, is_less_than, &limit) == 1);
    TEST_REQUIRE(iRange.FromArray(rightValues, 4, sizeof(int), &right) == 1);
    TEST_REQUIRE(iRange.DropWhile(&right, is_zero, NULL) == 1);
    TEST_REQUIRE(iRange.Concat(&left, &right) == 1 && right == NULL);

    TEST_REQUIRE(iRange.ForEach(left, collect_values, &visited) == 1);
    TEST_REQUIRE(visited.Count == 4 && visited.Values[0] == 1 &&
                 visited.Values[1] == 2 && visited.Values[2] == 5 &&
                 visited.Values[3] == 6);
    TEST_REQUIRE(iRange.FindIf(left, is_equal_to, &needle, &found) == 1 &&
                 found == 5);
    needle = 9;
    TEST_REQUIRE(iRange.FindIf(left, is_equal_to, &needle, &found) == 0);
    TEST_REQUIRE(iRange.AnyOf(left, is_even, NULL) == 1);
    TEST_REQUIRE(iRange.AllOf(left, is_positive, NULL) == 1);
    TEST_REQUIRE(iRange.CountIf(left, is_even, NULL, &count) == 1 && count == 2);
    TEST_REQUIRE(iRange.Fold(left, &sum, sum_fold, NULL) == 1 && sum == 14);
    TEST_REQUIRE(iRange.ToVector(left, &vector) == 1 && iVector.Size(vector) == 4);

    iVector.Finalize(vector);
    vector = NULL;
    TEST_REQUIRE(iRange.Finalize(left) == 1);
    return 0;

cleanup:
    if (vector) iVector.Finalize(vector);
    if (right) iRange.Finalize(right);
    if (left) iRange.Finalize(left);
    return -1;
}

static int test_container_source_and_mutation(void)
{
    int values[] = {5, 6, 7};
    Vector *vector = NULL;
    Range *range = NULL;
    Range *generic = NULL;
    RangeCursor *cursor = NULL;
    const void *element = NULL;
    VisitState visited = {{0}, 0, 2};
    int result;

    vector = iVector.InitializeWith(sizeof(int), 3, values);
    TEST_REQUIRE(vector != NULL);
    TEST_REQUIRE(iRange.FromSequential((SequentialContainer *)vector,
                                       &range) == 1);
    TEST_REQUIRE(iRange.GetElementSize(range) == sizeof(int));
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1 &&
                 *(const int *)element == 5);
    TEST_REQUIRE(iVector.Add(vector, &(int){8}) == 1);
    result = iRange.Next(cursor, &element);
    TEST_REQUIRE(result == CONTAINER_ERROR_OBJECT_CHANGED);
    TEST_REQUIRE(iRange.Next(cursor, &element) == result); /* sticky error */
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;

    TEST_REQUIRE(iRange.ForEach(range, collect_values, &visited) == 0);
    TEST_REQUIRE(visited.Count == 2);
    TEST_REQUIRE(iRange.FromGeneric((GenericContainer *)vector, sizeof(int),
                                    &generic) == 1);
    TEST_REQUIRE(iRange.AnyOf(generic, is_even, NULL) == 1);

    iRange.Finalize(generic);
    generic = NULL;
    iRange.Finalize(range);
    range = NULL;
    iVector.Finalize(vector);
    return 0;

cleanup:
    if (cursor) iRange.DeleteCursor(cursor);
    if (generic) iRange.Finalize(generic);
    if (range) iRange.Finalize(range);
    if (vector) iVector.Finalize(vector);
    return -1;
}

static int test_callback_and_transaction_errors(void)
{
    int values[] = {1, 2};
    int shortValues[] = {1};
    Range *range = NULL;
    Range *original;
    Range *other = NULL;
    RangeCursor *cursor = NULL;
    const void *element = NULL;
    ErrorFunction oldError;
    int accumulator = 0;

    captured_errors = 0;
    captured_code = 0;
    TEST_REQUIRE(iRange.FromArray(values, 2, sizeof(int), &range) == 1);
    oldError = iRange.SetErrorFunction(range, capture_error);
    TEST_REQUIRE(oldError != NULL);
    original = range;
    TEST_REQUIRE(iRange.Filter(&range, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(range == original && captured_code == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Filter(&range, predicate_error, NULL) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == CONTAINER_ERROR_BADMASK);
    TEST_REQUIRE(iRange.Next(cursor, &element) == CONTAINER_ERROR_BADMASK);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    iRange.Finalize(range);
    range = NULL;

    TEST_REQUIRE(iRange.FromArray(values, 2, sizeof(int), &range) == 1);
    iRange.SetErrorFunction(range, capture_error);
    TEST_REQUIRE(iRange.Transform(&range, sizeof(int), zero_transform, NULL) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == CONTAINER_ERROR_WRONGELEMENT);
    TEST_REQUIRE(captured_code == CONTAINER_ERROR_WRONGELEMENT);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    iRange.Finalize(range);
    range = NULL;

    TEST_REQUIRE(iRange.FromArray(values, 2, sizeof(int), &range) == 1);
    iRange.SetErrorFunction(range, capture_error);
    TEST_REQUIRE(iRange.Fold(range, &accumulator, zero_fold, NULL) ==
                 CONTAINER_ERROR_WRONGELEMENT);
    TEST_REQUIRE(iRange.FromArray(shortValues, 1, sizeof(short), &other) == 1);
    original = range;
    TEST_REQUIRE(iRange.Concat(&range, &other) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(range == original && other != NULL);

    iRange.Finalize(other);
    other = NULL;
    iRange.Finalize(range);
    return 0;

cleanup:
    if (cursor) iRange.DeleteCursor(cursor);
    if (other) iRange.Finalize(other);
    if (range) iRange.Finalize(range);
    return -1;
}

static int test_allocator_failures_and_empty_range(void)
{
    int values[] = {1, 2};
    Range *range = NULL;
    Range *original;
    RangeCursor *cursor = NULL;
    Vector *vector = NULL;

    fail_malloc = 0;
    fail_calloc = 1;
    TEST_REQUIRE(iRange.FromArrayWithAllocator(values, 2, sizeof(int),
                                               &range_test_allocator,
                                               &range) ==
                 CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(range == NULL);

    fail_calloc = 0;
    TEST_REQUIRE(iRange.FromArrayWithAllocator(values, 2, sizeof(int),
                                               &range_test_allocator,
                                               &range) == 1);
    original = range;
    fail_calloc = 1;
    TEST_REQUIRE(iRange.Filter(&range, is_even, NULL) ==
                 CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(range == original);
    fail_calloc = 0;
    TEST_REQUIRE(iRange.Transform(&range, sizeof(int), square, NULL) == 1);
    fail_malloc = 1;
    TEST_REQUIRE(iRange.Open(range, &cursor) == CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(cursor == NULL);
    fail_malloc = 0;
    iRange.Finalize(range);
    range = NULL;

    TEST_REQUIRE(iRange.FromArray(NULL, 0, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.AnyOf(range, is_even, NULL) == 0);
    TEST_REQUIRE(iRange.AllOf(range, is_even, NULL) == 1);
    TEST_REQUIRE(iRange.ToVector(range, &vector) == 1);
    TEST_REQUIRE(vector != NULL && iVector.Size(vector) == 0);
    iVector.Finalize(vector);
    vector = NULL;
    iRange.Finalize(range);
    return 0;

cleanup:
    fail_malloc = 0;
    fail_calloc = 0;
    if (cursor) iRange.DeleteCursor(cursor);
    if (vector) iVector.Finalize(vector);
    if (range) iRange.Finalize(range);
    return -1;
}

static int test_bad_arguments(void)
{
    int values[] = {1};
    int output = 0;
    size_t count = 0;
    ContainerAllocator badAllocator = range_test_allocator;
    Range *range = NULL;
    Range *alias = NULL;
    Range *temporary = NULL;
    Range *unknown = NULL;
    RangeCursor *cursor = NULL;
    Vector *opaqueVector = NULL;
    Vector *vector = (Vector *)(uintptr_t)1;
    const void *element = NULL;
    ErrorFunction old = iError.SetErrorFunction(capture_error);

    TEST_REQUIRE(iRange.FromArray(values, 1, 0, &range) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FromArray(NULL, 1, sizeof(int), &range) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FromArray(values, SIZE_MAX, 2, &range) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FromArray(values, 1, sizeof(int), NULL) == CONTAINER_ERROR_BADARG);
    badAllocator.malloc = NULL;
    TEST_REQUIRE(iRange.FromArrayWithAllocator(values, 1, sizeof(int),
                                               &badAllocator, &temporary) ==
                 CONTAINER_ERROR_BADARG);
    badAllocator = range_test_allocator;
    badAllocator.free = NULL;
    TEST_REQUIRE(iRange.FromArrayWithAllocator(values, 1, sizeof(int),
                                               &badAllocator, &temporary) ==
                 CONTAINER_ERROR_BADARG);
    badAllocator = range_test_allocator;
    badAllocator.realloc = NULL;
    TEST_REQUIRE(iRange.FromArrayWithAllocator(values, 1, sizeof(int),
                                               &badAllocator, &temporary) ==
                 CONTAINER_ERROR_BADARG);
    badAllocator = range_test_allocator;
    badAllocator.calloc = NULL;
    TEST_REQUIRE(iRange.FromArrayWithAllocator(values, 1, sizeof(int),
                                               &badAllocator, &temporary) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FromArrayWithAllocator(values, 1, sizeof(int), NULL,
                                               &temporary) == 1);
    iRange.Finalize(temporary);
    temporary = NULL;
    TEST_REQUIRE(iRange.FromSequential(NULL, &range) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FromGeneric(NULL, sizeof(int), &range) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Filter(NULL, is_even, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Filter(&temporary, is_even, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Transform(NULL, sizeof(int), square, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Take(NULL, 1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Drop(NULL, 1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.TakeWhile(NULL, is_even, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.DropWhile(NULL, is_even, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Concat(NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Open(NULL, &cursor) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Open(NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Next(NULL, &element) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.DeleteCursor(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.GetElementSize(NULL) == 0);
    TEST_REQUIRE(iRange.SetErrorFunction(NULL, capture_error) == capture_error);
    TEST_REQUIRE(iRange.ForEach(NULL, collect_values, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Fold(NULL, &output, sum_fold, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FindIf(NULL, is_even, NULL, &output) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.AnyOf(NULL, is_even, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.AllOf(NULL, is_even, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.CountIf(NULL, is_even, NULL, &count) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.ToVector(NULL, &vector) == CONTAINER_ERROR_BADARG && vector == NULL);
    TEST_REQUIRE(iRange.Finalize(NULL) == CONTAINER_ERROR_BADARG);

    TEST_REQUIRE(iRange.FromArray(values, 1, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Transform(&range, 0, square, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Transform(&range, sizeof(int), NULL, NULL) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Concat(&range, &range) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Open(range, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Next(cursor, &element) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    TEST_REQUIRE(iRange.CountIf(range, is_even, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.ForEach(range, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Fold(range, NULL, sum_fold, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Fold(range, &output, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FindIf(range, NULL, NULL, &output) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FindIf(range, is_even, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.AnyOf(range, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.AllOf(range, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.CountIf(range, NULL, NULL, &count) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.SetErrorFunction(range, NULL) != NULL);
    alias = range;
    TEST_REQUIRE(iRange.Take(&range, 1) == 1);
    TEST_REQUIRE(iRange.Filter(&alias, is_even, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Open(alias, &cursor) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.Finalize(alias) == CONTAINER_ERROR_BADARG);
    alias = NULL;

    /* Size zero is allowed for an opaque generic source, but copying
     * terminals reject it explicitly. */
    opaqueVector = iVector.Create(sizeof(int), 1);
    TEST_REQUIRE(opaqueVector != NULL);
    TEST_REQUIRE(iRange.FromGenericWithAllocator((GenericContainer *)opaqueVector,
                                                 sizeof(int), NULL, NULL) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FromSequential((SequentialContainer *)opaqueVector,
                                       NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRange.FromGeneric((GenericContainer *)opaqueVector, 0,
                                    &unknown) == 1);
    TEST_REQUIRE(iRange.FindIf(unknown, is_even, NULL, &output) ==
                 CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iRange.ToVector(unknown, &vector) ==
                 CONTAINER_ERROR_INCOMPATIBLE && vector == NULL);
    iRange.Finalize(unknown);
    unknown = NULL;
    iVector.Finalize(opaqueVector);
    opaqueVector = NULL;
    iRange.Finalize(range);
    iError.SetErrorFunction(old);
    return 0;

cleanup:
    if (cursor) iRange.DeleteCursor(cursor);
    if (temporary) iRange.Finalize(temporary);
    if (unknown) iRange.Finalize(unknown);
    if (opaqueVector) iVector.Finalize(opaqueVector);
    if (range) iRange.Finalize(range);
    iError.SetErrorFunction(old);
    return -1;
}

static const TestCase tests[] = {
    {"lazy pipeline and reuse", test_lazy_pipeline_and_reuse},
    {"while, concat, and terminals", test_while_concat_and_terminals},
    {"container source and mutation", test_container_source_and_mutation},
    {"callback and transaction errors", test_callback_and_transaction_errors},
    {"allocator failures and empty range", test_allocator_failures_and_empty_range},
    {"bad arguments", test_bad_arguments},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "range", tests, sizeof(tests) / sizeof(tests[0])
    };
    return &suite;
}
