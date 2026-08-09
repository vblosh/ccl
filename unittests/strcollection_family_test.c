#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

static int destructor_count;
static int quiet_error_count;

static void *quiet_error(const char *name,int code,...)
{
    (void)name;
    (void)code;
    ++quiet_error_count;
    return NULL;
}

static int count_destructor(void *p)
{
    (void)p;
    ++destructor_count;
    return 1;
}

static int apply_string(char *p,void *arg)
{
    (void)p;
    (void)arg;
    return 1;
}

typedef struct {
    size_t calls;
    size_t fail_at;
} FailureState;

static FailureState failure_state;

static void *failure_malloc(size_t size)
{
    ++failure_state.calls;
    if (failure_state.fail_at != 0 && failure_state.calls >= failure_state.fail_at)
        return NULL;
    return malloc(size);
}

static void *failure_realloc(void *p,size_t size)
{
    if (p == NULL)
        return failure_malloc(size);
    ++failure_state.calls;
    if (failure_state.fail_at != 0 && failure_state.calls >= failure_state.fail_at)
        return NULL;
    return realloc(p,size);
}

static void failure_free(void *p)
{
    free(p);
}

static void *failure_calloc(size_t n,size_t size)
{
    if (size != 0 && n > SIZE_MAX/size)
        return NULL;
    return failure_malloc(n*size);
}

static ContainerAllocator failure_allocator = {
    failure_malloc, failure_free, failure_realloc, failure_calloc
};

