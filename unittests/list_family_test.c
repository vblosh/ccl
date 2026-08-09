#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intlist.h"
#include "doublelist.h"
#include "longlonglist.h"
#include "test_support.h"

static int descending(const void *left, const void *right, CompareInfo *info)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    (void)info;
    return a < b ? 1 : a > b ? -1 : 0;
}

static int add_delta(int *value, void *arg)
{
    *(int *)value += *(const int *)arg;
    return 1;
}

static int scalar_destructor(void *value)
{
    (void)value;
    return 1;
}

static void *quiet_error(const char *operation, int code, ...)
{
    (void)operation;
    (void)code;
    return NULL;
}

typedef struct {
    size_t malloc_calls;
    size_t fail_malloc_at;
    int fail_calloc;
} FailureAllocatorState;

static FailureAllocatorState *failure_allocator_state;

static void *failure_malloc(size_t size)
{
    ++failure_allocator_state->malloc_calls;
    if (failure_allocator_state->fail_malloc_at != 0 &&
        failure_allocator_state->malloc_calls >=
            failure_allocator_state->fail_malloc_at)
        return NULL;
    return malloc(size);
}

static void *failure_calloc(size_t count, size_t size)
{
    if (failure_allocator_state->fail_calloc)
        return NULL;
    return calloc(count, size);
}

static void failure_free(void *ptr)
{
    free(ptr);
}

static void *failure_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static int test_int_scalar_lifecycle(void)
{
    static const int values[] = {20, 10, 30, 1, 2, 3, 4, 5, 6};
    static const int sorted[] = {1, 2, 3, 4, 5, 6, 7, 30};
    intList *list = NULL;
    int popped = 0;
    size_t i;

    list = iintList.InitializeWith(sizeof(values) / sizeof(values[0]), values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iintList.GetElementSize(list) == sizeof(int));
    TEST_REQUIRE(iintList.Size(list) == 9);
    TEST_REQUIRE(iintList.PopFront(list, &popped) == 1 && popped == 20);
    TEST_REQUIRE(iintList.PopFront(list, NULL) == 1);
    TEST_REQUIRE(iintList.Size(list) == 7);
    TEST_REQUIRE(iintList.Add(list, 7) == 1);
    TEST_REQUIRE(iintList.Sort(list) == 1);
    TEST_REQUIRE(iintList.Size(list) == sizeof(sorted) / sizeof(sorted[0]));
    for (i = 0; i < sizeof(sorted) / sizeof(sorted[0]); ++i)
        TEST_REQUIRE(*iintList.GetElement(list, i) == sorted[i]);
    TEST_REQUIRE(iintList.Front(list) != NULL && *iintList.Front(list) == 1);
    TEST_REQUIRE(iintList.Back(list) != NULL && *iintList.Back(list) == 30);
    TEST_REQUIRE(iintList.IndexOf(list, 7, NULL, &i) == 1 && i == 6);
    TEST_REQUIRE(iintList.Contains(list, 30) == 1);
    TEST_REQUIRE(iintList.Contains(list, 999) == 0);
    iintList.Finalize(list);
    return 0;

cleanup:
    if (list != NULL)
        iintList.Finalize(list);
    return -1;
}

static int test_int_sort_compare_and_iterator(void)
{
    static const int values[] = {8, 1, 7, 2, 6, 3, 5, 4, 0};
    intList *list = NULL;
    Iterator *iterator = NULL;
    int replacement = 42;
    size_t i;

    list = iintList.InitializeWith(sizeof(values) / sizeof(values[0]), values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iintList.SetCompareFunction(list, descending) != NULL);
    TEST_REQUIRE(iintList.Sort(list) == 1);
    for (i = 0; i < 9; ++i)
        TEST_REQUIRE(*iintList.GetElement(list, i) == (int)(8 - i));

    iterator = iintList.NewIterator(list);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(*(int *)iterator->GetFirst(iterator) == 8);
    TEST_REQUIRE(iterator->Replace(iterator, &replacement, 1) == 1);
    TEST_REQUIRE(*iintList.GetElement(list, 0) == replacement);
    TEST_REQUIRE(iintList.ReplaceAt(list, 0, 99) == 1);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    iintList.DeleteIterator(iterator);
    iterator = NULL;
    iintList.Finalize(list);
    return 0;

cleanup:
    if (iterator != NULL)
        iintList.DeleteIterator(iterator);
    if (list != NULL)
        iintList.Finalize(list);
    return -1;
}

