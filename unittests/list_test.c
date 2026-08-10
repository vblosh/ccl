#include "test_support.h"

#include "containers.h"
#include "ccl_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned destructor_calls;

static void list_observer(const void *object, unsigned operation,
                          const void *extra[])
{
    (void)object;
    (void)operation;
    (void)extra;
}

static int count_destructor(void *data)
{
    (void)data;
    ++destructor_calls;
    return 1;
}

static int free_pointer_destructor(void *data)
{
    free(*(void **)data);
    return 1;
}

static void *fail_malloc(size_t size);
static void fail_free(void *ptr) { free(ptr); }
static void *fail_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
static int fail_calloc_now;
static void *fail_calloc(size_t n, size_t size)
{
    return fail_calloc_now ? NULL : calloc(n, size);
}

static size_t malloc_calls;
static size_t fail_at;

static void *fail_malloc(size_t size)
{
    ++malloc_calls;
    if (fail_at && malloc_calls >= fail_at)
        return NULL;
    return malloc(size);
}

static int test_lifecycle_and_ranges(void)
{
    List *list = NULL;
    List *heap = NULL;
    int values[] = {1, 2, 2, 3, 2, 4};
    int out = 0;
    size_t i;
    char *owned = NULL;
    char *popped = NULL;

    list = iList.InitializeWith(sizeof(values[0]), 6, values);
    TEST_REQUIRE(list != NULL);
    iList.SetDestructor(list, count_destructor);
    destructor_calls = 0;
    TEST_REQUIRE(iList.EraseAll(list, &values[2]) == 1);
    TEST_REQUIRE(iList.Size(list) == 3 && destructor_calls == 3);
    TEST_REQUIRE(iList.CopyElement(list, 0, &out) == 1 && out == 1);
    TEST_REQUIRE(iList.RemoveRange(list, 1, 3) == 1);
    TEST_REQUIRE(iList.Size(list) == 1 && *(int *)iList.Front(list) == 1);
    TEST_REQUIRE(iList.EraseRange(list, 0, 99) == 1);
    TEST_REQUIRE(iList.Size(list) == 0 && iList.Front(list) == NULL);
    TEST_REQUIRE(destructor_calls == 6);
    iList.Finalize(list);
    list = NULL;

    heap = iList.Create(sizeof(int));
    TEST_REQUIRE(heap != NULL && iList.UseHeap(heap, NULL) == 1);
    iList.SetDestructor(heap, count_destructor);
    TEST_REQUIRE(iList.AddRange(heap, 6, values) == 1);
    destructor_calls = 0;
    TEST_REQUIRE(iList.RemoveRange(heap, 0, 6) == 1);
    TEST_REQUIRE(iList.Size(heap) == 0 && destructor_calls == 6);
    iList.Finalize(heap);
    heap = NULL;

    owned = (char *)malloc(8);
    TEST_REQUIRE(owned != NULL);
    memcpy(owned, "owned", 6);
    list = iList.InitializeWith(sizeof(owned), 1, &owned);
    TEST_REQUIRE(list != NULL);
    iList.SetDestructor(list, free_pointer_destructor);
    TEST_REQUIRE(iList.PopFront(list, &popped) == 1);
    TEST_REQUIRE(popped == owned && strcmp(popped, "owned") == 0);
    free(popped);
    owned = popped = NULL;
    iList.Finalize(list);
    list = NULL;

    {
        List placement;
        int placement_value = 0;
        memset(&placement, 0, sizeof(placement));
        TEST_REQUIRE(iList.Init(&placement, sizeof(int)) == &placement);
        for (i = 0; i < 3; ++i)
            TEST_REQUIRE(iList.Add(&placement, &placement_value) == 1);
        TEST_REQUIRE(iList.Finalize(&placement) == 1);
    }
    return 0;

cleanup:
    if (heap) iList.Finalize(heap);
    if (list) iList.Finalize(list);
    return -1;
}

