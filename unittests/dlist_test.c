#include "test_support.h"

#include "containers.h"
#include "ccl_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned destructor_calls;
static size_t allocation_calls;
static size_t fail_at;

static int count_destructor(void *data)
{
    (void)data;
    ++destructor_calls;
    return 1;
}

static void *count_malloc(size_t size)
{
    ++allocation_calls;
    if (fail_at != 0 && allocation_calls >= fail_at)
        return NULL;
    return malloc(size);
}

static void *count_calloc(size_t n, size_t size)
{
    ++allocation_calls;
    if (fail_at != 0 && allocation_calls >= fail_at)
        return NULL;
    return calloc(n, size);
}

static void *count_realloc(void *p, size_t size)
{
    return realloc(p, size);
}

static void count_free(void *p)
{
    free(p);
}

static int reverse_int_compare(const void *left, const void *right,
                               CompareInfo *info)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    (void)info;
    return a < b ? 1 : a > b ? -1 : 0;
}

static int stop_after_first(void *data, void *arg)
{
    unsigned *calls = arg;
    (void)data;
    ++*calls;
    return *calls < 2;
}

static int save_scalar(const void *element, void *arg, FILE *stream)
{
    (void)arg;
    return fwrite(element, sizeof(int), 1, stream) == 1 ? 1 : 0;
}

static int load_scalar(void *element, void *arg, FILE *stream)
{
    (void)arg;
    return fread(element, sizeof(int), 1, stream) == 1 ? 1 : 0;
}

static unsigned observer_events;

static void observe_all(const void *object, unsigned operation,
                        const void *extra[])
{
    (void)object;
    (void)operation;
    (void)extra;
    ++observer_events;
}

static int assert_links(const Dlist *list)
{
    const DlistElement *node;
    const DlistElement *previous = NULL;
    size_t count = 0;

    if (list->count == 0)
        return list->First == NULL && list->Last == NULL;
    if (list->First == NULL || list->Last == NULL ||
        list->First->Previous != NULL || list->Last->Next != NULL)
        return 0;
    for (node = list->First; node != NULL; node = node->Next) {
        if (node->Previous != previous || ++count > list->count)
            return 0;
        previous = node;
    }
    if (previous != list->Last || count != list->count)
        return 0;
    count = 0;
    previous = NULL;
    for (node = list->Last; node != NULL; node = node->Previous) {
        if (node->Next != previous || ++count > list->count)
            return 0;
        previous = node;
    }
    return previous == list->First && count == list->count;
}

static int test_lifecycle_and_erase(void)
{
    int values[] = {1, 2, 2, 3, 2, 4};
    int out;
    Dlist *list = NULL;

    destructor_calls = 0;
    list = iDlist.InitializeWith(sizeof(int), 6, values);
    TEST_REQUIRE(list != NULL && assert_links(list));
    iDlist.SetDestructor(list, count_destructor);
    TEST_REQUIRE(iDlist.PopFront(list, &out) == 1 && out == 1);
    TEST_REQUIRE(iDlist.PopBack(list, NULL) == 1);
    TEST_REQUIRE(destructor_calls == 1 && iDlist.Size(list) == 4);
    TEST_REQUIRE(iDlist.EraseAt(list, 3) == 1);
    TEST_REQUIRE(assert_links(list));
    TEST_REQUIRE(iDlist.EraseAll(list, &values[1]) == 1);
    TEST_REQUIRE(iDlist.Size(list) == 1 && destructor_calls == 4);
    TEST_REQUIRE(iDlist.RemoveRange(list, 0, 1) == 1);
    TEST_REQUIRE(iDlist.Size(list) == 0 && assert_links(list));
    TEST_REQUIRE(destructor_calls == 5);
    TEST_REQUIRE(iDlist.Finalize(list) == 1);
    list = NULL;
    return 0;

cleanup:
    if (list != NULL)
        iDlist.Finalize(list);
    return -1;
}