static int test_int_placement_iterator_and_ranges(void)
{
    static const int values[] = {1, 2, 3, 4, 5};
    intList *list = NULL;
    intList *tail = NULL;
    unsigned char storage[256];
    Iterator *iterator = NULL;
    intListElement *element;

    list = iintList.InitializeWith(5, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iintList.SizeofIterator(list) <= sizeof(storage));
    TEST_REQUIRE(iintList.InitIterator(list, storage) == 1);
    iterator = (Iterator *)storage;
    TEST_REQUIRE(*(int *)iterator->GetFirst(iterator) == 1);
    TEST_REQUIRE(*(int *)iterator->GetNext(iterator) == 2);
    TEST_REQUIRE(iintList.EraseRange(list, 1, 3) == 1);
    TEST_REQUIRE(iintList.Size(list) == 3);
    TEST_REQUIRE(*iintList.GetElement(list, 0) == 1);
    TEST_REQUIRE(*iintList.GetElement(list, 1) == 4);
    TEST_REQUIRE(*iintList.GetElement(list, 2) == 5);

    element = iintList.FirstElement(list);
    TEST_REQUIRE(element != NULL);
    tail = iintList.SplitAfter(list, element);
    TEST_REQUIRE(tail != NULL);
    TEST_REQUIRE(tail->VTable == &iintList);
    TEST_REQUIRE(iintList.Size(list) == 1 && iintList.Size(tail) == 2);
    TEST_REQUIRE(*iintList.Front(tail) == 4);
    TEST_REQUIRE(iintList.GetHeap(tail) == NULL);
    iintList.Finalize(tail);
    iintList.Finalize(list);
    return 0;

cleanup:
    if (tail != NULL)
        iintList.Finalize(tail);
    if (list != NULL)
        iintList.Finalize(list);
    return -1;
}

static int test_scalar_specializations(void)
{
    static const double doubles[] = {-DBL_MAX, -1.0, -0.0, 0.0, DBL_MAX};
    static const long long longs[] = {LLONG_MIN, -1, 0, 1, LLONG_MAX};
    doubleList *dl = NULL;
    longlongList *ll = NULL;
    double d = 0.0;
    long long q = 0;
    size_t i;

    dl = idoubleList.InitializeWith(5, doubles);
    TEST_REQUIRE(dl != NULL);
    TEST_REQUIRE(idoubleList.Contains(dl, -DBL_MAX) == 1);
    TEST_REQUIRE(idoubleList.ReplaceAt(dl, 1, 11.0) == 1);
    TEST_REQUIRE(idoubleList.CopyElement(dl, 1, &d) == 1 && d == 11.0);
    TEST_REQUIRE(idoubleList.Sort(dl) == 1);
    TEST_REQUIRE(*idoubleList.Front(dl) == -DBL_MAX);

    ll = ilonglongList.InitializeWith(5, longs);
    TEST_REQUIRE(ll != NULL);
    TEST_REQUIRE(ilonglongList.Contains(ll, LLONG_MIN) == 1);
    TEST_REQUIRE(ilonglongList.ReplaceAt(ll, 1, 11) == 1);
    TEST_REQUIRE(ilonglongList.CopyElement(ll, 1, &q) == 1 && q == 11);
    TEST_REQUIRE(ilonglongList.Sort(ll) == 1);
    for (i = 1; i < 5; ++i)
        TEST_REQUIRE(*ilonglongList.GetElement(ll, i - 1) <=
                     *ilonglongList.GetElement(ll, i));
    idoubleList.Finalize(dl);
    ilonglongList.Finalize(ll);
    return 0;

cleanup:
    if (dl != NULL)
        idoubleList.Finalize(dl);
    if (ll != NULL)
        ilonglongList.Finalize(ll);
    return -1;
}

