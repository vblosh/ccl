#include "test_support.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "ccl_internal.h"
#include "size_tvector.h"

_Static_assert(sizeof(size_tVector) == sizeof(Vector), "vector layout");
_Static_assert(offsetof(size_tVector,VTable) == offsetof(Vector,VTable), "VTable");
_Static_assert(offsetof(size_tVector,count) == offsetof(Vector,count), "count");
_Static_assert(offsetof(size_tVector,Flags) == offsetof(Vector,Flags), "Flags");
_Static_assert(offsetof(size_tVector,ElementSize) == offsetof(Vector,ElementSize), "ElementSize");
_Static_assert(offsetof(size_tVector,contents) == offsetof(Vector,contents), "contents");
_Static_assert(offsetof(size_tVector,capacity) == offsetof(Vector,capacity), "capacity");
_Static_assert(offsetof(size_tVector,timestamp) == offsetof(Vector,timestamp), "timestamp");
_Static_assert(offsetof(size_tVector,CompareFn) == offsetof(Vector,CompareFn), "CompareFn");
_Static_assert(offsetof(size_tVector,RaiseError) == offsetof(Vector,RaiseError), "RaiseError");
_Static_assert(offsetof(size_tVector,Allocator) == offsetof(Vector,Allocator), "Allocator");
_Static_assert(offsetof(size_tVector,DestructorFn) == offsetof(Vector,DestructorFn), "DestructorFn");
_Static_assert(sizeof(struct size_tVectorIterator) == sizeof(struct VectorIterator), "iterator layout");
_Static_assert(offsetof(struct size_tVectorIterator,Magic) == offsetof(struct VectorIterator,Magic), "iterator Magic");
_Static_assert(offsetof(struct size_tVectorIterator,L) == offsetof(struct VectorIterator,AL), "iterator vector");
_Static_assert(offsetof(struct size_tVectorIterator,index) == offsetof(struct VectorIterator,index), "iterator index");
_Static_assert(offsetof(struct size_tVectorIterator,Current) == offsetof(struct VectorIterator,Current), "iterator current");
_Static_assert(offsetof(struct size_tVectorIterator,ElementBuffer) == offsetof(struct VectorIterator,ElementBuffer), "iterator buffer");

static int compare_size(const void *left,const void *right,CompareInfo *info)
{
    size_t a = *(const size_t *)left;
    size_t b = *(const size_t *)right;
    (void)info;
    return a < b ? -1 : a > b;
}

static int bump_size(size_t *value,void *arg)
{
    *value += arg ? *(const size_t *)arg : 1;
    return 1;
}

static int save_size(const void *value,void *arg,FILE *stream)
{
    (void)arg;
    return fwrite(value,sizeof(size_t),1,stream) == 1;
}

static int load_size(void *value,void *arg,FILE *stream)
{
    (void)arg;
    return fread(value,sizeof(size_t),1,stream) == 1;
}

static int destructor_count;

static int count_destructor(void *value)
{
    (void)value;
    ++destructor_count;
    return 1;
}