static int test_ranges_and_transfers(void)
{
    int values[] = {1, 2, 3, 4};
    int insert_values[] = {8, 9};
    Dlist *list = NULL;
    Dlist *source = NULL;
    Dlist *tail = NULL;
    DlistElement *at;

    list = iDlist.InitializeWith(sizeof(int), 4, values);
    source = iDlist.InitializeWith(sizeof(int), 2, insert_values);
    TEST_REQUIRE(list != NULL && source != NULL);
    TEST_REQUIRE(iDlist.InsertIn(list, 0, source) == 1);
    TEST_REQUIRE(iDlist.Size(list) == 6 && iDlist.Size(source) == 2);
    TEST_REQUIRE(*(int *)iDlist.Front(list) == 8);
    TEST_REQUIRE(iDlist.InsertIn(list, iDlist.Size(list), source) == 1);
    TEST_REQUIRE(iDlist.Size(list) == 8 && *(int *)iDlist.Back(list) == 9);
    TEST_REQUIRE(assert_links(list));
    iDlist.Finalize(source);
    source = NULL;

    source = iDlist.InitializeWith(sizeof(int), 2, insert_values);
    TEST_REQUIRE(source != NULL);
    TEST_REQUIRE(iDlist.Splice(list, iDlist.FirstElement(list), source, 0) == list);
    TEST_REQUIRE(iDlist.Size(source) == 0 && iDlist.Size(list) == 10);
    TEST_REQUIRE(*(int *)iDlist.Front(list) == 8);
    TEST_REQUIRE(assert_links(list));
    iDlist.Finalize(source);
    source = NULL;

    source = iDlist.InitializeWith(sizeof(int), 2, insert_values);
    TEST_REQUIRE(source != NULL);
    TEST_REQUIRE(iDlist.Append(list, source) == 1);
    source = NULL;
    TEST_REQUIRE(iDlist.Size(list) == 12 && assert_links(list));

    at = iDlist.FirstElement(list);
    tail = iDlist.SplitAfter(list, at);
    TEST_REQUIRE(tail != NULL && iDlist.Size(list) == 1 &&
                 iDlist.Size(tail) == 11 && assert_links(list) &&
                 assert_links(tail));
    iDlist.Finalize(tail);
    tail = NULL;
    iDlist.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    if (tail != NULL)
        iDlist.Finalize(tail);
    if (source != NULL)
        iDlist.Finalize(source);
    if (list != NULL)
        iDlist.Finalize(list);
    return -1;
}

