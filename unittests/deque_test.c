#include "test_support.h"

#include "containers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned destructor_calls;
static unsigned observed_add;
static unsigned observed_pop;
static unsigned observed_clear;
static unsigned observed_copy;
static unsigned observed_finalize;
static const char *last_error_name;
static int last_error_code;

static int count_destructor(void *data)
{
    (void)data;
    ++destructor_calls;
    return 1;
}

static void *capture_error(const char *name, int code, ...)
{
    last_error_name = name;
    last_error_code = code;
    return NULL;
}

static int error_is(const char *name, int code)
{
    return last_error_name != NULL && strcmp(last_error_name, name) == 0 &&
           last_error_code == code;
}

static void observe(const void *object, unsigned operation,
                    const void *extra[])
{
    (void)object;
    (void)extra;
    if (operation == CCL_ADD)
        ++observed_add;
    else if (operation == CCL_POP)
        ++observed_pop;
    else if (operation == CCL_CLEAR)
        ++observed_clear;
    else if (operation == CCL_COPY)
        ++observed_copy;
    else if (operation == CCL_FINALIZE)
        ++observed_finalize;
}

static int check_sequence(Deque *d, const int *expected, size_t n)
{
    Iterator *it = NULL;
    size_t i;
    int *value;

    it = iDeque.NewIterator(d);
    if (it == NULL)
        return 0;
    value = (int *)it->GetFirst(it);
    for (i = 0; i < n; ++i) {
        if (value == NULL || *value != expected[i]) {
            (void)iDeque.DeleteIterator(it);
            return 0;
        }
        value = (int *)it->GetNext(it);
    }
    if (value != NULL || (it->GetCurrent(it) == NULL && n != 0)) {
        (void)iDeque.DeleteIterator(it);
        return 0;
    }
    (void)iDeque.DeleteIterator(it);
    return 1;
}

static int test_links_and_lifetime(void)
{
    Deque *d = NULL;
    int a = 1, b = 2, c = 3, out = 0;
    int expected[] = {1, 2, 3};

    destructor_calls = 0;
    d = iDeque.Create(sizeof(int));
    TEST_REQUIRE(d != NULL && iDeque.Size(d) == 0);
    TEST_REQUIRE(iDeque.PushFront(d, &a) == 1 && iDeque.Size(d) == 1);
    TEST_REQUIRE(iDeque.Front(d, &out) == 1 && out == 1);
    TEST_REQUIRE(iDeque.Back(d, &out) == 1 && out == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &b) == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &c) == 1);
    TEST_REQUIRE(check_sequence(d, expected, 3));
    TEST_REQUIRE(iDeque.SetDestructor(d, count_destructor) == NULL);
    TEST_REQUIRE(iDeque.PopFront(d, &out) == 1 && out == 3);
    TEST_REQUIRE(iDeque.PopBack(d, &out) == 1 && out == 1);
    TEST_REQUIRE(iDeque.Size(d) == 1 && iDeque.Front(d, &out) == 1 &&
                 out == 2);
    TEST_REQUIRE(iDeque.PopFront(d, &out) == 1 && out == 2);
    TEST_REQUIRE(iDeque.Size(d) == 0 && iDeque.PopBack(d, &out) == 0);
    TEST_REQUIRE(destructor_calls == 3);
    TEST_REQUIRE(iDeque.Finalize(d) == 1);
    d = NULL;
    return 0;

cleanup:
    if (d != NULL)
        (void)iDeque.Finalize(d);
    return -1;
}

static int apply_sum(void *data, void *arg)
{
    *(int *)arg += *(int *)data;
    return 1;
}