static int test_layout_and_scalar_surface(void)
{
    size_tVector *v = NULL, *copy = NULL, *range = NULL;
    size_t value = 7, out = 0, index = 0;
    size_t values[] = { 4, 2, 9, 2 };
    size_t extra[] = { 11, 13 };
    void **copies = NULL;
    size_t i;
    int result = 1;

    TEST_REQUIRE(isize_tVector.Size != NULL && isize_tVector.GetFlags != NULL);
    TEST_REQUIRE(isize_tVector.SetFlags != NULL && isize_tVector.Clear != NULL);
    TEST_REQUIRE(isize_tVector.Contains != NULL && isize_tVector.Erase != NULL);
    TEST_REQUIRE(isize_tVector.EraseAll != NULL && isize_tVector.Finalize != NULL);
    TEST_REQUIRE(isize_tVector.Apply != NULL && isize_tVector.Equal != NULL);
    TEST_REQUIRE(isize_tVector.Copy != NULL && isize_tVector.SetErrorFunction != NULL);
    TEST_REQUIRE(isize_tVector.Sizeof != NULL && isize_tVector.NewIterator != NULL);
    TEST_REQUIRE(isize_tVector.InitIterator != NULL && isize_tVector.deleteIterator != NULL);
    TEST_REQUIRE(isize_tVector.SizeofIterator != NULL && isize_tVector.Save != NULL);
    TEST_REQUIRE(isize_tVector.Load != NULL && isize_tVector.GetElementSize != NULL);
    TEST_REQUIRE(isize_tVector.Add != NULL && isize_tVector.GetElement != NULL);
    TEST_REQUIRE(isize_tVector.PushBack != NULL && isize_tVector.PopBack != NULL);
    TEST_REQUIRE(isize_tVector.InsertAt != NULL && isize_tVector.EraseAt != NULL);
    TEST_REQUIRE(isize_tVector.ReplaceAt != NULL && isize_tVector.IndexOf != NULL);
    TEST_REQUIRE(isize_tVector.Insert != NULL && isize_tVector.InsertIn != NULL);
    TEST_REQUIRE(isize_tVector.IndexIn != NULL && isize_tVector.GetCapacity != NULL);
    TEST_REQUIRE(isize_tVector.SetCapacity != NULL && isize_tVector.SetCompareFunction != NULL);
    TEST_REQUIRE(isize_tVector.Sort != NULL && isize_tVector.Create != NULL);
    TEST_REQUIRE(isize_tVector.CreateWithAllocator != NULL && isize_tVector.Init != NULL);
    TEST_REQUIRE(isize_tVector.AddRange != NULL && isize_tVector.GetRange != NULL);
    TEST_REQUIRE(isize_tVector.CopyElement != NULL && isize_tVector.CopyTo != NULL);
    TEST_REQUIRE(isize_tVector.Reverse != NULL && isize_tVector.Append != NULL);
    TEST_REQUIRE(isize_tVector.Mismatch != NULL && isize_tVector.GetAllocator != NULL);
    TEST_REQUIRE(isize_tVector.SetDestructor != NULL && isize_tVector.SearchWithKey != NULL);
    TEST_REQUIRE(isize_tVector.Select != NULL && isize_tVector.SelectCopy != NULL);
    TEST_REQUIRE(isize_tVector.Resize != NULL && isize_tVector.InitializeWith != NULL);
    TEST_REQUIRE(isize_tVector.GetData != NULL && isize_tVector.Back != NULL);
    TEST_REQUIRE(isize_tVector.Front != NULL && isize_tVector.RemoveRange != NULL);
    TEST_REQUIRE(isize_tVector.RotateLeft != NULL && isize_tVector.RotateRight != NULL);
    TEST_REQUIRE(isize_tVector.CompareEqual != NULL && isize_tVector.CompareEqualScalar != NULL);
    TEST_REQUIRE(isize_tVector.Reserve != NULL);

    v = isize_tVector.Create(1);
    TEST_REQUIRE(v != NULL && v->VTable == &isize_tVector);
    TEST_REQUIRE(isize_tVector.GetElementSize(v) == sizeof(size_t));
    TEST_REQUIRE(isize_tVector.GetAllocator(v) != NULL);
    TEST_REQUIRE(isize_tVector.Size(v) == 0 && isize_tVector.GetCapacity(v) >= 1);
    TEST_REQUIRE(isize_tVector.AddRange(v,4,values) == 1);
    TEST_REQUIRE(isize_tVector.Contains(v,9,NULL) == 1);
    TEST_REQUIRE(isize_tVector.IndexOf(v,9,NULL,&index) == 1 && index == 2);
    TEST_REQUIRE(isize_tVector.Contains(v,SIZE_MAX,NULL) == 0);
    TEST_REQUIRE(isize_tVector.Add(v,SIZE_MAX) == 1);
    TEST_REQUIRE(isize_tVector.Erase(v,9) == 1);
    TEST_REQUIRE(isize_tVector.EraseAll(v,2) == 1);
    TEST_REQUIRE(isize_tVector.InsertAt(v,1,value) == 1);
    TEST_REQUIRE(isize_tVector.ReplaceAt(v,1,8) == 1);
    TEST_REQUIRE(isize_tVector.Insert(v,3) == 1);
    TEST_REQUIRE(isize_tVector.PushBack(v,5) == 1);
    TEST_REQUIRE(isize_tVector.PopBack(v,&out) == 1 && out == 5);
    TEST_REQUIRE(isize_tVector.GetElement(v,1) != NULL);
    TEST_REQUIRE(isize_tVector.CopyElement(v,2,&out) == 1 && out == 8);
    TEST_REQUIRE(isize_tVector.CopyElement(v,999,&out) < 0);
    TEST_REQUIRE(isize_tVector.Back(v) != NULL && isize_tVector.Front(v) != NULL);
    TEST_REQUIRE(isize_tVector.SetCapacity(v,isize_tVector.GetCapacity(v)+5) == 1);
    TEST_REQUIRE(isize_tVector.Reserve(v,isize_tVector.GetCapacity(v)+3) == 1);
    TEST_REQUIRE(isize_tVector.AddRange(v,2,extra) == 1);
    range = isize_tVector.GetRange(v,1,isize_tVector.Size(v)-1);
    TEST_REQUIRE(range != NULL && isize_tVector.Size(range) == isize_tVector.Size(v)-1);
    copy = isize_tVector.Copy(v);
    TEST_REQUIRE(copy != NULL && isize_tVector.Equal(v,copy) == 1);
    copies = isize_tVector.CopyTo(v);
    TEST_REQUIRE(copies != NULL && copies[0] != NULL);
    for (i=0; copies[i] != NULL; ++i)
        v->Allocator->free(copies[i]);
    v->Allocator->free(copies);
    copies = NULL;
    TEST_REQUIRE(isize_tVector.SetCompareFunction(v,compare_size) != NULL);
    TEST_REQUIRE(isize_tVector.Sort(v) == 1);
    TEST_REQUIRE(isize_tVector.SearchWithKey(v,0,sizeof(size_t),0,8,&index) == 1);
    TEST_REQUIRE(isize_tVector.Apply(v,bump_size,&value) == 1);
    TEST_REQUIRE(isize_tVector.GetData(v) != NULL);
    TEST_REQUIRE(isize_tVector.RemoveRange(v,0,0) == 0);
    TEST_REQUIRE(isize_tVector.Reverse(v) == 1);
    TEST_REQUIRE(isize_tVector.RotateLeft(v,1) == 1);
    TEST_REQUIRE(isize_tVector.RotateRight(v,1) == 1);
    TEST_REQUIRE(isize_tVector.Mismatch(v,copy,&index) == 0 || index < isize_tVector.Size(v));
    result = 0;

cleanup:
    if (copies != NULL) {
        for (i=0; copies[i] != NULL; ++i)
            v->Allocator->free(copies[i]);
        v->Allocator->free(copies);
    }
    if (range != NULL) isize_tVector.Finalize(range);
    if (copy != NULL) isize_tVector.Finalize(copy);
    if (v != NULL) isize_tVector.Finalize(v);
    return result;
}