static int test_typed_load_rejects_wrong_width(void)
{
    double value = 3.5;
    List *generic = NULL;
    FILE *stream = NULL;
    intList *wrong = NULL;

    generic = iList.InitializeWith(sizeof(value), 1, &value);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(generic != NULL && stream != NULL);
    TEST_REQUIRE(iList.Save(generic, stream, NULL, NULL) == 1);
    rewind(stream);
    wrong = iintList.Load(stream, NULL, NULL);
    TEST_REQUIRE(wrong == NULL);
    fclose(stream);
    iList.Finalize(generic);
    return 0;

cleanup:
    if (wrong != NULL)
        iintList.Finalize(wrong);
    if (stream != NULL)
        fclose(stream);
    if (generic != NULL)
        iList.Finalize(generic);
    return -1;
}

static int test_delegated_surface_and_masks(void)
{
    static const int initial[] = {1, 2, 3, 4};
    static const int sort_values[] = {9, 8, 7, 6, 5, 4, 3, 2, 1};
    intList *list = NULL;
    intList *copy = NULL;
    intList *range = NULL;
    intList *selected = NULL;
    intList *source = NULL;
    intList *destination = NULL;
    intList *heap_list = NULL;
    intList *empty = NULL;
    intList *sort_fail = NULL;
    intListElement *element;
    Mask *mask = NULL;
    FILE *stream = NULL;
    Iterator *failure_iterator = NULL;
    int delta = 1;
    int value;
    ErrorFunction old_error;
    FailureAllocatorState failure_state = {0, 0, 0};
    ContainerAllocator failure_allocator = {
        failure_malloc, failure_free, failure_realloc, failure_calloc
    };

    (void)iintList.Size(NULL);
    TEST_REQUIRE(iintList.Sizeof(NULL) == iList.Sizeof(NULL));
    TEST_REQUIRE(iintList.SizeofIterator(NULL) >= sizeof(Iterator));
    TEST_REQUIRE(iintList.GetAllocator(NULL) == NULL);
    TEST_REQUIRE(iintList.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iintList.InitIterator(NULL, NULL) > 0);
    TEST_REQUIRE(iintList.Sort(NULL) < 0);
    TEST_REQUIRE(iintList.Advance(NULL) == NULL);
    TEST_REQUIRE(iintList.NextElement(NULL) == NULL);
    TEST_REQUIRE(iintList.ElementData(NULL) == NULL);
    TEST_REQUIRE(iintList.Finalize(NULL) < 0);
    TEST_REQUIRE(iintList.Erase(NULL, 1) < 0);
    TEST_REQUIRE(iintList.EraseAll(NULL, 1) < 0);
    TEST_REQUIRE(iintList.Copy(NULL) == NULL);
    TEST_REQUIRE(iintList.SelectCopy(NULL, NULL) == NULL);
    TEST_REQUIRE(iintList.Load(NULL, NULL, NULL) == NULL);
    TEST_REQUIRE(iintList.GetRange(NULL, 0, 1) == NULL);
    TEST_REQUIRE(iintList.SplitAfter(NULL, NULL) == NULL);

    empty = iintList.CreateWithAllocator(NULL);
    TEST_REQUIRE(empty != NULL);
    TEST_REQUIRE(iintList.Sort(empty) == 1);
    TEST_REQUIRE(iintList.PopFront(empty, NULL) == 0);
    TEST_REQUIRE(iintList.EraseRange(empty, 0, 1) == 0);
    iintList.Finalize(empty);
    empty = NULL;

    failure_allocator_state = &failure_state;
    failure_state.fail_calloc = 1;
    TEST_REQUIRE(iintList.CreateWithAllocator(&failure_allocator) == NULL);
    failure_state.fail_calloc = 0;
    sort_fail = iintList.CreateWithAllocator(&failure_allocator);
    TEST_REQUIRE(sort_fail != NULL);
    TEST_REQUIRE(iintList.AddRange(sort_fail, 9, sort_values) == 1);
    /* Refill after Clear to make a known nine-element list for the
     * sort-table allocation path. */
    iintList.Clear(sort_fail);
    failure_state.fail_malloc_at = 0;
    TEST_REQUIRE(iintList.AddRange(sort_fail, 9, sort_values) == 1);
    failure_state.fail_malloc_at = failure_state.malloc_calls + 1;
    TEST_REQUIRE(iintList.Sort(sort_fail) < 0);
    failure_state.fail_malloc_at = 0;
    failure_state.fail_calloc = 1;
    TEST_REQUIRE(iintList.GetRange(sort_fail, 0, 1) == NULL);
    failure_state.fail_calloc = 0;
    failure_iterator = iintList.NewIterator(sort_fail);
    TEST_REQUIRE(failure_iterator != NULL);
    iintList.DeleteIterator(failure_iterator);
    failure_iterator = NULL;
    /* Exercise the typed iterator allocation failure with a live list. */
    failure_state.fail_malloc_at = failure_state.malloc_calls + 1;
    TEST_REQUIRE(iintList.NewIterator(sort_fail) == NULL);
    failure_state.fail_malloc_at = 0;
    iintList.Finalize(sort_fail);
    sort_fail = NULL;

    list = iintList.InitializeWith(4, initial);
    TEST_REQUIRE(list != NULL);
    old_error = iintList.SetErrorFunction(list, quiet_error);
    TEST_REQUIRE(old_error != NULL);
    TEST_REQUIRE(iintList.SetDestructor(list, scalar_destructor) == NULL);
    TEST_REQUIRE(iintList.GetFlags(list) == 0);
    TEST_REQUIRE(iintList.SetFlags(list, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iintList.Add(list, 5) < 0);
    TEST_REQUIRE(iintList.Clear(list) < 0);
    TEST_REQUIRE(iintList.Erase(list, 2) < 0);
    TEST_REQUIRE(iintList.EraseRange(list, 0, 1) < 0);
    TEST_REQUIRE(iintList.Sort(list) < 0);
    TEST_REQUIRE(iintList.SetFlags(list, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(iintList.SetFlags(list, CONTAINER_HAS_OBSERVER) == 0);
    TEST_REQUIRE(iintList.SetFlags(list, 0) == CONTAINER_HAS_OBSERVER);
    TEST_REQUIRE(iintList.AddRange(list, 2, initial) == 1);
    TEST_REQUIRE(iintList.InitIterator(list, NULL) < 0);
    TEST_REQUIRE(iintList.Apply(list, add_delta, &delta) == 1);
    TEST_REQUIRE(iintList.GetElement(list, 0) != NULL);
    TEST_REQUIRE(iintList.CopyElement(list, 0, &value) == 1 && value == 2);

    element = iintList.FirstElement(list);
    TEST_REQUIRE(element != NULL && iintList.LastElement(list) != NULL);
    TEST_REQUIRE(iintList.ElementData(element) != NULL);
    TEST_REQUIRE(iintList.SetElementData(list, element, 10) == 1);
    TEST_REQUIRE(*iintList.ElementData(element) == 10);
    TEST_REQUIRE(iintList.Skip(element, 2) != NULL);
    TEST_REQUIRE(iintList.Advance(&element) != NULL);
    TEST_REQUIRE(iintList.GetElement(list, 0) != NULL);
    TEST_REQUIRE(iintList.ReplaceAt(list, 0, 20) == 1);
    TEST_REQUIRE(iintList.Erase(list, 20) == 1);
    TEST_REQUIRE(iintList.EraseAll(list, 3) >= 0);
    TEST_REQUIRE(iintList.RotateLeft(list, 1) == 1);
    TEST_REQUIRE(iintList.RotateRight(list, 1) == 1);
    TEST_REQUIRE(iintList.Reverse(list) == 1);
    TEST_REQUIRE(iintList.EraseRange(list, 99, 100) == 0);
    TEST_REQUIRE(iintList.EraseRange(list, 2, 1) == 1);
    TEST_REQUIRE(iintList.GetRange(list, 2, 1) == NULL);

    copy = iintList.Copy(list);
    range = iintList.GetRange(list, 0, 2);
    TEST_REQUIRE(copy != NULL && range != NULL);
    TEST_REQUIRE(iintList.Equal(list, copy) == 1);
    TEST_REQUIRE(iintList.Equal(range, range) == 1);
    iintList.Finalize(range);
    range = NULL;

    source = iintList.InitializeWith(2, initial);
    destination = iintList.InitializeWith(2, initial + 2);
    TEST_REQUIRE(source != NULL && destination != NULL);
    TEST_REQUIRE(iintList.InsertIn(destination, 1, source) == 1);
    TEST_REQUIRE(iintList.Size(source) == 2 && iintList.Size(destination) == 4);
    iintList.Finalize(source);
    source = NULL;
    iintList.Finalize(destination);
    destination = NULL;

    source = iintList.InitializeWith(4, initial);
    mask = iMask.Create(4);
    TEST_REQUIRE(source != NULL && mask != NULL);
    TEST_REQUIRE(iMask.SetElement(mask, 0, 1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 1, 0) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 2, 1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 3, 0) == 1);
    selected = iintList.SelectCopy(source, mask);
    TEST_REQUIRE(selected != NULL && iintList.Size(selected) == 2);
    TEST_REQUIRE(iintList.Select(source, mask) == 1 && iintList.Size(source) == 2);
    iintList.Finalize(selected);
    selected = NULL;
    iMask.Finalize(mask);
    mask = NULL;
    iintList.Finalize(source);
    source = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL && iintList.Save(list, stream, NULL, NULL) == 1);
    rewind(stream);
    selected = iintList.Load(stream, NULL, NULL);
    TEST_REQUIRE(selected != NULL && iintList.Size(selected) == iintList.Size(list));
    fclose(stream);
    stream = NULL;
    iintList.Finalize(selected);
    selected = NULL;

    iintList.Finalize(copy);
    copy = NULL;
    iintList.Finalize(list);
    list = NULL;

    heap_list = iintList.Create();
    TEST_REQUIRE(heap_list != NULL && iintList.UseHeap(heap_list, NULL) == 1);
    TEST_REQUIRE(iintList.GetHeap(heap_list) != NULL);
    iintList.Finalize(heap_list);
    heap_list = NULL;
    return 0;

cleanup:
    if (mask != NULL)
        iMask.Finalize(mask);
    if (stream != NULL)
        fclose(stream);
    if (selected != NULL)
        iintList.Finalize(selected);
    if (source != NULL)
        iintList.Finalize(source);
    if (destination != NULL)
        iintList.Finalize(destination);
    if (range != NULL)
        iintList.Finalize(range);
    if (copy != NULL)
        iintList.Finalize(copy);
    if (list != NULL)
        iintList.Finalize(list);
    if (heap_list != NULL)
        iintList.Finalize(heap_list);
    if (failure_iterator != NULL)
        iintList.DeleteIterator(failure_iterator);
    return -1;
}

static const TestCase tests[] = {
    {"int scalar lifecycle", test_int_scalar_lifecycle},
    {"int sort compare and iterator", test_int_sort_compare_and_iterator},
    {"int placement iterator and ranges", test_int_placement_iterator_and_ranges},
    {"scalar specializations", test_scalar_specializations},
    {"typed load width boundary", test_typed_load_rejects_wrong_width},
    {"delegated surface and masks", test_delegated_surface_and_masks}
};

static const TestSuite suite = {
    "typed list generator family",
    tests,
    sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