static int test_atomic_add_range(void)
{
    ContainerAllocator allocator = {
        fail_malloc, fail_free, fail_realloc, fail_calloc
    };
    List *list = NULL;
    int initial = 7;
    int values[] = {1, 2, 3, 4};
    size_t before;

    malloc_calls = 0;
    fail_at = 0;
    list = iList.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iList.Add(list, &initial) == 1);
    before = iList.Size(list);
    fail_at = malloc_calls + 2; /* one successful append, then fail */
    TEST_REQUIRE(iList.AddRange(list, 4, values) == CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(iList.Size(list) == before);
    TEST_REQUIRE(*(int *)iList.Front(list) == initial);
    TEST_REQUIRE(iList.Back(list) == iList.Front(list));
    fail_at = 0;
    iList.Finalize(list);
    return 0;

cleanup:
    fail_at = 0;
    if (list) iList.Finalize(list);
    return -1;
}

static int test_iterators_and_transfers(void)
{
    List *wide = NULL;
    List *source = NULL;
    List *destination = NULL;
    List *suffix = NULL;
    Iterator *it = NULL;
    unsigned char storage[512];
    unsigned char value[64];
    size_t i;

    memset(value, 0x5a, sizeof(value));
    wide = iList.Create(sizeof(value));
    TEST_REQUIRE(wide != NULL && iList.Add(wide, value) == 1);
    it = iList.NewIterator(wide);
    TEST_REQUIRE(it != NULL && it->GetFirst(it) != NULL);
    TEST_REQUIRE(memcmp(it->GetCurrent(it), value, sizeof(value)) == 0);
    iList.SetFlags(wide, CONTAINER_READONLY);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(iList.DeleteIterator(it) == 1);
    it = NULL;
    iList.SetFlags(wide, 0);
    iList.Finalize(wide);
    wide = NULL;

    source = iList.InitializeWith(sizeof(int), 2, (int[]){1, 2});
    destination = iList.InitializeWith(sizeof(int), 2, (int[]){3, 4});
    TEST_REQUIRE(source && destination);
    TEST_REQUIRE(iList.InsertIn(destination, 0, source) == 1);
    TEST_REQUIRE(iList.Size(source) == 2 && *(int *)iList.Front(destination) == 1);
    TEST_REQUIRE(iList.Append(destination, source) == 1);
    TEST_REQUIRE(iList.Size(source) == 2 && iList.Size(destination) == 6);
    suffix = iList.SplitAfter(destination, iList.FirstElement(destination));
    TEST_REQUIRE(suffix != NULL && iList.Size(destination) == 1 && iList.Size(suffix) == 5);
    iList.Finalize(suffix);
    suffix = NULL;
    iList.Finalize(source);
    source = NULL;
    iList.Finalize(destination);
    destination = NULL;

    {
        List placement;
        memset(&placement, 0, sizeof(placement));
        TEST_REQUIRE(iList.Init(&placement, sizeof(int)) == &placement);
        TEST_REQUIRE(iList.Add(&placement, &(int){9}) == 1);
        TEST_REQUIRE(iList.InitIterator(&placement, storage) == 1);
        it = (Iterator *)storage;
        TEST_REQUIRE(*(int *)it->GetFirst(it) == 9);
        TEST_REQUIRE(iList.DeleteIterator(it) == 1);
        it = NULL;
        TEST_REQUIRE(iList.Finalize(&placement) == 1);
    }
    for (i = 0; i < sizeof(storage); ++i)
        storage[i] = 0;
    return 0;

cleanup:
    if (it && it != (Iterator *)storage) iList.DeleteIterator(it);
    if (suffix) iList.Finalize(suffix);
    if (source) iList.Finalize(source);
    if (destination) iList.Finalize(destination);
    if (wide) {
        iList.SetFlags(wide, 0);
        iList.Finalize(wide);
    }
    return -1;
}

static int test_persistence_validation(void)
{
    List *list = NULL;
    List *loaded = NULL;
    FILE *stream = NULL;
    int values[] = {11, 22, 33};
    int out = 0;

    list = iList.InitializeWith(sizeof(int), 3, values);
    TEST_REQUIRE(list != NULL);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL && iList.Save(list, stream, NULL, NULL) == 1);
    rewind(stream);
    loaded = iList.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL && iList.Size(loaded) == 3);
    TEST_REQUIRE(iList.CopyElement(loaded, 2, &out) == 1 && out == 33);
    iList.Finalize(loaded);
    loaded = NULL;
    fclose(stream);
    stream = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    fputc(0, stream);
    rewind(stream);
    TEST_REQUIRE(iList.Load(stream, NULL, NULL) == NULL);
    fclose(stream);
    stream = NULL;
    iList.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    if (stream) fclose(stream);
    if (loaded) iList.Finalize(loaded);
    if (list) iList.Finalize(list);
    return -1;
}