static int test_narrow_sequence_and_ownership(void)
{
    strCollection *sc = NULL;
    strCollection *other = NULL;
    strCollection *range = NULL;
    const char *values[] = {"beta", "gamma"};
    const char *self_values[] = {"alpha"};
    size_t index;
    unsigned old_flags;

    sc = istrCollection.Create(0);
    TEST_REQUIRE(sc != NULL);
    istrCollection.SetErrorFunction(sc,quiet_error);
    TEST_REQUIRE(istrCollection.Add(sc,"alpha") == 1);
    TEST_REQUIRE(istrCollection.InsertAt(sc,1,"omega") == 1);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(sc,1),"omega") == 0);
    TEST_REQUIRE(istrCollection.InsertAt(sc,0,"zero") == 1);
    TEST_REQUIRE(istrCollection.PushFront(sc,"front") == 1);
    TEST_REQUIRE(istrCollection.PushBack(sc,"back") == 1);
    TEST_REQUIRE(istrCollection.AddRange(sc,2,values) == 1);
    TEST_REQUIRE(istrCollection.Size(sc) == 7);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(sc,6),"gamma") == 0);

    /* AddRange duplicates its input; the source can disappear independently. */
    other = istrCollection.Create(1);
    TEST_REQUIRE(other != NULL && istrCollection.Add(other,"owned") == 1);
    TEST_REQUIRE(istrCollection.Append(sc,other) == 1);
    TEST_REQUIRE(istrCollection.Clear(other) == 1);
    TEST_REQUIRE(istrCollection.Size(sc) == 8);
    TEST_REQUIRE(strcmp(istrCollection.Back(sc),"owned") == 0);

    /* Self-append is also transactional and produces independent copies. */
    TEST_REQUIRE(istrCollection.AddRange(sc,1,self_values) == 1);
    TEST_REQUIRE(istrCollection.Append(sc,sc) == 1);
    TEST_REQUIRE(istrCollection.Size(sc) == 18);

    range = istrCollection.GetRange(sc,2,5);
    TEST_REQUIRE(range != NULL && istrCollection.Size(range) == 3);
    TEST_REQUIRE(strcmp(istrCollection.Front(range),"alpha") == 0);
    TEST_REQUIRE(istrCollection.IndexOf(range,"omega",&index) == 1 && index == 1);

    TEST_REQUIRE(istrCollection.SetCapacity(sc,3) == 1);
    TEST_REQUIRE(istrCollection.Size(sc) == 3);
    TEST_REQUIRE(istrCollection.GetCapacity(sc) == 3);
    TEST_REQUIRE(istrCollection.SetCapacity(sc,0) == 1);
    TEST_REQUIRE(istrCollection.Size(sc) == 0);
    TEST_REQUIRE(istrCollection.Add(sc,"reuse") == 1);
    TEST_REQUIRE(strcmp(istrCollection.Front(sc),"reuse") == 0);

    destructor_count = 0;
    istrCollection.SetDestructor(sc,count_destructor);
    TEST_REQUIRE(istrCollection.Add(sc,"drop") == 1);
    TEST_REQUIRE(istrCollection.SetCapacity(sc,1) == 1);
    TEST_REQUIRE(destructor_count == 1);
    TEST_REQUIRE(istrCollection.Add(sc,"one") == 1);
    TEST_REQUIRE(istrCollection.Add(sc,"two") == 1);
    {
        Mask *mask = iMask.Create(3);
        TEST_REQUIRE(mask != NULL);
        TEST_REQUIRE(iMask.SetElement(mask,0,0) == 1);
        TEST_REQUIRE(iMask.SetElement(mask,1,1) == 1);
        TEST_REQUIRE(iMask.SetElement(mask,2,0) == 1);
        TEST_REQUIRE(istrCollection.Select(sc,mask) == 1);
        TEST_REQUIRE(istrCollection.Size(sc) == 1);
        TEST_REQUIRE(strcmp(istrCollection.Front(sc),"one") == 0);
        iMask.Finalize(mask);
    }

    old_flags = istrCollection.SetFlags(sc,CONTAINER_READONLY);
    TEST_REQUIRE(old_flags == 0);
    TEST_REQUIRE(istrCollection.GetElement(sc,0) == NULL);
    TEST_REQUIRE(istrCollection.RemoveRange(sc,0,1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(istrCollection.Finalize(sc) == 1);
    sc = NULL;
    istrCollection.Finalize(range);
    range = NULL;
    istrCollection.Finalize(other);
    other = NULL;
    return 0;

cleanup:
    if (range != NULL) istrCollection.Finalize(range);
    if (other != NULL) istrCollection.Finalize(other);
    if (sc != NULL) {
        istrCollection.SetFlags(sc,0);
        istrCollection.Finalize(sc);
    }
    return -1;
}

static int test_narrow_results_and_buffers(void)
{
    strCollection *sc = NULL;
    strCollection *copy = NULL;
    strCollection *selected = NULL;
    Vector *indices = NULL;
    Vector *positions = NULL;
    Vector *array = NULL;
    Mask *mask = NULL;
    const char *values[] = {"alpha one", "beta", "alpha two"};
    char out[16];
    size_t *ip;

    sc = istrCollection.InitializeWith(3,(char **)values);
    TEST_REQUIRE(sc != NULL);
    TEST_REQUIRE(istrCollection.Contains(sc,"beta") == 1);
    TEST_REQUIRE(istrCollection.FindFirst(sc,"alpha") == 1);
    TEST_REQUIRE(istrCollection.FindNext(sc,"alpha",1) == 3);
    indices = istrCollection.FindTextIndex(sc,"alpha");
    TEST_REQUIRE(indices != NULL && iVector.Size(indices) == 2);
    ip = (size_t *)iVector.GetElement(indices,1);
    TEST_REQUIRE(ip != NULL && *ip == 2);
    positions = istrCollection.FindTextPositions(sc,"alpha");
    TEST_REQUIRE(positions != NULL && iVector.Size(positions) == 4);
    ip = (size_t *)iVector.GetElement(positions,3);
    TEST_REQUIRE(ip != NULL && *ip == 0);
    array = istrCollection.CastToArray(sc);
    TEST_REQUIRE(array != NULL && iVector.Size(array) == 3);
    TEST_REQUIRE(*(char **)iVector.GetElement(array,0) == istrCollection.GetElement(sc,0));

    copy = istrCollection.Copy(sc);
    TEST_REQUIRE(copy != NULL && istrCollection.Equal(sc,copy) == 1);
    TEST_REQUIRE(istrCollection.GetElement(copy,0) != istrCollection.GetElement(sc,0));
    selected = istrCollection.SelectCopy(sc,mask);
    TEST_REQUIRE(selected == NULL); /* malformed/null mask is rejected safely */
    mask = iMask.Create(3);
    TEST_REQUIRE(mask != NULL);
    iMask.SetElement(mask,0,1);
    iMask.SetElement(mask,1,0);
    iMask.SetElement(mask,2,1);
    selected = istrCollection.SelectCopy(sc,mask);
    TEST_REQUIRE(selected != NULL && istrCollection.Size(selected) == 2);

    TEST_REQUIRE(istrCollection.PopFront(sc,out,sizeof(out)) == strlen("alpha one")+1);
    TEST_REQUIRE(strcmp(out,"alpha one") == 0);
    TEST_REQUIRE(istrCollection.PopBack(sc,out,2) == strlen("alpha two")+1);
    TEST_REQUIRE(out[0] == 'a' && out[1] == '\0');
    TEST_REQUIRE(istrCollection.PopBack(sc,NULL,0) == strlen("beta")+1);
    TEST_REQUIRE(istrCollection.Size(sc) == 0);

    iMask.Finalize(mask);
    iVector.Finalize(array);
    iVector.Finalize(positions);
    iVector.Finalize(indices);
    istrCollection.Finalize(selected);
    istrCollection.Finalize(copy);
    istrCollection.Finalize(sc);
    return 0;

cleanup:
    if (mask != NULL) iMask.Finalize(mask);
    if (array != NULL) iVector.Finalize(array);
    if (positions != NULL) iVector.Finalize(positions);
    if (indices != NULL) iVector.Finalize(indices);
    if (selected != NULL) istrCollection.Finalize(selected);
    if (copy != NULL) istrCollection.Finalize(copy);
    if (sc != NULL) istrCollection.Finalize(sc);
    return -1;
}

static int test_wide_and_persistence(void)
{
    WstrCollection *wide = NULL;
    WstrCollection *loaded = NULL;
    strCollection *narrow = NULL;
    strCollection *narrow_loaded = NULL;
    FILE *stream = NULL;
    wchar_t out[16];
    wchar_t *wide_values[] = {L"alpha", L"\u03b2eta", L""};
    char *narrow_values[] = {"short", "", "a longer record"};

    wide = iWstrCollection.InitializeWith(3,wide_values);
    TEST_REQUIRE(wide != NULL);
    TEST_REQUIRE(iWstrCollection.Sizeof(wide) > sizeof(*wide));
    TEST_REQUIRE(iWstrCollection.PopFront(wide,out,16) == 6);
    TEST_REQUIRE(wcscmp(out,L"alpha") == 0);
    TEST_REQUIRE(iWstrCollection.PopBack(wide,out,16) == 1);
    TEST_REQUIRE(out[0] == L'\0');
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iWstrCollection.Save(wide,stream,NULL,NULL) == 1);
    rewind(stream);
    loaded = iWstrCollection.Load(stream,NULL,NULL);
    TEST_REQUIRE(loaded != NULL && iWstrCollection.Size(loaded) == 1);
    TEST_REQUIRE(wcscmp(iWstrCollection.Front(loaded),L"\u03b2eta") == 0);
    fclose(stream);
    stream = NULL;

    narrow = istrCollection.InitializeWith(3,narrow_values);
    TEST_REQUIRE(narrow != NULL);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(istrCollection.Save(narrow,stream,NULL,NULL) == 1);
    rewind(stream);
    narrow_loaded = istrCollection.Load(stream,NULL,NULL);
    TEST_REQUIRE(narrow_loaded != NULL && istrCollection.Equal(narrow,narrow_loaded));
    fclose(stream);
    stream = NULL;

    iWstrCollection.Finalize(loaded);
    iWstrCollection.Finalize(wide);
    istrCollection.Finalize(narrow_loaded);
    istrCollection.Finalize(narrow);
    return 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (loaded != NULL) iWstrCollection.Finalize(loaded);
    if (wide != NULL) iWstrCollection.Finalize(wide);
    if (narrow_loaded != NULL) istrCollection.Finalize(narrow_loaded);
    if (narrow != NULL) istrCollection.Finalize(narrow);
    return -1;
}