static int test_results_masks_and_iterators(void)
{
    size_t values[] = { 10, 20, 30, 40 };
    size_tVector *v = NULL, *other = NULL, *indices = NULL;
    size_tVector *selected = NULL, *range = NULL;
    Mask *mask = NULL, *comparison = NULL;
    Iterator *heap = NULL, *placement = NULL;
    unsigned char *storage = NULL;
    size_t index0 = 0, index1 = 1, index2 = 2, out = 0;
    int result = 1;

    v = isize_tVector.InitializeWith(4,values);
    other = isize_tVector.InitializeWith(4,values);
    TEST_REQUIRE(v != NULL && other != NULL);
    TEST_REQUIRE(isize_tVector.Append(v,other) == 1 && isize_tVector.Size(v) == 8);
    TEST_REQUIRE(isize_tVector.InsertIn(v,2,other) == 1);
    TEST_REQUIRE(isize_tVector.Size(v) == 12);
    indices = isize_tVector.InitializeWith(3,(size_t[]){ index0,index1,index2 });
    TEST_REQUIRE(indices != NULL);
    selected = isize_tVector.IndexIn(v,indices);
    TEST_REQUIRE(selected != NULL && isize_tVector.Size(selected) == 3);
    mask = iMask.Create(isize_tVector.Size(v));
    TEST_REQUIRE(mask != NULL);
    for (index0=0; index0<isize_tVector.Size(v); ++index0)
        TEST_REQUIRE(iMask.SetElement(mask,index0,index0 % 2 == 0) == 1);
    isize_tVector.Finalize(selected);
    selected = NULL;
    selected = isize_tVector.SelectCopy(v,mask);
    TEST_REQUIRE(selected != NULL && isize_tVector.Size(selected) == 6);
    TEST_REQUIRE(isize_tVector.Select(v,mask) == 1 && isize_tVector.Size(v) == 6);
    comparison = isize_tVector.CompareEqual(v,v,NULL);
    TEST_REQUIRE(comparison != NULL && iMask.Size(comparison) == isize_tVector.Size(v));
    comparison = isize_tVector.CompareEqualScalar(v,values[0],comparison);
    TEST_REQUIRE(comparison != NULL && iMask.Size(comparison) == isize_tVector.Size(v));
    range = isize_tVector.GetRange(v,0,2);
    TEST_REQUIRE(range != NULL && isize_tVector.Size(range) == 3);

    heap = isize_tVector.NewIterator(v);
    TEST_REQUIRE(heap != NULL && heap->GetFirst(heap) != NULL);
    TEST_REQUIRE(*(size_t *)heap->GetCurrent(heap) == 10);
    TEST_REQUIRE(heap->GetNext(heap) != NULL && heap->GetPosition(heap) == 1);
    TEST_REQUIRE(heap->GetPrevious(heap) != NULL);
    TEST_REQUIRE(heap->GetLast(heap) != NULL && heap->Seek(heap,0) != NULL);
    out = 55;
    TEST_REQUIRE(heap->Replace(heap,&out,1) == 1);
    TEST_REQUIRE(isize_tVector.CopyElement(v,0,&out) == 1 && out == 55);
    TEST_REQUIRE(isize_tVector.deleteIterator(heap) == 1);
    heap = NULL;
    storage = malloc(isize_tVector.SizeofIterator(v));
    TEST_REQUIRE(storage != NULL);
    TEST_REQUIRE(isize_tVector.InitIterator(v,storage) == 1);
    placement = (Iterator *)storage;
    TEST_REQUIRE(placement->GetFirst(placement) != NULL);
    TEST_REQUIRE(isize_tVector.deleteIterator(placement) == 1);
    placement = NULL;
    result = 0;

cleanup:
    if (heap != NULL) isize_tVector.deleteIterator(heap);
    if (placement != NULL) isize_tVector.deleteIterator(placement);
    free(storage);
    if (comparison != NULL) iMask.Finalize(comparison);
    if (mask != NULL) iMask.Finalize(mask);
    if (range != NULL) isize_tVector.Finalize(range);
    if (selected != NULL) isize_tVector.Finalize(selected);
    if (indices != NULL) isize_tVector.Finalize(indices);
    if (other != NULL) isize_tVector.Finalize(other);
    if (v != NULL) isize_tVector.Finalize(v);
    return result;
}