static int test_search_mutation_copy_reverse(void)
{
    Deque *d = NULL;
    Deque *copy = NULL;
    Deque *other = NULL;
    int values[] = {4, 2, 2, 9, 2};
    int expected[] = {2, 4, 2, 2, 9};
    int needle = 2;
    int sum = 0;

    d = iDeque.Create(sizeof(int));
    TEST_REQUIRE(d != NULL);
    TEST_REQUIRE(iDeque.PushFront(d, &values[0]) == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &values[1]) == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &values[2]) == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &values[3]) == 1);
    TEST_REQUIRE(iDeque.PushFront(d, &values[4]) == 1);
    /* PushFront is the logical left end; PushBack appends at the right. */
    TEST_REQUIRE(iDeque.Contains(d, &needle) == 1);
    iDeque.Apply(d, apply_sum, &sum);
    TEST_REQUIRE(sum == 19);
    TEST_REQUIRE(iDeque.Erase(d, &needle) == 1);
    TEST_REQUIRE(iDeque.Size(d) == 4);
    TEST_REQUIRE(iDeque.EraseAll(d, &needle) == 1);
    TEST_REQUIRE(iDeque.Size(d) == 2);
    TEST_REQUIRE(iDeque.Reverse(d) == 1);
    /* The remaining sequence after erasing is [4, 9], then reverse [9, 4]. */
    {
        int two[] = {9, 4};
        TEST_REQUIRE(check_sequence(d, two, 2));
    }
    TEST_REQUIRE(iDeque.Reverse(d) == 1);
    {
        int two[] = {4, 9};
        TEST_REQUIRE(check_sequence(d, two, 2));
    }
    copy = iDeque.Copy(d);
    other = iDeque.Create(sizeof(int));
    TEST_REQUIRE(copy != NULL && other != NULL && iDeque.Equal(d, copy) == 1);
    TEST_REQUIRE(iDeque.PushBack(copy, &needle) == 1 &&
                 iDeque.Equal(d, copy) == 0);
    TEST_REQUIRE(iDeque.PushBack(other, &values[0]) == 1 &&
                 iDeque.Equal(d, other) == 0);
    (void)iDeque.Clear(other);
    TEST_REQUIRE(iDeque.Size(other) == 0);
    iDeque.Finalize(other);
    other = NULL;
    iDeque.Finalize(copy);
    copy = NULL;
    iDeque.Finalize(d);
    d = NULL;
    (void)expected;
    return 0;

cleanup:
    if (other != NULL)
        iDeque.Finalize(other);
    if (copy != NULL)
        iDeque.Finalize(copy);
    if (d != NULL)
        iDeque.Finalize(d);
    return -1;
}

static int test_shallow_copy_destruction(void)
{
    Deque *source = NULL;
    Deque *copy = NULL;
    void *payload = (void *)(uintptr_t)0x1234;

    destructor_calls = 0;
    source = iDeque.Create(sizeof(payload));
    TEST_REQUIRE(source != NULL);
    TEST_REQUIRE(iDeque.SetDestructor(source, count_destructor) == NULL);
    TEST_REQUIRE(iDeque.PushBack(source, &payload) == 1);
    copy = iDeque.Copy(source);
    TEST_REQUIRE(copy != NULL);
    TEST_REQUIRE(iDeque.Finalize(copy) == 1);
    copy = NULL;
    TEST_REQUIRE(destructor_calls == 0);
    TEST_REQUIRE(iDeque.Finalize(source) == 1);
    source = NULL;
    TEST_REQUIRE(destructor_calls == 1);
    return 0;

cleanup:
    if (copy != NULL)
        iDeque.Finalize(copy);
    if (source != NULL)
        iDeque.Finalize(source);
    return -1;
}