static int test_failure_rollback_and_iterator(void)
{
    strCollection *sc = NULL;
    strCollection *failed = NULL;
    Iterator *it = NULL;
    size_t iterator_size;
    char storage[256];
    const char *values[] = {"one", "two", "three"};

    failure_state.calls = 0;
    failure_state.fail_at = 4;
    failed = istrCollection.CreateWithAllocator(0,&failure_allocator);
    TEST_REQUIRE(failed != NULL);
    TEST_REQUIRE(istrCollection.AddRange(failed,3,values) <= 0);
    TEST_REQUIRE(istrCollection.Size(failed) == 0);
    failure_state.fail_at = 0;
    istrCollection.Finalize(failed);
    failed = NULL;

    sc = istrCollection.InitializeWith(3,(char **)values);
    TEST_REQUIRE(sc != NULL);
    it = istrCollection.NewIterator(sc);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) == istrCollection.GetElement(sc,0));
    TEST_REQUIRE(it->GetPosition(it) == 0);
    TEST_REQUIRE(it->GetLast(it) == istrCollection.GetElement(sc,2));
    TEST_REQUIRE(it->GetPosition(it) == 2);
    TEST_REQUIRE(istrCollection.RemoveRange(sc,1,2) == 1);
    TEST_REQUIRE(it->GetCurrent(it) == NULL);
    TEST_REQUIRE(it->GetPosition(it) == SIZE_MAX);
    istrCollection.DeleteIterator(it);
    it = NULL;

    iterator_size = istrCollection.SizeofIterator(sc);
    TEST_REQUIRE(iterator_size <= sizeof(storage));
    memset(storage,0xa5,sizeof(storage));
    TEST_REQUIRE(istrCollection.InitIterator(sc,storage) == 1);
    it = (Iterator *)storage;
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->GetPosition(it) == 0);
    TEST_REQUIRE(istrCollection.Clear(sc) == 1);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    TEST_REQUIRE(istrCollection.DeleteIterator(it) == 1);
    it = NULL;
    /* The legacy generic adapter dispatches through incompatible historical
     * function-pointer typedefs; keep this regression in the normal and
     * coverage runs, while avoiding an unrelated UBSan indirect-call abort. */