static int test_persistence_and_placement(void)
{
    size_t values[] = { 1, SIZE_MAX, 3 };
    size_tVector *v = NULL, *loaded = NULL, *bad = NULL;
    Vector *generic = NULL;
    size_tVector placement;
    FILE *stream = NULL;
    int result = 1;

    v = isize_tVector.InitializeWith(3,values);
    TEST_REQUIRE(v != NULL);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL && isize_tVector.Save(v,stream,save_size,NULL) == 1);
    rewind(stream);
    loaded = isize_tVector.Load(stream,load_size,NULL);
    TEST_REQUIRE(loaded != NULL && loaded->VTable == &isize_tVector);
    TEST_REQUIRE(isize_tVector.Equal(v,loaded) == 1);
    fclose(stream);
    stream = NULL;
    if (sizeof(int) != sizeof(size_t)) {
        int ivalue = 4;
        generic = iVector.InitializeWith(sizeof(int),1,&ivalue);
        TEST_REQUIRE(generic != NULL);
        stream = ccl_test_tmpfile();
        TEST_REQUIRE(stream != NULL && iVector.Save(generic,stream,NULL,NULL) == 1);
        rewind(stream);
        bad = isize_tVector.Load(stream,NULL,NULL);
        TEST_REQUIRE(bad == NULL);
    }
    memset(&placement,0,sizeof(placement));
    TEST_REQUIRE(isize_tVector.Init(&placement,1) == &placement);
    TEST_REQUIRE(placement.VTable == &isize_tVector);
    TEST_REQUIRE(isize_tVector.Add(&placement,99) == 1);
    TEST_REQUIRE(isize_tVector.Finalize(&placement) == 1);
    result = 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (bad != NULL) isize_tVector.Finalize(bad);
    if (generic != NULL) iVector.Finalize(generic);
    if (loaded != NULL) isize_tVector.Finalize(loaded);
    if (v != NULL) isize_tVector.Finalize(v);
    return result;
}

