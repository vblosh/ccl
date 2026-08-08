#include <limits.h>
#include <stdio.h>

#include "intdlist.h"
#include "doubledlist.h"
#include "longlongdlist.h"
#include "test_support.h"

static int descending_int(const void *left, const void *right,
                          CompareInfo *info)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    (void)info;
    return a < b ? 1 : a > b ? -1 : 0;
}

static int descending_double(const void *left, const void *right,
                             CompareInfo *info)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    (void)info;
    return a < b ? 1 : a > b ? -1 : 0;
}

static int descending_longlong(const void *left, const void *right,
                               CompareInfo *info)
{
    longlong a = *(const longlong *)left;
    longlong b = *(const longlong *)right;
    (void)info;
    return a < b ? 1 : a > b ? -1 : 0;
}

static int add_one_double(double *value, void *arg)
{
    *value += *(const double *)arg;
    return 1;
}

static int add_one_longlong(longlong *value, void *arg)
{
    *value += *(const longlong *)arg;
    return 1;
}

static int assert_int_links(const intDlist *list)
{
    intDlistElement *element;
    intDlistElement *previous = NULL;
    size_t count = 0;

    for (element = iintDlist.FirstElement((intDlist *)list); element != NULL;
         element = iintDlist.NextElement(element)) {
        TEST_REQUIRE(element->Previous == previous);
        previous = element;
        ++count;
    }
    TEST_REQUIRE(previous == list->Last);
    TEST_REQUIRE(count == list->count);

    count = 0;
    for (element = list->Last; element != NULL;
         element = iintDlist.PreviousElement(element)) {
        if (element->Next != NULL)
            TEST_REQUIRE(element->Next->Previous == element);
        ++count;
    }
    TEST_REQUIRE(count == list->count);
    return 0;

cleanup:
    return -1;
}

static int test_int_abi_iterator_sort(void)
{
    static const int values[] = {4, 1, 3, 2};
    intDlist *list = NULL;
    Iterator *iterator = NULL;
    int replacement = 9;
    intDlistElement *element;
    size_t i;

    TEST_REQUIRE(iintDlist.Size != NULL && iintDlist.PreviousElement != NULL);
    TEST_REQUIRE(iintDlist.Sizeof(NULL) == sizeof(intDlist));
    list = iintDlist.InitializeWith(sizeof(int), 4, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(list->VTable == &iintDlist);
    TEST_REQUIRE(iintDlist.GetAllocator(list) == list->Allocator);
    TEST_REQUIRE(iintDlist.GetElementSize(list) == sizeof(int));
    TEST_REQUIRE(assert_int_links(list) == 0);

    iterator = iintDlist.NewIterator(list);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(*(int *)iterator->GetFirst(iterator) == 4);
    TEST_REQUIRE(iterator->Replace(iterator, &replacement, 0) == 1);
    TEST_REQUIRE(*(int *)iintDlist.GetElement(list, 0) == replacement);
    TEST_REQUIRE(iintDlist.Sort(list) == 1);
    for (i = 1; i < list->count; ++i)
        TEST_REQUIRE(*(int *)iintDlist.GetElement(list, i - 1) <=
                     *(int *)iintDlist.GetElement(list, i));
    TEST_REQUIRE(assert_int_links(list) == 0);
    TEST_REQUIRE(iintDlist.SetCompareFunction(list, descending_int) != NULL);
    TEST_REQUIRE(iintDlist.Sort(list) == 1);
    TEST_REQUIRE(*(int *)iintDlist.Front(list) == 9);
    TEST_REQUIRE(assert_int_links(list) == 0);

    element = iintDlist.FirstElement(list);
    TEST_REQUIRE(element != NULL);
    TEST_REQUIRE(iintDlist.GetElementData(element) == &element->Data);
    iintDlist.DeleteIterator(iterator);
    iterator = NULL;
    iintDlist.Finalize(list);
    return 0;

cleanup:
    if (iterator != NULL)
        iintDlist.DeleteIterator(iterator);
    if (list != NULL)
        iintDlist.Finalize(list);
    return -1;
}

static int test_add_range_split_and_typed_boundaries(void)
{
    static const int values[] = {INT_MIN, 0, INT_MAX};
    intDlist *list = NULL;
    intDlist *tail = NULL;
    intDlistElement *first;
    int range[] = {7, 8, 9};

    list = iintDlist.Create(sizeof(int));
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iintDlist.AddRange(list, 3, range) == 1);
    TEST_REQUIRE(iintDlist.Size(list) == 3);
    TEST_REQUIRE(iintDlist.AddRange(list, 0, NULL) == 1);
    TEST_REQUIRE(*iintDlist.Front(list) == 7 && *iintDlist.Back(list) == 9);
    TEST_REQUIRE(iintDlist.Add(list, INT_MIN) == 1);
    TEST_REQUIRE(iintDlist.Contains(list, INT_MIN) == 1);
    TEST_REQUIRE(iintDlist.Contains(list, 42) == 0);
    TEST_REQUIRE(iintDlist.Contains(NULL, 1) == CONTAINER_ERROR_BADARG);

    first = iintDlist.FirstElement(list);
    tail = iintDlist.SplitAfter(list, first);
    TEST_REQUIRE(tail != NULL);
    TEST_REQUIRE(tail->VTable == &iintDlist);
    TEST_REQUIRE(tail->First->Previous == NULL);
    TEST_REQUIRE(list->Last->Next == NULL);
    TEST_REQUIRE(iintDlist.Size(list) == 1 && iintDlist.Size(tail) == 3);
    TEST_REQUIRE(iintDlist.Add(tail, 10) == 1);

    iintDlist.Finalize(tail);
    iintDlist.Finalize(list);
    list = NULL;
    tail = NULL;

    list = iintDlist.InitializeWith(sizeof(values[0]), 3, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iintDlist.Create(sizeof(long long)) == NULL);
    iintDlist.Finalize(list);
    return 0;

cleanup:
    if (tail != NULL)
        iintDlist.Finalize(tail);
    if (list != NULL)
        iintDlist.Finalize(list);
    return -1;
}

