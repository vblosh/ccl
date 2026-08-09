#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

static int destructor_calls;

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

static void *failure_calloc(size_t count,size_t size)
{
    if (size != 0 && count > SIZE_MAX/size)
        return NULL;
    return failure_malloc(count*size);
}

static void *failure_realloc(void *ptr,size_t size)
{
    if (ptr == NULL)
        return failure_malloc(size);
    ++failure_state.calls;
    if (failure_state.fail_at != 0 && failure_state.calls >= failure_state.fail_at)
        return NULL;
    return realloc(ptr,size);
}

static void failure_free(void *ptr)
{
    free(ptr);
}

static ContainerAllocator failure_allocator = {
    failure_malloc, failure_free, failure_realloc, failure_calloc
};

static int count_destructor(void *p)
{
    (void)p;
    ++destructor_calls;
    return 1;
}

static int compare_reverse(const void *left,const void *right,CompareInfo *ci)
{
    (void)ci;
    return -strcmp((const char *)left,(const char *)right);
}

static int compare_reverse_w(const void *left,const void *right,CompareInfo *ci)
{
    (void)ci;
    return -wcscmp((const wchar_t *)left,(const wchar_t *)right);
}

static void observe_list(const void *object,unsigned operation,const void *extra[])
{
    (void)object;
    (void)operation;
    (void)extra;
}

static int append_mark(char *s,void *arg)
{
    (void)arg;
    if (s[0] != '\0')
        s[0] = (char)(s[0] == 'a' ? 'A' : s[0]);
    return 1;
}

static int append_mark_w(wchar_t *s,void *arg)
{
    (void)arg;
    if (s[0] != L'\0')
        s[0] = (wchar_t)(s[0] == L'a' ? L'A' : s[0]);
    return 1;
}

