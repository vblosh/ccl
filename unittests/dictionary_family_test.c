#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "containers.h"
#include "test_support.h"

static void *quiet_error(const char *operation,int code,...)
{
    (void)operation;
    (void)code;
    return NULL;
}

static size_t constant_hash(const char *key)
{
    (void)key;
    return 0;
}

static size_t wide_constant_hash(const wchar_t *key)
{
    (void)key;
    return 0;
}

static int failing_save(const void *element,void *arg,FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int destructor_calls;

static int count_destructor(void *value)
{
    (void)value;
    ++destructor_calls;
    return 1;
}

static unsigned observer_events;

static void dictionary_observer(const void *object,unsigned operation,
                                const void *extra[])
{
    (void)extra;
    observer_events |= operation;
    if (operation == CCL_FINALIZE)
        iObserver.Unsubscribe((void *)object,NULL);
}

static int clear_from_apply(const char *key,const void *value,void *arg)
{
    Dictionary *dict = arg;
    (void)key;
    (void)value;
    iDictionary.Clear(dict);
    return 1;
}

static int wide_clear_from_apply(const wchar_t *key,const void *value,void *arg)
{
    WDictionary *dict = arg;
    (void)key;
    (void)value;
    iWDictionary.Clear(dict);
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

static void *failure_calloc(size_t count,size_t size)
{
    size_t bytes;
    if (size != 0 && count > (size_t)-1 / size)
        return NULL;
    bytes = count * size;
    return failure_malloc(bytes);
}

static void *failure_realloc(void *ptr,size_t size)
{
    if (ptr == NULL)
        return failure_malloc(size);
    return realloc(ptr,size);
}

static void failure_free(void *ptr)
{
    free(ptr);
}

static ContainerAllocator failure_allocator = {
    failure_malloc,
    failure_free,
    failure_realloc,
    failure_calloc
};

static int test_narrow_crud_hash_iterator(void)
{
    Dictionary *dict = NULL;
    Dictionary *left = NULL;
    Dictionary *right = NULL;
    Iterator *iterator = NULL;
    int one = 1, two = 2, three = 3, out = 0;
    void *first;

    dict = iDictionary.Create(sizeof(int),510);
    TEST_REQUIRE(dict != NULL);
    iDictionary.SetErrorFunction(dict,quiet_error);
    TEST_REQUIRE(iDictionary.Add(dict,"a",&one) == 1);
    TEST_REQUIRE(iDictionary.Add(dict,"a",NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(*(int *)iDictionary.GetElement(dict,"a") == one);
    TEST_REQUIRE(iDictionary.CopyElement(dict,"a",&out) == 1 && out == one);
    TEST_REQUIRE(iDictionary.Sizeof(dict) > iDictionary.Sizeof(NULL));
    destructor_calls = 0;
    iDictionary.SetDestructor(dict,count_destructor);
    TEST_REQUIRE(iDictionary.Add(dict,"a",&two) == 0);
    TEST_REQUIRE(destructor_calls == 1 && *(int *)iDictionary.GetElement(dict,"a") == two);

    iDictionary.SetHashFunction(dict,constant_hash);
    TEST_REQUIRE(iDictionary.Add(dict,"b",&three) == 1);
    TEST_REQUIRE(iDictionary.Contains(dict,"a") == 1 && iDictionary.Contains(dict,"b") == 1);
    iterator = iDictionary.NewIterator(dict);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    first = iterator->GetFirst(iterator);
    TEST_REQUIRE(first != NULL && iterator->GetCurrent(iterator) == first);
    TEST_REQUIRE(iterator->GetPosition(iterator) == 0);
    TEST_REQUIRE(iterator->Replace(iterator,&one,1) == 1);
    TEST_REQUIRE(iDictionary.Size(dict) == 2);
    TEST_REQUIRE(iterator->Replace(iterator,NULL,1) == 1);
    TEST_REQUIRE(iDictionary.Size(dict) == 1);
    TEST_REQUIRE(iterator->GetLast(iterator) != NULL);
    TEST_REQUIRE(iterator->GetPrevious(iterator) == NULL);
    TEST_REQUIRE(iterator->Seek(iterator,99) == NULL);
    iDictionary.DeleteIterator(iterator);
    iterator = NULL;

    left = iDictionary.Create(sizeof(int),0);
    right = iDictionary.Create(sizeof(int),0);
    TEST_REQUIRE(left != NULL && right != NULL);
    iDictionary.SetHashFunction(left,constant_hash);
    iDictionary.SetHashFunction(right,constant_hash);
    TEST_REQUIRE(iDictionary.Add(left,"a",&one) == 1);
    TEST_REQUIRE(iDictionary.Add(left,"b",&two) == 1);
    TEST_REQUIRE(iDictionary.Add(right,"b",&two) == 1);
    TEST_REQUIRE(iDictionary.Add(right,"a",&one) == 1);
    TEST_REQUIRE(iDictionary.Equal(left,right) == 1);
    iDictionary.Finalize(right);
    right = NULL;
    iDictionary.Finalize(left);
    left = NULL;
    iDictionary.Finalize(dict);
    return 0;

cleanup:
    if (iterator != NULL)
        iDictionary.DeleteIterator(iterator);
    if (right != NULL)
        iDictionary.Finalize(right);
    if (left != NULL)
        iDictionary.Finalize(left);
    if (dict != NULL)
        iDictionary.Finalize(dict);
    return -1;
}

static int test_narrow_bulk_copy_and_persistence(void)
{
    Dictionary *dict = NULL;
    Dictionary *copy = NULL;
    Dictionary *loaded = NULL;
    Dictionary *set = NULL;
    Dictionary *custom = NULL;
    Dictionary *custom_copy = NULL;
    FILE *stream = NULL;
    int value = 17;
    long end;

    dict = iDictionary.Create(sizeof(int),1);
    TEST_REQUIRE(dict != NULL);
    iDictionary.SetErrorFunction(dict,quiet_error);
    TEST_REQUIRE(iDictionary.Add(dict,"persist",&value) == 1);
    copy = iDictionary.Copy(dict);
    TEST_REQUIRE(copy != NULL && iDictionary.Size(copy) == 1);
    TEST_REQUIRE(iDictionary.GetAllocator(copy) == iDictionary.GetAllocator(dict));
    failure_state.calls = 0;
    failure_state.fail_at = 0;
    custom = iDictionary.CreateWithAllocator(sizeof(int),0,&failure_allocator);
    TEST_REQUIRE(custom != NULL);
    TEST_REQUIRE(iDictionary.Add(custom,"custom",&value) == 1);
    custom_copy = iDictionary.Copy(custom);
    TEST_REQUIRE(custom_copy != NULL &&
                 iDictionary.GetAllocator(custom_copy) == &failure_allocator);
    iDictionary.SetFlags(dict,CONTAINER_READONLY);
    {
        Dictionary *readonly_copy = iDictionary.Copy(dict);
        TEST_REQUIRE(readonly_copy != NULL && iDictionary.GetFlags(readonly_copy) & CONTAINER_READONLY);
        iDictionary.SetFlags(readonly_copy,0);
        iDictionary.Finalize(readonly_copy);
    }

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDictionary.Save(dict,stream,NULL,NULL) == 1);
    rewind(stream);
    loaded = iDictionary.Load(stream,NULL,NULL);
    TEST_REQUIRE(loaded != NULL && iDictionary.Size(loaded) == 1);
    TEST_REQUIRE(*(int *)iDictionary.GetElement(loaded,"persist") == value);
    fclose(stream);
    stream = NULL;

    set = iDictionary.Create(0,0);
    TEST_REQUIRE(set != NULL);
    TEST_REQUIRE(iDictionary.Add(set,"member",NULL) == 1);
    TEST_REQUIRE(strcmp((char *)iDictionary.GetElement(set,"member"),"member") == 0);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDictionary.Save(set,stream,NULL,NULL) == CONTAINER_ERROR_BADARG);
    end = ftell(stream);
    TEST_REQUIRE(end == 0);
    fclose(stream);
    stream = NULL;

    iDictionary.Clear(set);
    iDictionary.Finalize(set);
    iDictionary.SetFlags(dict,0);
    iDictionary.Finalize(custom_copy);
    iDictionary.Finalize(custom);
    iDictionary.Finalize(loaded);
    iDictionary.Finalize(copy);
    iDictionary.Finalize(dict);
    return 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (set != NULL)
        iDictionary.Finalize(set);
    if (custom_copy != NULL)
        iDictionary.Finalize(custom_copy);
    if (custom != NULL)
        iDictionary.Finalize(custom);
    if (loaded != NULL)
        iDictionary.Finalize(loaded);
    if (copy != NULL)
        iDictionary.Finalize(copy);
    if (dict != NULL)
    {
        iDictionary.SetFlags(dict,0);
        iDictionary.Finalize(dict);
    }
    return -1;
}

static int test_narrow_apply_and_placement_iterator(void)
{
    Dictionary *dict = NULL;
    unsigned char storage[256];
    Iterator *iterator;
    int value = 1;

    dict = iDictionary.Create(sizeof(int),0);
    TEST_REQUIRE(dict != NULL);
    TEST_REQUIRE(iDictionary.Add(dict,"clear",&value) == 1);
    TEST_REQUIRE(iDictionary.Apply(dict,clear_from_apply,dict) == 0);
    TEST_REQUIRE(iDictionary.Size(dict) == 0);
    TEST_REQUIRE(iDictionary.InitIterator(dict,storage) == 1);
    iterator = (Iterator *)storage;
    TEST_REQUIRE(iterator->GetNext != NULL && iterator->GetPrevious != NULL &&
                 iterator->GetCurrent != NULL && iterator->GetLast != NULL &&
                 iterator->Seek != NULL && iterator->GetPosition != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) == NULL);
    iDictionary.Finalize(dict);
    return 0;

cleanup:
    if (dict != NULL)
        iDictionary.Finalize(dict);
    return -1;
}

static int test_wide_dictionary(void)
{
    WDictionary *dict = NULL;
    WDictionary *copy = NULL;
    Iterator *iterator = NULL;
    int value = 41, replacement = 42;
    const wchar_t *keys[] = {L"wide", L"\x03a9"};
    int values[] = {7, 8};

    dict = iWDictionary.InitializeWith(sizeof(int),2,keys,values);
    TEST_REQUIRE(dict != NULL);
    iWDictionary.SetErrorFunction(dict,quiet_error);
    TEST_REQUIRE(iWDictionary.Contains(dict,L"wide") == 1);
    TEST_REQUIRE(*(int *)iWDictionary.GetElement(dict,L"\x03a9") == 8);
    TEST_REQUIRE(iWDictionary.Add(dict,L"replace",&value) == 1);
    TEST_REQUIRE(iWDictionary.Replace(dict,L"replace",&replacement) == 1);
    TEST_REQUIRE(*(int *)iWDictionary.GetElement(dict,L"replace") == replacement);
    iWDictionary.SetHashFunction(dict,wide_constant_hash);
    TEST_REQUIRE(iWDictionary.Contains(dict,L"wide") == 1 &&
                 iWDictionary.Contains(dict,L"\x03a9") == 1);
    iterator = iWDictionary.NewIterator(dict);
    TEST_REQUIRE(iterator != NULL && iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) != NULL);
    iWDictionary.DeleteIterator(iterator);
    iterator = NULL;
    copy = iWDictionary.Copy(dict);
    TEST_REQUIRE(copy != NULL && iWDictionary.Equal(dict,copy) == 1);
    iWDictionary.Finalize(copy);
    iWDictionary.Finalize(dict);
    return 0;

cleanup:
    if (iterator != NULL)
        iWDictionary.DeleteIterator(iterator);
    if (copy != NULL)
        iWDictionary.Finalize(copy);
    if (dict != NULL)
        iWDictionary.Finalize(dict);
    return -1;
}

