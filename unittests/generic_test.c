#include "test_support.h"

#include "containers.h"
#include "ccl_internal.h"

#include <string.h>

static int error_count;
static int callback_delta;

static void *capture_error(const char *name, int code, ...)
{
    (void)name;
    (void)code;
    ++error_count;
    return NULL;
}

static int add_delta(void *element, void *arg)
{
    *(int *)element += *(int *)arg;
    return 1;
}

static int save_int(const void *element, void *arg, FILE *stream)
{
    (void)arg;
    return fwrite(element, sizeof(int), 1, stream) == 1;
}

static int save_one(const void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 1;
}

static int string_apply(void *element, void *arg)
{
    (void)element;
    (void)arg;
    return 1;
}

static int test_other_concrete_dispatch(void)
{
    Dlist *dlist = NULL;
    Vector *vector = NULL;
    WstrCollection *wstrings = NULL;
    strCollection *strings = NULL;
    Dlist *dlist_copy = NULL;
    Vector *vector_copy = NULL;
    WstrCollection *wstring_copy = NULL;
    strCollection *string_copy = NULL;
    int values[] = {4, 5};
    int needle = 4;
    wchar_t *wide_values[] = {L"one", L"two"};
    char *string_values[] = {(char *)"one", (char *)"two"};
    char *string_needle = (char *)"one";
    wchar_t *wide_needle = (wchar_t *)L"one";
    FILE *stream = NULL;

    dlist = iDlist.InitializeWith(sizeof(int), 2, values);
    vector = iVector.InitializeWith(sizeof(int), 2, values);
    strings = istrCollection.InitializeWith(2, string_values);
    wstrings = iWstrCollection.InitializeWith(2, wide_values);
    TEST_REQUIRE(dlist && vector && strings && wstrings);

    TEST_REQUIRE(iGeneric.GetFlags((GenericContainer *)dlist) == 0);
    TEST_REQUIRE(iGeneric.SetFlags((GenericContainer *)dlist, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iGeneric.SetFlags((GenericContainer *)dlist, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(iGeneric.Contains((GenericContainer *)dlist, &needle) == 1);
    iGeneric.Apply((GenericContainer *)dlist, add_delta, &(int){1});
    dlist_copy = (Dlist *)iGeneric.Copy((GenericContainer *)dlist);
    TEST_REQUIRE(dlist_copy && iGeneric.Equal((GenericContainer *)dlist,
                                               (GenericContainer *)dlist_copy));
    TEST_REQUIRE(iGeneric.Sizeof((GenericContainer *)dlist) > sizeof(Dlist));
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL &&
                 iGeneric.Save((GenericContainer *)dlist, stream, save_one, NULL) == 1);
    fclose(stream);
    stream = NULL;
    TEST_REQUIRE(iGeneric.Erase((GenericContainer *)dlist, &(int){5}) == 1);
    TEST_REQUIRE(iGeneric.EraseAll((GenericContainer *)dlist, &(int){5}) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iGeneric.Clear((GenericContainer *)dlist) == 1);

    TEST_REQUIRE(iGeneric.Contains((GenericContainer *)vector, &needle) == 1);
    iGeneric.Apply((GenericContainer *)vector, add_delta, &(int){1});
    vector_copy = (Vector *)iGeneric.Copy((GenericContainer *)vector);
    TEST_REQUIRE(vector_copy && iGeneric.Equal((GenericContainer *)vector,
                                                (GenericContainer *)vector_copy));
    TEST_REQUIRE(iGeneric.Sizeof((GenericContainer *)vector) > sizeof(Vector));
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL &&
                 iGeneric.Save((GenericContainer *)vector, stream, save_one, NULL) == 1);
    fclose(stream);
    stream = NULL;
    TEST_REQUIRE(iGeneric.Erase((GenericContainer *)vector, &(int){5}) == 1);
    TEST_REQUIRE(iGeneric.EraseAll((GenericContainer *)vector, &(int){5}) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iGeneric.Clear((GenericContainer *)vector) == 1);

    iGeneric.Apply((GenericContainer *)strings, string_apply, NULL);
    TEST_REQUIRE(iGeneric.Contains((GenericContainer *)strings, string_needle) == 1);
    string_copy = (strCollection *)iGeneric.Copy((GenericContainer *)strings);
    TEST_REQUIRE(string_copy && iGeneric.Equal((GenericContainer *)strings,
                                                (GenericContainer *)string_copy));
    TEST_REQUIRE(iGeneric.Sizeof((GenericContainer *)strings) > sizeof(strCollection));
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL &&
                 iGeneric.Save((GenericContainer *)strings, stream, save_one, NULL) == 1);
    fclose(stream);
    stream = NULL;
    TEST_REQUIRE(iGeneric.EraseAll((GenericContainer *)strings, string_needle) == 1);
    TEST_REQUIRE(iGeneric.Clear((GenericContainer *)strings) == 1);

    iGeneric.Apply((GenericContainer *)wstrings, string_apply, NULL);
    TEST_REQUIRE(iGeneric.Contains((GenericContainer *)wstrings, wide_needle) == 1);
    wstring_copy = (WstrCollection *)iGeneric.Copy((GenericContainer *)wstrings);
    TEST_REQUIRE(wstring_copy && iGeneric.Equal((GenericContainer *)wstrings,
                                                 (GenericContainer *)wstring_copy));
    TEST_REQUIRE(iGeneric.Sizeof((GenericContainer *)wstrings) > sizeof(WstrCollection));
    TEST_REQUIRE(iGeneric.EraseAll((GenericContainer *)wstrings, wide_needle) == 1);
    TEST_REQUIRE(iGeneric.Clear((GenericContainer *)wstrings) == 1);

    iGeneric.Finalize((GenericContainer *)wstring_copy);
    iGeneric.Finalize((GenericContainer *)string_copy);
    iGeneric.Finalize((GenericContainer *)vector_copy);
    iGeneric.Finalize((GenericContainer *)dlist_copy);
    iGeneric.Finalize((GenericContainer *)wstrings);
    iGeneric.Finalize((GenericContainer *)strings);
    iGeneric.Finalize((GenericContainer *)vector);
    iGeneric.Finalize((GenericContainer *)dlist);
    return 0;

cleanup:
    if (stream) fclose(stream);
    if (wstring_copy) iGeneric.Finalize((GenericContainer *)wstring_copy);
    if (string_copy) iGeneric.Finalize((GenericContainer *)string_copy);
    if (vector_copy) iGeneric.Finalize((GenericContainer *)vector_copy);
    if (dlist_copy) iGeneric.Finalize((GenericContainer *)dlist_copy);
    if (wstrings) iGeneric.Finalize((GenericContainer *)wstrings);
    if (strings) iGeneric.Finalize((GenericContainer *)strings);
    if (vector) iGeneric.Finalize((GenericContainer *)vector);
    if (dlist) iGeneric.Finalize((GenericContainer *)dlist);
    return -1;
}