#if defined(__clang__)
# if __has_feature(address_sanitizer)
#  define CCL_STR_ASAN 1
# endif
#endif
#ifndef CCL_STR_ASAN
# define CCL_STR_ASAN 0
#endif
#if !defined(__SANITIZE_ADDRESS__) && !CCL_STR_ASAN
    memset(storage,0xa5,sizeof(storage));
    TEST_REQUIRE(iGeneric.InitIterator((GenericContainer *)sc,storage) == 1);
    it = (Iterator *)storage;
    TEST_REQUIRE(it->GetFirst(it) == NULL);
    TEST_REQUIRE(iGeneric.DeleteIterator(it) == 1);
    it = NULL;
#endif
    istrCollection.Finalize(sc);
    return 0;

cleanup:
    if (it != NULL && (void *)it != (void *)storage)
        istrCollection.DeleteIterator(it);
    if (failed != NULL) {
        failure_state.fail_at = 0;
        istrCollection.Finalize(failed);
    }
    if (sc != NULL) istrCollection.Finalize(sc);
    return -1;
}

static int test_api_surface(void)
{
    strCollection *sc = NULL;
    strCollection *empty = NULL;
    strCollection *copy = NULL;
    strCollection *loaded = NULL;
    strCollection *from_file = NULL;
    strCollection *range = NULL;
    strCollection *selected = NULL;
    Vector *vector = NULL;
    Vector *array = NULL;
    Mask *mask = NULL;
    Mask *comparison = NULL;
    Mask *scalar_comparison = NULL;
    Iterator *it = NULL;
    char **owned = NULL;
    FILE *stream = NULL;
    char storage[256];
    size_t idx = 1;
    const char *file_name = "strcollection_family_lines.txt";

    sc = istrCollection.Create(1);
    empty = istrCollection.Create(0);
    TEST_REQUIRE(sc != NULL && empty != NULL);
    TEST_REQUIRE(istrCollection.GetAllocator(sc) != NULL);
    TEST_REQUIRE(istrCollection.GetAllocator(NULL) == NULL);
    (void)istrCollection.SetCompareFunction(sc,sc->strcompare);
    TEST_REQUIRE(istrCollection.GetElementSize(sc) == sizeof(void *));
    TEST_REQUIRE(istrCollection.Sizeof(sc) >= sizeof(*sc));
    TEST_REQUIRE(istrCollection.Add(sc,"b") == 1);
    TEST_REQUIRE(istrCollection.PushBack(sc,"c") == 1);
    TEST_REQUIRE(istrCollection.PushFront(sc,"a") == 1);
    TEST_REQUIRE(istrCollection.Insert(sc,"start") == 1);
    TEST_REQUIRE(istrCollection.InsertAt(sc,2,"middle") == 1);
    TEST_REQUIRE(istrCollection.ReplaceAt(sc,2,"replaced") == 1);
    (void)istrCollection.SetCompareFunction(sc,NULL);
    TEST_REQUIRE(istrCollection.Contains(sc,"replaced") == 1);
    TEST_REQUIRE(istrCollection.IndexOf(sc,"replaced",&idx) == 1);
    TEST_REQUIRE(istrCollection.GetData(sc) != NULL);
    TEST_REQUIRE(istrCollection.Front(sc) != NULL && istrCollection.Back(sc) != NULL);
    TEST_REQUIRE(istrCollection.Apply(sc,apply_string,NULL) == 1);
    TEST_REQUIRE(istrCollection.Reverse(sc) == 1);
    TEST_REQUIRE(istrCollection.Sort(sc) == 1);
    TEST_REQUIRE(istrCollection.FindFirst(sc,"b") != 0);
    TEST_REQUIRE(istrCollection.FindNext(sc,"b",0) != 0);
    TEST_REQUIRE(istrCollection.FindFirst(sc,"absent") == 0);
    {
        strCollection *found = istrCollection.FindText(sc,"b");
        TEST_REQUIRE(found != NULL);
        istrCollection.Finalize(found);
    }
    vector = istrCollection.FindTextIndex(sc,"b");
    TEST_REQUIRE(vector != NULL);
    iVector.Finalize(vector);
    vector = NULL;
    vector = istrCollection.FindTextPositions(sc,"b");
    TEST_REQUIRE(vector != NULL);
    iVector.Finalize(vector);
    vector = NULL;
    array = istrCollection.CastToArray(sc);
    TEST_REQUIRE(array != NULL);
    iVector.Finalize(array);
    array = NULL;

    copy = istrCollection.Copy(sc);
    TEST_REQUIRE(copy != NULL);
    TEST_REQUIRE(istrCollection.Equal(sc,copy) == 1);
    TEST_REQUIRE(istrCollection.Equal(empty,empty) == 1);
    TEST_REQUIRE(istrCollection.Mismatch(sc,copy,&idx) == 0);
    TEST_REQUIRE(istrCollection.Mismatch(empty,empty,&idx) == 0);
    TEST_REQUIRE(istrCollection.Equal(empty,sc) == 0);
    range = istrCollection.GetRange(sc,1,3);
    TEST_REQUIRE(range != NULL);
    {
        strCollection *reversed = istrCollection.GetRange(sc,3,1);
        TEST_REQUIRE(reversed != NULL && istrCollection.Size(reversed) == 0);
        istrCollection.Finalize(reversed);
    }
    owned = istrCollection.CopyTo(range);
    TEST_REQUIRE(owned != NULL);
    for (idx=0; owned[idx] != NULL; ++idx)
        istrCollection.GetAllocator(range)->free(owned[idx]);
    istrCollection.GetAllocator(range)->free(owned);
    owned = NULL;

    vector = iVector.Create(sizeof(size_t),1);
    TEST_REQUIRE(vector != NULL && iVector.Add(vector,&idx) == 1);
    selected = istrCollection.IndexIn(sc,vector);
    TEST_REQUIRE(selected != NULL);
    istrCollection.Finalize(selected);
    selected = NULL;
    iVector.Finalize(vector);
    vector = NULL;
    mask = iMask.Create(istrCollection.Size(sc));
    TEST_REQUIRE(mask != NULL);
    for (idx=0; idx<istrCollection.Size(sc); ++idx)
        iMask.SetElement(mask,idx,(int)(idx%2));
    comparison = istrCollection.CompareEqual(sc,sc,NULL);
    TEST_REQUIRE(comparison != NULL);
    comparison = istrCollection.CompareEqual(sc,sc,comparison);
    TEST_REQUIRE(comparison != NULL);
    scalar_comparison = istrCollection.CompareEqualScalar(sc,"b",NULL);
    TEST_REQUIRE(scalar_comparison != NULL);
    selected = istrCollection.SelectCopy(sc,mask);
    TEST_REQUIRE(selected != NULL);
    TEST_REQUIRE(istrCollection.Select(sc,mask) == 1);
    iMask.Finalize(comparison);
    comparison = NULL;
    iMask.Finalize(scalar_comparison);
    scalar_comparison = NULL;
    iMask.Finalize(mask);
    mask = NULL;

    TEST_REQUIRE(istrCollection.Add(sc,"insert-source") == 1);
    TEST_REQUIRE(istrCollection.InsertIn(sc,1,sc) == 1);
    TEST_REQUIRE(istrCollection.Erase(sc,"insert-source") == 1);
    TEST_REQUIRE(istrCollection.EraseAll(sc,"not-present") == CONTAINER_ERROR_NOTFOUND);
    {
        strCollection *single = istrCollection.Create(1);
        TEST_REQUIRE(single != NULL && istrCollection.Add(single,"single") == 1);
        TEST_REQUIRE(istrCollection.Reverse(single) == 1);
        TEST_REQUIRE(istrCollection.EraseAt(single,0) == 1);
        istrCollection.Finalize(single);
    }
    {
        strCollection *clamped = istrCollection.GetRange(sc,0,SIZE_MAX);
        TEST_REQUIRE(clamped != NULL);
        istrCollection.Finalize(clamped);
    }
    TEST_REQUIRE(istrCollection.RemoveRange(sc,0,0) == 0);
    TEST_REQUIRE(istrCollection.RemoveRange(sc,0,1) == 1);
    TEST_REQUIRE(istrCollection.SetCapacity(sc,istrCollection.Size(sc)+4) == 1);

    it = istrCollection.NewIterator(sc);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetCurrent(it) == NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->GetNext(it) != NULL || istrCollection.Size(sc) == 1);
    TEST_REQUIRE(it->GetLast(it) != NULL);
    TEST_REQUIRE(it->GetPrevious(it) != NULL || istrCollection.Size(sc) == 1);
    TEST_REQUIRE(it->Seek(it,0) != NULL);
    TEST_REQUIRE(it->GetPosition(it) == 0);
    TEST_REQUIRE(it->Replace(it,"iterator-replacement",1) == 1);
    TEST_REQUIRE(it->GetLast(it) != NULL);
    TEST_REQUIRE(it->Replace(it,NULL,0) == 1);
    istrCollection.DeleteIterator(it);
    it = NULL;
    memset(storage,0xa5,sizeof(storage));
    TEST_REQUIRE(istrCollection.InitIterator(sc,storage) == 1);
    it = (Iterator *)storage;
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    it = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL && istrCollection.Save(sc,stream,NULL,NULL) == 1);
    rewind(stream);
    loaded = istrCollection.Load(stream,NULL,NULL);
    TEST_REQUIRE(loaded != NULL);
    fclose(stream);
    stream = NULL;
    TEST_REQUIRE(istrCollection.WriteToFile(sc,file_name) == 1);
    from_file = istrCollection.CreateFromFile(file_name);
    TEST_REQUIRE(from_file != NULL);
    remove(file_name);
    istrCollection.Finalize(from_file);
    from_file = NULL;
    istrCollection.Finalize(loaded);
    loaded = NULL;
    istrCollection.Finalize(selected);
    selected = NULL;
    istrCollection.Finalize(range);
    range = NULL;
    istrCollection.Finalize(copy);
    copy = NULL;
    istrCollection.SetDestructor(sc,count_destructor);
    TEST_REQUIRE(istrCollection.Clear(sc) == 1);
    istrCollection.Finalize(sc);
    sc = NULL;
    istrCollection.Finalize(empty);
    empty = NULL;

    {
        strCollection placement;
        TEST_REQUIRE(istrCollection.Init(&placement,0) == &placement);
        TEST_REQUIRE(istrCollection.Size(&placement) == 0);
    }
    return 0;

