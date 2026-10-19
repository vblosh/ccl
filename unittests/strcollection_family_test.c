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
    TEST_REQUIRE(istrCollection.SelectCopy(NULL, mask) == NULL);
    TEST_REQUIRE(istrCollection.SelectCopy(sc, NULL) == NULL);
    TEST_REQUIRE(istrCollection.PopFront(NULL, out, sizeof(out)) == 0);
    TEST_REQUIRE(istrCollection.GetCapacity(NULL) == 0);
    {
        Iterator *dummy_it = istrCollection.NewIterator(sc);
        TEST_REQUIRE(dummy_it != NULL);
        TEST_REQUIRE(dummy_it->GetPosition(NULL) == SIZE_MAX);
        TEST_REQUIRE(dummy_it->GetLast(NULL) == NULL);
        istrCollection.DeleteIterator(dummy_it);
    }
    {
        strCollection *flags_sc = istrCollection.Create(3);
        strCollection *flags_sel = NULL;
        TEST_REQUIRE(flags_sc != NULL);
        istrCollection.Add(flags_sc, "a");
        istrCollection.Add(flags_sc, "b");
        istrCollection.Add(flags_sc, "c");
        istrCollection.SetFlags(flags_sc, CONTAINER_READONLY);
        istrCollection.SetDestructor(flags_sc, count_destructor);
        flags_sel = istrCollection.SelectCopy(flags_sc, mask);
        TEST_REQUIRE(flags_sel != NULL);
        TEST_REQUIRE(istrCollection.GetFlags(flags_sel) == CONTAINER_READONLY);
        destructor_count = 0;
        istrCollection.Finalize(flags_sel);
        TEST_REQUIRE(destructor_count == 2);
        istrCollection.SetFlags(flags_sc, 0);
        istrCollection.Finalize(flags_sc);
    }

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

static int custom_str_cmp(const void **s1, const void **s2, CompareInfo *info)
{
    (void)info;
    return strcmp(*(const char **)s1, *(const char **)s2);
}

static int apply_modifies_sc(char *str, void *arg)
{
    strCollection *coll = (strCollection *)arg;
    (void)str;
    istrCollection.Add(coll, "mutated_in_apply");
    return 1;
}

static int apply_returns_negative(char *str, void *arg)
{
    (void)str;
    (void)arg;
    return -1;
}