static GenericContainer spy_container;
static Iterator spy_iterator;
static int spy_calls;

static size_t spy_size(const GenericContainer *container)
{ (void)container; ++spy_calls; return 11; }
static unsigned spy_get_flags(const GenericContainer *container)
{ (void)container; ++spy_calls; return 12; }
static unsigned spy_set_flags(GenericContainer *container, unsigned flags)
{ (void)container; (void)flags; ++spy_calls; return 13; }
static int spy_int(GenericContainer *container, const void *value)
{ (void)container; (void)value; ++spy_calls; return 1; }
static int spy_contains(const GenericContainer *container, const void *value)
{ (void)container; (void)value; ++spy_calls; return 1; }
static int spy_clear(GenericContainer *container)
{ (void)container; ++spy_calls; return 1; }
static int spy_finalize(GenericContainer *container)
{ (void)container; ++spy_calls; return 1; }
static void spy_apply(GenericContainer *container, int (*fn)(void *, void *), void *arg)
{ (void)container; (void)fn; (void)arg; ++spy_calls; }
static int spy_equal(const GenericContainer *left, const GenericContainer *right)
{ (void)left; (void)right; ++spy_calls; return 1; }
static GenericContainer *spy_copy(const GenericContainer *container)
{ ++spy_calls; return (GenericContainer *)container; }
static ErrorFunction spy_error(GenericContainer *container, ErrorFunction fn)
{ (void)container; (void)fn; ++spy_calls; return capture_error; }
static size_t spy_sizeof(const GenericContainer *container)
{ (void)container; ++spy_calls; return sizeof(GenericContainer); }
static Iterator *spy_new_iterator(GenericContainer *container)
{ (void)container; ++spy_calls; memset(&spy_iterator, 0, sizeof(spy_iterator)); return &spy_iterator; }
static int spy_init_iterator(GenericContainer *container, void *buffer)
{ (void)container; (void)buffer; ++spy_calls; return 1; }
static int spy_delete_iterator(Iterator *iterator)
{ (void)iterator; ++spy_calls; return 1; }
static size_t spy_sizeof_iterator(const GenericContainer *container)
{ (void)container; ++spy_calls; return sizeof(Iterator); }
static int spy_save(const GenericContainer *container, FILE *stream,
                    SaveFunction fn, void *arg)
{ (void)container; (void)stream; (void)fn; (void)arg; ++spy_calls; return 1; }