cleanup:
    if (stream != NULL) fclose(stream);
    remove(file_name);
    if (it != NULL && (void *)it != (void *)storage)
        istrCollection.DeleteIterator(it);
    if (owned != NULL) {
        for (idx=0; owned[idx] != NULL; ++idx)
            istrCollection.GetAllocator(range)->free(owned[idx]);
        istrCollection.GetAllocator(range)->free(owned);
    }
    if (mask != NULL) iMask.Finalize(mask);
    if (comparison != NULL) iMask.Finalize(comparison);
    if (scalar_comparison != NULL) iMask.Finalize(scalar_comparison);
    if (vector != NULL) iVector.Finalize(vector);
    if (array != NULL) iVector.Finalize(array);
    if (from_file != NULL) istrCollection.Finalize(from_file);
    if (loaded != NULL) istrCollection.Finalize(loaded);
    if (selected != NULL) istrCollection.Finalize(selected);
    if (range != NULL) istrCollection.Finalize(range);
    if (copy != NULL) istrCollection.Finalize(copy);
    if (sc != NULL) istrCollection.Finalize(sc);
    if (empty != NULL) istrCollection.Finalize(empty);
    return -1;
}

static int test_null_and_error_surface(void)
{
    const char *value = "value";
    size_t idx = 0;
    char storage[256];
    FILE *stream = ccl_test_tmpfile();
    Vector *bad_vector = NULL;

    TEST_REQUIRE(stream != NULL);
    (void)istrCollection.Size(NULL);
    (void)istrCollection.GetFlags(NULL);
    (void)istrCollection.SetFlags(NULL,0);
    (void)istrCollection.Clear(NULL);
    (void)istrCollection.Contains(NULL,value);
    (void)istrCollection.Erase(NULL,value);
    (void)istrCollection.EraseAll(NULL,value);
    (void)istrCollection.Finalize(NULL);
    (void)istrCollection.Apply(NULL,apply_string,NULL);
    (void)istrCollection.Equal(NULL,NULL);
    (void)istrCollection.Copy(NULL);
    (void)istrCollection.SetErrorFunction(NULL,quiet_error);
    (void)istrCollection.Sizeof(NULL);
    (void)istrCollection.NewIterator(NULL);
    (void)istrCollection.InitIterator(NULL,storage);
    (void)istrCollection.InitIterator(NULL,NULL);
    (void)istrCollection.DeleteIterator(NULL);
    (void)istrCollection.SizeofIterator(NULL);
    (void)istrCollection.Save(NULL,stream,NULL,NULL);
    (void)istrCollection.Load(NULL,NULL,NULL);
    (void)istrCollection.GetElementSize(NULL);
    (void)istrCollection.Add(NULL,value);
    (void)istrCollection.PushFront(NULL,(char *)value);
    (void)istrCollection.PopFront(NULL,NULL,0);
    (void)istrCollection.InsertAt(NULL,0,value);
    (void)istrCollection.EraseAt(NULL,0);
    (void)istrCollection.ReplaceAt(NULL,0,(char *)value);
    (void)istrCollection.IndexOf(NULL,value,&idx);
    (void)istrCollection.Sort(NULL);
    (void)istrCollection.CastToArray(NULL);
    (void)istrCollection.FindFirst(NULL,value);
    (void)istrCollection.FindNext(NULL,value,0);
    (void)istrCollection.FindText(NULL,value);
    (void)istrCollection.FindTextIndex(NULL,value);
    (void)istrCollection.FindTextPositions(NULL,value);
    (void)istrCollection.WriteToFile(NULL,"invalid");
    (void)istrCollection.IndexIn(NULL,bad_vector);
    (void)istrCollection.CreateFromFile(NULL);
    (void)istrCollection.AddRange(NULL,1,&value);
    (void)istrCollection.CopyTo(NULL);
    (void)istrCollection.Insert(NULL,(char *)value);
    (void)istrCollection.InsertIn(NULL,0,NULL);
    (void)istrCollection.GetElement(NULL,0);
    (void)istrCollection.GetCapacity(NULL);
    (void)istrCollection.SetCapacity(NULL,0);
    (void)istrCollection.SetCompareFunction(NULL,NULL);
    (void)istrCollection.Reverse(NULL);
    (void)istrCollection.Append(NULL,NULL);
    (void)istrCollection.PopBack(NULL,NULL,0);
    (void)istrCollection.PushBack(NULL,value);
    (void)istrCollection.GetRange(NULL,0,0);
    (void)istrCollection.GetAllocator(NULL);
    (void)istrCollection.Mismatch(NULL,NULL,&idx);
    (void)istrCollection.InitWithAllocator(NULL,0,NULL);
    (void)istrCollection.Init(NULL,0);
    (void)istrCollection.SetDestructor(NULL,NULL);
    (void)istrCollection.InitializeWith(1,NULL);
    (void)istrCollection.GetData(NULL);
    (void)istrCollection.Back(NULL);
    (void)istrCollection.Front(NULL);
    (void)istrCollection.RemoveRange(NULL,0,0);
    (void)istrCollection.CompareEqual(NULL,NULL,NULL);
    (void)istrCollection.CompareEqualScalar(NULL,value,NULL);
    (void)istrCollection.Select(NULL,NULL);
    (void)istrCollection.SelectCopy(NULL,NULL);
    fclose(stream);
    return 0;

cleanup:
    if (stream != NULL) fclose(stream);
    return -1;
}