static int test_double_and_longlong_scalar_widths(void)
{
    const double double_values[] = {-1.5, 0.0, 2.5};
    const longlong long_values[] = {LLONG_MIN, 0, LLONG_MAX};
    doubleDlist *doubles = NULL;
    longlongDlist *longs = NULL;
    size_t i;

    doubles = idoubleDlist.InitializeWith(sizeof(double), 3, double_values);
    longs = ilonglongDlist.InitializeWith(sizeof(longlong), 3, long_values);
    TEST_REQUIRE(doubles != NULL && longs != NULL);
    TEST_REQUIRE(idoubleDlist.GetElementSize(doubles) == sizeof(double));
    TEST_REQUIRE(ilonglongDlist.GetElementSize(longs) == sizeof(longlong));
    TEST_REQUIRE(idoubleDlist.Sort(doubles) == 1);
    TEST_REQUIRE(ilonglongDlist.Sort(longs) == 1);
    for (i = 1; i < 3; ++i) {
        TEST_REQUIRE(*idoubleDlist.GetElement(doubles, i - 1) <=
                     *idoubleDlist.GetElement(doubles, i));
        TEST_REQUIRE(*ilonglongDlist.GetElement(longs, i - 1) <=
                     *ilonglongDlist.GetElement(longs, i));
    }
    idoubleDlist.Finalize(doubles);
    ilonglongDlist.Finalize(longs);
    return 0;

cleanup:
    if (doubles != NULL)
        idoubleDlist.Finalize(doubles);
    if (longs != NULL)
        ilonglongDlist.Finalize(longs);
    return -1;
}