static GenericContainerInterface spy_interface = {
    spy_size, spy_get_flags, spy_set_flags, spy_clear, spy_contains, spy_int,
    spy_int, spy_finalize, spy_apply, spy_equal, spy_copy, spy_error,
    spy_sizeof, spy_new_iterator, spy_init_iterator, spy_delete_iterator,
    spy_sizeof_iterator, spy_save
};

static int test_protocol_spy(void)
{
    unsigned char storage[sizeof(Iterator)];
    Iterator *iterator;
    int value = 1;
    size_t size;

    memset(&spy_container, 0, sizeof(spy_container));
    spy_container.vTable = &spy_interface;
    spy_calls = 0;
    TEST_REQUIRE(iGeneric.Size(&spy_container) == 11);
    TEST_REQUIRE(iGeneric.GetFlags(&spy_container) == 12);
    TEST_REQUIRE(iGeneric.SetFlags(&spy_container, 0) == 13);
    TEST_REQUIRE(iGeneric.Clear(&spy_container) == 1);
    TEST_REQUIRE(iGeneric.Contains(&spy_container, &value) == 1);
    TEST_REQUIRE(iGeneric.Erase(&spy_container, &value) == 1);
    TEST_REQUIRE(iGeneric.EraseAll(&spy_container, &value) == 1);
    TEST_REQUIRE(iGeneric.Finalize(&spy_container) == 1);
    iGeneric.Apply(&spy_container, add_delta, &value);
    TEST_REQUIRE(iGeneric.Equal(&spy_container, &spy_container) == 1);
    TEST_REQUIRE(iGeneric.Copy(&spy_container) == &spy_container);
    TEST_REQUIRE(iGeneric.SetErrorFunction(&spy_container, capture_error) == capture_error);
    TEST_REQUIRE(iGeneric.Sizeof(&spy_container) == sizeof(GenericContainer));
    iterator = iGeneric.NewIterator(&spy_container);
    TEST_REQUIRE(iterator == &spy_iterator);
    TEST_REQUIRE(iGeneric.DeleteIterator(iterator) == 1);
    TEST_REQUIRE(iGeneric.InitIterator(&spy_container, storage) == 1);
    size = iGeneric.SizeofIterator(&spy_container);
    TEST_REQUIRE(size == sizeof(Iterator));
    TEST_REQUIRE(iGeneric.DeleteIterator((Iterator *)storage) == 1);
    TEST_REQUIRE(iGeneric.Save(&spy_container, NULL, NULL, NULL) == 1);
    TEST_REQUIRE(spy_calls >= 18);
    return 0;

cleanup:
    return -1;
}