static int test_iterator_and_errors(void)
{
    Deque *d = NULL;
    Iterator *heap_it = NULL;
    Iterator *placement_it = NULL;
    void *storage = NULL;
    int a = 7, b = 8, out;
    int *value;

    last_error_name = NULL;
    last_error_code = 0;
    d = iDeque.Create(sizeof(int));
    TEST_REQUIRE(d != NULL);
    (void)iDeque.SetErrorFunction(d, capture_error);
    TEST_REQUIRE(iDeque.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iDeque.InitIterator(d, NULL) == CONTAINER_ERROR_BADARG);
    heap_it = iDeque.NewIterator(d);
    TEST_REQUIRE(heap_it != NULL && heap_it->GetFirst(heap_it) == NULL &&
                 heap_it->GetNext(heap_it) == NULL);
    TEST_REQUIRE(iDeque.PushBack(d, &a) == 1);
    TEST_REQUIRE(heap_it->GetFirst(heap_it) == NULL &&
                 error_is("iDeque.GetFirst", CONTAINER_ERROR_OBJECT_CHANGED));
    iDeque.DeleteIterator(heap_it);
    heap_it = NULL;
    storage = malloc(iDeque.SizeofIterator(d));
    TEST_REQUIRE(storage != NULL);
    TEST_REQUIRE(iDeque.InitIterator(d, storage) == 1);
    placement_it = (Iterator *)storage;
    value = (int *)placement_it->GetFirst(placement_it);
    TEST_REQUIRE(value != NULL && *value == 7 &&
                 placement_it->GetCurrent(placement_it) == value);
    TEST_REQUIRE(iDeque.PushBack(d, &b) == 1);
    TEST_REQUIRE(placement_it->GetNext(placement_it) == NULL &&
                 error_is("iDeque.GetNext", CONTAINER_ERROR_OBJECT_CHANGED));
    TEST_REQUIRE(iDeque.DeleteIterator(placement_it) == 1);
    placement_it = NULL;
    free(storage);
    storage = NULL;
    heap_it = iDeque.NewIterator(d);
    TEST_REQUIRE(heap_it != NULL && heap_it->GetFirst(heap_it) != NULL &&
                 heap_it->GetNext(heap_it) != NULL &&
                 heap_it->GetPrevious(heap_it) != NULL &&
                 heap_it->GetPrevious(heap_it) == NULL);
    TEST_REQUIRE(iDeque.DeleteIterator(heap_it) == 1);
    heap_it = NULL;
    TEST_REQUIRE(iDeque.Front(d, NULL) == CONTAINER_ERROR_BADARG &&
                 error_is("iDeque.Front", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iDeque.PopBack(d, &out) == 1 && out == 7);
    TEST_REQUIRE(iDeque.PopBack(d, &out) == 1 && out == 8);
    iDeque.Finalize(d);
    d = NULL;
    return 0;

cleanup:
    if (heap_it != NULL)
        iDeque.DeleteIterator(heap_it);
    if (placement_it != NULL)
        iDeque.DeleteIterator(placement_it);
    free(storage);
    if (d != NULL)
        iDeque.Finalize(d);
    return -1;
}

static int save_int(const void *element, void *arg, FILE *stream)
{
    (void)arg;
    return fwrite(element, sizeof(int), 1, stream) == 1;
}

static int load_int(void *element, void *arg, FILE *stream)
{
    (void)arg;
    return fread(element, sizeof(int), 1, stream) == 1;
}

static int fail_load(void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int test_persistence_observer_and_init(void)
{
    Deque *d = NULL;
    Deque *loaded = NULL;
    Deque *bad = NULL;
    Deque *placement = NULL;
    FILE *stream = NULL;
    int values[] = {11, 12, 13};
    int out;

    observed_add = observed_pop = observed_clear = observed_copy = 0;
    observed_finalize = 0;
    d = iDeque.Create(sizeof(int));
    TEST_REQUIRE(d != NULL);
    TEST_REQUIRE(iObserver.Subscribe(d, observe,
                                     CCL_ADD | CCL_POP | CCL_CLEAR |
                                     CCL_COPY | CCL_FINALIZE) == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &values[0]) == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &values[1]) == 1);
    TEST_REQUIRE(iDeque.PushBack(d, &values[2]) == 1);
    TEST_REQUIRE(observed_add == 3);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL && iDeque.Save(d, stream, save_int, NULL) == 1);
    rewind(stream);
    loaded = iDeque.Load(stream, load_int, NULL);
    TEST_REQUIRE(loaded != NULL && iDeque.Equal(d, loaded) == 1);
    iDeque.Finalize(loaded);
    loaded = NULL;
    rewind(stream);
    TEST_REQUIRE(iDeque.Save(d, stream, NULL, NULL) == 1);
    rewind(stream);
    loaded = iDeque.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL && iDeque.Equal(d, loaded) == 1);
    rewind(stream);
    bad = iDeque.Load(stream, fail_load, NULL);
    TEST_REQUIRE(bad == NULL);
    TEST_REQUIRE(iDeque.PopFront(d, &out) == 1 && out == 13);
    TEST_REQUIRE(observed_pop == 1);
    TEST_REQUIRE(iDeque.Clear(d) == 0 && observed_clear == 1);
    {
        Deque *empty_copy = iDeque.Copy(d);
        TEST_REQUIRE(empty_copy != NULL);
        iDeque.Finalize(empty_copy);
    }
    TEST_REQUIRE(observed_copy == 1);
    iObserver.Unsubscribe(d, observe);
    iDeque.Finalize(loaded);
    loaded = NULL;
    fclose(stream);
    stream = NULL;
    iDeque.Finalize(d);
    d = NULL;
    TEST_REQUIRE(observed_finalize == 0);

    placement = (Deque *)calloc(1, iDeque.Sizeof(NULL));
    TEST_REQUIRE(placement != NULL);
    TEST_REQUIRE(iDeque.Init(placement, sizeof(int)) == placement);
    TEST_REQUIRE(iDeque.PushFront(placement, &values[0]) == 1);
    TEST_REQUIRE(iDeque.Finalize(placement) == 1);
    free(placement);
    placement = NULL;
    return 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (bad != NULL)
        iDeque.Finalize(bad);
    if (loaded != NULL)
        iDeque.Finalize(loaded);
    if (d != NULL) {
        iObserver.Unsubscribe(d, observe);
        iDeque.Finalize(d);
    }
    free(placement);
    return -1;
}