static int test_heap_readonly_and_direct_nodes(void)
{
    int values[] = {1, 2, 3, 4};
    int replacement = 12;
    Dlist *list = NULL;
    Dlist *foreign = NULL;
    DlistElement *node;

    destructor_calls = 0;
    list = iDlist.Create(sizeof(int));
    foreign = iDlist.Create(sizeof(int));
    TEST_REQUIRE(list != NULL && foreign != NULL);
    iDlist.SetDestructor(list, count_destructor);
    TEST_REQUIRE(iDlist.Add(foreign, &values[0]) == 1);
    TEST_REQUIRE(iDlist.UseHeap(list, NULL) == 1);
    TEST_REQUIRE(iDlist.AddRange(list, 4, values) == 1);
    TEST_REQUIRE(iDlist.RemoveRange(list, 1, 3) == 1);
    TEST_REQUIRE(destructor_calls == 2 && iDlist.Size(list) == 2);
    node = iDlist.FirstElement(list);
    TEST_REQUIRE(node != NULL && iDlist.SetElementData(list, node, &replacement) == 1);
    TEST_REQUIRE(iDlist.SetElementData(list, iDlist.FirstElement(foreign),
                                       &replacement) == CONTAINER_ERROR_WRONGELEMENT);
    TEST_REQUIRE(iDlist.SetFlags(list, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iDlist.Clear(list) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.Erase(list, &replacement) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.SetFlags(list, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(iDlist.Clear(list) == 1 && destructor_calls == 5);
    iDlist.Finalize(foreign);
    foreign = NULL;
    iDlist.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    if (foreign != NULL)
        iDlist.Finalize(foreign);
    if (list != NULL)
        iDlist.Finalize(list);
    return -1;
}

static int test_iterators_and_readonly_buffer(void)
{
    unsigned char values[3][32];
    unsigned char storage[256];
    Dlist *list = NULL;
    Iterator *iterator = NULL;
    Iterator *heap_iterator = NULL;
    unsigned char *p;

    memset(values, 0, sizeof(values));
    strcpy((char *)values[0], "one");
    strcpy((char *)values[1], "two");
    strcpy((char *)values[2], "three");
    list = iDlist.InitializeWith(sizeof(values[0]), 3, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iDlist.SizeofIterator(list) <= sizeof(storage));
    TEST_REQUIRE(iDlist.InitIterator(list, storage) == 1);
    iterator = (Iterator *)storage;
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(strcmp((char *)iterator->GetFirst(iterator), "one") == 0);
    TEST_REQUIRE(iterator->GetPosition(iterator) == 0);
    TEST_REQUIRE(strcmp((char *)iterator->GetNext(iterator), "two") == 0);
    TEST_REQUIRE(strcmp((char *)iterator->GetPrevious(iterator), "one") == 0);
    TEST_REQUIRE(strcmp((char *)iterator->GetLast(iterator), "three") == 0);
    p = iterator->Seek(iterator, 1);
    TEST_REQUIRE(p != NULL && strcmp((char *)p, "two") == 0);
    TEST_REQUIRE(iterator->Seek(iterator, 3) == NULL);
    TEST_REQUIRE(iDlist.SetFlags(list, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iDlist.InitIterator(list, storage) == 1);
    iterator = (Iterator *)storage;
    p = iterator->GetFirst(iterator);
    TEST_REQUIRE(p != NULL && p != (unsigned char *)list->First->Data &&
                 strcmp((char *)p, "one") == 0);
    heap_iterator = iDlist.NewIterator(list);
    TEST_REQUIRE(heap_iterator != NULL);
    TEST_REQUIRE(iDlist.DeleteIterator(heap_iterator) == 1);
    heap_iterator = NULL;
    /* Placement storage is caller-owned and DeleteIterator leaves it alone. */
    iDlist.SetFlags(list, 0);
    iDlist.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    if (heap_iterator != NULL)
        iDlist.DeleteIterator(heap_iterator);
    if (list != NULL)
        iDlist.Finalize(list);
    return -1;
}

static int test_api_edges_and_error_paths(void)
{
    int values[] = {3, 1, 4, 1, 5};
    int replacement = 8;
    int out = 0;
    unsigned calls = 0;
    size_t index = 0;
    Dlist *a = NULL;
    Dlist *b = NULL;
    Dlist *empty = NULL;
    Dlist *copy = NULL;
    Dlist *range = NULL;
    Dlist *selected = NULL;
    Dlist *heap = NULL;
    DlistElement *element;
    DlistElement foreign_element;
    Mask *mask = NULL;
    Iterator bad_iterator;
    unsigned char iterator_storage[256];
    FILE *stream = NULL;

    /* Every public entry point should reject a null header or argument
     * without dereferencing it. */
    (void)iDlist.Size(NULL);
    (void)iDlist.GetFlags(NULL);
    (void)iDlist.SetFlags(NULL, 0);
    (void)iDlist.Clear(NULL);
    (void)iDlist.Contains(NULL, &replacement);
    (void)iDlist.Erase(NULL, &replacement);
    (void)iDlist.EraseAll(NULL, &replacement);
    (void)iDlist.Apply(NULL, stop_after_first, &calls);
    (void)iDlist.Equal(NULL, NULL);
    (void)iDlist.Copy(NULL);
    (void)iDlist.SetErrorFunction(NULL, NULL);
    (void)iDlist.Sizeof(NULL);
    (void)iDlist.NewIterator(NULL);
    (void)iDlist.InitIterator(NULL, iterator_storage);
    (void)iDlist.DeleteIterator(NULL);
    (void)iDlist.SizeofIterator(NULL);
    (void)iDlist.Save(NULL, NULL, NULL, NULL);
    (void)iDlist.Load(NULL, NULL, NULL);
    (void)iDlist.GetElementSize(NULL);
    (void)iDlist.Add(NULL, &replacement);
    (void)iDlist.GetElement(NULL, 0);
    (void)iDlist.PushFront(NULL, &replacement);
    (void)iDlist.PopFront(NULL, &out);
    (void)iDlist.InsertAt(NULL, 0, &replacement);
    (void)iDlist.EraseAt(NULL, 0);
    (void)iDlist.ReplaceAt(NULL, 0, &replacement);
    (void)iDlist.IndexOf(NULL, &replacement, NULL, &index);
    (void)iDlist.PushBack(NULL, &replacement);
    (void)iDlist.PopBack(NULL, &out);
    (void)iDlist.Splice(NULL, NULL, NULL, 0);
    (void)iDlist.Sort(NULL);
    (void)iDlist.Reverse(NULL);
    (void)iDlist.GetRange(NULL, 0, 1);
    (void)iDlist.Append(NULL, NULL);
    (void)iDlist.SetCompareFunction(NULL, reverse_int_compare);
    (void)iDlist.UseHeap(NULL, NULL);
    (void)iDlist.AddRange(NULL, 1, &replacement);
    (void)iDlist.CopyElement(NULL, 0, &out);
    (void)iDlist.InsertIn(NULL, 0, NULL);
    (void)iDlist.SetDestructor(NULL, count_destructor);
    (void)iDlist.InitializeWith(sizeof(int), 1, NULL);
    (void)iDlist.GetAllocator(NULL);
    (void)iDlist.Back(NULL);
    (void)iDlist.Front(NULL);
    (void)iDlist.RemoveRange(NULL, 0, 1);
    (void)iDlist.RotateLeft(NULL, 1);
    (void)iDlist.RotateRight(NULL, 1);
    (void)iDlist.Select(NULL, NULL);
    (void)iDlist.SelectCopy(NULL, NULL);
    (void)iDlist.FirstElement(NULL);
    (void)iDlist.LastElement(NULL);
    (void)iDlist.NextElement(NULL);
    (void)iDlist.PreviousElement(NULL);
    (void)iDlist.GetElementData(NULL);
    (void)iDlist.SetElementData(NULL, NULL, &replacement);
    (void)iDlist.Advance(NULL);
    (void)iDlist.Skip(NULL, 1);
    (void)iDlist.MoveBack(NULL);
    (void)iDlist.SplitAfter(NULL, NULL);

    memset(&bad_iterator, 0, sizeof(bad_iterator));
    (void)bad_iterator.GetNext;

    a = iDlist.Create(sizeof(int));
    b = iDlist.Create(sizeof(int));
    empty = iDlist.Create(sizeof(int));
    TEST_REQUIRE(a != NULL && b != NULL && empty != NULL);
    TEST_REQUIRE(iDlist.AddRange(a, 5, values) == 1);
    TEST_REQUIRE(iDlist.Add(b, &replacement) == 1);
    TEST_REQUIRE(iDlist.GetElement(a, 99) == NULL);
    TEST_REQUIRE(iDlist.GetElement(a, 2) != NULL);
    TEST_REQUIRE(iDlist.CopyElement(a, 4, &out) == 1 && out == 5);
    TEST_REQUIRE(iDlist.IndexOf(a, &replacement, NULL, &index) ==
                 CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iDlist.Contains(a, &replacement) == 0);
    TEST_REQUIRE(iDlist.ReplaceAt(a, 0, &replacement) == 1);
    TEST_REQUIRE(iDlist.ReplaceAt(a, iDlist.Size(a), &replacement) ==
                 CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iDlist.PushFront(a, &out) == 1);
    TEST_REQUIRE(iDlist.PushBack(a, &out) == 1);
    TEST_REQUIRE(iDlist.InsertAt(a, 1, &out) == 1);
    TEST_REQUIRE(iDlist.InsertAt(a, iDlist.Size(a), &out) == 1);
    TEST_REQUIRE(iDlist.InsertAt(a, iDlist.Size(a) + 1, &out) ==
                 CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iDlist.PopFront(a, &out) == 1);
    TEST_REQUIRE(iDlist.PopBack(a, &out) == 1);
    TEST_REQUIRE(iDlist.EraseAt(a, 0) == 1);
    TEST_REQUIRE(iDlist.Reverse(a) == 1 && assert_links(a));
    TEST_REQUIRE(iDlist.RotateLeft(a, 0) == 0);
    TEST_REQUIRE(iDlist.RotateRight(a, 99) == 1 && assert_links(a));
    TEST_REQUIRE(iDlist.RotateLeft(a, 99) == 1 && assert_links(a));
    TEST_REQUIRE(iDlist.SetCompareFunction(a, reverse_int_compare) != NULL);
    TEST_REQUIRE(iDlist.Sort(a) == 1);
    TEST_REQUIRE(iDlist.SetCompareFunction(a, NULL) == reverse_int_compare);

    copy = iDlist.Copy(a);
    range = iDlist.GetRange(a, 0, iDlist.Size(a) + 10);
    TEST_REQUIRE(copy != NULL && range != NULL);
    TEST_REQUIRE(iDlist.Equal(a, copy) == 1);
    selected = iDlist.GetRange(a, 4, 2);
    TEST_REQUIRE(selected != NULL);
    iDlist.Finalize(selected);
    selected = NULL;
    TEST_REQUIRE(iDlist.CopyElement(a, iDlist.Size(a), &out) ==
                 CONTAINER_ERROR_INDEX);

    mask = iMask.Create(iDlist.Size(a));
    TEST_REQUIRE(mask != NULL);
    selected = iDlist.SelectCopy(a, mask);
    TEST_REQUIRE(selected != NULL);
    iDlist.Finalize(selected);
    selected = NULL;
    TEST_REQUIRE(iDlist.Select(a, mask) == 1 && iDlist.Size(a) == 0);
    iMask.Finalize(mask);
    mask = NULL;
    TEST_REQUIRE(iDlist.Select(a, NULL) == CONTAINER_ERROR_BADARG);

    TEST_REQUIRE(iDlist.Splice(empty, NULL, b, 1) == empty);
    TEST_REQUIRE(iDlist.Size(empty) == 1 && iDlist.Size(b) == 0);
    TEST_REQUIRE(iDlist.Splice(empty, NULL, empty, 1) == NULL);
    TEST_REQUIRE(iDlist.Append(empty, empty) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iDlist.Append(empty, b) == 1);
    b = NULL;
    TEST_REQUIRE(iDlist.Append(empty, empty) == CONTAINER_ERROR_BADARG);

    element = iDlist.FirstElement(empty);
    TEST_REQUIRE(element != NULL);
    memset(&foreign_element, 0, sizeof(foreign_element));
    TEST_REQUIRE(iDlist.SetElementData(empty, &foreign_element, &replacement) ==
                 CONTAINER_ERROR_WRONGELEMENT);
    TEST_REQUIRE(iDlist.SetElementData(empty, element, &replacement) == 1);
    TEST_REQUIRE(iDlist.NextElement(NULL) == NULL &&
                 iDlist.PreviousElement(NULL) == NULL &&
                 iDlist.GetElementData(NULL) == NULL);
    element = iDlist.FirstElement(empty);
    TEST_REQUIRE(iDlist.Advance(&element) != NULL && element == NULL);
    element = iDlist.LastElement(empty);
    TEST_REQUIRE(iDlist.MoveBack(&element) != NULL && element == NULL);
    TEST_REQUIRE(iDlist.Skip(iDlist.FirstElement(empty), 99) == NULL);

    TEST_REQUIRE(iDlist.UseHeap(empty, NULL) == 0);
    heap = iDlist.Create(sizeof(int));
    TEST_REQUIRE(heap != NULL && iDlist.UseHeap(heap, NULL) == 1);
    TEST_REQUIRE(iDlist.UseHeap(heap, NULL) == 0);
    TEST_REQUIRE(iDlist.Add(heap, &replacement) == 1);
    TEST_REQUIRE(iDlist.SplitAfter(heap, iDlist.FirstElement(heap)) == NULL);
    iDlist.Clear(heap);
    iDlist.Finalize(heap);
    heap = NULL;

    TEST_REQUIRE(iDlist.SetFlags(empty, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iDlist.GetElement(empty, 0) == NULL);
    TEST_REQUIRE(iDlist.Front(empty) == NULL && iDlist.Back(empty) == NULL);
    TEST_REQUIRE(iDlist.FirstElement(empty) == NULL &&
                 iDlist.LastElement(empty) == NULL);
    TEST_REQUIRE(iDlist.PushFront(empty, &replacement) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.PushBack(empty, &replacement) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.InsertAt(empty, 0, &replacement) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.ReplaceAt(empty, 0, &replacement) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.RemoveRange(empty, 0, 1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.RotateLeft(empty, 1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.RotateRight(empty, 1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.Reverse(empty) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iDlist.SelectCopy(empty, NULL) == NULL);
    TEST_REQUIRE(iDlist.SetCompareFunction(empty, reverse_int_compare) != NULL);
    TEST_REQUIRE(iDlist.SetFlags(empty, 0) == CONTAINER_READONLY);

    iterator_storage[0] = 0;
    TEST_REQUIRE(iDlist.InitIterator(empty, iterator_storage) == 1);
    TEST_REQUIRE(((Iterator *)iterator_storage)->GetFirst((Iterator *)iterator_storage) != NULL);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDlist.Save(empty, stream, save_scalar, NULL) == 1);
    rewind(stream);
    copy = iDlist.Load(stream, load_scalar, NULL);
    TEST_REQUIRE(copy != NULL);
    fclose(stream);
    stream = NULL;
    iDlist.Finalize(copy);
    copy = NULL;
    iDlist.Finalize(range);
    range = NULL;
    iDlist.Finalize(a);
    a = NULL;
    iDlist.Finalize(empty);
    empty = NULL;
    return 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (mask != NULL)
        iMask.Finalize(mask);
    if (heap != NULL)
        iDlist.Finalize(heap);
    if (selected != NULL)
        iDlist.Finalize(selected);
    if (copy != NULL)
        iDlist.Finalize(copy);
    if (range != NULL)
        iDlist.Finalize(range);
    if (b != NULL)
        iDlist.Finalize(b);
    if (a != NULL)
        iDlist.Finalize(a);
    if (empty != NULL)
        iDlist.Finalize(empty);
    return -1;
}

static int test_persistence_and_atomic_failure(void)
{
    ContainerAllocator allocator = {
        count_malloc, count_free, count_realloc, count_calloc
    };
    int values[] = {4, 5, 6};
    Dlist *list = NULL;
    Dlist *loaded = NULL;
    FILE *stream = NULL;
    size_t before;

    allocation_calls = 0;
    fail_at = 0;
    list = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(list != NULL && iDlist.Add(list, &values[0]) == 1);
    before = iDlist.Size(list);
    fail_at = allocation_calls + 2;
    TEST_REQUIRE(iDlist.AddRange(list, 2, &values[1]) == CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(iDlist.Size(list) == before && assert_links(list));
    fail_at = 0;
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDlist.AddRange(list, 2, &values[1]) == 1);
    TEST_REQUIRE(iDlist.Save(list, stream, NULL, NULL) == 1);
    rewind(stream);
    loaded = iDlist.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL && iDlist.Equal(list, loaded) == 1);
    fclose(stream);
    stream = NULL;
    iDlist.Finalize(loaded);
    loaded = NULL;
    iDlist.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    fail_at = 0;
    if (stream != NULL)
        fclose(stream);
    if (loaded != NULL)
        iDlist.Finalize(loaded);
    if (list != NULL)
        iDlist.Finalize(list);
    return -1;
}

static int test_full_branch_matrix(void)
{
    ContainerAllocator allocator = {
        count_malloc, count_free, count_realloc, count_calloc
    };
    int values[] = {7, 2, 9, 2};
    int one = 1;
    int out = 0;
    unsigned apply_calls = 0;
    size_t index = 0;
    Dlist *list = NULL;
    Dlist *source = NULL;
    Dlist *other = NULL;
    Dlist *empty = NULL;
    Dlist *dest = NULL;
    Dlist *placement = NULL;
    Dlist *tmp = NULL;
    Dlist *single = NULL;
    DlistElement *element = NULL;
    DlistElement foreign;
    Iterator *it = NULL;
    Mask *mask = NULL;
    FILE *stream = NULL;

    placement = malloc(sizeof(Dlist));
    TEST_REQUIRE(placement != NULL);
    TEST_REQUIRE(iDlist.Init(placement, sizeof(int)) == placement);
    TEST_REQUIRE(iDlist.Add(placement, &one) == 1);
    TEST_REQUIRE(iDlist.Finalize(placement) == 1);
    free(placement);
    placement = NULL;
    memset(&foreign, 0, sizeof(foreign));

    allocation_calls = 0;
    fail_at = allocation_calls + 1;
    TEST_REQUIRE(iDlist.CreateWithAllocator(sizeof(int), &allocator) == NULL);
    fail_at = 0;
    list = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    empty = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(list != NULL && empty != NULL);
    fail_at = allocation_calls + 1;
    TEST_REQUIRE(iDlist.Add(list, &one) == CONTAINER_ERROR_NOMEMORY);
    fail_at = 0;
    TEST_REQUIRE(iDlist.AddRange(list, 0, NULL) == 1);
    TEST_REQUIRE(iDlist.AddRange(list, 1, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iDlist.AddRange(list, SIZE_MAX, &one) == CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(iDlist.Add(list, &one) == 1);
    single = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(single != NULL && iDlist.Add(single, &one) == 1);
    TEST_REQUIRE(iDlist.PopFront(single, &out) == 1 && out == one);
    TEST_REQUIRE(iDlist.Add(single, &one) == 1);
    TEST_REQUIRE(iDlist.PopBack(single, &out) == 1 && out == one);
    iDlist.Finalize(single);
    single = NULL;
    TEST_REQUIRE(iDlist.IndexOf(list, &one, NULL, &index) == 1 && index == 0);
    iDlist.SetDestructor(list, count_destructor);
    TEST_REQUIRE(iDlist.ReplaceAt(list, 0, &one) == 1);

    /* Exercise both placement constructors and the all-observer callbacks. */
    {
        Dlist stack_list;
        TEST_REQUIRE(iDlist.InitWithAllocator(&stack_list, sizeof(int),
                                             CurrentAllocator) == &stack_list);
        TEST_REQUIRE(iDlist.Add(&stack_list, &one) == 1);
        TEST_REQUIRE(iDlist.Clear(&stack_list) == 1);
    }
    observer_events = 0;
    TEST_REQUIRE(iObserver.Subscribe(list, observe_all, UINT_MAX) == 1);
    TEST_REQUIRE(iDlist.AddRange(list, 2, values) == 1);
    TEST_REQUIRE(iDlist.Add(list, &one) == 1);
    TEST_REQUIRE(iDlist.PushFront(list, &one) == 1);
    TEST_REQUIRE(iDlist.PushBack(list, &one) == 1);
    TEST_REQUIRE(iDlist.InsertAt(list, 1, &one) == 1);
    TEST_REQUIRE(iDlist.ReplaceAt(list, 1, &one) == 1);
    TEST_REQUIRE(observer_events > 0);
    TEST_REQUIRE(iObserver.Unsubscribe(list, observe_all) == 1);

    TEST_REQUIRE(iDlist.GetElement(list, 0) != NULL);
    TEST_REQUIRE(iDlist.GetElement(list, list->count - 1) != NULL);
    TEST_REQUIRE(iDlist.CopyElement(list, 0, &out) == 1);
    TEST_REQUIRE(iDlist.CopyElement(list, list->count - 1, &out) == 1);
    TEST_REQUIRE(iDlist.IndexOf(list, &one, NULL, NULL) == 1);
    TEST_REQUIRE(iDlist.Contains(list, &one) == 1);
    TEST_REQUIRE(iDlist.Equal(list, list) == 1);
    other = iDlist.Copy(list);
    TEST_REQUIRE(other != NULL);
    TEST_REQUIRE(iDlist.Equal(list, other) == 1);
    TEST_REQUIRE(iDlist.Equal(list, empty) == 0);
    TEST_REQUIRE((iDlist.SetFlags(other, CONTAINER_READONLY) &
                 CONTAINER_READONLY) == 0);
    tmp = iDlist.Copy(other);
    TEST_REQUIRE(tmp != NULL);
    iDlist.Finalize(tmp);
    tmp = NULL;
    TEST_REQUIRE((iDlist.SetFlags(other, 0) & CONTAINER_READONLY) != 0);

    TEST_REQUIRE(iDlist.EraseAt(list, list->count - 1) == 1);
    TEST_REQUIRE(iDlist.EraseAt(list, 0) == 1);
    TEST_REQUIRE(iDlist.EraseAll(list, &one) == 1);
    TEST_REQUIRE(iDlist.Erase(list, &one) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iDlist.RemoveRange(list, 3, 1) == 1);
    TEST_REQUIRE(iDlist.RemoveRange(list, 0, SIZE_MAX) == 1);
    TEST_REQUIRE(iDlist.RemoveRange(list, 0, 0) == 0);
    TEST_REQUIRE(iDlist.Size(list) == 0);

    TEST_REQUIRE(iDlist.AddRange(list, 4, values) == 1);
    tmp = iDlist.GetRange(empty, 0, 1);
    TEST_REQUIRE(tmp != NULL);
    iDlist.Finalize(tmp);
    tmp = NULL;
    tmp = iDlist.GetRange(list, list->count - 1, list->count);
    TEST_REQUIRE(tmp != NULL);
    iDlist.Finalize(tmp);
    tmp = NULL;
    tmp = iDlist.GetRange(list, list->count, list->count + 1);
    TEST_REQUIRE(tmp != NULL && iDlist.Size(tmp) == 0);
    iDlist.Finalize(tmp);
    tmp = NULL;
    TEST_REQUIRE(iDlist.Reverse(list) == 1);
    TEST_REQUIRE(iDlist.RotateLeft(list, 1) == 1);
    TEST_REQUIRE(iDlist.RotateRight(list, 1) == 1);
    TEST_REQUIRE(iDlist.RotateLeft(list, list->count) == 0);
    TEST_REQUIRE(iDlist.RotateRight(list, list->count) == 0);
    TEST_REQUIRE(iDlist.Apply(list, stop_after_first, &apply_calls) == 0);
    TEST_REQUIRE(iDlist.Apply(list, NULL, NULL) == CONTAINER_ERROR_BADARG);

    mask = iMask.Create(iDlist.Size(list));
    TEST_REQUIRE(mask != NULL);
    TEST_REQUIRE(iMask.SetElement(mask, 0, 1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 1, 0) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 2, 1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 3, 0) == 1);
    tmp = iDlist.SelectCopy(list, mask);
    TEST_REQUIRE(tmp != NULL && iDlist.Size(tmp) == 2);
    iDlist.Finalize(tmp);
    tmp = NULL;
    TEST_REQUIRE(iDlist.Select(list, mask) == 1 && iDlist.Size(list) == 2);
    iMask.Finalize(mask);
    mask = NULL;

    source = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(source != NULL);
    source->DestructorFn = list->DestructorFn;
    TEST_REQUIRE(iDlist.AddRange(source, 2, values) == 1);
    TEST_REQUIRE(iDlist.Splice(list, iDlist.FirstElement(list), source, 1) == list);
    TEST_REQUIRE(iDlist.Size(source) == 0 && assert_links(list));
    iDlist.Finalize(source);
    source = NULL;
    source = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(source != NULL);
    source->DestructorFn = list->DestructorFn;
    TEST_REQUIRE(iDlist.Add(source, &one) == 1);
    TEST_REQUIRE(iDlist.Splice(list, iDlist.FirstElement(list), source, 0) == list);
    TEST_REQUIRE(iDlist.Size(source) == 0);
    iDlist.Finalize(source);
    source = NULL;
    dest = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    source = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(dest != NULL && source != NULL);
    source->DestructorFn = dest->DestructorFn;
    TEST_REQUIRE(iDlist.Add(source, &one) == 1);
    TEST_REQUIRE(iDlist.Append(dest, source) == 1);
    iDlist.Finalize(dest);
    dest = NULL;
    source = NULL;
    source = iDlist.Create(sizeof(int));
    TEST_REQUIRE(source != NULL);
    TEST_REQUIRE(iDlist.Splice(list, iDlist.FirstElement(list), source, 0) == list);
    iDlist.Finalize(source);
    source = NULL;

    source = iDlist.CreateWithAllocator(sizeof(int), &allocator);
    other = iDlist.CreateWithAllocator(sizeof(long long), &allocator);
    TEST_REQUIRE(source != NULL && other != NULL);
    source->DestructorFn = list->DestructorFn;
    TEST_REQUIRE(iDlist.Add(source, &one) == 1);
    TEST_REQUIRE(iDlist.SetCompareFunction(source, reverse_int_compare) != NULL);
    TEST_REQUIRE(iDlist.Splice(list, iDlist.FirstElement(list), source, 0) == NULL);
    source->Compare = list->Compare;
    TEST_REQUIRE(iDlist.Append(list, source) == 1);
    source = NULL;
    TEST_REQUIRE(iDlist.Append(list, other) == CONTAINER_ERROR_INCOMPATIBLE);
    iDlist.Finalize(other);
    other = NULL;

    element = iDlist.FirstElement(list);
    TEST_REQUIRE(element != NULL);
    TEST_REQUIRE(iDlist.SplitAfter(list, &foreign) == NULL);
    TEST_REQUIRE(iDlist.SetElementData(list, &foreign, &one) == CONTAINER_ERROR_WRONGELEMENT);
    TEST_REQUIRE(iDlist.SetElementData(list, element, &one) == 1);
    TEST_REQUIRE(iDlist.SetElementData(list, element, element->Data) == 1);

    it = iDlist.NewIterator(list);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    TEST_REQUIRE(it->GetPrevious(it) == NULL);
    TEST_REQUIRE(it->GetCurrent(it) == NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->GetLast(it) != NULL);
    TEST_REQUIRE(it->GetPosition(it) == list->count - 1);
    TEST_REQUIRE(it->Replace(it, &one, 1) == 1);
    TEST_REQUIRE(it->Replace(it, NULL, 0) == 1);
    TEST_REQUIRE(iDlist.DeleteIterator(it) == 1);
    it = NULL;

    /* Invalidating an iterator and every method's wrong-magic branch. */
    it = iDlist.NewIterator(list);
    TEST_REQUIRE(it != NULL);
    ((struct DListIterator *)it)->Magic = 0;
    (void)it->GetNext(it);
    (void)it->GetPrevious(it);
    (void)it->GetFirst(it);
    (void)it->GetCurrent(it);
    (void)it->GetLast(it);
    (void)it->Seek(it, 0);
    (void)it->GetPosition(it);
    (void)it->Replace(it, NULL, 0);
    ((struct DListIterator *)it)->Magic = DLIST_MAGIC_NUMBER;
    iDlist.DeleteIterator(it);
    it = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDlist.Save(list, stream, NULL, NULL) == 1);
    rewind(stream);
    tmp = iDlist.Load(stream, NULL, NULL);
    TEST_REQUIRE(tmp != NULL);
    iDlist.Finalize(tmp);
    tmp = NULL;
    fclose(stream);
    stream = NULL;

    iDlist.Finalize(list);
    list = NULL;
    iDlist.Finalize(empty);
    empty = NULL;
    iDlist.Finalize(other);
    other = NULL;
    return 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (mask != NULL)
        iMask.Finalize(mask);
    if (it != NULL)
        iDlist.DeleteIterator(it);
    if (tmp != NULL)
        iDlist.Finalize(tmp);
    if (single != NULL)
        iDlist.Finalize(single);
    if (source != NULL)
        iDlist.Finalize(source);
    if (dest != NULL)
        iDlist.Finalize(dest);
    if (other != NULL)
        iDlist.Finalize(other);
    if (list != NULL)
        iDlist.Finalize(list);
    if (empty != NULL)
        iDlist.Finalize(empty);
    if (placement != NULL) {
        iDlist.Finalize(placement);
        free(placement);
    }
    fail_at = 0;
    return -1;
}

static const TestCase dlist_tests[] = {
    {"lifecycle, ownership, and erase", test_lifecycle_and_erase},
    {"ranges and ownership transfers", test_ranges_and_transfers},
    {"heap, read-only, and direct nodes", test_heap_readonly_and_direct_nodes},
    {"iterators and read-only buffers", test_iterators_and_readonly_buffer},
    {"API edges and error paths", test_api_edges_and_error_paths},
    {"persistence and atomic allocation failure", test_persistence_and_atomic_failure},
    {"full branch matrix", test_full_branch_matrix},
};

static const TestSuite dlist_suite = {
    "standalone dlist",
    dlist_tests,
    sizeof(dlist_tests) / sizeof(dlist_tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &dlist_suite;
}
