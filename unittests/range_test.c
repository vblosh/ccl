#include "test_support.h"

#include "containers.h"
#include "ccl_internal.h"

#include <stdint.h>
#include <stdlib.h>

static int captured_errors;
static int captured_code;
static int fail_malloc;
static int fail_calloc;
static size_t allocator_calloc_calls;
static size_t allocator_free_calls;
static size_t allocator_malloc_calls;

static void *capture_error(const char *name, int code, ...)
{
    (void)name;
    ++captured_errors;
    captured_code = code;
    return NULL;
}

static void *range_test_malloc(size_t size)
{
    ++allocator_malloc_calls;
    return fail_malloc ? NULL : malloc(size);
}

static void range_test_free(void *memory)
{
    ++allocator_free_calls;
    free(memory);
}

static void *range_test_realloc(void *memory, size_t size)
{
    return realloc(memory, size);
}

static void *range_test_calloc(size_t count, size_t size)
{
    ++allocator_calloc_calls;
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

typedef struct ProtocolSource ProtocolSource;

typedef struct ProtocolIterator {
    Iterator Iterator;
    ProtocolSource *Source;
    size_t Position;
} ProtocolIterator;

struct ProtocolSource {
    GenericContainer Container;
    int Values[4];
    size_t ReportedSize;
    size_t YieldedSize;
    int ReturnNullIterator;
    int MissingFirst;
    int MissingNext;
    int DeleteResult;
};

static void *protocol_first(Iterator *iterator)
{
    ProtocolIterator *current = (ProtocolIterator *)iterator;

    current->Position = 0;
    if (current->Source->YieldedSize == 0)
        return NULL;
    return &current->Source->Values[0];
}

static void *protocol_next(Iterator *iterator)
{
    ProtocolIterator *current = (ProtocolIterator *)iterator;

    if (current->Position + 1 >= current->Source->YieldedSize)
        return NULL;
    ++current->Position;
    return &current->Source->Values[current->Position];
}

static size_t protocol_position(Iterator *iterator)
{
    return ((ProtocolIterator *)iterator)->Position;
}

static size_t protocol_size(const GenericContainer *container)
{
    return ((const ProtocolSource *)container)->ReportedSize;
}

static Iterator *protocol_new_iterator(GenericContainer *container)
{
    ProtocolSource *source = (ProtocolSource *)container;
    ProtocolIterator *iterator;

    if (source->ReturnNullIterator)
        return NULL;
    iterator = calloc(1, sizeof(*iterator));
    if (iterator == NULL)
        return NULL;
    iterator->Iterator.GetFirst = source->MissingFirst ? NULL : protocol_first;
    iterator->Iterator.GetNext = source->MissingNext ? NULL : protocol_next;
    iterator->Iterator.GetPosition = protocol_position;
    iterator->Source = source;
    return &iterator->Iterator;
}

static int protocol_delete_iterator(Iterator *iterator)
{
    ProtocolIterator *current = (ProtocolIterator *)iterator;
    int result = current->Source->DeleteResult;

    free(current);
    return result < 0 ? result : 1;
}

static GenericContainerInterface protocol_interface = {
    .Size = protocol_size,
    .NewIterator = protocol_new_iterator,
    .DeleteIterator = protocol_delete_iterator,
};

static void init_protocol_source(ProtocolSource *source, size_t reported,
                                 size_t yielded)
{
    size_t index;

    memset(source, 0, sizeof(*source));
    source->Container.vTable = &protocol_interface;
    source->ReportedSize = reported;
    source->YieldedSize = yielded;
    for (index = 0; index < sizeof(source->Values) / sizeof(source->Values[0]);
         ++index)
        source->Values[index] = (int)(index + 10);
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

static int test_allocator_aware_container_sources(void)
{
    int values[] = {3, 4};
    Dlist *dlist = NULL;
    Vector *vector = NULL;
    Range *range = NULL;
    RangeCursor *cursor = NULL;
    const void *element = NULL;

    fail_malloc = 0;
    fail_calloc = 0;
    dlist = iDlist.InitializeWith(sizeof(int), 2, values);
    vector = iVector.InitializeWith(sizeof(int), 2, values);
    TEST_REQUIRE(dlist != NULL && vector != NULL);

    allocator_calloc_calls = 0;
    allocator_free_calls = 0;
    allocator_malloc_calls = 0;
    TEST_REQUIRE(iRange.FromSequentialWithAllocator(
                     (SequentialContainer *)dlist, &range_test_allocator,
                     &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1 &&
                 *(const int *)element == 3);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    TEST_REQUIRE(iRange.Finalize(range) == 1);
    range = NULL;
    TEST_REQUIRE(allocator_calloc_calls == 2);
    TEST_REQUIRE(allocator_free_calls == 2);
    TEST_REQUIRE(allocator_malloc_calls == 0);

    allocator_calloc_calls = 0;
    allocator_free_calls = 0;
    TEST_REQUIRE(iRange.FromGenericWithAllocator(
                     (GenericContainer *)vector, sizeof(int),
                     &range_test_allocator, &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1 &&
                 *(const int *)element == 3);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    TEST_REQUIRE(iRange.Finalize(range) == 1);
    range = NULL;
    TEST_REQUIRE(allocator_calloc_calls == 2);
    TEST_REQUIRE(allocator_free_calls == 2);

    iVector.Finalize(vector);
    vector = NULL;
    iDlist.Finalize(dlist);
    return 0;

cleanup:
    fail_malloc = 0;
    fail_calloc = 0;
    if (cursor) iRange.DeleteCursor(cursor);
    if (range) iRange.Finalize(range);
    if (vector) iVector.Finalize(vector);
    if (dlist) iDlist.Finalize(dlist);
    return -1;
}

static int test_generic_protocol_contract(void)
{
    ProtocolSource source;
    Range *range = NULL;
    RangeCursor *cursor = NULL;
    const void *element = NULL;

    init_protocol_source(&source, 2, 2);
    TEST_REQUIRE(iRange.FromGeneric(&source.Container, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1 &&
                 *(const int *)element == 10);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1 &&
                 *(const int *)element == 11);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 0);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    iRange.Finalize(range);
    range = NULL;

    init_protocol_source(&source, 2, 3);
    TEST_REQUIRE(iRange.FromGeneric(&source.Container, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) ==
                 CONTAINER_ERROR_OBJECT_CHANGED);
    TEST_REQUIRE(iRange.Next(cursor, &element) ==
                 CONTAINER_ERROR_OBJECT_CHANGED);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    iRange.Finalize(range);
    range = NULL;

    init_protocol_source(&source, 3, 2);
    TEST_REQUIRE(iRange.FromGeneric(&source.Container, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) == 1);
    TEST_REQUIRE(iRange.Next(cursor, &element) ==
                 CONTAINER_ERROR_OBJECT_CHANGED);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == 1);
    cursor = NULL;
    iRange.Finalize(range);
    range = NULL;

    init_protocol_source(&source, 1, 1);
    source.MissingFirst = 1;
    TEST_REQUIRE(iRange.FromGeneric(&source.Container, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) ==
                 CONTAINER_ERROR_WRONG_ITERATOR);
    TEST_REQUIRE(cursor == NULL);
    iRange.Finalize(range);
    range = NULL;

    init_protocol_source(&source, 1, 1);
    source.MissingNext = 1;
    TEST_REQUIRE(iRange.FromGeneric(&source.Container, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) ==
                 CONTAINER_ERROR_WRONG_ITERATOR);
    TEST_REQUIRE(cursor == NULL);
    iRange.Finalize(range);
    range = NULL;

    init_protocol_source(&source, 1, 1);
    source.ReturnNullIterator = 1;
    TEST_REQUIRE(iRange.FromGeneric(&source.Container, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(cursor == NULL);
    iRange.Finalize(range);
    range = NULL;

    init_protocol_source(&source, 1, 1);
    source.DeleteResult = CONTAINER_ERROR_WRONG_ITERATOR;
    TEST_REQUIRE(iRange.FromGeneric(&source.Container, sizeof(int), &range) == 1);
    TEST_REQUIRE(iRange.Open(range, &cursor) == 1);
    TEST_REQUIRE(iRange.DeleteCursor(cursor) == CONTAINER_ERROR_WRONG_ITERATOR);
    cursor = NULL;
    iRange.Finalize(range);
    return 0;

cleanup:
    if (cursor) iRange.DeleteCursor(cursor);
    if (range) iRange.Finalize(range);
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

static int test_fluent_query(void)
{
    int values[] = {1, 2, 3, 4, 5, 6, 7, 8};
    RangeQuery query = {0};
    RangeQuery invalid = {0};
    RangeQuery terminal = {0};
    Vector *vector = NULL;
    Vector *sentinel = (Vector *)(uintptr_t)1;
    RangeQuery *current;

    current = iRangeQuery.FromArray(&query, values, 8, sizeof(int));
    TEST_REQUIRE(current == &query && current->Error == 0);
    current = current->Where(current, is_even, NULL);
    TEST_REQUIRE(current == &query && current->Error == 0);
    current = current->Select(current, sizeof(int), square, NULL);
    current = current->Skip(current, 1);
    current = current->Take(current, 2);
    current = current->ToVector(current, &vector);
    TEST_REQUIRE(current == &query && query.Error == 0 &&
                 query.ErrorSource == NULL && query.LastResult == 1);
    TEST_REQUIRE(vector != NULL && iVector.Size(vector) == 2);
    TEST_REQUIRE(*(const int *)iVector.GetElement(vector, 0) == 16 &&
                 *(const int *)iVector.GetElement(vector, 1) == 36);
    iVector.Finalize(vector);
    vector = NULL;
    TEST_REQUIRE(query.Finalize(&query) == &query && query.Range == NULL);
    TEST_REQUIRE(query.Finalize(&query) == &query);

    current = iRangeQuery.FromArray(&invalid, values, 2, 0);
    TEST_REQUIRE(current == &invalid && invalid.Error < 0 &&
                 strcmp(invalid.ErrorSource, "iRangeQuery.FromArray") == 0);
    current = current->Where(current, is_even, NULL);
    TEST_REQUIRE(current == &invalid && invalid.LastResult < 0 &&
                 strcmp(invalid.ErrorSource, "iRangeQuery.FromArray") == 0);
    current = current->ToVector(current, &sentinel);
    TEST_REQUIRE(current == &invalid && sentinel == (Vector *)(uintptr_t)1);
    TEST_REQUIRE(invalid.Finalize(&invalid) == &invalid);

    current = iRangeQuery.FromArray(&invalid, values, 2, sizeof(int));
    TEST_REQUIRE(current == &invalid && invalid.Error == 0);
    current = current->Where(current, NULL, NULL);
    TEST_REQUIRE(invalid.Error == CONTAINER_ERROR_BADARG &&
                 strcmp(invalid.ErrorSource, "iRangeQuery.Where") == 0);
    current = current->Take(current, 1);
    TEST_REQUIRE(current == &invalid && invalid.LastResult ==
                 CONTAINER_ERROR_BADARG);
    current->Finalize(current);

    current = iRangeQuery.FromArray(&terminal, values, 2, sizeof(int));
    current = current->Select(current, sizeof(int), zero_transform, NULL);
    current = current->ToVector(current, &vector);
    TEST_REQUIRE(current == &terminal && vector == NULL &&
                 terminal.Error == CONTAINER_ERROR_WRONGELEMENT &&
                 strcmp(terminal.ErrorSource, "iRangeQuery.ToVector") == 0);
    terminal.Finalize(&terminal);
    return 0;

cleanup:
    if (vector) iVector.Finalize(vector);
    query.Finalize(&query);
    invalid.Finalize(&invalid);
    terminal.Finalize(&terminal);
    return -1;
}

static const TestCase tests[] = {
    {"lazy pipeline and reuse", test_lazy_pipeline_and_reuse},
    {"while, concat, and terminals", test_while_concat_and_terminals},
    {"container source and mutation", test_container_source_and_mutation},
    {"allocator-aware container sources", test_allocator_aware_container_sources},
    {"generic protocol contract", test_generic_protocol_contract},
    {"callback and transaction errors", test_callback_and_transaction_errors},
    {"allocator failures and empty range", test_allocator_failures_and_empty_range},
    {"bad arguments", test_bad_arguments},
    {"fluent query", test_fluent_query},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "range", tests, sizeof(tests) / sizeof(tests[0])
    };
    return &suite;
}