static int test_error_branches(void)
{
    strCollection *sc = NULL;
    strCollection *empty = NULL;
    strCollection *source = NULL;
    strCollection *failure = NULL;
    Iterator *it = NULL;
    Mask *mask = NULL;
    const char *values[] = {"one", "two"};
    const char *bad_values[] = {"ok", NULL};
    size_t idx = 0;
    FILE *stream = NULL;

    sc = istrCollection.Create(0);
    empty = istrCollection.Create(0);
    source = istrCollection.Create(0);
    TEST_REQUIRE(sc != NULL && empty != NULL && source != NULL);
    (void)istrCollection.Add(sc,NULL);
    (void)istrCollection.AddRange(sc,2,bad_values);
    (void)istrCollection.AddRange(sc,1,NULL);
    TEST_REQUIRE(istrCollection.AddRange(sc,2,values) == 1);
    {
        char scratch;
        TEST_REQUIRE(istrCollection.PopFront(sc,&scratch,0) == 4);
        TEST_REQUIRE(istrCollection.PopBack(sc,&scratch,0) == 4);
        TEST_REQUIRE(istrCollection.AddRange(sc,2,values) == 1);
    }
    TEST_REQUIRE(istrCollection.SetFlags(sc,CONTAINER_READONLY) == 0);
    (void)istrCollection.Add(sc,"readonly");
    (void)istrCollection.PushBack(sc,"readonly");
    (void)istrCollection.PushFront(sc,(char *)"readonly");
    (void)istrCollection.InsertAt(sc,0,"readonly");
    (void)istrCollection.ReplaceAt(sc,0,(char *)"readonly");
    (void)istrCollection.EraseAt(sc,0);
    (void)istrCollection.Sort(sc);
    (void)istrCollection.SetCapacity(sc,10);
    (void)istrCollection.Clear(sc);
    (void)istrCollection.RemoveRange(sc,0,1);
    (void)istrCollection.PopFront(sc,NULL,0);
    (void)istrCollection.PopBack(sc,NULL,0);
    TEST_REQUIRE(istrCollection.SetFlags(sc,0) == CONTAINER_READONLY);
    (void)istrCollection.InsertAt(sc,99,"bad");
    (void)istrCollection.InsertAt(sc,0,NULL);
    (void)istrCollection.ReplaceAt(sc,99,(char *)"bad");
    (void)istrCollection.ReplaceAt(sc,0,NULL);
    (void)istrCollection.IndexOf(sc,NULL,&idx);
    (void)istrCollection.IndexOf(sc,"one",NULL);
    (void)istrCollection.Erase(sc,NULL);
    (void)istrCollection.EraseAt(sc,99);
    (void)istrCollection.RemoveRange(sc,2,1);
    (void)istrCollection.RemoveRange(sc,99,99);
    (void)istrCollection.Append(sc,NULL);
    (void)istrCollection.InsertIn(sc,99,source);
    (void)istrCollection.PushBack(sc,NULL);
    (void)istrCollection.PushFront(sc,NULL);
    (void)istrCollection.Apply(sc,NULL,NULL);
    (void)istrCollection.CompareEqual(empty,sc,NULL);
    (void)istrCollection.Mismatch(empty,sc,&idx);
    (void)istrCollection.Mismatch(sc,empty,NULL);
    (void)istrCollection.SetCapacity(sc,1);
    TEST_REQUIRE(istrCollection.Size(sc) == 1);
    TEST_REQUIRE(istrCollection.SetCapacity(sc,0) == 1);
    TEST_REQUIRE(istrCollection.Size(sc) == 0);
    (void)istrCollection.PopFront(sc,NULL,0);
    (void)istrCollection.PopBack(sc,NULL,0);

    it = istrCollection.NewIterator(empty);
    TEST_REQUIRE(it != NULL);
    (void)it->GetFirst(it);
    (void)it->GetLast(it);
    (void)it->GetNext(it);
    (void)it->GetPrevious(it);
    (void)it->Seek(it,0);
    (void)it->GetCurrent(it);
    (void)it->Replace(it,NULL,1);
    istrCollection.DeleteIterator(it);
    it = NULL;

    mask = iMask.Create(istrCollection.Size(sc));
    TEST_REQUIRE(mask != NULL);
    TEST_REQUIRE(istrCollection.SetFlags(sc,CONTAINER_READONLY) == 0);
    (void)istrCollection.Select(empty,mask);
    (void)istrCollection.Select(sc,mask);
    TEST_REQUIRE(istrCollection.SetFlags(sc,0) == CONTAINER_READONLY);
    iMask.Finalize(mask);
    mask = NULL;

    failure_state.calls = 0;
    failure_state.fail_at = 3;
    failure = istrCollection.CreateWithAllocator(1,&failure_allocator);
    TEST_REQUIRE(failure != NULL);
    (void)istrCollection.Add(failure,"first");
    (void)istrCollection.SetCapacity(failure,10);
    (void)istrCollection.CopyTo(failure);
    (void)istrCollection.NewIterator(failure);
    failure_state.fail_at = 0;
    istrCollection.Finalize(failure);
    failure = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    fputc(0x80,stream);
    rewind(stream);
    TEST_REQUIRE(istrCollection.Load(stream,NULL,NULL) == NULL);
    fclose(stream);
    stream = NULL;
    (void)istrCollection.CreateFromFile("does-not-exist");

    istrCollection.Finalize(source);
    istrCollection.Finalize(empty);
    istrCollection.Finalize(sc);
    source = NULL;
    empty = NULL;
    sc = NULL;
    return 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (it != NULL) istrCollection.DeleteIterator(it);
    if (mask != NULL) iMask.Finalize(mask);
    failure_state.fail_at = 0;
    if (failure != NULL) istrCollection.Finalize(failure);
    if (source != NULL) istrCollection.Finalize(source);
    if (empty != NULL) istrCollection.Finalize(empty);
    if (sc != NULL) istrCollection.Finalize(sc);
    return -1;
}

static const TestCase tests[] = {
    {"narrow sequence and ownership", test_narrow_sequence_and_ownership},
    {"narrow results and buffers", test_narrow_results_and_buffers},
    {"wide and persistence", test_wide_and_persistence},
    {"failure rollback and iterator", test_failure_rollback_and_iterator},
    {"complete narrow API surface", test_api_surface},
    {"null and error surface", test_null_and_error_surface},
    {"error branches", test_error_branches},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {"strcollection family", tests,
                                    sizeof(tests)/sizeof(tests[0])};
    return &suite;
}