static int test_readonly_and_failures(void)
{
    size_tVector *v = NULL, *empty = NULL;
    size_t value = 4, out = 0, index = 0;
    Iterator *it = NULL;
    Mask *mask = NULL;
    int result = 1;

    empty = isize_tVector.Create(0);
    TEST_REQUIRE(empty != NULL);
    v = isize_tVector.Create(2);
    TEST_REQUIRE(v != NULL);
    TEST_REQUIRE(isize_tVector.Add(v,value) == 1);
    TEST_REQUIRE(isize_tVector.SetFlags(v,CONTAINER_READONLY) == 0);
    TEST_REQUIRE(isize_tVector.GetFlags(v) == CONTAINER_READONLY);
    TEST_REQUIRE(isize_tVector.Apply(v,bump_size,&value) == 1);
    TEST_REQUIRE(isize_tVector.Add(v,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(isize_tVector.PushBack(v,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(isize_tVector.PopBack(v,&out) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(isize_tVector.ReplaceAt(v,0,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(isize_tVector.Clear(v) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(isize_tVector.Sort(v) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(isize_tVector.Resize(v,2) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(isize_tVector.GetElement(v,0) == NULL);
    TEST_REQUIRE(isize_tVector.GetData(v) == NULL);
    TEST_REQUIRE(isize_tVector.SearchWithKey(v,0,sizeof(size_t),0,value,&index) == 1);
    it = isize_tVector.NewIterator(v);
    TEST_REQUIRE(it != NULL && it->GetFirst(it) != NULL);
    isize_tVector.SetFlags(v,0);
    TEST_REQUIRE(isize_tVector.Add(v,value) == 1);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    isize_tVector.deleteIterator(it);
    it = NULL;
    TEST_REQUIRE(isize_tVector.GetRange(v,99,100) == NULL);
    TEST_REQUIRE(isize_tVector.IndexOf(v,value,NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(isize_tVector.SearchWithKey(v,0,sizeof(size_t),0,value,NULL) < 0);
    mask = iMask.Create(1);
    TEST_REQUIRE(mask != NULL && isize_tVector.Select(v,mask) == CONTAINER_ERROR_BADMASK);
    result = 0;

cleanup:
    if (it != NULL) isize_tVector.deleteIterator(it);
    if (mask != NULL) iMask.Finalize(mask);
    if (empty != NULL) isize_tVector.Finalize(empty);
    if (v != NULL) isize_tVector.Finalize(v);
    return result;
}

static int test_destructor_and_nulls(void)
{
    size_tVector *v = NULL, *empty = NULL;
    Vector *wrong = NULL;
    int result = 1;

    TEST_REQUIRE(isize_tVector.Size(NULL) == 0);
    TEST_REQUIRE(isize_tVector.GetFlags(NULL) == 0);
    TEST_REQUIRE(isize_tVector.SetFlags(NULL,0) == 0);
    TEST_REQUIRE(isize_tVector.Equal(NULL,NULL) == 1);
    TEST_REQUIRE(isize_tVector.SizeofIterator(NULL) > 0);
    TEST_REQUIRE(isize_tVector.Save(NULL,NULL,NULL,NULL) < 0);
    TEST_REQUIRE(isize_tVector.Load(NULL,NULL,NULL) == NULL);
    TEST_REQUIRE(isize_tVector.Apply(NULL,bump_size,NULL) < 0);
    TEST_REQUIRE(isize_tVector.Init(NULL,1) == NULL);
    TEST_REQUIRE(isize_tVector.GetElementSize(NULL) == 0);
    TEST_REQUIRE(isize_tVector.Copy(NULL) == NULL);
    TEST_REQUIRE(isize_tVector.GetRange(NULL,0,0) == NULL);
    TEST_REQUIRE(isize_tVector.NewIterator(NULL) == NULL);
    TEST_REQUIRE(isize_tVector.InitIterator(NULL,NULL) < 0);
    TEST_REQUIRE(isize_tVector.Finalize(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(isize_tVector.InitializeWith(1,NULL) == NULL);
    empty = isize_tVector.InitializeWith(0,NULL);
    TEST_REQUIRE(empty != NULL);
    if (sizeof(int) != sizeof(size_t)) {
        wrong = iVector.Create(sizeof(int),1);
        TEST_REQUIRE(wrong != NULL);
        TEST_REQUIRE(isize_tVector.GetElementSize((size_tVector *)wrong) == 0);
    }
    v = isize_tVector.Create(2);
    TEST_REQUIRE(v != NULL);
    destructor_count = 0;
    TEST_REQUIRE(isize_tVector.SetDestructor(v,count_destructor) == NULL);
    TEST_REQUIRE(isize_tVector.Add(v,1) == 1 && isize_tVector.Add(v,2) == 1);
    TEST_REQUIRE(isize_tVector.Clear(v) == 1 && destructor_count == 2);
    result = 0;

cleanup:
    if (wrong != NULL) iVector.Finalize(wrong);
    if (empty != NULL) isize_tVector.Finalize(empty);
    if (v != NULL) isize_tVector.Finalize(v);
    return result;
}

static const TestCase tests[] = {
    { "layout and scalar surface", test_layout_and_scalar_surface },
    { "results, masks, and iterators", test_results_masks_and_iterators },
    { "persistence and placement", test_persistence_and_placement },
    { "read-only and failures", test_readonly_and_failures },
    { "destructor and null paths", test_destructor_and_nulls }
};

static const TestSuite suite = {
    "vector family",
    tests,
    sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