static int reverse_compare(const void *left, const void *right, CompareInfo *ci)
{
    (void)ci;
    return -memcmp(left, right, sizeof(int));
}

static int increment_apply(void *element, void *arg)
{
    *(int *)element += arg ? *(int *)arg : 1;
    return 1;
}

static int stop_apply(void *element, void *arg)
{
    (void)element;
    (void)arg;
    return 0;
}

static int test_api_edges_and_navigation(void)
{
    List *list = NULL;
    List *other = NULL;
    List *range = NULL;
    List *selected = NULL;
    List *copy = NULL;
    List *empty = NULL;
    List *heap = NULL;
    List *observed = NULL;
    Mask *mask = NULL;
    Iterator *it = NULL;
    FILE *stream = NULL;
    unsigned char storage[256];
    int values[] = {3, 1, 2, 1};
    int replacement = 8;
    int delta = 1;
    int out = 0;
    ListElement *element;

    /* Query and argument failures should be contained, not crash or return
     * a wrapped size_t error. */
    (void)iList.Size(NULL);
    (void)iList.GetFlags(NULL);
    (void)iList.SetFlags(NULL, 0);
    (void)iList.Clear(NULL);
    (void)iList.Contains(NULL, &replacement);
    (void)iList.Erase(NULL, &replacement);
    (void)iList.EraseAll(NULL, &replacement);
    (void)iList.Finalize(NULL);
    (void)iList.Apply(NULL, increment_apply, NULL);
    (void)iList.Equal(NULL, NULL);
    (void)iList.Copy(NULL);
    (void)iList.SetErrorFunction(NULL, NULL);
    (void)iList.Sizeof(NULL);
    (void)iList.NewIterator(NULL);
    (void)iList.InitIterator(NULL, NULL);
    (void)iList.DeleteIterator(NULL);
    (void)iList.SizeofIterator(NULL);
    (void)iList.Save(NULL, NULL, NULL, NULL);
    (void)iList.Load(NULL, NULL, NULL);
    (void)iList.GetElementSize(NULL);
    (void)iList.Add(NULL, &replacement);
    (void)iList.GetElement(NULL, 0);
    (void)iList.PushFront(NULL, &replacement);
    (void)iList.PopFront(NULL, &out);
    (void)iList.InsertAt(NULL, 0, &replacement);
    (void)iList.EraseAt(NULL, 0);
    (void)iList.ReplaceAt(NULL, 0, &replacement);
    (void)iList.IndexOf(NULL, &replacement, NULL, NULL);
    (void)iList.InsertIn(NULL, 0, NULL);
    (void)iList.CopyElement(NULL, 0, &out);
    (void)iList.EraseRange(NULL, 0, 1);
    (void)iList.Sort(NULL);
    (void)iList.Reverse(NULL);
    (void)iList.GetRange(NULL, 0, 1);
    (void)iList.Append(NULL, NULL);
    (void)iList.SetCompareFunction(NULL, reverse_compare);
    (void)iList.UseHeap(NULL, NULL);
    (void)iList.GetHeap(NULL);
    (void)iList.AddRange(NULL, 1, &replacement);
    (void)iList.Init(NULL, sizeof(int));
    (void)iList.InitWithAllocator(NULL, sizeof(int), NULL);
    (void)iList.GetAllocator(NULL);
    (void)iList.SetDestructor(NULL, count_destructor);
    (void)iList.InitializeWith(sizeof(int), 1, NULL);
    (void)iList.Back(NULL);
    (void)iList.Front(NULL);
    (void)iList.RemoveRange(NULL, 0, 1);
    (void)iList.RotateLeft(NULL, 1);
    (void)iList.RotateRight(NULL, 1);
    (void)iList.Select(NULL, NULL);
    (void)iList.SelectCopy(NULL, NULL);
    (void)iList.FirstElement(NULL);
    (void)iList.LastElement(NULL);
    (void)iList.NextElement(NULL);
    (void)iList.GetElementData(NULL);
    (void)iList.SetElementData(NULL, NULL, NULL);
    (void)iList.Advance(NULL);
    (void)iList.Skip(NULL, 1);
    (void)iList.SplitAfter(NULL, NULL);

    /* A NULL allocator selects the current allocator; release the returned
     * header instead of discarding it in this argument-surface probe. */
    heap = iList.CreateWithAllocator(sizeof(int), NULL);
    TEST_REQUIRE(heap != NULL);
    iList.Finalize(heap);
    heap = NULL;

    /* Exercise the observer notifications emitted by each mutation family. */
    observed = iList.InitializeWith(sizeof(int), 2, values);
    TEST_REQUIRE(observed != NULL);
    TEST_REQUIRE(iObserver.Subscribe(observed, list_observer,
                                     CCL_MODIFY | CCL_INSERT_AT |
                                     CCL_INSERT_IN | CCL_REPLACEAT |
                                     CCL_COPY) == 1);
    TEST_REQUIRE(iList.Add(observed, &replacement) == 1);
    TEST_REQUIRE(iList.PushFront(observed, &delta) == 1);
    TEST_REQUIRE(iList.InsertAt(observed, 1, &replacement) == 1);
    TEST_REQUIRE(iList.ReplaceAt(observed, 1, &delta) == 1);
    TEST_REQUIRE(iList.PopFront(observed, &out) == 1);
    TEST_REQUIRE(iList.AddRange(observed, 2, values) == 1);
    copy = iList.Copy(observed);
    TEST_REQUIRE(copy != NULL);
    iList.Finalize(copy);
    copy = NULL;
    TEST_REQUIRE(iList.RemoveRange(observed, 1, 2) == 1);
    TEST_REQUIRE(iList.Clear(observed) == 1);
    TEST_REQUIRE(iObserver.Unsubscribe(observed, list_observer) == 1);
    iList.Finalize(observed);
    observed = NULL;

    list = iList.InitializeWith(sizeof(int), 4, values);
    other = iList.InitializeWith(sizeof(int), 2, values);
    TEST_REQUIRE(list && other);
    TEST_REQUIRE(iList.GetFlags(list) == 0);
    TEST_REQUIRE(iList.GetAllocator(list) == CurrentAllocator);
    TEST_REQUIRE(iList.GetElementSize(list) == sizeof(int));
    TEST_REQUIRE(iList.Sizeof(list) > sizeof(List));
    TEST_REQUIRE(iList.GetElement(list, 1) != NULL);
    TEST_REQUIRE(iList.Front(list) != NULL && iList.Back(list) != NULL);
    TEST_REQUIRE(iList.CopyElement(list, 1, &out) == 1 && out == 1);
    copy = iList.Copy(list);
    TEST_REQUIRE(copy != NULL && iList.Equal(list, copy) == 1);
    TEST_REQUIRE(iList.Equal(list, other) == 0);
    empty = iList.Create(sizeof(int));
    TEST_REQUIRE(empty != NULL && iList.Equal(empty, empty) == 1);
    TEST_REQUIRE(iList.Equal(empty, list) == 0);
    iList.Finalize(empty);
    empty = NULL;
    iList.Finalize(copy);
    copy = NULL;
    TEST_REQUIRE(iList.PushFront(list, &replacement) == 1);
    TEST_REQUIRE(iList.InsertAt(list, 1, &replacement) == 1);
    TEST_REQUIRE(iList.InsertAt(list, iList.Size(list), &replacement) == 1);
    TEST_REQUIRE(iList.PopFront(list, &out) == 1);
    TEST_REQUIRE(iList.EraseAt(list, 1) == 1);
    TEST_REQUIRE(iList.ReplaceAt(list, 0, &replacement) == 1);
    TEST_REQUIRE(iList.Erase(list, &replacement) == 1);
    TEST_REQUIRE(iList.EraseAll(list, &replacement) == 1);
    TEST_REQUIRE(iList.UseHeap(list, NULL) == CONTAINER_ERROR_NOT_EMPTY);
    heap = iList.Create(sizeof(int));
    TEST_REQUIRE(heap != NULL && iList.UseHeap(heap, NULL) == 1);
    TEST_REQUIRE(iList.GetHeap(heap) != NULL);
    TEST_REQUIRE(iList.PopFront(heap, NULL) == 0);
    iList.Finalize(heap);
    heap = NULL;
    TEST_REQUIRE(iList.GetElement(list, 99) == NULL);
    TEST_REQUIRE(iList.CopyElement(list, 99, &out) < 0);
    TEST_REQUIRE(iList.Add(list, NULL) < 0);
    TEST_REQUIRE(iList.PushFront(list, NULL) < 0);
    TEST_REQUIRE(iList.InsertAt(list, 99, &replacement) < 0);
    TEST_REQUIRE(iList.ReplaceAt(list, 99, &replacement) < 0);
    TEST_REQUIRE(iList.IndexOf(list, &replacement, NULL, NULL) < 0);
    TEST_REQUIRE(iList.InsertIn(list, 99, other) < 0);
    TEST_REQUIRE(iList.InsertIn(list, 0, list) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iList.Append(list, list) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iList.Erase(list, &replacement) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iList.EraseAll(list, &replacement) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iList.RemoveRange(list, 99, 100) == 0);
    TEST_REQUIRE(iList.RemoveRange(list, 2, 1) == 1);
    TEST_REQUIRE(iList.RotateLeft(list, 1) == 1);
    TEST_REQUIRE(iList.RotateRight(list, 1) == 1);
    TEST_REQUIRE(iList.Reverse(list) == 1);
    TEST_REQUIRE(iList.SetCompareFunction(list, reverse_compare) != NULL);
    TEST_REQUIRE(iList.Sort(list) == 1);
    TEST_REQUIRE(iList.SetCompareFunction(list, NULL) == reverse_compare);
    TEST_REQUIRE(iList.Apply(list, increment_apply, &delta) == 1);
    TEST_REQUIRE(iList.Apply(list, stop_apply, NULL) == 1);
    iList.SetFlags(list, CONTAINER_READONLY);
    TEST_REQUIRE(iList.Add(list, &replacement) < 0);
    TEST_REQUIRE(iList.PushFront(list, &replacement) < 0);
    TEST_REQUIRE(iList.InsertAt(list, 0, &replacement) < 0);
    TEST_REQUIRE(iList.EraseAt(list, 0) < 0);
    TEST_REQUIRE(iList.ReplaceAt(list, 0, &replacement) < 0);
    TEST_REQUIRE(iList.Erase(list, &replacement) < 0);
    TEST_REQUIRE(iList.EraseAll(list, &replacement) < 0);
    TEST_REQUIRE(iList.InsertIn(list, 0, other) < 0);
    TEST_REQUIRE(iList.EraseRange(list, 0, 1) < 0);
    TEST_REQUIRE(iList.RemoveRange(list, 0, 1) < 0);
    TEST_REQUIRE(iList.Sort(list) < 0);
    TEST_REQUIRE(iList.Reverse(list) < 0);
    TEST_REQUIRE(iList.RotateLeft(list, 1) < 0);
    TEST_REQUIRE(iList.RotateRight(list, 1) < 0);
    TEST_REQUIRE(iList.Select(list, NULL) < 0);
    TEST_REQUIRE(iList.Apply(list, increment_apply, &delta) == 1);
    TEST_REQUIRE(iList.GetElement(list, 0) == NULL);
    TEST_REQUIRE(iList.Front(list) == NULL);
    TEST_REQUIRE(iList.Back(list) == NULL);
    iList.SetFlags(list, 0);

    empty = iList.Create(sizeof(int));
    TEST_REQUIRE(empty != NULL);
    TEST_REQUIRE(iList.PopFront(empty, NULL) == 0);
    TEST_REQUIRE(iList.AddRange(empty, 0, &replacement) == 1);
    TEST_REQUIRE(iList.InsertIn(empty, 0, other) == 1);
    TEST_REQUIRE(iList.Clear(empty) == 1 && iList.Size(empty) == 0);
    TEST_REQUIRE(iList.RemoveRange(empty, 0, 0) == 0);
    TEST_REQUIRE(iList.EraseRange(empty, 0, 1) == 0);
    iList.Finalize(empty);
    empty = NULL;

    TEST_REQUIRE(iList.SetDestructor(list, count_destructor) == NULL);
    TEST_REQUIRE(iList.SetDestructor(list, NULL) == count_destructor);
    TEST_REQUIRE(iList.SetErrorFunction(list, NULL) != NULL);

    range = iList.GetRange(list, 0, 2);
    TEST_REQUIRE(range != NULL && iList.Size(range) == 2);
    empty = iList.GetRange(list, 1, 1);
    TEST_REQUIRE(empty == NULL);
    TEST_REQUIRE(iList.GetRange(list, 4, 2) == NULL);
    TEST_REQUIRE(iList.GetRange(list, 99, 100) == NULL);
    mask = iMask.Create(iList.Size(list));
    TEST_REQUIRE(mask != NULL);
    selected = iList.SelectCopy(list, mask);
    TEST_REQUIRE(selected != NULL && iList.Select(list, mask) == 1);
    iList.Finalize(selected);
    selected = NULL;
    iMask.Finalize(mask);
    mask = NULL;
    iList.Finalize(range);
    range = NULL;
    iList.Finalize(list);
    list = NULL;

    list = iList.InitializeWith(sizeof(int), 4, values);
    TEST_REQUIRE(list != NULL);
    it = iList.NewIterator(list);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL && it->GetPosition(it) == 0);
    TEST_REQUIRE(it->GetNext(it) != NULL);
    TEST_REQUIRE(it->GetLast(it) != NULL && it->GetPosition(it) == 3);
    TEST_REQUIRE(it->GetPrevious(it) != NULL && it->GetPosition(it) == 2);
    TEST_REQUIRE(it->Seek(it, 1) != NULL && it->GetPosition(it) == 1);
    TEST_REQUIRE(it->Seek(it, 100) == NULL);
    TEST_REQUIRE(it->Replace(it, &replacement, 1) == 1);
    TEST_REQUIRE(it->Replace(it, NULL, 0) == 1);
    {
        struct ListIterator *fake = (struct ListIterator *)storage;
        memcpy(fake, (struct ListIterator *)it, sizeof(*fake));
        fake->Magic = 0;
        TEST_REQUIRE(fake->it.GetNext(&fake->it) == NULL);
        TEST_REQUIRE(fake->it.GetPrevious(&fake->it) == NULL);
        TEST_REQUIRE(fake->it.GetFirst(&fake->it) == NULL);
        TEST_REQUIRE(fake->it.GetCurrent(&fake->it) == NULL);
        TEST_REQUIRE(fake->it.GetLast(&fake->it) == NULL);
        TEST_REQUIRE(fake->it.Seek(&fake->it, 0) == NULL);
        TEST_REQUIRE(fake->it.GetPosition(&fake->it) == (size_t)-1);
        TEST_REQUIRE(fake->it.Replace(&fake->it, NULL, 0) == CONTAINER_ERROR_WRONG_ITERATOR);
        TEST_REQUIRE(iList.DeleteIterator(&fake->it) == CONTAINER_ERROR_WRONG_ITERATOR);
    }
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(iList.Add(list, &replacement) == 1);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    TEST_REQUIRE(it->GetPrevious(it) == NULL);
    TEST_REQUIRE(it->GetCurrent(it) == NULL);
    TEST_REQUIRE(it->GetLast(it) == NULL);
    TEST_REQUIRE(it->Seek(it, 0) == NULL);
    TEST_REQUIRE(it->GetPosition(it) == (size_t)-1);
    TEST_REQUIRE(it->Replace(it, NULL, 0) == CONTAINER_ERROR_OBJECT_CHANGED);
    iList.DeleteIterator(it);
    it = NULL;
    element = iList.FirstElement(list);
    TEST_REQUIRE(element != NULL && iList.GetElementData(element) != NULL);
    TEST_REQUIRE(iList.SetElementData(list, element, &replacement) == 1);
    TEST_REQUIRE(iList.Skip(element, 1) != NULL);
    TEST_REQUIRE(iList.Advance(&element) != NULL);
    TEST_REQUIRE(iList.SplitAfter(list, (ListElement *)&out) == NULL);
    iList.Finalize(list);
    list = NULL;

    list = iList.Create(sizeof(int));
    TEST_REQUIRE(list != NULL && iList.UseHeap(list, NULL) == 1);
    TEST_REQUIRE(iList.SplitAfter(list, NULL) == NULL);
    iList.Finalize(list);
    list = NULL;
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iList.Save(other, stream, NULL, NULL) == 1);
    fclose(stream);
    stream = NULL;
    iList.Finalize(other);
    other = NULL;
    (void)storage;
    return 0;