static int failing_save_str(const void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int failing_read_str(void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int test_strcollection_coverage_boost(void)
{
    strCollection *sc = NULL;
    strCollection *sc2 = NULL;
    strCollection *sc_file = NULL;
    strCollection *range = NULL;
    strCollection *found = NULL;
    WstrCollection *wsc = NULL;
    WstrCollection *wsc_file = NULL;
    Vector *vec = NULL;
    Vector *indices = NULL;
    Vector *positions = NULL;
    Mask *eq_mask = NULL;
    Iterator *it = NULL;
    FILE *stream = NULL;
    char pop_buf[32];
    const char *init_data[] = {"banana", "apple", "cherry", "applepie", "date"};
    const wchar_t *w_init_data[] = {L"banana", L"apple", L"cherry"};
    int result = -1;

    /* 1. InitializeWith, Sort, Reverse */
    sc = istrCollection.InitializeWith(5, (char **)init_data);
    TEST_REQUIRE(sc != NULL);
    TEST_REQUIRE(istrCollection.Size(sc) == 5);
    TEST_REQUIRE(istrCollection.Sort(sc) == 1);
    TEST_REQUIRE(istrCollection.Reverse(sc) == 1);

    /* FindText, FindTextIndex, FindTextPositions on pristine sc */
    found = istrCollection.FindText(sc, "apple");
    TEST_REQUIRE(found != NULL);
    TEST_REQUIRE(istrCollection.Size(found) == 2);
    istrCollection.Finalize(found);
    found = NULL;
    TEST_REQUIRE(istrCollection.FindText(sc, "nonexistent") == NULL);
    TEST_REQUIRE(istrCollection.FindText(NULL, "apple") == NULL);
    TEST_REQUIRE(istrCollection.FindText(sc, NULL) == NULL);

    indices = istrCollection.FindTextIndex(sc, "apple");
    TEST_REQUIRE(indices != NULL);
    TEST_REQUIRE(iVector.Size(indices) == 2);
    iVector.Finalize(indices);
    indices = NULL;
    TEST_REQUIRE(istrCollection.FindTextIndex(NULL, "apple") == NULL);
    TEST_REQUIRE(istrCollection.FindTextIndex(sc, NULL) == NULL);

    positions = istrCollection.FindTextPositions(sc, "apple");
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iVector.Size(positions) == 4); /* 2 pairs: (index, offset) */
    iVector.Finalize(positions);
    positions = NULL;
    TEST_REQUIRE(istrCollection.FindTextPositions(NULL, "apple") == NULL);
    TEST_REQUIRE(istrCollection.FindTextPositions(sc, NULL) == NULL);

    /* CastToArray */
    vec = istrCollection.CastToArray(sc);
    TEST_REQUIRE(vec != NULL);
    TEST_REQUIRE(iVector.Size(vec) == 5);
    iVector.Finalize(vec);
    vec = NULL;
    TEST_REQUIRE(istrCollection.CastToArray(NULL) == NULL);

    /* Front, Back, GetData */
    TEST_REQUIRE(istrCollection.Front(sc) != NULL);
    TEST_REQUIRE(istrCollection.Back(sc) != NULL);
    TEST_REQUIRE(istrCollection.GetData(sc) != NULL);
    /* Empty and readonly Front/Back/GetData */
    {
        strCollection *empty = istrCollection.Create(0);
        TEST_REQUIRE(empty != NULL);
        TEST_REQUIRE(istrCollection.Front(empty) == NULL);
        TEST_REQUIRE(istrCollection.Back(empty) == NULL);
        istrCollection.Finalize(empty);

        istrCollection.SetFlags(sc, CONTAINER_READONLY);
        TEST_REQUIRE(istrCollection.Front(sc) == NULL);
        TEST_REQUIRE(istrCollection.Back(sc) == NULL);
        TEST_REQUIRE(istrCollection.GetData(sc) == NULL);
        istrCollection.SetFlags(sc, 0);

        TEST_REQUIRE(istrCollection.Front(NULL) == NULL);
        TEST_REQUIRE(istrCollection.Back(NULL) == NULL);
        TEST_REQUIRE(istrCollection.GetData(NULL) == NULL);
    }

    /* Mismatch */
    {
        size_t mm_pos = 0;
        TEST_REQUIRE(istrCollection.Mismatch(sc, sc, &mm_pos) == 0);
        TEST_REQUIRE(istrCollection.Mismatch(sc, NULL, &mm_pos) == 1);
        strCollection *e1 = istrCollection.Create(0);
        strCollection *e2 = istrCollection.Create(0);
        TEST_REQUIRE(istrCollection.Mismatch(e1, e2, &mm_pos) == 0);
        istrCollection.Finalize(e2);
        istrCollection.Finalize(e1);
    }

    /* CopyTo */
    {
        char **copied_arr = istrCollection.CopyTo(sc);
        TEST_REQUIRE(copied_arr != NULL);
        for (size_t k = 0; copied_arr[k] != NULL; ++k)
            free(copied_arr[k]);
        free(copied_arr);
    }

    /* Apply return 0 / modified */
    {
        TEST_REQUIRE(istrCollection.Apply(sc, apply_returns_negative, NULL) == 0);
        TEST_REQUIRE(istrCollection.Apply(sc, apply_modifies_sc, sc) == CONTAINER_ERROR_OBJECT_CHANGED);
    }

    /* ReplaceAt, Erase, EraseAll, EraseAt, RemoveRange */
    TEST_REQUIRE(istrCollection.ReplaceAt(sc, 0, "replaced_head") == 1);
    TEST_REQUIRE(istrCollection.Add(sc, "to_erase") == 1);
    TEST_REQUIRE(istrCollection.Add(sc, "to_erase") == 1);
    TEST_REQUIRE(istrCollection.Erase(sc, "to_erase") == 1);
    TEST_REQUIRE(istrCollection.EraseAll(sc, "to_erase") == 1);
    TEST_REQUIRE(istrCollection.EraseAt(sc, 0) == 1);
    TEST_REQUIRE(istrCollection.RemoveRange(sc, 0, 1) == 1);

    /* PushFront & PushBack with resize, PopFront with buffer */
    {
        strCollection *small_sc = istrCollection.Create(1);
        TEST_REQUIRE(small_sc != NULL);
        TEST_REQUIRE(istrCollection.PushBack(small_sc, "one") == 1);
        TEST_REQUIRE(istrCollection.PushBack(small_sc, "two") == 1);
        TEST_REQUIRE(istrCollection.PushFront(small_sc, "zero") == 1);
        TEST_REQUIRE(istrCollection.PopFront(small_sc, pop_buf, sizeof(pop_buf)) > 0);
        TEST_REQUIRE(strcmp(pop_buf, "zero") == 0);
        istrCollection.Finalize(small_sc);
    }

    /* 6. IndexIn */
    {
        Vector *idx_vec = iVector.Create(sizeof(size_t), 3);
        size_t idx0 = 0, idx2 = 2;
        iVector.Add(idx_vec, &idx0);
        iVector.Add(idx_vec, &idx2);
        strCollection *indexed = istrCollection.IndexIn(sc, idx_vec);
        TEST_REQUIRE(indexed != NULL);
        TEST_REQUIRE(istrCollection.Size(indexed) == 2);
        istrCollection.Finalize(indexed);

        /* Incompatible vector element size */
        Vector *bad_vec = iVector.Create(sizeof(int), 2);
        TEST_REQUIRE(istrCollection.IndexIn(sc, bad_vec) == NULL);
        iVector.Finalize(bad_vec);

        /* Index out of bounds */
        size_t bad_idx = 999;
        iVector.Add(idx_vec, &bad_idx);
        TEST_REQUIRE(istrCollection.IndexIn(sc, idx_vec) == NULL);
        iVector.Finalize(idx_vec);

        TEST_REQUIRE(istrCollection.IndexIn(NULL, NULL) == NULL);
    }

    /* 7. InsertIn with growth and edge cases */
    sc2 = istrCollection.Create(2);
    TEST_REQUIRE(sc2 != NULL);
    TEST_REQUIRE(istrCollection.Add(sc2, "extra1") == 1);
    TEST_REQUIRE(istrCollection.Add(sc2, "extra2") == 1);
    /* Insert into sc causing capacity expansion */
    TEST_REQUIRE(istrCollection.SetCapacity(sc, istrCollection.Size(sc)) == 1);
    size_t before_sz = istrCollection.Size(sc);
    TEST_REQUIRE(istrCollection.InsertIn(sc, 1, sc2) == 1);
    TEST_REQUIRE(istrCollection.Size(sc) == before_sz + 2);
    /* InsertIn empty collection */
    {
        strCollection *empty = istrCollection.Create(0);
        TEST_REQUIRE(istrCollection.InsertIn(sc, 0, empty) == 1);
        TEST_REQUIRE(istrCollection.InsertIn(empty, 0, empty) == 1);
        istrCollection.Finalize(empty);
    }
    /* Readonly InsertIn */
    istrCollection.SetFlags(sc, CONTAINER_READONLY);
    TEST_REQUIRE(istrCollection.InsertIn(sc, 0, sc2) < 0);
    istrCollection.SetFlags(sc, 0);

    /* 8. CompareEqual, CompareEqualScalar, SelectCopy */
    eq_mask = istrCollection.CompareEqual(sc, sc, NULL);
    TEST_REQUIRE(eq_mask != NULL);
    /* Reusing smaller mask to trigger reallocation */
    {
        Mask *small_m = iMask.Create(1);
        Mask *re_m = istrCollection.CompareEqual(sc, sc, small_m);
        TEST_REQUIRE(re_m != NULL);
        iMask.Finalize(re_m);
    }
    TEST_REQUIRE(istrCollection.CompareEqual(NULL, sc, NULL) == NULL);

    /* CompareEqualScalar */
    {
        Mask *scalar_m = istrCollection.CompareEqualScalar(sc, "apple", NULL);
        TEST_REQUIRE(scalar_m != NULL);
        iMask.Finalize(scalar_m);
        TEST_REQUIRE(istrCollection.CompareEqualScalar(NULL, "apple", NULL) == NULL);
    }

    /* SelectCopy */
    {
        strCollection *copied = istrCollection.SelectCopy(sc, eq_mask);
        TEST_REQUIRE(copied != NULL);
        TEST_REQUIRE(istrCollection.Size(copied) == istrCollection.Size(sc));
        istrCollection.Finalize(copied);

        TEST_REQUIRE(istrCollection.SelectCopy(NULL, eq_mask) == NULL);
        Mask *bad_len_m = iMask.Create(1);
        TEST_REQUIRE(istrCollection.SelectCopy(sc, bad_len_m) == NULL);
        TEST_REQUIRE(istrCollection.Select(sc, bad_len_m) < 0);
        iMask.Finalize(bad_len_m);
    }
    iMask.Finalize(eq_mask);
    eq_mask = NULL;

    /* 9. SetCompareFunction and Equal branches */
    {
        StringCompareFn old_fn = istrCollection.SetCompareFunction(sc, custom_str_cmp);
        TEST_REQUIRE(old_fn != NULL);
        /* Setting same function */
        TEST_REQUIRE(istrCollection.SetCompareFunction(sc, custom_str_cmp) == custom_str_cmp);
        /* Setting NULL */
        TEST_REQUIRE(istrCollection.SetCompareFunction(sc, NULL) == custom_str_cmp);
        TEST_REQUIRE(istrCollection.SetCompareFunction(NULL, custom_str_cmp) == NULL);
        /* Restore */
        istrCollection.SetCompareFunction(sc, old_fn);

        /* Equal edge cases */
        TEST_REQUIRE(istrCollection.Equal(NULL, NULL) == 1);
        TEST_REQUIRE(istrCollection.Equal(sc, NULL) == 0);
        TEST_REQUIRE(istrCollection.Equal(NULL, sc) == 0);
        TEST_REQUIRE(istrCollection.Equal(sc, sc2) == 0); /* different size */
        strCollection *sc_clone = istrCollection.Copy(sc);
        TEST_REQUIRE(sc_clone != NULL);
        TEST_REQUIRE(istrCollection.Equal(sc, sc_clone) == 1);
        istrCollection.SetCompareFunction(sc_clone, custom_str_cmp);
        TEST_REQUIRE(istrCollection.Equal(sc, sc_clone) == 0); /* different compare fn */
        istrCollection.Finalize(sc_clone);
    }

    /* 10. WriteToFile and CreateFromFile (narrow and wide) */
    {
        const char *tmp_name = "test_sc_tmp.txt";
        TEST_REQUIRE(istrCollection.WriteToFile(NULL, tmp_name) < 0);
        TEST_REQUIRE(istrCollection.WriteToFile(sc, tmp_name) == 1);
        sc_file = istrCollection.CreateFromFile(tmp_name);
        TEST_REQUIRE(sc_file != NULL);
        TEST_REQUIRE(istrCollection.Size(sc_file) == istrCollection.Size(sc));
        remove(tmp_name);

        wsc = iWstrCollection.InitializeWith(3, (wchar_t **)w_init_data);
        TEST_REQUIRE(wsc != NULL);
        TEST_REQUIRE(iWstrCollection.WriteToFile(wsc, tmp_name) == 1);
        wsc_file = iWstrCollection.CreateFromFile(tmp_name);
        TEST_REQUIRE(wsc_file != NULL);
        TEST_REQUIRE(iWstrCollection.Size(wsc_file) == 3);
        remove(tmp_name);

        TEST_REQUIRE(istrCollection.CreateFromFile(NULL) == NULL);
        TEST_REQUIRE(iWstrCollection.CreateFromFile(NULL) == NULL);
    }

    /* 11. Iterator edge cases */
    it = istrCollection.NewIterator(sc);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    /* Direct NULL it calls */
    TEST_REQUIRE(it->GetLast(NULL) == NULL);
    TEST_REQUIRE(it->GetNext(NULL) == NULL);
    TEST_REQUIRE(it->GetPrevious(NULL) == NULL);
    TEST_REQUIRE(it->GetFirst(NULL) == NULL);
    TEST_REQUIRE(it->Seek(NULL, 0) == NULL);
    TEST_REQUIRE(it->GetCurrent(NULL) == NULL);
    TEST_REQUIRE(it->Replace(NULL, "x", 1) < 0);

    /* GetPrevious to beginning */
    while (it->GetPrevious(it) != NULL) {}
    TEST_REQUIRE(it->GetPrevious(it) == NULL);

    /* Seek out of bounds */
    TEST_REQUIRE(it->Seek(it, 9999) == NULL);

    /* Replace with direction == 0 */
    TEST_REQUIRE(it->GetLast(it) != NULL);
    TEST_REQUIRE(it->Replace(it, "replaced_prev", 0) == 1);

    /* Stale iterator checks */
    istrCollection.Add(sc, "change_stamp");
    TEST_REQUIRE(it->GetLast(it) == NULL);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    TEST_REQUIRE(it->GetPrevious(it) == NULL);
    TEST_REQUIRE(it->GetFirst(it) == NULL);
    TEST_REQUIRE(it->Seek(it, 0) == NULL);
    TEST_REQUIRE(it->Replace(it, "x", 1) < 0);
    istrCollection.DeleteIterator(it);
    it = NULL;

    /* Iterator on empty collection */
    {
        strCollection *empty = istrCollection.Create(0);
        Iterator *empty_it = istrCollection.NewIterator(empty);
        TEST_REQUIRE(empty_it != NULL);
        TEST_REQUIRE(empty_it->GetLast(empty_it) == NULL);
        TEST_REQUIRE(empty_it->GetFirst(empty_it) == NULL);
        TEST_REQUIRE(empty_it->Replace(empty_it, "x", 1) == 0);
        istrCollection.DeleteIterator(empty_it);
        istrCollection.Finalize(empty);
    }

    /* Iterator Replace on readonly */
    istrCollection.SetFlags(sc, CONTAINER_READONLY);
    it = istrCollection.NewIterator(sc);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->Replace(it, "x", 1) < 0);
    istrCollection.DeleteIterator(it);
    it = NULL;
    istrCollection.SetFlags(sc, 0);

    /* 12. Save and Load edge cases */
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(istrCollection.Save(NULL, stream, NULL, NULL) < 0);
    TEST_REQUIRE(istrCollection.Save(sc, NULL, NULL, NULL) < 0);
    TEST_REQUIRE(istrCollection.Load(NULL, NULL, NULL) == NULL);
    /* Load wrong GUID */
    rewind(stream);
    TEST_REQUIRE(fwrite("WRONG_GUID_SC_1234567890", 1, 24, stream) == 24);
    rewind(stream);
    TEST_REQUIRE(istrCollection.Load(stream, NULL, NULL) == NULL);
    fclose(stream);
    stream = NULL;

    /* Failing save function */
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(istrCollection.Save(sc, stream, failing_save_str, NULL) < 0);
    fclose(stream);
    stream = NULL;

    /* Load with corruptions using saved header from sc */
    {
        char header_buf[sizeof(guid) + sizeof(strCollection)];
        FILE *save_stream = ccl_test_tmpfile();
        TEST_REQUIRE(save_stream != NULL);
        TEST_REQUIRE(istrCollection.Save(sc, save_stream, NULL, NULL) == 1);
        rewind(save_stream);
        TEST_REQUIRE(fread(header_buf, 1, sizeof(header_buf), save_stream) == sizeof(header_buf));
        fclose(save_stream);

        /* Truncated header (guid + 2 bytes) */
        stream = ccl_test_tmpfile();
        TEST_REQUIRE(stream != NULL);
        TEST_REQUIRE(fwrite(header_buf, 1, sizeof(guid) + 2, stream) == sizeof(guid) + 2);
        rewind(stream);
        TEST_REQUIRE(istrCollection.Load(stream, NULL, NULL) == NULL);
        fclose(stream);

        /* Corrupted count (> SIZE_MAX / sizeof(char*)) */
        stream = ccl_test_tmpfile();
        TEST_REQUIRE(stream != NULL);
        {
            char bad_hdr[sizeof(guid) + sizeof(strCollection)];
            memcpy(bad_hdr, header_buf, sizeof(bad_hdr));
            strCollection *col = (strCollection *)(bad_hdr + sizeof(guid));
            col->count = SIZE_MAX;
            TEST_REQUIRE(fwrite(bad_hdr, 1, sizeof(bad_hdr), stream) == sizeof(bad_hdr));
        }
        rewind(stream);
        TEST_REQUIRE(istrCollection.Load(stream, NULL, NULL) == NULL);
        fclose(stream);

        /* Truncated string in stream (valid header, count = 1, but EOF in ule128) */
        stream = ccl_test_tmpfile();
        TEST_REQUIRE(stream != NULL);
        {
            char one_hdr[sizeof(guid) + sizeof(strCollection)];
            memcpy(one_hdr, header_buf, sizeof(one_hdr));
            strCollection *col = (strCollection *)(one_hdr + sizeof(guid));
            col->count = 1;
            TEST_REQUIRE(fwrite(one_hdr, 1, sizeof(one_hdr), stream) == sizeof(one_hdr));
        }
        rewind(stream);
        TEST_REQUIRE(istrCollection.Load(stream, NULL, NULL) == NULL);
        fclose(stream);

        /* Failing read function */
        stream = ccl_test_tmpfile();
        TEST_REQUIRE(stream != NULL);
        {
            char one_hdr[sizeof(guid) + sizeof(strCollection)];
            memcpy(one_hdr, header_buf, sizeof(one_hdr));
            strCollection *col = (strCollection *)(one_hdr + sizeof(guid));
            col->count = 1;
            TEST_REQUIRE(fwrite(one_hdr, 1, sizeof(one_hdr), stream) == sizeof(one_hdr));
            fputc(5, stream);
        }
        rewind(stream);
        TEST_REQUIRE(istrCollection.Load(stream, failing_read_str, NULL) == NULL);
        fclose(stream);
        stream = NULL;
    }

    /* ReplaceWithIterator with NULL data forward and backward */
    {
        strCollection *del_sc = istrCollection.Create(3);
        TEST_REQUIRE(del_sc != NULL);
        TEST_REQUIRE(istrCollection.Add(del_sc, "d1") == 1);
        TEST_REQUIRE(istrCollection.Add(del_sc, "d2") == 1);
        TEST_REQUIRE(istrCollection.Add(del_sc, "d3") == 1);
        Iterator *del_it = istrCollection.NewIterator(del_sc);
        TEST_REQUIRE(del_it != NULL);
        TEST_REQUIRE(del_it->GetFirst(del_it) != NULL);
        TEST_REQUIRE(del_it->GetNext(del_it) != NULL);
        /* Delete at index 1 forward */
        TEST_REQUIRE(del_it->Replace(del_it, NULL, 1) == 1);
        /* Delete at index 1 backward */
        TEST_REQUIRE(del_it->Replace(del_it, NULL, 0) == 1);
        /* Delete last element -> empty */
        TEST_REQUIRE(del_it->Replace(del_it, NULL, 1) == 1);
        istrCollection.DeleteIterator(del_it);
        istrCollection.Finalize(del_sc);
    }

    /* WriteToFile with bad path and InitIterator with NULL */
    TEST_REQUIRE(istrCollection.WriteToFile(sc, "/nonexistent_dir/bad_file.txt") < 0);
    {
        char it_storage[512];
        TEST_REQUIRE(istrCollection.InitIterator(NULL, it_storage) < 0);
    }

    /* Equal with differing contents */
    {
        strCollection *sc_diff = istrCollection.Create(1);
        TEST_REQUIRE(sc_diff != NULL);
        TEST_REQUIRE(istrCollection.Add(sc_diff, "different_string") == 1);
        strCollection *sc_same = istrCollection.Create(1);
        TEST_REQUIRE(sc_same != NULL);
        TEST_REQUIRE(istrCollection.Add(sc_same, "banana") == 1);
        TEST_REQUIRE(istrCollection.Equal(sc_same, sc_diff) == 0);
        istrCollection.Finalize(sc_diff);
        istrCollection.Finalize(sc_same);
    }

    /* InitializeWith with NULL element */
    {
        const char *data_with_null[] = {"a", NULL};
        TEST_REQUIRE(istrCollection.InitializeWith(2, (char **)data_with_null) == NULL);
    }

    /* Reverse with count < 2 */
    {
        strCollection *sc_1 = istrCollection.Create(1);
        TEST_REQUIRE(sc_1 != NULL);
        TEST_REQUIRE(istrCollection.Add(sc_1, "single") == 1);
        TEST_REQUIRE(istrCollection.Reverse(sc_1) == 1);
        istrCollection.Finalize(sc_1);
    }

    /* CompareEqualScalar with existing mask */
    {
        Mask *ex_m = iMask.Create(istrCollection.Size(sc));
        TEST_REQUIRE(ex_m != NULL);
        Mask *res_m = istrCollection.CompareEqualScalar(sc, "banana", ex_m);
        TEST_REQUIRE(res_m != NULL);
        iMask.Finalize(res_m);
    }

    /* FindNext */
    TEST_REQUIRE(istrCollection.FindNext(sc, "banana", 0) > 0);
    TEST_REQUIRE(istrCollection.FindNext(sc, "nonexistent", 0) == 0);
    TEST_REQUIRE(istrCollection.FindNext(NULL, "banana", 0) == 0);

    /* GetElement and InsertAt with out-of-bounds index */
    TEST_REQUIRE(istrCollection.GetElement(sc, 99999) == NULL);
    TEST_REQUIRE(istrCollection.InsertAt(sc, 99999, "bad") < 0);

    /* Contains with NULL and missing */
    TEST_REQUIRE(istrCollection.Contains(sc, NULL) == 0);
    TEST_REQUIRE(istrCollection.Contains(sc, "nonexistent") == 0);

    /* SetCapacity reduction to 1 and 0 */
    {
        strCollection *cap_sc = istrCollection.Create(5);
        TEST_REQUIRE(cap_sc != NULL);
        TEST_REQUIRE(istrCollection.Add(cap_sc, "a") == 1);
        TEST_REQUIRE(istrCollection.Add(cap_sc, "b") == 1);
        TEST_REQUIRE(istrCollection.SetCapacity(cap_sc, 1) == 1);
        TEST_REQUIRE(istrCollection.SetCapacity(cap_sc, 0) == 1);
        istrCollection.Finalize(cap_sc);
    }

    /* Equal with different StringCompareContext */
    {
        sc2 = istrCollection.Copy(sc);
        TEST_REQUIRE(sc2 != NULL);
        sc->StringCompareContext = (void *)1;
        sc2->StringCompareContext = (void *)2;
        TEST_REQUIRE(istrCollection.Equal(sc, sc2) == 0);
        sc->StringCompareContext = (void *)1;
        sc2->StringCompareContext = NULL;
        TEST_REQUIRE(istrCollection.Equal(sc, sc2) == 1);
        sc->StringCompareContext = NULL;
        sc2->StringCompareContext = (void *)1;
        TEST_REQUIRE(istrCollection.Equal(sc, sc2) == 1);
        sc->StringCompareContext = NULL;
        sc2->StringCompareContext = NULL;
        istrCollection.Finalize(sc2);
        sc2 = NULL;
    }

    /* Iterator boundary checks: previous at start and next at end */
    {
        it = istrCollection.NewIterator(sc);
        TEST_REQUIRE(it != NULL);
        TEST_REQUIRE(it->GetFirst(it) != NULL);
        TEST_REQUIRE(it->GetPrevious(it) == NULL);
        while (it->GetNext(it) != NULL) {}
        TEST_REQUIRE(it->GetNext(it) == NULL);
        istrCollection.DeleteIterator(it);
        it = NULL;
    }

    /* Heap Init */
    {
        strCollection *heap_sc = malloc(sizeof(strCollection));
        TEST_REQUIRE(istrCollection.Init(heap_sc, 5) == heap_sc);
        TEST_REQUIRE(istrCollection.Add(heap_sc, "item") == 1);
        TEST_REQUIRE(istrCollection.Finalize(heap_sc) == 1);
    }

    /* Wide collection extra operations */
    {
        wsc = iWstrCollection.Create(2);
        TEST_REQUIRE(wsc != NULL);
        TEST_REQUIRE(iWstrCollection.Add(wsc, L"w1") == 1);
        TEST_REQUIRE(iWstrCollection.Add(wsc, L"w2") == 1);
        TEST_REQUIRE(iWstrCollection.Reverse(wsc) == 1);
        TEST_REQUIRE(iWstrCollection.FindNext(wsc, L"w1", 0) > 0);
        TEST_REQUIRE(iWstrCollection.Contains(wsc, L"w1") == 1);
        TEST_REQUIRE(iWstrCollection.Contains(wsc, NULL) == 0);
        TEST_REQUIRE(iWstrCollection.Contains(wsc, L"nonexistent") == 0);
        TEST_REQUIRE(iWstrCollection.GetElement(wsc, 9999) == NULL);
        iWstrCollection.Finalize(wsc);
        wsc = NULL;
    }

    /* 13. GetRange with start > end */
    range = istrCollection.GetRange(sc, 3, 1);
    TEST_REQUIRE(range != NULL);
    TEST_REQUIRE(istrCollection.Size(range) == 0);
    istrCollection.Finalize(range);
    range = NULL;

    /* 14. Additional coverage cases */
    /* Iterator GetPosition & fresh iterator next/previous */
    {
        Iterator *it_pos = istrCollection.NewIterator(sc);
        TEST_REQUIRE(it_pos != NULL);
        TEST_REQUIRE(it_pos->GetPosition(NULL) == SIZE_MAX);
        /* fresh iterator has index SIZE_MAX: GetNext sets it to 0, GetPrevious sets it to count-1 */
        TEST_REQUIRE(it_pos->GetNext(it_pos) != NULL);
        TEST_REQUIRE(it_pos->GetPosition(it_pos) == 0);
        istrCollection.DeleteIterator(it_pos);

        it_pos = istrCollection.NewIterator(sc);
        TEST_REQUIRE(it_pos != NULL);
        TEST_REQUIRE(it_pos->GetPrevious(it_pos) != NULL);
        TEST_REQUIRE(it_pos->GetPosition(it_pos) == istrCollection.Size(sc) - 1);
        /* Stale iterator GetPosition */
        istrCollection.Add(sc, "change_for_stale");
        TEST_REQUIRE(it_pos->GetPosition(it_pos) == SIZE_MAX);
        istrCollection.DeleteIterator(it_pos);
    }

    /* SizeofIterator */
    TEST_REQUIRE(istrCollection.SizeofIterator(sc) > 0);
    TEST_REQUIRE(iWstrCollection.SizeofIterator(NULL) > 0);

    /* InitIterator bad args */
    TEST_REQUIRE(istrCollection.InitIterator(sc, NULL) < 0);

    /* PopBack and PopFront with small buffer (buflen < string len) */
    {
        strCollection *pop_sc = istrCollection.Create(2);
        char sbuf[2];
        TEST_REQUIRE(pop_sc != NULL);
        TEST_REQUIRE(istrCollection.Add(pop_sc, "longstring1") == 1);
        TEST_REQUIRE(istrCollection.Add(pop_sc, "longstring2") == 1);
        TEST_REQUIRE(istrCollection.PopBack(pop_sc, sbuf, sizeof(sbuf)) > 0);
        TEST_REQUIRE(sbuf[1] == '\0');
        TEST_REQUIRE(istrCollection.PopFront(pop_sc, sbuf, sizeof(sbuf)) > 0);
        TEST_REQUIRE(sbuf[1] == '\0');
        istrCollection.Finalize(pop_sc);
    }

    /* Mismatch branches */
    {
        strCollection *m1 = istrCollection.Create(2);
        strCollection *m2 = istrCollection.Create(2);
        size_t mm_pos = 0;
        TEST_REQUIRE(m1 != NULL && m2 != NULL);
        TEST_REQUIRE(istrCollection.Add(m1, "same") == 1);
        TEST_REQUIRE(istrCollection.Add(m1, "diff1") == 1);
        TEST_REQUIRE(istrCollection.Add(m2, "same") == 1);
        TEST_REQUIRE(istrCollection.Add(m2, "diff2") == 1);
        TEST_REQUIRE(istrCollection.Mismatch(m1, m2, &mm_pos) == 1);
        TEST_REQUIRE(mm_pos == 1);

        /* m1 larger count than m2 */
        TEST_REQUIRE(istrCollection.Add(m1, "extra") == 1);
        TEST_REQUIRE(istrCollection.Mismatch(m1, m2, &mm_pos) == 1);

        istrCollection.Finalize(m2);
        istrCollection.Finalize(m1);
    }

    /* Append, Erase, Reverse on readonly collection and Reverse count < 2 */
    {
        strCollection *ro_sc = istrCollection.Create(2);
        TEST_REQUIRE(ro_sc != NULL);
        TEST_REQUIRE(istrCollection.Add(ro_sc, "item") == 1);
        istrCollection.SetFlags(ro_sc, CONTAINER_READONLY);
        TEST_REQUIRE(istrCollection.Append(ro_sc, ro_sc) < 0);
        TEST_REQUIRE(istrCollection.Erase(ro_sc, "item") < 0);
        TEST_REQUIRE(istrCollection.Reverse(ro_sc) < 0);
        istrCollection.SetFlags(ro_sc, 0);
        TEST_REQUIRE(istrCollection.Reverse(ro_sc) == 1);
        istrCollection.Finalize(ro_sc);
    }

    /* IndexOf not found */
    {
        size_t not_found_idx = 0;
        TEST_REQUIRE(istrCollection.IndexOf(sc, "nonexistent_element_xyz", &not_found_idx) == CONTAINER_ERROR_NOTFOUND);
    }

    /* WriteToFile with NULL element in collection and /dev/full */
    {
        strCollection *null_sc = istrCollection.Create(2);
        TEST_REQUIRE(null_sc != NULL);
        null_sc->count = 1;
        null_sc->contents[0] = NULL;
        TEST_REQUIRE(istrCollection.WriteToFile(null_sc, "test_null_elem.txt") < 0);
        null_sc->count = 0;
        istrCollection.Finalize(null_sc);

        TEST_REQUIRE(istrCollection.WriteToFile(sc, "/dev/full") < 0);
    }

    /* Save with NULL element in collection */
    {
        FILE *null_f = ccl_test_tmpfile();
        strCollection *null_sc = istrCollection.Create(2);
        TEST_REQUIRE(null_f != NULL);
        TEST_REQUIRE(null_sc != NULL);
        null_sc->count = 1;
        null_sc->contents[0] = NULL;
        TEST_REQUIRE(istrCollection.Save(null_sc, null_f, NULL, NULL) < 0);
        null_sc->count = 0;
        istrCollection.Finalize(null_sc);
        fclose(null_f);
    }

    /* InsertIn with NULL element in newData */
    {
        strCollection *null_elem_sc = istrCollection.Create(2);
        TEST_REQUIRE(null_elem_sc != NULL);
        null_elem_sc->count = 2;
        null_elem_sc->contents[0] = strdup("ok");
        null_elem_sc->contents[1] = NULL;
        TEST_REQUIRE(istrCollection.InsertIn(sc, 0, null_elem_sc) < 0);
        free(null_elem_sc->contents[0]);
        null_elem_sc->count = 0;
        istrCollection.Finalize(null_elem_sc);
    }

    /* ReplaceWithIterator removing last element with direction != 0 and direction == 0 */
    {
        strCollection *last_sc = istrCollection.Create(3);
        TEST_REQUIRE(last_sc != NULL);
        TEST_REQUIRE(istrCollection.Add(last_sc, "a") == 1);
        TEST_REQUIRE(istrCollection.Add(last_sc, "b") == 1);
        Iterator *last_it = istrCollection.NewIterator(last_sc);
        TEST_REQUIRE(last_it != NULL);
        TEST_REQUIRE(last_it->GetLast(last_it) != NULL);
        TEST_REQUIRE(last_it->Replace(last_it, NULL, 1) == 1);
        TEST_REQUIRE(last_it->GetCurrent(last_it) != NULL);
        TEST_REQUIRE(last_it->Replace(last_it, NULL, 0) == 1);
        istrCollection.DeleteIterator(last_it);
        istrCollection.Finalize(last_sc);
    }

    /* Custom heap VTable for Finalize */
    {
        strCollection *custom_sc = istrCollection.Create(1);
        TEST_REQUIRE(custom_sc != NULL);
        custom_sc->VTable = malloc(sizeof(strCollectionInterface));
        TEST_REQUIRE(custom_sc->VTable != NULL);
        memcpy(custom_sc->VTable, &istrCollection, sizeof(strCollectionInterface));
        istrCollection.Finalize(custom_sc);
    }

    result = 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (it != NULL) istrCollection.DeleteIterator(it);
    if (eq_mask != NULL) iMask.Finalize(eq_mask);
    if (range != NULL) istrCollection.Finalize(range);
    if (vec != NULL) iVector.Finalize(vec);
    if (indices != NULL) iVector.Finalize(indices);
    if (positions != NULL) iVector.Finalize(positions);
    if (found != NULL) istrCollection.Finalize(found);
    if (wsc_file != NULL) iWstrCollection.Finalize(wsc_file);
    if (wsc != NULL) iWstrCollection.Finalize(wsc);
    if (sc_file != NULL) istrCollection.Finalize(sc_file);
    if (sc2 != NULL) istrCollection.Finalize(sc2);
    if (sc != NULL) istrCollection.Finalize(sc);
    return result;
}

static const TestCase tests[] = {
    {"narrow sequence and ownership", test_narrow_sequence_and_ownership},
    {"narrow results and buffers", test_narrow_results_and_buffers},
    {"wide and persistence", test_wide_and_persistence},
    {"failure rollback and iterator", test_failure_rollback_and_iterator},
    {"complete narrow API surface", test_api_surface},
    {"null and error surface", test_null_and_error_surface},
    {"error branches", test_error_branches},
    {"strcollection coverage boost", test_strcollection_coverage_boost},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {"strcollection family", tests,
                                    sizeof(tests)/sizeof(tests[0])};
    return &suite;
}