static int test_api_surface_and_errors(void)
{
    StringList *list = NULL;
    StringList *copy = NULL;
    StringListElement *element = NULL;
    Iterator *it = NULL;
    unsigned char storage[sizeof(struct StringListIterator)];
    char out[32];
    size_t index = 0;
    char value[] = "value";
    char *values[] = {value};

    TEST_REQUIRE(iStringInterface.Size(NULL) == (size_t)CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.GetFlags(NULL) == 0);
    TEST_REQUIRE(iStringInterface.SetFlags(NULL,0) == 0);
    TEST_REQUIRE(iStringInterface.Clear(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Contains(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Finalize(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Apply(NULL,NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Equal(NULL,NULL) == 1);
    TEST_REQUIRE(iStringInterface.Copy(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.SetErrorFunction(NULL,NULL) == iError.RaiseError);
    TEST_REQUIRE(iStringInterface.Sizeof(NULL) == sizeof(StringList));
    TEST_REQUIRE(iStringInterface.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.DeleteIterator(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.SizeofIterator(NULL) == sizeof(struct StringListIterator));
    TEST_REQUIRE(iStringInterface.Save(NULL,NULL,NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Load(NULL,NULL,NULL) == NULL);
    TEST_REQUIRE(iStringInterface.GetElementSize(NULL) == 0);
    TEST_REQUIRE(iStringInterface.Add(NULL,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.GetElement(NULL,0) == NULL);
    TEST_REQUIRE(iStringInterface.PushFront(NULL,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.PopFront(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.InsertAt(NULL,0,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.EraseAt(NULL,0) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.ReplaceAt(NULL,0,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.IndexOf(NULL,value,NULL,&index) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.InsertIn(NULL,0,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.CopyElement(NULL,0,out) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.EraseRange(NULL,0,1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Sort(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Reverse(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.GetRange(NULL,0,1) == NULL);
    TEST_REQUIRE(iStringInterface.Append(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.SetCompareFunction(NULL,NULL) == NULL);
    TEST_REQUIRE(iStringInterface.UseHeap(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.AddRange(NULL,1,values) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Init(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.SetAllocator(NULL,NULL) == NULL);
    TEST_REQUIRE(iStringInterface.GetAllocator(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.SetDestructor(NULL,NULL) == NULL);
    TEST_REQUIRE(iStringInterface.InitializeWith(1,NULL) == NULL);
    TEST_REQUIRE(iStringInterface.Back(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.Front(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.Select(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.SelectCopy(NULL,NULL) == NULL);
    TEST_REQUIRE(iStringInterface.FirstElement(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.LastElement(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.NextElement(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.ElementData(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.SetElementData(NULL,NULL,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Advance(NULL) == NULL);
    TEST_REQUIRE(iStringInterface.Skip(NULL,1) == NULL);
    TEST_REQUIRE(iStringInterface.SplitAfter(NULL,NULL) == NULL);

    list = iStringInterface.Create();
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iStringInterface.PopFront(list,NULL) == 0);
    TEST_REQUIRE(iStringInterface.Erase(list,value) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iStringInterface.Add(list,value) == 1);
    TEST_REQUIRE(iStringInterface.GetElement(list,1) == NULL);
    TEST_REQUIRE(iStringInterface.InsertAt(list,3,value) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iStringInterface.EraseAt(list,3) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iStringInterface.ReplaceAt(list,3,value) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iStringInterface.CopyElement(list,3,out) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iStringInterface.IndexOf(list,"missing",NULL,&index) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iStringInterface.SetElementData(list,&element,value) == CONTAINER_ERROR_WRONGELEMENT);
    TEST_REQUIRE(iStringInterface.SetElementData(list,NULL,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.SetElementData(list,&element,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.EraseRange(list,0,0) == 0);
    TEST_REQUIRE(iStringInterface.EraseRange(list,5,6) == 0);
    TEST_REQUIRE(iStringInterface.AddRange(list,0,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.AddRange(list,1,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.SetAllocator(list,NULL) == NULL);
    TEST_REQUIRE(iStringInterface.UseHeap(list,NULL) == CONTAINER_ERROR_NOTIMPLEMENTED);
    TEST_REQUIRE(iStringInterface.InitIterator(list,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.InitIterator(NULL,NULL) == sizeof(struct StringListIterator));
    TEST_REQUIRE(iStringInterface.InitIterator(list,storage) == 1);
    it = (Iterator *)storage;
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    TEST_REQUIRE(it->GetPrevious(it) == NULL);
    TEST_REQUIRE(it->GetCurrent(it) != NULL);
    TEST_REQUIRE(it->Seek(it,100) != NULL);
    TEST_REQUIRE(it->GetPosition(it) == 0);
    TEST_REQUIRE(it->Replace(it,NULL,1) == 1);
    TEST_REQUIRE(iStringInterface.Size(list) == 0);
    TEST_REQUIRE(iStringInterface.DeleteIterator(it) == 1);
    it = NULL;
    iStringInterface.Finalize(list);
    list = NULL;

    list = iStringInterface.Create();
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iStringInterface.Add(list,"keep") == 1);
    iStringInterface.SetFlags(list,CONTAINER_READONLY);
    TEST_REQUIRE(iStringInterface.Add(list,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.PushFront(list,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.PopFront(list,NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.InsertAt(list,0,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.EraseAt(list,0) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.ReplaceAt(list,0,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.InsertIn(list,0,list) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iStringInterface.EraseRange(list,0,1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.Sort(list) == 1);
    TEST_REQUIRE(iStringInterface.Reverse(list) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.Append(list,list) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iStringInterface.AddRange(list,1,values) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.Select(list,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.FirstElement(list) == NULL);
    TEST_REQUIRE(iStringInterface.LastElement(list) == NULL);
    TEST_REQUIRE(iStringInterface.SetElementData(list,&element,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iStringInterface.SplitAfter(list,iStringInterface.FirstElement(list)) == NULL);
    TEST_REQUIRE(iStringInterface.GetElement(list,0) == NULL);
    TEST_REQUIRE(iStringInterface.Front(list) == NULL);
    TEST_REQUIRE(iStringInterface.Back(list) == NULL);
    copy = iStringInterface.Copy(list);
    TEST_REQUIRE(copy != NULL && iStringInterface.GetFlags(copy) == CONTAINER_READONLY);
    it = iStringInterface.NewIterator(list);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->GetCurrent(it) != NULL);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    TEST_REQUIRE(it->GetPrevious(it) == NULL);
    TEST_REQUIRE(it->Replace(it,NULL,1) == CONTAINER_ERROR_READONLY);
    iStringInterface.DeleteIterator(it);
    it = NULL;
    iStringInterface.SetFlags(list,0);
    iStringInterface.SetFlags(copy,0);
    iStringInterface.Finalize(copy);
    copy = NULL;

    if (list != NULL) iStringInterface.Finalize(list);
    return 0;

cleanup:
    if (it != NULL) iStringInterface.DeleteIterator(it);
    if (copy != NULL) {
        iStringInterface.SetFlags(copy,0);
        iStringInterface.Finalize(copy);
    }
    if (list != NULL) {
        iStringInterface.SetFlags(list,0);
        iStringInterface.Finalize(list);
    }
    return -1;
}

static int test_narrow_mutations(void)
{
    StringList *list = NULL;
    StringList *range = NULL;
    StringListElement *element;
    const char *values[] = {"delta", "beta", "gamma"};
    Mask *mask = NULL;
    size_t index;

    list = iStringInterface.Create();
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iStringInterface.AddRange(list,3,(char **)values) == 1);
    TEST_REQUIRE(iStringInterface.PushFront(list,"alpha") == 1);
    TEST_REQUIRE(iStringInterface.InsertAt(list,2,"a") == 1);
    TEST_REQUIRE(iStringInterface.ReplaceAt(list,2,"a much longer replacement") == 1);
    TEST_REQUIRE(strcmp(iStringInterface.GetElement(list,2),"a much longer replacement") == 0);
    TEST_REQUIRE(iStringInterface.IndexOf(list,"gamma",NULL,&index) == 1 && index == 4);

    element = iStringInterface.FirstElement(list);
    TEST_REQUIRE(element != NULL);
    TEST_REQUIRE(iStringInterface.SetElementData(list,&element,"rewritten") == 1);
    TEST_REQUIRE(strcmp((char *)iStringInterface.ElementData(element),"rewritten") == 0);

    TEST_REQUIRE(iStringInterface.EraseRange(list,1,3) == 1);
    TEST_REQUIRE(iStringInterface.Size(list) == 3);
    TEST_REQUIRE(strcmp(iStringInterface.Front(list),"rewritten") == 0);
    TEST_REQUIRE(iStringInterface.EraseRange(list,0,3) == 1);
    TEST_REQUIRE(iStringInterface.Size(list) == 0);
    TEST_REQUIRE(iStringInterface.EraseRange(list,0,0) == 0);

    TEST_REQUIRE(iStringInterface.Add(list,"z") == 1);
    TEST_REQUIRE(iStringInterface.Add(list,"x") == 1);
    TEST_REQUIRE(iStringInterface.Add(list,"y") == 1);
    TEST_REQUIRE(iStringInterface.Sort(list) == 1);
    TEST_REQUIRE(strcmp(iStringInterface.Front(list),"x") == 0);
    TEST_REQUIRE(iStringInterface.Reverse(list) == 1);
    TEST_REQUIRE(strcmp(iStringInterface.Front(list),"z") == 0);
    range = iStringInterface.GetRange(list,1,3);
    TEST_REQUIRE(range != NULL && iStringInterface.Size(range) == 2);

    mask = iMask.Create(iStringInterface.Size(list));
    TEST_REQUIRE(mask != NULL);
    TEST_REQUIRE(iMask.SetElement(mask,0,1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask,2,1) == 1);
    TEST_REQUIRE(iMask.SetElement(mask,1,0) == 1);
    TEST_REQUIRE(iStringInterface.Select(list,mask) == 1);
    TEST_REQUIRE(iStringInterface.Size(list) == 2);

    if (mask != NULL) iMask.Finalize(mask);
    if (range != NULL) iStringInterface.Finalize(range);
    if (list != NULL) iStringInterface.Finalize(list);
    return 0;

cleanup:
    if (mask != NULL) iMask.Finalize(mask);
    if (range != NULL) iStringInterface.Finalize(range);
    if (list != NULL) iStringInterface.Finalize(list);
    return -1;
}

static int test_narrow_success_surface(void)
{
    StringList *list = NULL;
    StringList *other = NULL;
    StringList *range = NULL;
    StringList *selected = NULL;
    StringList *suffix = NULL;
    StringList *empty_insert = NULL;
    StringListElement *point;
    Mask *mask = NULL;
    char output[64];
    char *batch[] = {"one","two"};
    size_t position;

    list = iStringInterface.Create();
    other = iStringInterface.Create();
    TEST_REQUIRE(list != NULL && other != NULL);
    TEST_REQUIRE(iObserver.Subscribe(list,observe_list,CCL_MODIFY|CCL_INSERT_AT|CCL_INSERT_IN) == 1);
    TEST_REQUIRE(iStringInterface.Add(list,"b") == 1);
    TEST_REQUIRE(iStringInterface.PushFront(list,"a") == 1);
    TEST_REQUIRE(iStringInterface.InsertAt(list,2,"c") == 1);
    TEST_REQUIRE(iStringInterface.InsertAt(list,0,"zero") == 1);
    TEST_REQUIRE(iStringInterface.CopyElement(list,1,output) == 1);
    TEST_REQUIRE(strcmp(output,"a") == 0);
    TEST_REQUIRE(strcmp(iStringInterface.GetElement(list,0),"zero") == 0);
    TEST_REQUIRE(strcmp(iStringInterface.Front(list),"zero") == 0);
    TEST_REQUIRE(strcmp(iStringInterface.Back(list),"c") == 0);
    TEST_REQUIRE(iStringInterface.Contains(list,"b") == 1);
    TEST_REQUIRE(iStringInterface.IndexOf(list,"c",NULL,&position) == 1 && position == 3);
    TEST_REQUIRE(iStringInterface.SetCompareFunction(list,compare_reverse) != NULL);
    TEST_REQUIRE(iStringInterface.Sort(list) == 1);
    TEST_REQUIRE(iStringInterface.SetCompareFunction(list,NULL) == compare_reverse);
    TEST_REQUIRE(iStringInterface.SetErrorFunction(list,NULL) == iError.RaiseError);
    TEST_REQUIRE(iStringInterface.Apply(list,append_mark,NULL) == 1);
    TEST_REQUIRE(iStringInterface.Sizeof(list) > sizeof(StringList));

    TEST_REQUIRE(iStringInterface.AddRange(other,2,batch) == 1);
    TEST_REQUIRE(iStringInterface.InsertIn(list,0,other) == 1);
    TEST_REQUIRE(iStringInterface.InsertIn(list,iStringInterface.Size(list),other) == 1);
    TEST_REQUIRE(iStringInterface.InsertIn(list,2,other) == 1);
    TEST_REQUIRE(iStringInterface.Append(list,other) == 1);
    other = NULL;
    other = iStringInterface.Create();
    TEST_REQUIRE(other != NULL);
    TEST_REQUIRE(iObserver.Subscribe(other,observe_list,CCL_MODIFY) == 1);
    TEST_REQUIRE(iStringInterface.Append(list,other) == 1);
    other = NULL;
    range = iStringInterface.GetRange(list,1,4);
    TEST_REQUIRE(range != NULL && iStringInterface.Size(range) == 3);
    mask = iMask.Create(iStringInterface.Size(list));
    TEST_REQUIRE(mask != NULL);
    for (position=0; position<iStringInterface.Size(list); ++position)
        TEST_REQUIRE(iMask.SetElement(mask,position,(int)(position % 2)) == 1);
    selected = iStringInterface.SelectCopy(list,mask);
    TEST_REQUIRE(selected != NULL);
    TEST_REQUIRE(iStringInterface.Select(list,mask) == 1);
    TEST_REQUIRE(iStringInterface.Size(list) == iMask.PopulationCount(mask));
    TEST_REQUIRE(iStringInterface.Contains(list,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Append(list,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.Append(NULL,list) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iStringInterface.InsertIn(list,iStringInterface.Size(list)+1,list) == CONTAINER_ERROR_INCOMPATIBLE);
    empty_insert = iStringInterface.Create();
    TEST_REQUIRE(empty_insert != NULL);
    TEST_REQUIRE(iStringInterface.InsertIn(list,0,empty_insert) == 1);
    iStringInterface.Finalize(empty_insert);
    empty_insert = NULL;

    {
        StringList *empty = iStringInterface.Create();
        StringList *source = iStringInterface.Create();
        TEST_REQUIRE(empty != NULL && source != NULL);
        TEST_REQUIRE(iStringInterface.Add(source,"source") == 1);
        TEST_REQUIRE(iStringInterface.Append(empty,source) == 1);
        source = NULL;
        iStringInterface.Finalize(empty);
    }
    {
        StringList placement;
        memset(&placement,0,sizeof(placement));
        TEST_REQUIRE(iStringInterface.Init(&placement) == &placement);
        TEST_REQUIRE(iStringInterface.Add(&placement,"placement") == 1);
        TEST_REQUIRE(iStringInterface.Append(list,&placement) == 1);
        TEST_REQUIRE(iStringInterface.Size(&placement) == 0);
    }
    {
        StringList *readonly = iStringInterface.Create();
        StringList *mismatch = iStringInterface.CreateWithAllocator(&failure_allocator);
        TEST_REQUIRE(readonly != NULL && mismatch != NULL);
        TEST_REQUIRE(iStringInterface.Add(readonly,"readonly") == 1);
        iStringInterface.SetFlags(readonly,CONTAINER_READONLY);
        TEST_REQUIRE(iStringInterface.Append(list,readonly) == CONTAINER_ERROR_READONLY);
        TEST_REQUIRE(iStringInterface.SetCompareFunction(readonly,compare_reverse) != NULL);
        TEST_REQUIRE(iStringInterface.Clear(readonly) == CONTAINER_ERROR_READONLY);
        iStringInterface.SetFlags(readonly,0);
        iStringInterface.Finalize(readonly);
        TEST_REQUIRE(iStringInterface.Append(list,mismatch) == CONTAINER_ERROR_INCOMPATIBLE);
        iStringInterface.Finalize(mismatch);
    }
    {
        StringList placement;
        StringList *relocated;
        memset(&placement,0,sizeof(placement));
        TEST_REQUIRE(iStringInterface.Init(&placement) == &placement);
        relocated = iStringInterface.SetAllocator(&placement,(ContainerAllocator *)CurrentAllocator);
        TEST_REQUIRE(relocated != NULL);
        iStringInterface.Finalize(relocated);
    }
    iStringInterface.Finalize(range);
    range = iStringInterface.GetRange(list,0,(size_t)-1);
    TEST_REQUIRE(range != NULL);
    iStringInterface.Finalize(range);
    range = iStringInterface.GetRange(list,iStringInterface.Size(list)+1,0);
    TEST_REQUIRE(range != NULL);
    TEST_REQUIRE(iStringInterface.Equal(list,list) == 1);

    point = iStringInterface.FirstElement(list);
    TEST_REQUIRE(point != NULL);
    TEST_REQUIRE(iStringInterface.ElementData(point) != NULL);
    TEST_REQUIRE(iStringInterface.NextElement(point) != NULL);
    TEST_REQUIRE(iStringInterface.LastElement(list) != NULL);
    TEST_REQUIRE(iStringInterface.Skip(point,1) != NULL);
    TEST_REQUIRE(iStringInterface.Advance(&point) != NULL);
    suffix = iStringInterface.SplitAfter(list,iStringInterface.FirstElement(list));
    TEST_REQUIRE(suffix != NULL);
    /* The split result above owns the suffix; restore a simple list for the
     * remaining removal paths. */
    iStringInterface.Clear(list);
    TEST_REQUIRE(iStringInterface.Add(list,"first") == 1);
    TEST_REQUIRE(iStringInterface.Add(list,"middle") == 1);
    TEST_REQUIRE(iStringInterface.Add(list,"last") == 1);
    TEST_REQUIRE(iStringInterface.PopFront(list,output) == 1);
    TEST_REQUIRE(strcmp(output,"first") == 0);
    TEST_REQUIRE(iStringInterface.Erase(list,"last") == 1);
    TEST_REQUIRE(iStringInterface.EraseAt(list,0) == 1);
    TEST_REQUIRE(iStringInterface.Size(list) == 0);
    TEST_REQUIRE(iObserver.Unsubscribe(list,NULL) >= 1);

    /* Exercise iterator bounds, stale-object checks, and the non-first seek
     * traversal. */
    TEST_REQUIRE(iStringInterface.Add(list,"one") == 1);
    TEST_REQUIRE(iStringInterface.Add(list,"two") == 1);
    TEST_REQUIRE(iStringInterface.Add(list,"three") == 1);
    {
        Iterator *fresh = iStringInterface.NewIterator(list);
        Iterator bad;
        TEST_REQUIRE(fresh != NULL);
        TEST_REQUIRE(fresh->Seek(fresh,1) != NULL);
        TEST_REQUIRE(fresh->GetPrevious(fresh) != NULL);
        TEST_REQUIRE(fresh->GetNext(fresh) != NULL);
        TEST_REQUIRE(fresh->GetNext(fresh) != NULL);
        TEST_REQUIRE(fresh->GetNext(fresh) == NULL);
        TEST_REQUIRE(fresh->GetPosition(fresh) == 2);
        iStringInterface.Add(list,"stale");
        TEST_REQUIRE(fresh->Seek(fresh,0) == NULL);
        TEST_REQUIRE(fresh->GetNext(NULL) == NULL);
        iStringInterface.DeleteIterator(fresh);
        memset(&bad,0,sizeof(bad));
        TEST_REQUIRE(bad.GetNext == NULL);
        TEST_REQUIRE(iStringInterface.GetFlags(list) == 0);
        {
            Iterator *valid = iStringInterface.NewIterator(list);
            struct StringListIterator fake;
            TEST_REQUIRE(valid != NULL);
            memcpy(&fake,valid,sizeof(fake));
            fake.Magic = 0;
            fake.ownsStorage = 0;
            fake.ElementBuffer = NULL;
            TEST_REQUIRE(fake.it.Seek(&fake.it,0) == NULL);
            TEST_REQUIRE(fake.it.GetNext(&fake.it) == NULL);
            TEST_REQUIRE(fake.it.GetPrevious(&fake.it) == NULL);
            TEST_REQUIRE(fake.it.GetCurrent(&fake.it) == NULL);
            TEST_REQUIRE(fake.it.GetFirst(&fake.it) == NULL);
            TEST_REQUIRE(fake.it.GetPosition(&fake.it) == (size_t)-1);
            TEST_REQUIRE(fake.it.Replace(&fake.it,NULL,1) == CONTAINER_ERROR_WRONG_ITERATOR);
            TEST_REQUIRE(iStringInterface.DeleteIterator(&fake.it) == CONTAINER_ERROR_WRONG_ITERATOR);
            iStringInterface.DeleteIterator(valid);
        }
    }

    iStringInterface.Finalize(selected);
    selected = NULL;
    iMask.Finalize(mask);
    mask = NULL;
    iStringInterface.Finalize(range);
    range = NULL;
    iStringInterface.Finalize(suffix);
    suffix = NULL;
    iStringInterface.Finalize(list);
    list = NULL;
    iStringInterface.Finalize(other);
    other = NULL;
    return 0;

cleanup:
    if (mask != NULL) iMask.Finalize(mask);
    if (selected != NULL) iStringInterface.Finalize(selected);
    if (empty_insert != NULL) iStringInterface.Finalize(empty_insert);
    if (range != NULL) iStringInterface.Finalize(range);
    if (suffix != NULL) iStringInterface.Finalize(suffix);
    if (other != NULL) iStringInterface.Finalize(other);
    if (list != NULL) {
        iObserver.Unsubscribe(list,NULL);
        iStringInterface.SetFlags(list,0);
        iStringInterface.Finalize(list);
    }
    return -1;
}

static int test_wide_storage_and_readonly(void)
{
    wStringList *list = NULL;
    Iterator *it = NULL;
    wchar_t *value;
    wchar_t *placement;
    unsigned char storage[sizeof(struct wStringListIterator)];
    const wchar_t *long_value = L"a wide string with non-ASCII: \u03bb\u2603";

    list = iwStringInterface.Create();
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iwStringInterface.Add(list,L"a") == 1);
    TEST_REQUIRE(iwStringInterface.ReplaceAt(list,0,(wchar_t *)long_value) == 1);
    value = iwStringInterface.GetElement(list,0);
    TEST_REQUIRE(value != NULL && wcscmp(value,long_value) == 0);
    TEST_REQUIRE(iwStringInterface.Apply(list,append_mark_w,NULL) == 1);
    TEST_REQUIRE(iwStringInterface.GetElementSize(list) == sizeof(wchar_t));

    it = iwStringInterface.NewIterator(list);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(wcscmp((wchar_t *)it->GetFirst(it),L"A wide string with non-ASCII: \u03bb\u2603") == 0);
    TEST_REQUIRE(iwStringInterface.SetFlags(list,CONTAINER_READONLY) == 0);
    TEST_REQUIRE(wcscmp((wchar_t *)it->GetCurrent(it),L"A wide string with non-ASCII: \u03bb\u2603") == 0);
    TEST_REQUIRE(iwStringInterface.SetFlags(list,0) == CONTAINER_READONLY);
    iwStringInterface.DeleteIterator(it);
    it = NULL;

    placement = (wchar_t *)storage;
    TEST_REQUIRE(iwStringInterface.InitIterator(list,placement) == 1);
    TEST_REQUIRE(((Iterator *)placement)->GetFirst((Iterator *)placement) != NULL);
    TEST_REQUIRE(iwStringInterface.DeleteIterator((Iterator *)placement) == 1);

    if (list != NULL) {
        iwStringInterface.SetFlags(list,0);
        iwStringInterface.Finalize(list);
    }
    return 0;

cleanup:
    if (it != NULL) iwStringInterface.DeleteIterator(it);
    if (list != NULL) {
        iwStringInterface.SetFlags(list,0);
        iwStringInterface.Finalize(list);
    }
    return -1;
}

static int test_wide_api_surface(void)
{
    wStringList *list = NULL;
    wStringList *range = NULL;
    wStringList *copy = NULL;
    wStringListElement *element = NULL;
    Iterator *it = NULL;
    unsigned char storage[sizeof(struct wStringListIterator)];
    wchar_t value[] = L"value";
    wchar_t *values[] = {value};
    wchar_t out[32];
    size_t index = 0;

    TEST_REQUIRE(iwStringInterface.Size(NULL) == (size_t)CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.GetFlags(NULL) == 0);
    TEST_REQUIRE(iwStringInterface.SetFlags(NULL,0) == 0);
    TEST_REQUIRE(iwStringInterface.Clear(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Contains(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Finalize(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Apply(NULL,NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Equal(NULL,NULL) == 1);
    TEST_REQUIRE(iwStringInterface.Copy(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.SetErrorFunction(NULL,NULL) == iError.RaiseError);
    TEST_REQUIRE(iwStringInterface.Sizeof(NULL) == sizeof(wStringList));
    TEST_REQUIRE(iwStringInterface.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.DeleteIterator(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.SizeofIterator(NULL) == sizeof(struct wStringListIterator));
    TEST_REQUIRE(iwStringInterface.Save(NULL,NULL,NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Load(NULL,NULL,NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.GetElementSize(NULL) == 0);
    TEST_REQUIRE(iwStringInterface.Add(NULL,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.GetElement(NULL,0) == NULL);
    TEST_REQUIRE(iwStringInterface.PushFront(NULL,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.PopFront(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.InsertAt(NULL,0,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.EraseAt(NULL,0) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.ReplaceAt(NULL,0,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.IndexOf(NULL,value,NULL,&index) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.InsertIn(NULL,0,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.CopyElement(NULL,0,out) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.EraseRange(NULL,0,1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Sort(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Reverse(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.GetRange(NULL,0,1) == NULL);
    TEST_REQUIRE(iwStringInterface.Append(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.SetCompareFunction(NULL,NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.UseHeap(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.AddRange(NULL,1,values) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Init(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.SetAllocator(NULL,NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.GetAllocator(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.SetDestructor(NULL,NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.InitializeWith(1,NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.Back(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.Front(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.Select(NULL,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.SelectCopy(NULL,NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.FirstElement(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.LastElement(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.NextElement(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.ElementData(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.SetElementData(NULL,NULL,value) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.Advance(NULL) == NULL);
    TEST_REQUIRE(iwStringInterface.Skip(NULL,1) == NULL);
    TEST_REQUIRE(iwStringInterface.SplitAfter(NULL,NULL) == NULL);

    list = iwStringInterface.Create();
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iwStringInterface.Add(list,L"b") == 1);
    TEST_REQUIRE(iwStringInterface.PushFront(list,L"a") == 1);
    TEST_REQUIRE(iwStringInterface.InsertAt(list,2,L"c") == 1);
    TEST_REQUIRE(iwStringInterface.Contains(list,L"b") == 1);
    TEST_REQUIRE(iwStringInterface.CopyElement(list,0,out) == 1);
    TEST_REQUIRE(wcscmp(out,L"a") == 0);
    element = iwStringInterface.FirstElement(list);
    TEST_REQUIRE(element != NULL);
    TEST_REQUIRE(iwStringInterface.SetElementData(list,&element,L"aa") == 1);
    TEST_REQUIRE(iwStringInterface.Sort(list) == 1);
    TEST_REQUIRE(iwStringInterface.Reverse(list) == 1);
    range = iwStringInterface.GetRange(list,0,2);
    TEST_REQUIRE(range != NULL && iwStringInterface.Size(range) == 2);
    copy = iwStringInterface.SelectCopy(list,NULL);
    TEST_REQUIRE(copy == NULL);
    {
        Mask *mask = iMask.Create(iwStringInterface.Size(list));
        TEST_REQUIRE(mask != NULL);
        TEST_REQUIRE(iMask.SetElement(mask,0,1) == 1);
        TEST_REQUIRE(iwStringInterface.Select(list,mask) == 1);
        iMask.Finalize(mask);
    }
    TEST_REQUIRE(iwStringInterface.Erase(list,L"missing") == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iwStringInterface.EraseAt(list,0) == 1);
    TEST_REQUIRE(iwStringInterface.PopFront(list,NULL) == 0);
    TEST_REQUIRE(iwStringInterface.Size(list) == 0);
    TEST_REQUIRE(iwStringInterface.InitIterator(list,storage) == 1);
    it = (Iterator *)storage;
    TEST_REQUIRE(it->GetFirst(it) == NULL);
    TEST_REQUIRE(it->GetCurrent(it) == NULL);
    TEST_REQUIRE(iwStringInterface.DeleteIterator(it) == 1);
    it = NULL;
    iwStringInterface.Finalize(range);
    range = NULL;
    iwStringInterface.Finalize(list);
    list = NULL;

    list = iwStringInterface.Create();
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iwStringInterface.Add(list,L"keep") == 1);
    iwStringInterface.SetFlags(list,CONTAINER_READONLY);
    TEST_REQUIRE(iwStringInterface.Add(list,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.PushFront(list,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.PopFront(list,NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.InsertAt(list,0,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.EraseAt(list,0) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.ReplaceAt(list,0,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.EraseRange(list,0,1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.Reverse(list) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.AddRange(list,1,values) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.Select(list,NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iwStringInterface.FirstElement(list) == NULL);
    TEST_REQUIRE(iwStringInterface.LastElement(list) == NULL);
    TEST_REQUIRE(iwStringInterface.SetElementData(list,&element,value) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iwStringInterface.GetElement(list,0) == NULL);
    TEST_REQUIRE(iwStringInterface.Front(list) == NULL);
    TEST_REQUIRE(iwStringInterface.Back(list) == NULL);
    it = iwStringInterface.NewIterator(list);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->GetCurrent(it) != NULL);
    TEST_REQUIRE(it->GetNext(it) == NULL);
    TEST_REQUIRE(it->GetPrevious(it) == NULL);
    TEST_REQUIRE(it->Replace(it,NULL,1) == CONTAINER_ERROR_READONLY);
    iwStringInterface.DeleteIterator(it);
    it = NULL;
    copy = iwStringInterface.Copy(list);
    TEST_REQUIRE(copy != NULL);
    iwStringInterface.SetFlags(copy,0);
    iwStringInterface.Finalize(copy);
    copy = NULL;
    iwStringInterface.SetFlags(list,0);
    iwStringInterface.Finalize(list);
    list = NULL;

    if (copy != NULL) iwStringInterface.Finalize(copy);
    return 0;

cleanup:
    if (it != NULL) iwStringInterface.DeleteIterator(it);
    if (copy != NULL) {
        iwStringInterface.SetFlags(copy,0);
        iwStringInterface.Finalize(copy);
    }
    if (range != NULL) iwStringInterface.Finalize(range);
    if (list != NULL) {
        iwStringInterface.SetFlags(list,0);
        iwStringInterface.Finalize(list);
    }
    return -1;
}

static int test_copy_append_split_and_destructor(void)
{
    StringList *left = NULL;
    StringList *right = NULL;
    StringList *copy = NULL;
    StringList *suffix = NULL;
    StringListElement *point;

    destructor_calls = 0;
    left = iStringInterface.Create();
    right = iStringInterface.Create();
    TEST_REQUIRE(left != NULL && right != NULL);
    iStringInterface.SetDestructor(left,count_destructor);
    iStringInterface.SetDestructor(right,count_destructor);
    TEST_REQUIRE(iStringInterface.Add(left,"one") == 1);
    TEST_REQUIRE(iStringInterface.Add(left,"two") == 1);
    TEST_REQUIRE(iStringInterface.Add(right,"three") == 1);
    copy = iStringInterface.Copy(left);
    TEST_REQUIRE(copy != NULL && iStringInterface.Size(copy) == 2);
    TEST_REQUIRE(iStringInterface.Append(left,right) == 1);
    right = NULL;
    TEST_REQUIRE(iStringInterface.Size(left) == 3);
    point = iStringInterface.FirstElement(left);
    TEST_REQUIRE(point != NULL);
    suffix = iStringInterface.SplitAfter(left,point);
    TEST_REQUIRE(suffix != NULL && iStringInterface.Size(left) == 1 && iStringInterface.Size(suffix) == 2);
    TEST_REQUIRE(iStringInterface.Clear(left) == 1);
    TEST_REQUIRE(iStringInterface.Clear(suffix) == 1);
    TEST_REQUIRE(destructor_calls == 3);

    if (copy != NULL) iStringInterface.Finalize(copy);
    if (suffix != NULL) iStringInterface.Finalize(suffix);
    if (right != NULL) iStringInterface.Finalize(right);
    if (left != NULL) iStringInterface.Finalize(left);
    return 0;

cleanup:
    if (copy != NULL) iStringInterface.Finalize(copy);
    if (suffix != NULL) iStringInterface.Finalize(suffix);
    if (right != NULL) iStringInterface.Finalize(right);
    if (left != NULL) iStringInterface.Finalize(left);
    return -1;
}

static int test_erase_destructor_once(void)
{
    StringList *list = NULL;

    destructor_calls = 0;
    list = iStringInterface.Create();
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iStringInterface.SetDestructor(list,count_destructor) == NULL);
    TEST_REQUIRE(iStringInterface.Add(list,"owned") == 1);
    TEST_REQUIRE(iStringInterface.Erase(list,"owned") == 1);
    TEST_REQUIRE(destructor_calls == 1);
    TEST_REQUIRE(iStringInterface.Size(list) == 0);
    iStringInterface.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    if (list != NULL) iStringInterface.Finalize(list);
    return -1;
}

static int test_wide_success_surface(void)
{
    wStringList *list = NULL;
    wStringList *other = NULL;
    wStringList *range = NULL;
    wStringList *selected = NULL;
    wStringList *suffix = NULL;
    wStringListElement *point;
    Mask *mask = NULL;
    wchar_t output[64];
    wchar_t *batch[] = {L"one",L"two"};
    size_t position;
    FILE *stream = NULL;

    list = iwStringInterface.Create();
    other = iwStringInterface.Create();
    TEST_REQUIRE(list != NULL && other != NULL);
    TEST_REQUIRE(iwStringInterface.Add(list,L"b") == 1);
    TEST_REQUIRE(iwStringInterface.PushFront(list,L"a") == 1);
    TEST_REQUIRE(iwStringInterface.InsertAt(list,2,L"c") == 1);
    TEST_REQUIRE(iwStringInterface.InsertAt(list,0,L"zero") == 1);
    TEST_REQUIRE(iwStringInterface.CopyElement(list,1,output) == 1);
    TEST_REQUIRE(wcscmp(output,L"a") == 0);
    TEST_REQUIRE(iwStringInterface.Contains(list,L"b") == 1);
    TEST_REQUIRE(iwStringInterface.IndexOf(list,L"c",NULL,&position) == 1 && position == 3);
    TEST_REQUIRE(iwStringInterface.SetCompareFunction(list,compare_reverse_w) != NULL);
    TEST_REQUIRE(iwStringInterface.Sort(list) == 1);
    TEST_REQUIRE(iwStringInterface.SetCompareFunction(list,NULL) == compare_reverse_w);
    TEST_REQUIRE(iwStringInterface.Apply(list,append_mark_w,NULL) == 1);
    TEST_REQUIRE(iwStringInterface.Sizeof(list) > sizeof(wStringList));
    TEST_REQUIRE(iwStringInterface.AddRange(other,2,batch) == 1);
    TEST_REQUIRE(iwStringInterface.InsertIn(list,0,other) == 1);
    TEST_REQUIRE(iwStringInterface.InsertIn(list,iwStringInterface.Size(list),other) == 1);
    TEST_REQUIRE(iwStringInterface.InsertIn(list,2,other) == 1);
    TEST_REQUIRE(iwStringInterface.Append(list,other) == 1);
    other = NULL;
    range = iwStringInterface.GetRange(list,1,4);
    TEST_REQUIRE(range != NULL && iwStringInterface.Size(range) == 3);
    mask = iMask.Create(iwStringInterface.Size(list));
    TEST_REQUIRE(mask != NULL);
    for (position=0; position<iwStringInterface.Size(list); ++position)
        TEST_REQUIRE(iMask.SetElement(mask,position,(int)(position % 2)) == 1);
    selected = iwStringInterface.SelectCopy(list,mask);
    TEST_REQUIRE(selected != NULL);
    TEST_REQUIRE(iwStringInterface.Select(list,mask) == 1);
    point = iwStringInterface.FirstElement(list);
    TEST_REQUIRE(point != NULL);
    TEST_REQUIRE(iwStringInterface.ElementData(point) != NULL);
    TEST_REQUIRE(iwStringInterface.NextElement(point) != NULL);
    TEST_REQUIRE(iwStringInterface.LastElement(list) != NULL);
    TEST_REQUIRE(iwStringInterface.Skip(point,1) != NULL);
    TEST_REQUIRE(iwStringInterface.Advance(&point) != NULL);
    suffix = iwStringInterface.SplitAfter(list,iwStringInterface.FirstElement(list));
    TEST_REQUIRE(suffix != NULL);
    iwStringInterface.Clear(list);
    TEST_REQUIRE(iwStringInterface.Add(list,L"first") == 1);
    TEST_REQUIRE(iwStringInterface.Add(list,L"middle") == 1);
    TEST_REQUIRE(iwStringInterface.Add(list,L"last") == 1);
    TEST_REQUIRE(iwStringInterface.PopFront(list,output) == 1);
    TEST_REQUIRE(wcscmp(output,L"first") == 0);
    TEST_REQUIRE(iwStringInterface.Erase(list,L"last") == 1);
    TEST_REQUIRE(iwStringInterface.EraseAt(list,0) == 1);
    TEST_REQUIRE(iwStringInterface.Size(list) == 0);

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iwStringInterface.Add(list,L"persisted \u03bb") == 1);
    TEST_REQUIRE(iwStringInterface.Save(list,stream,NULL,NULL) == 1);
    rewind(stream);
    other = iwStringInterface.Load(stream,NULL,NULL);
    TEST_REQUIRE(other != NULL && iwStringInterface.Size(other) == 1);
    TEST_REQUIRE(wcscmp(iwStringInterface.GetElement(other,0),L"persisted \u03bb") == 0);
    fclose(stream);
    stream = NULL;

    iwStringInterface.Finalize(other);
    other = NULL;
    iwStringInterface.Finalize(selected);
    selected = NULL;
    iwStringInterface.Finalize(range);
    range = NULL;
    iwStringInterface.Finalize(suffix);
    suffix = NULL;
    iMask.Finalize(mask);
    mask = NULL;
    iwStringInterface.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (mask != NULL) iMask.Finalize(mask);
    if (selected != NULL) iwStringInterface.Finalize(selected);
    if (range != NULL) iwStringInterface.Finalize(range);
    if (suffix != NULL) iwStringInterface.Finalize(suffix);
    if (other != NULL) {
        iwStringInterface.SetFlags(other,0);
        iwStringInterface.Finalize(other);
    }
    if (list != NULL) {
        iwStringInterface.SetFlags(list,0);
        iwStringInterface.Finalize(list);
    }
    return -1;
}

static int test_persistence_and_truncation(void)
{
    StringList *source = NULL;
    StringList *loaded = NULL;
    FILE *stream = NULL;
    char *value;

    source = iStringInterface.Create();
    TEST_REQUIRE(source != NULL);
    TEST_REQUIRE(iStringInterface.Add(source,"short") == 1);
    TEST_REQUIRE(iStringInterface.Add(source,"a longer persisted value") == 1);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iStringInterface.Save(source,stream,NULL,NULL) == 1);
    rewind(stream);
    loaded = iStringInterface.Load(stream,NULL,NULL);
    TEST_REQUIRE(loaded != NULL && iStringInterface.Equal(source,loaded) == 1);
    value = iStringInterface.GetElement(loaded,1);
    TEST_REQUIRE(value != NULL && strcmp(value,"a longer persisted value") == 0);
    iStringInterface.Finalize(loaded);
    loaded = NULL;
    fclose(stream);
    stream = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iStringInterface.Save(source,stream,NULL,NULL) == 1);
    TEST_REQUIRE(ftell(stream) > 8);
    TEST_REQUIRE(fflush(stream) == 0);
    TEST_REQUIRE(ftruncate(fileno(stream),8) == 0);
    rewind(stream);
    TEST_REQUIRE(iStringInterface.Load(stream,NULL,NULL) == NULL);

    if (stream != NULL) fclose(stream);
    if (loaded != NULL) iStringInterface.Finalize(loaded);
    if (source != NULL) iStringInterface.Finalize(source);
    return 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (loaded != NULL) iStringInterface.Finalize(loaded);
    if (source != NULL) iStringInterface.Finalize(source);
    return -1;
}

static int test_allocator_failure_paths(void)
{
    StringList *list = NULL;
    StringList *copy = NULL;
    wStringList *wide = NULL;
    Iterator *it = NULL;
    FILE *stream = NULL;
    unsigned char iterator_storage[sizeof(struct StringListIterator)];
    FailureState saved;

    saved = failure_state;
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iStringInterface.CreateWithAllocator(&failure_allocator) == NULL);
    failure_state.calls = 0;
    failure_state.fail_at = 2;
    list = iStringInterface.CreateWithAllocator(&failure_allocator);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iStringInterface.Add(list,"fails") == CONTAINER_ERROR_NOMEMORY);
    iStringInterface.Finalize(list);
    list = NULL;

    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iwStringInterface.CreateWithAllocator(&failure_allocator) == NULL);
    failure_state.calls = 0;
    failure_state.fail_at = 0;
    wide = iwStringInterface.CreateWithAllocator(&failure_allocator);
    TEST_REQUIRE(wide != NULL);
    TEST_REQUIRE(iwStringInterface.Add(wide,L"fails") == 1);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iwStringInterface.NewIterator(wide) == NULL);
    TEST_REQUIRE(iwStringInterface.Copy(wide) == NULL);
    TEST_REQUIRE(iwStringInterface.ReplaceAt(wide,0,L"replacement") == CONTAINER_ERROR_NOMEMORY);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    iwStringInterface.SetFlags(wide,CONTAINER_READONLY);
    TEST_REQUIRE(iwStringInterface.Apply(wide,append_mark_w,NULL) == CONTAINER_ERROR_NOMEMORY);
    iwStringInterface.SetFlags(wide,0);
    failure_state.calls = 0;
    failure_state.fail_at = 0;
    TEST_REQUIRE(iwStringInterface.Add(wide,L"second") == 1);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iwStringInterface.Sort(wide) == CONTAINER_ERROR_NOMEMORY);
    iwStringInterface.Finalize(wide);
    wide = NULL;

    failure_state.calls = 0;
    failure_state.fail_at = 0;
    list = iStringInterface.CreateWithAllocator(&failure_allocator);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iStringInterface.Add(list,"value") == 1);
    failure_state.calls = 2;
    failure_state.fail_at = 2;
    TEST_REQUIRE(iStringInterface.NewIterator(list) == NULL);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iStringInterface.Copy(list) == NULL);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iStringInterface.ReplaceAt(list,0,"replacement") == CONTAINER_ERROR_NOMEMORY);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iStringInterface.SetElementData(list,&(StringListElement *){iStringInterface.FirstElement(list)},"again") == CONTAINER_ERROR_NOMEMORY);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    iStringInterface.SetFlags(list,CONTAINER_READONLY);
    TEST_REQUIRE(iStringInterface.Apply(list,append_mark,NULL) == CONTAINER_ERROR_NOMEMORY);
    iStringInterface.SetFlags(list,0);
    failure_state.calls = 0;
    failure_state.fail_at = 0;
    TEST_REQUIRE(iStringInterface.Add(list,"second") == 1);
    failure_state.calls = 0;
    failure_state.fail_at = 1;
    TEST_REQUIRE(iStringInterface.Sort(list) == CONTAINER_ERROR_NOMEMORY);
    iStringInterface.SetFlags(list,0);
    iStringInterface.Finalize(list);
    list = NULL;

    failure_state.calls = 0;
    failure_state.fail_at = 0;
    TEST_REQUIRE(iStringInterface.InitIterator(NULL,NULL) == sizeof(struct StringListIterator));
    list = iStringInterface.CreateWithAllocator(&failure_allocator);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iStringInterface.InitIterator(list,iterator_storage) == 1);
    it = (Iterator *)iterator_storage;
    TEST_REQUIRE(iStringInterface.DeleteIterator(it) == 1);
    it = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    fputs("bad",stream);
    rewind(stream);
    TEST_REQUIRE(iStringInterface.Load(stream,NULL,NULL) == NULL);
    fclose(stream);
    stream = NULL;
    iStringInterface.Finalize(list);
    list = NULL;
    failure_state = saved;
    return 0;

cleanup:
    failure_state = saved;
    if (stream != NULL) fclose(stream);
    if (it != NULL) iStringInterface.DeleteIterator(it);
    if (copy != NULL) iStringInterface.Finalize(copy);
    if (wide != NULL) {
        iwStringInterface.SetFlags(wide,0);
        iwStringInterface.Finalize(wide);
    }
    if (list != NULL) {
        iStringInterface.SetFlags(list,0);
        iStringInterface.Finalize(list);
    }
    return -1;
}

static const TestCase tests[] = {
    {"API surface and errors", test_api_surface_and_errors},
    {"narrow success surface", test_narrow_success_surface},
    {"narrow mutations", test_narrow_mutations},
    {"wide storage and readonly", test_wide_storage_and_readonly},
    {"wide API surface", test_wide_api_surface},
    {"wide success surface", test_wide_success_surface},
    {"copy append split destructor", test_copy_append_split_and_destructor},
    {"erase destructor once", test_erase_destructor_once},
    {"persistence and truncation", test_persistence_and_truncation},
    {"allocator failure paths", test_allocator_failure_paths},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {"stringlist family", tests,
                                    sizeof(tests)/sizeof(tests[0])};
    return &suite;
}