static int test_double_surface(void)
{
    double values[] = {3.0, 1.0, 2.0};
    doubleDlist *list = NULL;
    doubleDlist *copy = NULL;
    doubleDlist *range = NULL;
    doubleDlist *selected = NULL;
    doubleDlist *loaded = NULL;
    doubleDlist *split = NULL;
    doubleDlist *scratch = NULL;
    doubleDlist *scratch2 = NULL;
    doubleDlist placement;
    doubleDlistElement *element;
    Iterator *iterator = NULL;
    Mask *mask = NULL;
    FILE *stream = NULL;
    double value = 0.0;
    double delta = 1.0;
    size_t index = 0;

    list = idoubleDlist.InitializeWith(sizeof(double), 3, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(idoubleDlist.Sizeof(list) > sizeof(doubleDlist));
    TEST_REQUIRE(idoubleDlist.SizeofIterator(list) > 0);
    TEST_REQUIRE(idoubleDlist.GetAllocator(list) == list->Allocator);
    TEST_REQUIRE(idoubleDlist.Contains(list, 3.0) == 1);
    TEST_REQUIRE(idoubleDlist.Add(list, 5.0) == 1);
    TEST_REQUIRE(idoubleDlist.AddRange(list, 1, &delta) == 1);
    TEST_REQUIRE(idoubleDlist.PopFront(list, &value) == 1);
    TEST_REQUIRE(idoubleDlist.PopBack(list, &value) == 1);
    TEST_REQUIRE(idoubleDlist.SetFlags(list, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(idoubleDlist.GetFlags(list) == CONTAINER_READONLY);
    TEST_REQUIRE(idoubleDlist.SetFlags(list, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(idoubleDlist.SetErrorFunction(list, NULL) != NULL);
    TEST_REQUIRE(idoubleDlist.SetDestructor(list, NULL) == NULL);
    TEST_REQUIRE(idoubleDlist.Init(&placement, sizeof(double)) == &placement);
    TEST_REQUIRE(idoubleDlist.Clear(&placement) == 1);
    scratch = idoubleDlist.Create(sizeof(double));
    scratch2 = idoubleDlist.CreateWithAllocator(sizeof(double), CurrentAllocator);
    TEST_REQUIRE(scratch != NULL && scratch2 != NULL);
    TEST_REQUIRE(idoubleDlist.InitWithAllocator(&placement, sizeof(double),
                                                CurrentAllocator) == &placement);
    TEST_REQUIRE(idoubleDlist.UseHeap(scratch, NULL) == 1);
    TEST_REQUIRE(idoubleDlist.Add(scratch, 11.0) == 1);
    TEST_REQUIRE(idoubleDlist.Clear(scratch) == 1);
    TEST_REQUIRE(idoubleDlist.Add(scratch2, 12.0) == 1);
    TEST_REQUIRE(idoubleDlist.InsertIn(list, 0, scratch2) == 1);
    TEST_REQUIRE(idoubleDlist.Append(list, scratch) == 1);
    scratch = NULL;
    idoubleDlist.Finalize(scratch2);
    scratch2 = NULL;
    TEST_REQUIRE(idoubleDlist.CopyElement(list, 1, &value) == 1);
    TEST_REQUIRE(idoubleDlist.ReplaceAt(list, 1, 9.0) == 1);
    TEST_REQUIRE(idoubleDlist.PushFront(list, 0.0) == 1);
    TEST_REQUIRE(idoubleDlist.PushBack(list, 4.0) == 1);
    TEST_REQUIRE(idoubleDlist.InsertAt(list, 2, 7.0) == 1);
    TEST_REQUIRE(idoubleDlist.IndexOf(list, 7.0, NULL, &index) == 1);
    TEST_REQUIRE(idoubleDlist.Erase(list, 7.0) == 1);
    TEST_REQUIRE(idoubleDlist.EraseAll(list, 77.0) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(idoubleDlist.Apply(list, add_one_double, &delta) == 1);
    TEST_REQUIRE(idoubleDlist.SetCompareFunction(list, descending_double) != NULL);
    TEST_REQUIRE(idoubleDlist.Sort(list) == 1);
    TEST_REQUIRE(idoubleDlist.Reverse(list) == 1);
    TEST_REQUIRE(idoubleDlist.Front(list) != NULL && idoubleDlist.Back(list) != NULL);
    element = idoubleDlist.FirstElement(list);
    TEST_REQUIRE(element != NULL && idoubleDlist.LastElement(list) != NULL);
    TEST_REQUIRE(idoubleDlist.NextElement(element) == element->Next);
    TEST_REQUIRE(idoubleDlist.PreviousElement(element) == NULL);
    TEST_REQUIRE(idoubleDlist.GetElementData(element) == &element->Data);
    TEST_REQUIRE(idoubleDlist.SetElementData(list, element, 12.0) == 1);
    TEST_REQUIRE(idoubleDlist.Advance(&element) != NULL);
    TEST_REQUIRE(idoubleDlist.MoveBack(&element) != NULL);
    TEST_REQUIRE(idoubleDlist.Skip(idoubleDlist.FirstElement(list), 1) != NULL);

    copy = idoubleDlist.Copy(list);
    range = idoubleDlist.GetRange(list, 0, 1);
    TEST_REQUIRE(copy != NULL && range != NULL);
    TEST_REQUIRE(idoubleDlist.Equal(list, copy) == 1);
    TEST_REQUIRE(idoubleDlist.RemoveRange(range, 0, 0) == 0);
    TEST_REQUIRE(idoubleDlist.RotateLeft(list, 1) == 1);
    TEST_REQUIRE(idoubleDlist.RotateRight(list, 1) == 1);
    split = idoubleDlist.SplitAfter(range, idoubleDlist.FirstElement(range));
    TEST_REQUIRE(split != NULL);
    idoubleDlist.Finalize(split);
    split = NULL;
    scratch = idoubleDlist.Create(sizeof(double));
    TEST_REQUIRE(scratch != NULL);
    TEST_REQUIRE(idoubleDlist.Splice(list, idoubleDlist.FirstElement(list),
                                     scratch, 1) == list);
    idoubleDlist.Finalize(scratch);
    scratch = NULL;
    mask = iMask.Create(idoubleDlist.Size(list));
    TEST_REQUIRE(mask != NULL);
    for (index = 0; index < idoubleDlist.Size(list); ++index)
        TEST_REQUIRE(iMask.SetElement(mask, index, index % 2) == 1);
    selected = idoubleDlist.SelectCopy(list, mask);
    TEST_REQUIRE(selected != NULL);
    TEST_REQUIRE(idoubleDlist.Select(list, mask) == 1);
    iMask.Finalize(mask);
    mask = NULL;

    iterator = idoubleDlist.NewIterator(list);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(idoubleDlist.DeleteIterator(iterator) == 1);
    iterator = NULL;
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(idoubleDlist.Save(list, stream, NULL, NULL) == 1);
    rewind(stream);
    loaded = idoubleDlist.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL);
    fclose(stream);
    stream = NULL;
    idoubleDlist.Finalize(loaded);
    idoubleDlist.Finalize(selected);
    idoubleDlist.Finalize(range);
    idoubleDlist.Finalize(copy);
    idoubleDlist.Finalize(list);
    return 0;

cleanup:
    if (mask != NULL)
        iMask.Finalize(mask);
    if (iterator != NULL)
        idoubleDlist.DeleteIterator(iterator);
    if (stream != NULL)
        fclose(stream);
    if (loaded != NULL)
        idoubleDlist.Finalize(loaded);
    if (scratch2 != NULL)
        idoubleDlist.Finalize(scratch2);
    if (scratch != NULL)
        idoubleDlist.Finalize(scratch);
    if (split != NULL)
        idoubleDlist.Finalize(split);
    if (selected != NULL)
        idoubleDlist.Finalize(selected);
    if (range != NULL)
        idoubleDlist.Finalize(range);
    if (copy != NULL)
        idoubleDlist.Finalize(copy);
    if (list != NULL)
        idoubleDlist.Finalize(list);
    return -1;
}

static int test_longlong_surface(void)
{
    longlong values[] = {3, 1, 2};
    longlongDlist *list = NULL;
    longlongDlist *copy = NULL;
    longlongDlist *range = NULL;
    longlongDlist *selected = NULL;
    longlongDlist *loaded = NULL;
    longlongDlist *scratch = NULL;
    longlongDlist *scratch2 = NULL;
    longlongDlist *split = NULL;
    longlongDlist placement;
    longlongDlistElement *element;
    Iterator *iterator = NULL;
    Mask *mask = NULL;
    longlong value = 0;
    longlong delta = 1;
    size_t index = 0;
    unsigned char iterator_storage[128];
    FILE *stream = NULL;

    list = ilonglongDlist.InitializeWith(sizeof(longlong), 3, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(ilonglongDlist.Sizeof(list) > sizeof(longlongDlist));
    TEST_REQUIRE(ilonglongDlist.SizeofIterator(list) > 0);
    TEST_REQUIRE(ilonglongDlist.GetAllocator(list) == list->Allocator);
    TEST_REQUIRE(ilonglongDlist.Contains(list, 3) == 1);
    TEST_REQUIRE(ilonglongDlist.Add(list, 5) == 1);
    TEST_REQUIRE(ilonglongDlist.AddRange(list, 1, &delta) == 1);
    TEST_REQUIRE(ilonglongDlist.PopFront(list, &value) == 1);
    TEST_REQUIRE(ilonglongDlist.PopBack(list, &value) == 1);
    TEST_REQUIRE(ilonglongDlist.SetFlags(list, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(ilonglongDlist.GetFlags(list) == CONTAINER_READONLY);
    TEST_REQUIRE(ilonglongDlist.SetFlags(list, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(ilonglongDlist.SetErrorFunction(list, NULL) != NULL);
    TEST_REQUIRE(ilonglongDlist.SetDestructor(list, NULL) == NULL);
    TEST_REQUIRE(ilonglongDlist.Init(&placement, sizeof(longlong)) == &placement);
    TEST_REQUIRE(ilonglongDlist.Clear(&placement) == 1);
    scratch = ilonglongDlist.Create(sizeof(longlong));
    scratch2 = ilonglongDlist.CreateWithAllocator(sizeof(longlong), CurrentAllocator);
    TEST_REQUIRE(scratch != NULL && scratch2 != NULL);
    TEST_REQUIRE(ilonglongDlist.InitWithAllocator(&placement, sizeof(longlong),
                                                  CurrentAllocator) == &placement);
    TEST_REQUIRE(ilonglongDlist.UseHeap(scratch, NULL) == 1);
    TEST_REQUIRE(ilonglongDlist.Add(scratch, 11) == 1);
    TEST_REQUIRE(ilonglongDlist.Clear(scratch) == 1);
    TEST_REQUIRE(ilonglongDlist.Add(scratch2, 12) == 1);
    TEST_REQUIRE(ilonglongDlist.InsertIn(list, 0, scratch2) == 1);
    TEST_REQUIRE(ilonglongDlist.Append(list, scratch) == 1);
    scratch = NULL;
    ilonglongDlist.Finalize(scratch2);
    scratch2 = NULL;
    TEST_REQUIRE(ilonglongDlist.CopyElement(list, 1, &value) == 1);
    TEST_REQUIRE(ilonglongDlist.ReplaceAt(list, 1, 9) == 1);
    TEST_REQUIRE(ilonglongDlist.PushFront(list, 0) == 1);
    TEST_REQUIRE(ilonglongDlist.PushBack(list, 4) == 1);
    TEST_REQUIRE(ilonglongDlist.InsertAt(list, 2, 7) == 1);
    TEST_REQUIRE(ilonglongDlist.IndexOf(list, 7, NULL, &index) == 1);
    TEST_REQUIRE(ilonglongDlist.Erase(list, 7) == 1);
    TEST_REQUIRE(ilonglongDlist.EraseAll(list, 77) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(ilonglongDlist.Apply(list, add_one_longlong, &delta) == 1);
    TEST_REQUIRE(ilonglongDlist.SetCompareFunction(list, descending_longlong) != NULL);
    TEST_REQUIRE(ilonglongDlist.Sort(list) == 1);
    TEST_REQUIRE(ilonglongDlist.Reverse(list) == 1);
    element = ilonglongDlist.FirstElement(list);
    TEST_REQUIRE(element != NULL && ilonglongDlist.LastElement(list) != NULL);
    TEST_REQUIRE(ilonglongDlist.GetElementData(element) == &element->Data);
    TEST_REQUIRE(ilonglongDlist.SetElementData(list, element, 12) == 1);
    TEST_REQUIRE(ilonglongDlist.NextElement(element) == element->Next);
    TEST_REQUIRE(ilonglongDlist.PreviousElement(element) == NULL);
    TEST_REQUIRE(ilonglongDlist.Front(list) != NULL &&
                 ilonglongDlist.Back(list) != NULL);
    TEST_REQUIRE(ilonglongDlist.Advance(&element) != NULL);
    TEST_REQUIRE(ilonglongDlist.MoveBack(&element) != NULL);
    TEST_REQUIRE(ilonglongDlist.Skip(ilonglongDlist.FirstElement(list), 1) != NULL);

    copy = ilonglongDlist.Copy(list);
    range = ilonglongDlist.GetRange(list, 0, 1);
    TEST_REQUIRE(copy != NULL && range != NULL);
    TEST_REQUIRE(ilonglongDlist.Equal(list, copy) == 1);
    TEST_REQUIRE(ilonglongDlist.RemoveRange(range, 0, 0) == 0);
    TEST_REQUIRE(ilonglongDlist.RotateLeft(list, 1) == 1);
    TEST_REQUIRE(ilonglongDlist.RotateRight(list, 1) == 1);
    split = ilonglongDlist.SplitAfter(range, ilonglongDlist.FirstElement(range));
    TEST_REQUIRE(split != NULL);
    ilonglongDlist.Finalize(split);
    split = NULL;
    scratch = ilonglongDlist.Create(sizeof(longlong));
    TEST_REQUIRE(scratch != NULL);
    TEST_REQUIRE(ilonglongDlist.Splice(list, ilonglongDlist.FirstElement(list),
                                       scratch, 1) == list);
    ilonglongDlist.Finalize(scratch);
    scratch = NULL;
    mask = iMask.Create(ilonglongDlist.Size(list));
    TEST_REQUIRE(mask != NULL);
    for (index = 0; index < ilonglongDlist.Size(list); ++index)
        TEST_REQUIRE(iMask.SetElement(mask, index, index % 2) == 1);
    selected = ilonglongDlist.SelectCopy(list, mask);
    TEST_REQUIRE(selected != NULL);
    TEST_REQUIRE(ilonglongDlist.Select(list, mask) == 1);
    iMask.Finalize(mask);
    mask = NULL;
    iterator = ilonglongDlist.NewIterator(list);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(ilonglongDlist.DeleteIterator(iterator) == 1);
    iterator = NULL;
    TEST_REQUIRE(ilonglongDlist.InitIterator(list, iterator_storage) == 1);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(ilonglongDlist.Save(list, stream, NULL, NULL) == 1);
    rewind(stream);
    loaded = ilonglongDlist.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL);
    fclose(stream);
    stream = NULL;
    ilonglongDlist.Finalize(loaded);
    loaded = NULL;
    ilonglongDlist.Finalize(range);
    ilonglongDlist.Finalize(selected);
    ilonglongDlist.Finalize(copy);
    ilonglongDlist.Finalize(list);
    return 0;

cleanup:
    if (mask != NULL)
        iMask.Finalize(mask);
    if (iterator != NULL)
        ilonglongDlist.DeleteIterator(iterator);
    if (stream != NULL)
        fclose(stream);
    if (loaded != NULL)
        ilonglongDlist.Finalize(loaded);
    if (range != NULL)
        ilonglongDlist.Finalize(range);
    if (selected != NULL)
        ilonglongDlist.Finalize(selected);
    if (scratch2 != NULL)
        ilonglongDlist.Finalize(scratch2);
    if (scratch != NULL)
        ilonglongDlist.Finalize(scratch);
    if (split != NULL)
        ilonglongDlist.Finalize(split);
    if (copy != NULL)
        ilonglongDlist.Finalize(copy);
    if (list != NULL)
        ilonglongDlist.Finalize(list);
    return -1;
}

static int test_null_status_and_placement(void)
{
    intDlist list_storage;
    unsigned char iterator_storage[128];
    intDlist *list;

    TEST_REQUIRE(iintDlist.Finalize(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iintDlist.InitIterator(NULL, iterator_storage) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iintDlist.Init(&list_storage, sizeof(int)) == &list_storage);
    list = &list_storage;
    TEST_REQUIRE(iintDlist.InitIterator(list, iterator_storage) == 1);
    TEST_REQUIRE(iintDlist.SizeofIterator(list) <= sizeof(iterator_storage));
    TEST_REQUIRE(iintDlist.Clear(list) == 1);
    return 0;

cleanup:
    return -1;
}

static int test_load_rejects_incompatible_width(void)
{
    double value = 3.5;
    Dlist *generic = NULL;
    intDlist *wrong = NULL;
    FILE *stream = NULL;

    generic = iDlist.Create(sizeof(double));
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(generic != NULL && stream != NULL);
    TEST_REQUIRE(iDlist.Add(generic, &value) == 1);
    TEST_REQUIRE(iDlist.Save(generic, stream, NULL, NULL) == 1);
    rewind(stream);
    wrong = iintDlist.Load(stream, NULL, NULL);
    TEST_REQUIRE(wrong == NULL);
    fclose(stream);
    iDlist.Finalize(generic);
    return 0;

cleanup:
    if (wrong != NULL)
        iintDlist.Finalize(wrong);
    if (stream != NULL)
        fclose(stream);
    if (generic != NULL)
        iDlist.Finalize(generic);
    return -1;
}

static int add_one_int(int *value, void *arg)
{
    *value += *(const int *)arg;
    return 1;
}

static int test_full_int_surface(void)
{
    intDlist *list = NULL;
    intDlist *copy = NULL;
    intDlist *range = NULL;
    intDlist *selected = NULL;
    intDlist *source = NULL;
    int value = 0;
    int replacement = 8;
    int delta = 1;
    int values[] = {1, 2, 3};
    size_t index = 0;
    intDlistElement *element;
    Mask *mask = NULL;
    FILE *stream = NULL;

    list = iintDlist.InitializeWith(sizeof(int), 3, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iintDlist.CopyElement(list, 1, &value) == 1 && value == 2);
    TEST_REQUIRE(iintDlist.ReplaceAt(list, 1, replacement) == 1);
    TEST_REQUIRE(iintDlist.PushFront(list, 0) == 1);
    TEST_REQUIRE(iintDlist.PushBack(list, 4) == 1);
    TEST_REQUIRE(iintDlist.InsertAt(list, 2, 7) == 1);
    TEST_REQUIRE(iintDlist.IndexOf(list, 7, NULL, &index) == 1);
    TEST_REQUIRE(iintDlist.Erase(list, 7) == 1);
    TEST_REQUIRE(iintDlist.EraseAll(list, 99) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iintDlist.PopFront(list, &value) == 1 && value == 0);
    TEST_REQUIRE(iintDlist.PopBack(list, &value) == 1 && value == 4);
    TEST_REQUIRE(iintDlist.Apply(list, add_one_int, &delta) == 1);
    TEST_REQUIRE(iintDlist.GetElement(list, 0) != NULL);
    TEST_REQUIRE(iintDlist.Front(list) != NULL && iintDlist.Back(list) != NULL);

    copy = iintDlist.Copy(list);
    TEST_REQUIRE(copy != NULL && iintDlist.Equal(list, copy) == 1);
    range = iintDlist.GetRange(list, 0, 1);
    TEST_REQUIRE(range != NULL && iintDlist.Size(range) == 2);

    mask = iMask.Create(iintDlist.Size(list));
    TEST_REQUIRE(mask != NULL);
    TEST_REQUIRE(iMask.SetElement(mask, 0, 1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 1, 0) == 1);
    TEST_REQUIRE(iMask.SetElement(mask, 2, 1) == 1);
    selected = iintDlist.SelectCopy(list, mask);
    TEST_REQUIRE(selected != NULL && iintDlist.Size(selected) == 2);
    TEST_REQUIRE(iintDlist.Select(list, mask) == 1);
    TEST_REQUIRE(iintDlist.Size(list) == 2);
    iMask.Finalize(mask);
    mask = NULL;

    TEST_REQUIRE(iintDlist.SetFlags(list, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iintDlist.GetFlags(list) == CONTAINER_READONLY);
    TEST_REQUIRE(iintDlist.SetFlags(list, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(iintDlist.RemoveRange(range, 0, 1) == 1);
    TEST_REQUIRE(iintDlist.RotateLeft(list, 1) == 1);
    TEST_REQUIRE(iintDlist.RotateRight(list, 1) == 1);
    TEST_REQUIRE(iintDlist.Reverse(list) == 1);

    element = iintDlist.FirstElement(list);
    TEST_REQUIRE(element != NULL && iintDlist.LastElement(list) != NULL);
    TEST_REQUIRE(iintDlist.NextElement(element) == element->Next);
    TEST_REQUIRE(iintDlist.PreviousElement(element) == NULL);
    TEST_REQUIRE(iintDlist.GetElementData(element) == &element->Data);
    TEST_REQUIRE(iintDlist.SetElementData(list, element, 22) == 1);
    TEST_REQUIRE(iintDlist.Advance(&element) != NULL);
    TEST_REQUIRE(iintDlist.MoveBack(&element) != NULL);
    TEST_REQUIRE(iintDlist.Skip(iintDlist.FirstElement(list), 1) != NULL);

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iintDlist.Save(list, stream, NULL, NULL) == 1);
    rewind(stream);
    iintDlist.Finalize(list);
    list = iintDlist.Load(stream, NULL, NULL);
    TEST_REQUIRE(list != NULL);
    fclose(stream);
    stream = NULL;

    source = iintDlist.Create(sizeof(int));
    TEST_REQUIRE(source != NULL && iintDlist.Add(source, 30) == 1);
    TEST_REQUIRE(iintDlist.InsertIn(list, 1, source) == 1);
    iintDlist.Finalize(source);
    source = NULL;

    source = iintDlist.Create(sizeof(int));
    TEST_REQUIRE(source != NULL && iintDlist.Add(source, 40) == 1);
    TEST_REQUIRE(iintDlist.Append(list, source) == 1);
    source = NULL;

    TEST_REQUIRE(iintDlist.Clear(list) >= 0);
    TEST_REQUIRE(iintDlist.UseHeap(list, NULL) == 1);
    TEST_REQUIRE(iintDlist.Add(list, 55) == 1);
    TEST_REQUIRE(iintDlist.Clear(list) >= 0);

    iintDlist.Finalize(selected);
    iintDlist.Finalize(range);
    iintDlist.Finalize(copy);
    iintDlist.Finalize(list);
    return 0;

cleanup:
    if (mask != NULL)
        iMask.Finalize(mask);
    if (stream != NULL)
        fclose(stream);
    if (source != NULL)
        iintDlist.Finalize(source);
    if (selected != NULL)
        iintDlist.Finalize(selected);
    if (range != NULL)
        iintDlist.Finalize(range);
    if (copy != NULL)
        iintDlist.Finalize(copy);
    if (list != NULL)
        iintDlist.Finalize(list);
    return -1;
}

static int test_typed_error_branches(void)
{
    intDlist *list = NULL;
    intDlist *other = NULL;
    intDlist *foreign = NULL;
    Mask *bad_mask = NULL;
    unsigned char storage[128];

    TEST_REQUIRE(iintDlist.Create(0) == NULL);
    TEST_REQUIRE(iintDlist.CreateWithAllocator(sizeof(int), NULL) == NULL);
    TEST_REQUIRE(iintDlist.InitializeWith(sizeof(long long), 0, NULL) == NULL);
    TEST_REQUIRE(iintDlist.Init(NULL, sizeof(int)) == NULL);
    TEST_REQUIRE(iintDlist.InitWithAllocator(NULL, sizeof(int), CurrentAllocator) == NULL);
    TEST_REQUIRE(iintDlist.GetAllocator(NULL) == NULL);
    TEST_REQUIRE(iintDlist.NextElement(NULL) == NULL);
    TEST_REQUIRE(iintDlist.InitIterator(NULL, storage) == CONTAINER_ERROR_BADARG);

    list = iintDlist.Create(sizeof(int));
    other = iintDlist.Create(sizeof(int));
    TEST_REQUIRE(list != NULL && other != NULL);
    TEST_REQUIRE(iintDlist.Add(list, 1) == 1);
    bad_mask = iMask.Create(2);
    TEST_REQUIRE(bad_mask != NULL);
    TEST_REQUIRE(iintDlist.SelectCopy(list, bad_mask) == NULL);
    TEST_REQUIRE(iintDlist.SelectCopy(NULL, bad_mask) == NULL);
    TEST_REQUIRE(iintDlist.GetRange(NULL, 0, 1) == NULL);
    TEST_REQUIRE(iintDlist.SplitAfter(NULL, NULL) == NULL);
    TEST_REQUIRE(iintDlist.Splice(NULL, NULL, other, 0) == NULL);
    TEST_REQUIRE(iintDlist.Append(NULL, other) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iintDlist.Apply(list, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iintDlist.SetDestructor(NULL, NULL) == NULL);
    TEST_REQUIRE(iintDlist.SetCompareFunction(list, NULL) != NULL);

    foreign = iintDlist.CreateWithAllocator(sizeof(int), &iDebugMalloc);
    TEST_REQUIRE(foreign != NULL && iintDlist.Add(foreign, 2) == 1);
    TEST_REQUIRE(iintDlist.Append(list, foreign) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iintDlist.Splice(list, iintDlist.FirstElement(list),
                                  foreign, 0) == NULL);

    iMask.Finalize(bad_mask);
    iintDlist.Finalize(foreign);
    iintDlist.Finalize(other);
    iintDlist.Finalize(list);
    return 0;

cleanup:
    if (bad_mask != NULL)
        iMask.Finalize(bad_mask);
    if (foreign != NULL)
        iintDlist.Finalize(foreign);
    if (other != NULL)
        iintDlist.Finalize(other);
    if (list != NULL)
        iintDlist.Finalize(list);
    return -1;
}

static const TestCase dlist_family_tests[] = {
    {"typed ABI, iterator, and sort", test_int_abi_iterator_sort},
    {"AddRange, split, and boundaries", test_add_range_split_and_typed_boundaries},
    {"double and long long scalar boundaries", test_double_and_longlong_scalar_widths},
    {"NULL status and placement", test_null_status_and_placement},
    {"persistence width rejection", test_load_rejects_incompatible_width},
    {"complete int interface surface", test_full_int_surface},
    {"complete double interface surface", test_double_surface},
    {"complete long long interface surface", test_longlong_surface},
    {"typed error and ownership branches", test_typed_error_branches},
};

static const TestSuite dlist_family_suite = {
    "typed dlist family",
    dlist_family_tests,
    sizeof(dlist_family_tests) / sizeof(dlist_family_tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &dlist_family_suite;
}