cleanup:
    if (stream) fclose(stream);
    if (it) iList.DeleteIterator(it);
    if (mask) iMask.Finalize(mask);
    if (selected) iList.Finalize(selected);
    if (range) iList.Finalize(range);
    if (other) iList.Finalize(other);
    if (list) iList.Finalize(list);
    return -1;
}

static int test_allocation_failure_paths(void)
{
    ContainerAllocator allocator = {
        fail_malloc, fail_free, fail_realloc, fail_calloc
    };
    List *list = NULL;
    List *source = NULL;
    List *destination = NULL;
    Iterator *it = NULL;
    int values[] = {1, 2, 3};

    malloc_calls = 0;
    fail_at = 0;
    fail_calloc_now = 1;
    TEST_REQUIRE(iList.CreateWithAllocator(sizeof(int), &allocator) == NULL);
    fail_calloc_now = 0;
    list = iList.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(list != NULL && iList.AddRange(list, 3, values) == 1);

    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.Copy(list) == NULL);
    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.GetRange(list, 0, 2) == NULL);
    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.Sort(list) == CONTAINER_ERROR_NOMEMORY);
    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.NewIterator(list) == NULL);
    fail_at = 0;
    iList.Finalize(list);
    list = NULL;

    list = iList.CreateWithAllocator(sizeof(int), &allocator);
    source = iList.CreateWithAllocator(sizeof(int), &allocator);
    destination = iList.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(list && source && destination);
    TEST_REQUIRE(iList.Add(list, &values[0]) == 1);
    TEST_REQUIRE(iList.Add(source, &values[1]) == 1);
    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.InsertIn(list, 0, source) == CONTAINER_ERROR_NOMEMORY);
    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.Append(destination, source) == CONTAINER_ERROR_NOMEMORY);
    fail_at = 0;
    iList.Finalize(list);
    iList.Finalize(source);
    iList.Finalize(destination);
    list = source = destination = NULL;

    list = iList.CreateWithAllocator(sizeof(int), &allocator);
    TEST_REQUIRE(list != NULL);
    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.UseHeap(list, &allocator) == CONTAINER_ERROR_NOMEMORY);
    fail_at = 0;
    TEST_REQUIRE(iList.UseHeap(list, &allocator) == 1);
    iList.SetFlags(list, CONTAINER_READONLY);
    fail_at = malloc_calls + 1;
    TEST_REQUIRE(iList.Apply(list, increment_apply, NULL) == CONTAINER_ERROR_NOMEMORY);
    fail_at = 0;
    iList.SetFlags(list, 0);
    iList.Finalize(list);
    list = NULL;
    (void)it;
    return 0;

cleanup:
    fail_at = 0;
    fail_calloc_now = 0;
    if (it) iList.DeleteIterator(it);
    if (destination) iList.Finalize(destination);
    if (source) iList.Finalize(source);
    if (list) iList.Finalize(list);
    return -1;
}

static const TestCase tests[] = {
    {"lifecycle, ownership and ranges", test_lifecycle_and_ranges},
    {"atomic AddRange rollback", test_atomic_add_range},
    {"iterators and safe transfers", test_iterators_and_transfers},
    {"persistence validation", test_persistence_validation},
    {"API edges and navigation", test_api_edges_and_navigation},
    {"allocation failure paths", test_allocation_failure_paths}
};

static const TestSuite suite = {
    "generic list", tests, sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