static int test_wide_apply_clear(void)
{
    WDictionary *dict = NULL;
    int value = 1;

    dict = iWDictionary.Create(sizeof(int),0);
    TEST_REQUIRE(dict != NULL);
    TEST_REQUIRE(iWDictionary.Add(dict,L"clear",&value) == 1);
    TEST_REQUIRE(iWDictionary.Apply(dict,wide_clear_from_apply,dict) == 0);
    TEST_REQUIRE(iWDictionary.Size(dict) == 0);
    iWDictionary.Finalize(dict);
    return 0;

cleanup:
    if (dict != NULL)
        iWDictionary.Finalize(dict);
    return -1;
}

static int test_dictionary_error_and_allocator_paths(void)
{
    Dictionary *dict = NULL;
    Dictionary *other = NULL;
    Dictionary *failed = NULL;
    FILE *stream = NULL;
    const char *keys[] = {"one", "two"};
    int value = 3;
    ContainerAllocator *old_allocator = NULL;
    size_t fail;

    TEST_REQUIRE(iDictionary.Size(NULL) == 0);
    TEST_REQUIRE(iDictionary.GetFlags(NULL) == 0);
    TEST_REQUIRE(iDictionary.SetFlags(NULL,0) == 0);
    TEST_REQUIRE(iDictionary.Clear(NULL) < 0);
    TEST_REQUIRE(iDictionary.Contains(NULL,"x") < 0);
    TEST_REQUIRE(iDictionary.Erase(NULL,"x") < 0);
    TEST_REQUIRE(iDictionary.Finalize(NULL) < 0);
    TEST_REQUIRE(iDictionary.Apply(NULL,NULL,NULL) < 0);
    TEST_REQUIRE(iDictionary.Equal(NULL,NULL) == 1);
    TEST_REQUIRE(iDictionary.Equal(NULL,(Dictionary *)1) == 0);
    TEST_REQUIRE(iDictionary.Copy(NULL) == NULL);
    TEST_REQUIRE(iDictionary.Sizeof(NULL) > 0);
    TEST_REQUIRE(iDictionary.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iDictionary.InitIterator(NULL,NULL) < 0);
    TEST_REQUIRE(iDictionary.DeleteIterator(NULL) < 0);
    TEST_REQUIRE(iDictionary.SizeofIterator(NULL) >= sizeof(Iterator));
    TEST_REQUIRE(iDictionary.GetElementSize(NULL) == 0);
    TEST_REQUIRE(iDictionary.Add(NULL,"x",&value) < 0);
    TEST_REQUIRE(iDictionary.GetElement(NULL,"x") == NULL);
    TEST_REQUIRE(iDictionary.Replace(NULL,"x",&value) < 0);
    TEST_REQUIRE(iDictionary.Insert(NULL,"x",&value) < 0);
    TEST_REQUIRE(iDictionary.CastToArray(NULL) == NULL);
    TEST_REQUIRE(iDictionary.CopyElement(NULL,"x",&value) < 0);
    TEST_REQUIRE(iDictionary.InsertIn(NULL,NULL) < 0);
    TEST_REQUIRE(iDictionary.CreateWithAllocator(sizeof(int),0,NULL) == NULL);
    TEST_REQUIRE(iDictionary.Init(NULL,sizeof(int),0) == NULL);
    TEST_REQUIRE(iDictionary.InitWithAllocator(NULL,sizeof(int),0,NULL) == NULL);
    TEST_REQUIRE(iDictionary.GetKeys(NULL) == NULL);
    TEST_REQUIRE(iDictionary.GetAllocator(NULL) == NULL);
    TEST_REQUIRE(iDictionary.SetDestructor(NULL,count_destructor) == NULL);
    TEST_REQUIRE(iDictionary.InitializeWith(sizeof(int),1,NULL,&value) == NULL);
    TEST_REQUIRE(iDictionary.InitializeWith(sizeof(int),1,keys,NULL) == NULL);
    TEST_REQUIRE(iDictionary.SetHashFunction(NULL,NULL) != NULL);
    TEST_REQUIRE(iDictionary.GetLoadFactor(NULL) == 0.0);

    dict = iDictionary.Create(sizeof(int),0);
    other = iDictionary.Create(0,0);
    TEST_REQUIRE(dict != NULL && other != NULL);
    iDictionary.SetErrorFunction(dict,quiet_error);
    TEST_REQUIRE(iDictionary.GetElement(dict,NULL) == NULL);
    TEST_REQUIRE(iDictionary.CopyElement(dict,NULL,&value) < 0);
    TEST_REQUIRE(iDictionary.CopyElement(dict,"missing",NULL) == 0);
    TEST_REQUIRE(iDictionary.Contains(dict,NULL) < 0);
    TEST_REQUIRE(iDictionary.Erase(dict,"missing") == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iDictionary.Replace(dict,"missing",&value) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iDictionary.Insert(dict,"x",NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iDictionary.Add(dict,"x",&value) == 1);
    iDictionary.SetFlags(dict,CONTAINER_READONLY);
    TEST_REQUIRE(iDictionary.Add(dict,"y",&value) < 0);
    TEST_REQUIRE(iDictionary.Insert(dict,"y",&value) < 0);
    TEST_REQUIRE(iDictionary.Replace(dict,"x",&value) < 0);
    TEST_REQUIRE(iDictionary.Erase(dict,"x") < 0);
    TEST_REQUIRE(iDictionary.Clear(dict) < 0);
    TEST_REQUIRE(iDictionary.GetElement(dict,"x") == NULL);
    iDictionary.SetFlags(dict,0);
    TEST_REQUIRE(iDictionary.InsertIn(dict,other) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iDictionary.InsertIn(dict,dict) == 1);
    {
        Dictionary *empty = iDictionary.InitializeWith(0,0,NULL,NULL);
        TEST_REQUIRE(empty != NULL);
        iDictionary.Finalize(empty);
    }

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDictionary.Load(stream,NULL,NULL) == NULL);
    fclose(stream);
    stream = NULL;
    iDictionary.Finalize(other);
    other = NULL;
    iDictionary.Finalize(dict);
    dict = NULL;

    old_allocator = iAllocator.Change(&failure_allocator);
    for (fail=1; fail<=4; ++fail) {
        failure_state.calls = 0;
        failure_state.fail_at = fail;
        failed = iDictionary.Create(sizeof(int),0);
        if (failed != NULL)
            iDictionary.Finalize(failed);
        failed = NULL;
    }
    failure_state.calls = 0;
    failure_state.fail_at = 0;
    dict = iDictionary.Create(sizeof(int),0);
    TEST_REQUIRE(dict != NULL);
    failure_state.fail_at = failure_state.calls + 1;
    TEST_REQUIRE(iDictionary.Add(dict,"oom",&value) < 0);
    failure_state.fail_at = 0;
    TEST_REQUIRE(iDictionary.Size(dict) == 0);
    iDictionary.Finalize(dict);
    dict = NULL;
    iAllocator.Change(old_allocator);
    return 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (failed != NULL)
        iDictionary.Finalize(failed);
    if (other != NULL)
        iDictionary.Finalize(other);
    if (dict != NULL)
        iDictionary.Finalize(dict);
    if (old_allocator != NULL)
        iAllocator.Change(old_allocator);
    return -1;
}