static int test_list_dispatch(void)
{
    List *list = NULL;
    List *copy = NULL;
    Iterator *iterator = NULL;
    int values[] = {1, 2, 2};
    int needle = 2;
    int increment = 3;
    int output = 0;
    unsigned old_flags;
    ErrorFunction old_error;
    FILE *stream = NULL;

    list = iList.InitializeWith(sizeof(int), 3, values);
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iGeneric.Size((GenericContainer *)list) == 3);
    old_flags = iGeneric.SetFlags((GenericContainer *)list, CONTAINER_READONLY);
    TEST_REQUIRE(old_flags == 0 &&
                 iGeneric.GetFlags((GenericContainer *)list) == CONTAINER_READONLY);
    TEST_REQUIRE(iGeneric.SetFlags((GenericContainer *)list, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(iGeneric.Contains((GenericContainer *)list, &needle) == 1);
    TEST_REQUIRE(iGeneric.Erase((GenericContainer *)list, &needle) == 1);
    TEST_REQUIRE(iGeneric.EraseAll((GenericContainer *)list, &needle) == 1);

    callback_delta = increment;
    iGeneric.Apply((GenericContainer *)list, add_delta, &callback_delta);
    TEST_REQUIRE(*(int *)iList.GetElement(list, 0) == 4);

    copy = (List *)iGeneric.Copy((const GenericContainer *)list);
    TEST_REQUIRE(copy != NULL &&
                 iGeneric.Equal((const GenericContainer *)list,
                                (const GenericContainer *)copy) == 1);
    TEST_REQUIRE(iGeneric.Sizeof((const GenericContainer *)list) >= sizeof(List));

    old_error = iGeneric.SetErrorFunction((GenericContainer *)list, capture_error);
    TEST_REQUIRE(old_error != NULL);
    TEST_REQUIRE(iGeneric.SetFlags((GenericContainer *)list, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(error_count == 0); /* SetFlags is valid and does not report. */
    (void)iGeneric.SetErrorFunction((GenericContainer *)list, old_error);
    iGeneric.SetFlags((GenericContainer *)list, 0);

    iterator = iGeneric.NewIterator((GenericContainer *)list);
    TEST_REQUIRE(iterator != NULL && iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(*(int *)iterator->GetCurrent(iterator) == 4);
    TEST_REQUIRE(iGeneric.DeleteIterator(iterator) == 1);
    iterator = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iGeneric.Save((const GenericContainer *)list, stream,
                               save_int, NULL) == 1);
    TEST_REQUIRE(fseek(stream, 0, SEEK_END) == 0 && ftell(stream) > 0);
    (void)output;
    fclose(stream);
    stream = NULL;

    TEST_REQUIRE(iGeneric.Clear((GenericContainer *)list) == 1);
    TEST_REQUIRE(iGeneric.Size((GenericContainer *)list) == 0);
    iGeneric.Finalize((GenericContainer *)copy);
    copy = NULL;
    iGeneric.Finalize((GenericContainer *)list);
    list = NULL;
    return 0;

cleanup:
    if (stream) fclose(stream);
    if (iterator) iGeneric.DeleteIterator(iterator);
    if (copy) iGeneric.Finalize((GenericContainer *)copy);
    if (list) iGeneric.Finalize((GenericContainer *)list);
    return -1;
}

static int test_concrete_iterators(void)
{
    Dlist *dlist = NULL;
    Vector *vector = NULL;
    Iterator *iterator = NULL;
    unsigned char *storage = NULL;
    int values[] = {7, 8};

    dlist = iDlist.InitializeWith(sizeof(int), 2, values);
    TEST_REQUIRE(dlist != NULL);
    TEST_REQUIRE(iGeneric.SizeofIterator((GenericContainer *)dlist) ==
                 iDlist.SizeofIterator(dlist));
    iterator = iGeneric.NewIterator((GenericContainer *)dlist);
    TEST_REQUIRE(iterator != NULL && *(int *)iterator->GetFirst(iterator) == 7);
    TEST_REQUIRE(iGeneric.DeleteIterator(iterator) == 1);
    iterator = NULL;

    vector = iVector.InitializeWith(sizeof(int), 2, values);
    TEST_REQUIRE(vector != NULL);
    storage = (unsigned char *)malloc(iGeneric.SizeofIterator((GenericContainer *)vector));
    TEST_REQUIRE(storage != NULL);
    TEST_REQUIRE(iGeneric.InitIterator((GenericContainer *)vector, storage) == 1);
    iterator = (Iterator *)storage;
    TEST_REQUIRE(*(int *)iterator->GetLast(iterator) == 8);
    TEST_REQUIRE(iGeneric.DeleteIterator(iterator) == 1);
    iterator = NULL;
    free(storage);
    storage = NULL;

    iGeneric.Finalize((GenericContainer *)vector);
    iGeneric.Finalize((GenericContainer *)dlist);
    return 0;

cleanup:
    if (iterator && iterator != (Iterator *)storage)
        iGeneric.DeleteIterator(iterator);
    if (storage) free(storage);
    if (vector) iGeneric.Finalize((GenericContainer *)vector);
    if (dlist) iGeneric.Finalize((GenericContainer *)dlist);
    return -1;
}

static int test_string_dispatch(void)
{
    strCollection *strings = NULL;
    Iterator *iterator = NULL;
    char *values[] = {(char *)"alpha", (char *)"beta"};
    char *needle = (char *)"beta";

    strings = istrCollection.InitializeWith(2, values);
    TEST_REQUIRE(strings != NULL);
    TEST_REQUIRE(iGeneric.Size((GenericContainer *)strings) == 2);
    TEST_REQUIRE(iGeneric.Contains((GenericContainer *)strings, needle) == 1);
    iterator = iGeneric.NewIterator((GenericContainer *)strings);
    TEST_REQUIRE(iterator != NULL && strcmp((char *)iterator->GetFirst(iterator), "alpha") == 0);
    TEST_REQUIRE(iGeneric.DeleteIterator(iterator) == 1);
    iterator = NULL;
    TEST_REQUIRE(iGeneric.Erase((GenericContainer *)strings, needle) == 1);
    TEST_REQUIRE(iGeneric.Size((GenericContainer *)strings) == 1);
    iGeneric.Finalize((GenericContainer *)strings);
    strings = NULL;
    return 0;

cleanup:
    if (iterator) iGeneric.DeleteIterator(iterator);
    if (strings) iGeneric.Finalize((GenericContainer *)strings);
    return -1;
}

static int test_null_arguments(void)
{
    GenericContainer *null_gen = NULL;
    Iterator *null_iterator = NULL;
    int value = 1;
    size_t result = 0;
    ErrorFunction old_error;
    error_count = 0;
    old_error = iError.SetErrorFunction(capture_error);

    TEST_REQUIRE(iGeneric.Size(null_gen) == 0);
    TEST_REQUIRE(iGeneric.GetFlags(null_gen) == 0);
    TEST_REQUIRE(iGeneric.SetFlags(null_gen, 0) == 0);
    TEST_REQUIRE(iGeneric.Clear(null_gen) < 0);
    TEST_REQUIRE(iGeneric.Contains(null_gen, &value) < 0);
    TEST_REQUIRE(iGeneric.Erase(null_gen, &value) < 0);
    TEST_REQUIRE(iGeneric.EraseAll(null_gen, &value) < 0);
    TEST_REQUIRE(iGeneric.Finalize(null_gen) < 0);
    iGeneric.Apply(null_gen, add_delta, &value);
    TEST_REQUIRE(iGeneric.Equal(null_gen, null_gen) < 0);
    TEST_REQUIRE(iGeneric.Copy(null_gen) == NULL);
    TEST_REQUIRE(iGeneric.SetErrorFunction(null_gen, capture_error) != NULL);
    TEST_REQUIRE(iGeneric.Sizeof(null_gen) == 0);
    TEST_REQUIRE(iGeneric.NewIterator((GenericContainer *)null_gen) == NULL);
    TEST_REQUIRE(iGeneric.InitIterator(null_gen, &value) < 0);
    TEST_REQUIRE(iGeneric.DeleteIterator(null_iterator) < 0);
    TEST_REQUIRE(iGeneric.SizeofIterator(null_gen) == 0);
    TEST_REQUIRE(iGeneric.Save(null_gen, NULL, NULL, NULL) < 0);
    TEST_REQUIRE(error_count >= 18);
    (void)iError.SetErrorFunction(old_error);
    (void)result;
    return 0;

cleanup:
    (void)iError.SetErrorFunction(old_error);
    return -1;
}

static const TestCase tests[] = {
    {"list dispatch", test_list_dispatch},
    {"other concrete dispatch", test_other_concrete_dispatch},
    {"protocol spy", test_protocol_spy},
    {"concrete iterators", test_concrete_iterators},
    {"string dispatch", test_string_dispatch},
    {"null arguments", test_null_arguments},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {"generic", tests, sizeof(tests) / sizeof(tests[0])};
    return &suite;
}