static int test_bad_arguments(void)
{
    Deque *d = NULL;
    Deque *empty = NULL;
    Iterator *it = NULL;
    int value = 1;
    int out = 0;

    TEST_REQUIRE(iDeque.Create(0) == NULL);
    TEST_REQUIRE(iDeque.Init(NULL, sizeof(int)) == NULL);
    (void)iDeque.Size(NULL);
    (void)iDeque.GetFlags(NULL);
    (void)iDeque.SetFlags(NULL, 0);
    (void)iDeque.Clear(NULL);
    (void)iDeque.Contains(NULL, &value);
    (void)iDeque.Erase(NULL, &value);
    (void)iDeque.EraseAll(NULL, &value);
    (void)iDeque.Finalize(NULL);
    (void)iDeque.Apply(NULL, apply_sum, &out);
    (void)iDeque.Equal(NULL, NULL);
    (void)iDeque.Copy(NULL);
    (void)iDeque.Sizeof(NULL);
    (void)iDeque.DeleteIterator(NULL);
    d = iDeque.Create(sizeof(int));
    TEST_REQUIRE(d != NULL);
    empty = iDeque.Create(sizeof(int));
    TEST_REQUIRE(empty != NULL);
    TEST_REQUIRE(iDeque.Reverse(empty) == 0 && iDeque.PopFront(empty, &out) == 0 &&
                 iDeque.PopBack(empty, &out) == 0 &&
                 iDeque.Front(empty, &out) == 0 &&
                 iDeque.Back(empty, &out) == 0);
    TEST_REQUIRE(iDeque.SetFlags(d, CONTAINER_READONLY) == 0 &&
                 iDeque.GetFlags(d) == CONTAINER_READONLY);
    TEST_REQUIRE(iDeque.SetFlags(d, 0) == CONTAINER_READONLY);
    TEST_REQUIRE(iDeque.PushBack(d, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iDeque.PushFront(d, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iDeque.Contains(d, NULL) == 0);
    TEST_REQUIRE(iDeque.Erase(d, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iDeque.EraseAll(d, NULL) == CONTAINER_ERROR_BADARG);
    iDeque.Apply(d, NULL, NULL);
    TEST_REQUIRE(iDeque.Save(d, NULL, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iDeque.Load(NULL, NULL, NULL) == NULL);
    TEST_REQUIRE(iDeque.SetDestructor(d, count_destructor) == NULL);
    TEST_REQUIRE(iDeque.SetDestructor(d, NULL) == count_destructor);
    TEST_REQUIRE(iDeque.PushBack(d, &value) == 1);
    it = iDeque.NewIterator(d);
    TEST_REQUIRE(it != NULL && it->GetCurrent(it) == NULL &&
                 it->GetPrevious(it) == NULL && it->GetNext(it) == NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL && it->GetPrevious(it) == NULL &&
                 it->GetNext(it) == NULL);
    TEST_REQUIRE(iDeque.DeleteIterator(it) == 1);
    it = NULL;
    TEST_REQUIRE(iDeque.Sizeof(d) > iDeque.Sizeof(NULL));
    TEST_REQUIRE(iDeque.Finalize(d) == 1);
    d = NULL;
    TEST_REQUIRE(iDeque.Finalize(empty) == 1);
    empty = NULL;
    return 0;

cleanup:
    if (it != NULL)
        iDeque.DeleteIterator(it);
    if (empty != NULL)
        iDeque.Finalize(empty);
    if (d != NULL)
        iDeque.Finalize(d);
    return -1;
}

static const TestCase deque_tests[] = {
    {"links, endpoints, and lifetime", test_links_and_lifetime},
    {"search, mutation, copy, and reverse", test_search_mutation_copy_reverse},
    {"shallow copy destruction ownership", test_shallow_copy_destruction},
    {"iterators and errors", test_iterator_and_errors},
    {"persistence, observers, and placement", test_persistence_observer_and_init},
    {"bad arguments", test_bad_arguments},
};

static const TestSuite deque_suite = {
    "standalone deque",
    deque_tests,
    sizeof(deque_tests) / sizeof(deque_tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &deque_suite;
}