static int test_dictionary_views_observers_and_stale_iterators(void)
{
    Dictionary *dict = NULL;
    Dictionary *source = NULL;
    Dictionary *destination = NULL;
    strCollection *keys = NULL;
    Vector *array = NULL;
    Iterator *iterator = NULL;
    int one = 1, two = 2;
    unsigned char storage[256];
    FILE *stream = NULL;

    dict = iDictionary.Create(sizeof(int),0);
    source = iDictionary.Create(sizeof(int),0);
    destination = iDictionary.Create(sizeof(int),0);
    TEST_REQUIRE(dict != NULL && source != NULL && destination != NULL);
    TEST_REQUIRE(iDictionary.Add(dict,"one",&one) == 1);
    TEST_REQUIRE(iDictionary.Add(dict,"two",&two) == 1);
    keys = iDictionary.GetKeys(dict);
    array = iDictionary.CastToArray(dict);
    TEST_REQUIRE(keys != NULL && array != NULL);
    TEST_REQUIRE(istrCollection.Size(keys) == 2 && iVector.Size(array) == 2);
    TEST_REQUIRE(iDictionary.InsertIn(destination,dict) == 1);
    TEST_REQUIRE(iDictionary.Size(destination) == 2);
    istrCollection.Finalize(keys);
    iVector.Finalize(array);
    keys = NULL;
    array = NULL;

    iterator = iDictionary.NewIterator(dict);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetPrevious(iterator) == NULL);
    TEST_REQUIRE(iterator->GetLast(iterator) != NULL);
    TEST_REQUIRE(iterator->GetPrevious(iterator) != NULL);
    TEST_REQUIRE(iterator->Seek(iterator,0) != NULL);
    TEST_REQUIRE(iterator->GetNext(iterator) != NULL);
    iDictionary.Add(dict,"three",&one);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    iDictionary.DeleteIterator(iterator);
    iterator = NULL;
    TEST_REQUIRE(iDictionary.InitIterator(dict,storage) == 1);
    iterator = (Iterator *)storage;
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    iDictionary.SetHashFunction(dict,constant_hash);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    iterator = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDictionary.Save(dict,stream,NULL,NULL) == 1);
    fclose(stream);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iDictionary.Save(dict,stream,failing_save,NULL) < 0 ||
                 ferror(stream));
    fclose(stream);
    stream = NULL;

    observer_events = 0;
    TEST_REQUIRE(iObserver.Subscribe(source,dictionary_observer,
                                     CCL_MODIFY|CCL_COPY|CCL_INSERT_IN|
                                     CCL_FINALIZE) == 1);
    TEST_REQUIRE(iDictionary.Add(source,"one",&one) == 1);
    TEST_REQUIRE(iDictionary.Insert(source,"two",&two) == 1);
    TEST_REQUIRE(iDictionary.Replace(source,"two",&one) == 1);
    TEST_REQUIRE(iDictionary.Erase(source,"one") == 1);
    TEST_REQUIRE(iDictionary.Clear(source) == 1);
    TEST_REQUIRE((observer_events & (CCL_ADD|CCL_INSERT|CCL_REPLACE|
                                     CCL_ERASE_AT|CCL_CLEAR)) != 0);
    TEST_REQUIRE(iDictionary.Add(source,"copy",&one) == 1);
    {
        Dictionary *copy = iDictionary.Copy(source);
        TEST_REQUIRE(copy != NULL);
        iDictionary.Finalize(copy);
    }
    TEST_REQUIRE((observer_events & CCL_COPY) != 0);
    iDictionary.Finalize(source);
    source = NULL;

    iDictionary.Finalize(destination);
    destination = NULL;
    iDictionary.Finalize(dict);
    dict = NULL;
    return 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (keys != NULL)
        istrCollection.Finalize(keys);
    if (array != NULL)
        iVector.Finalize(array);
    if (iterator != NULL && iterator != (Iterator *)storage)
        iDictionary.DeleteIterator(iterator);
    if (source != NULL) {
        iObserver.Unsubscribe(source,NULL);
        iDictionary.Finalize(source);
    }
    if (destination != NULL)
        iDictionary.Finalize(destination);
    if (dict != NULL)
        iDictionary.Finalize(dict);
    return -1;
}

static const TestCase dictionary_tests[] = {
    {"narrow CRUD, hash, equality, and iterator", test_narrow_crud_hash_iterator},
    {"narrow copy, persistence, and set policy", test_narrow_bulk_copy_and_persistence},
    {"narrow apply and placement iterator", test_narrow_apply_and_placement_iterator},
    {"wide CRUD, hash, and copy", test_wide_dictionary},
    {"wide apply clear mutation", test_wide_apply_clear},
    {"dictionary error and allocator paths", test_dictionary_error_and_allocator_paths},
    {"dictionary views, observers, and stale iterators", test_dictionary_views_observers_and_stale_iterators},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "Dictionary generator family",
        dictionary_tests,
        sizeof(dictionary_tests) / sizeof(dictionary_tests[0])
    };
    return &suite;
}
