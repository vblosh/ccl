#include "test_support.h"

#include "containers.h"
#include "ccl_internal.h"

static int sequential_apply(void *element, void *arg)
{
    *(int *)element += arg ? *(int *)arg : 1;
    return 1;
}

static void *sequential_error(const char *name, int code, ...)
{
    (void)name;
    (void)code;
    return NULL;
}

static int test_operations(void)
{
    List *list = NULL;
    List *generic_list = NULL;
    List *generic_copy = NULL;
    SequentialContainer *sc;
    SequentialContainer *generic_sc;
    Iterator *iterator = NULL;
    int value;
    size_t result_index = 0;
    int initial[] = {2, 3};
    FILE *stream = NULL;

    list = iList.InitializeWith(sizeof(int), 2, initial);
    TEST_REQUIRE(list != NULL);
    sc = (SequentialContainer *)list;

    /* Exercise every delegated generic entry on the sequential view. */
    generic_list = iList.InitializeWith(sizeof(int), 2, initial);
    TEST_REQUIRE(generic_list != NULL);
    generic_sc = (SequentialContainer *)generic_list;
    TEST_REQUIRE(iSequentialContainer.Contains(generic_sc, &(int){2}) == 1);
    TEST_REQUIRE(iSequentialContainer.Erase(generic_sc, &(int){2}) == 1);
    TEST_REQUIRE(iSequentialContainer.EraseAll(generic_sc, &(int){3}) == 1);
    iSequentialContainer.Apply(generic_sc, sequential_apply, &(int){1});
    generic_copy = (List *)iSequentialContainer.Copy(generic_sc);
    TEST_REQUIRE(generic_copy != NULL &&
                 iSequentialContainer.Equal(generic_sc,
                                             (SequentialContainer *)generic_copy) == 1);
    TEST_REQUIRE(iSequentialContainer.Sizeof(generic_sc) >= sizeof(List));
    TEST_REQUIRE(iSequentialContainer.SetErrorFunction(generic_sc, sequential_error) != NULL);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL &&
                 iSequentialContainer.Save(generic_sc, stream, NULL, NULL) == 1);
    fclose(stream);
    stream = NULL;
    TEST_REQUIRE(iSequentialContainer.Clear(generic_sc) == 1);
    iSequentialContainer.Finalize((SequentialContainer *)generic_copy);
    generic_copy = NULL;
    iSequentialContainer.Finalize(generic_sc);
    generic_list = NULL;

    TEST_REQUIRE(iSequentialContainer.Size(sc) == 2);
    TEST_REQUIRE(iSequentialContainer.GetElementSize(sc) == sizeof(int));
    TEST_REQUIRE(iSequentialContainer.Add(sc, &(int){4}) == 1);
    TEST_REQUIRE(*(int *)iSequentialContainer.GetElement(sc, 2) == 4);
    TEST_REQUIRE(iSequentialContainer.Push(sc, &(int){1}) == 1);
    TEST_REQUIRE(iSequentialContainer.Pop(sc, &value) == 1 && value == 1);
    TEST_REQUIRE(iSequentialContainer.InsertAt(sc, 1, &(int){9}) == 1);
    TEST_REQUIRE(iSequentialContainer.ReplaceAt(sc, 1, &(int){8}) == 1);
    TEST_REQUIRE(iSequentialContainer.IndexOf(sc, &(int){8}, NULL, &result_index) == 1);
    TEST_REQUIRE(result_index == 1);
    TEST_REQUIRE(iSequentialContainer.EraseAt(sc, 1) == 1);
    TEST_REQUIRE(iSequentialContainer.Size(sc) == 3);
    TEST_REQUIRE(iSequentialContainer.SetFlags(sc, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iSequentialContainer.GetFlags(sc) == CONTAINER_READONLY);
    TEST_REQUIRE(iSequentialContainer.SetFlags(sc, 0) == CONTAINER_READONLY);

    iterator = iSequentialContainer.NewIterator(sc);
    TEST_REQUIRE(iterator != NULL && *(int *)iterator->GetFirst(iterator) == 2);
    TEST_REQUIRE(iSequentialContainer.SizeofIterator(sc) == iList.SizeofIterator(list));
    TEST_REQUIRE(iSequentialContainer.DeleteIterator(iterator) == 1);
    iterator = NULL;

    TEST_REQUIRE(iSequentialContainer.Clear(sc) == 1);
    TEST_REQUIRE(iSequentialContainer.Size(sc) == 0);
    iSequentialContainer.Finalize(sc);
    list = NULL;
    return 0;

cleanup:
    if (stream) fclose(stream);
    if (iterator) iSequentialContainer.DeleteIterator(iterator);
    if (generic_copy) iSequentialContainer.Finalize((SequentialContainer *)generic_copy);
    if (generic_list) iSequentialContainer.Finalize((SequentialContainer *)generic_list);
    if (list) iSequentialContainer.Finalize((SequentialContainer *)list);
    return -1;
}

static int test_append_and_compatibility(void)
{
    List *destination = NULL;
    List *source = NULL;
    Vector *vector = NULL;
    Vector *short_vector = NULL;
    SequentialContainer *dst_sc;
    SequentialContainer *src_sc;
    int values[] = {1, 2};
    int out;

    destination = iList.Create(sizeof(int));
    source = iList.InitializeWith(sizeof(int), 2, values);
    TEST_REQUIRE(destination != NULL && source != NULL);
    dst_sc = (SequentialContainer *)destination;
    src_sc = (SequentialContainer *)source;
    TEST_REQUIRE(iSequentialContainer.Append(dst_sc, src_sc) == 1);
    TEST_REQUIRE(iSequentialContainer.Size(dst_sc) == 2 &&
                 iSequentialContainer.Size(src_sc) == 2);
    TEST_REQUIRE(*(int *)iSequentialContainer.GetElement(dst_sc, 0) == 1);
    TEST_REQUIRE(iSequentialContainer.Append(dst_sc, dst_sc) == CONTAINER_ERROR_BADARG);

    vector = iVector.Create(sizeof(int), 1);
    TEST_REQUIRE(vector != NULL);
    TEST_REQUIRE(iSequentialContainer.Add((SequentialContainer *)vector, &(int){7}) == 1);
    TEST_REQUIRE(iSequentialContainer.Append((SequentialContainer *)vector, src_sc) == 1);
    TEST_REQUIRE(iSequentialContainer.Size((SequentialContainer *)vector) == 3);
    TEST_REQUIRE(iSequentialContainer.Pop((SequentialContainer *)vector, &out) == 1 && out == 2);

    short_vector = iVector.Create(sizeof(short), 1);
    TEST_REQUIRE(short_vector != NULL);
    TEST_REQUIRE(iSequentialContainer.Append((SequentialContainer *)vector,
                                             (SequentialContainer *)short_vector) ==
                 CONTAINER_ERROR_INCOMPATIBLE);

    iSequentialContainer.SetFlags(dst_sc, CONTAINER_READONLY);
    TEST_REQUIRE(iSequentialContainer.Append(dst_sc, src_sc) == CONTAINER_ERROR_READONLY);
    iSequentialContainer.SetFlags(dst_sc, 0);
    iSequentialContainer.Finalize((SequentialContainer *)short_vector);
    short_vector = NULL;
    iSequentialContainer.Finalize((SequentialContainer *)vector);
    vector = NULL;
    iSequentialContainer.Finalize(src_sc);
    source = NULL;
    iSequentialContainer.Finalize(dst_sc);
    destination = NULL;
    return 0;

cleanup:
    if (short_vector) iSequentialContainer.Finalize((SequentialContainer *)short_vector);
    if (vector) iSequentialContainer.Finalize((SequentialContainer *)vector);
    if (source) iSequentialContainer.Finalize((SequentialContainer *)source);
    if (destination) iSequentialContainer.Finalize((SequentialContainer *)destination);
    return -1;
}

static int test_dlist_and_vector_iterators(void)
{
    Dlist *dlist = NULL;
    Vector *vector = NULL;
    Iterator *iterator = NULL;
    int data[] = {5, 6};

    dlist = iDlist.InitializeWith(sizeof(int), 2, data);
    vector = iVector.InitializeWith(sizeof(int), 2, data);
    TEST_REQUIRE(dlist != NULL && vector != NULL);
    iterator = iSequentialContainer.NewIterator((SequentialContainer *)dlist);
    TEST_REQUIRE(iterator != NULL && *(int *)iterator->GetLast(iterator) == 6);
    TEST_REQUIRE(iSequentialContainer.DeleteIterator(iterator) == 1);
    iterator = NULL;
    iterator = iSequentialContainer.NewIterator((SequentialContainer *)vector);
    TEST_REQUIRE(iterator != NULL && *(int *)iterator->GetFirst(iterator) == 5);
    TEST_REQUIRE(iSequentialContainer.DeleteIterator(iterator) == 1);
    iterator = NULL;
    TEST_REQUIRE(iSequentialContainer.SizeofIterator((SequentialContainer *)dlist) ==
                 iDlist.SizeofIterator(dlist));
    TEST_REQUIRE(iSequentialContainer.SizeofIterator((SequentialContainer *)vector) ==
                 iVector.SizeofIterator(vector));
    TEST_REQUIRE(iSequentialContainer.GetElementSize((SequentialContainer *)dlist) ==
                 sizeof(int));
    TEST_REQUIRE(iSequentialContainer.GetElementSize((SequentialContainer *)vector) ==
                 sizeof(int));
    iSequentialContainer.Finalize((SequentialContainer *)vector);
    iSequentialContainer.Finalize((SequentialContainer *)dlist);
    vector = NULL;
    dlist = NULL;
    return 0;

cleanup:
    if (iterator) iSequentialContainer.DeleteIterator(iterator);
    if (vector) iSequentialContainer.Finalize((SequentialContainer *)vector);
    if (dlist) iSequentialContainer.Finalize((SequentialContainer *)dlist);
    return -1;
}

static int test_unsupported_variable_strings(void)
{
    strCollection *left = NULL;
    strCollection *right = NULL;
    WstrCollection *wide = NULL;
    SequentialContainer *left_sc;
    SequentialContainer *right_sc;
    char *values[] = {(char *)"a"};
    wchar_t *wide_values[] = {(wchar_t *)L"a"};
    char output[8];
    size_t index = 0;

    left = istrCollection.InitializeWith(1, values);
    right = istrCollection.InitializeWith(1, values);
    wide = iWstrCollection.InitializeWith(1, wide_values);
    TEST_REQUIRE(left != NULL && right != NULL && wide != NULL);
    left_sc = (SequentialContainer *)left;
    right_sc = (SequentialContainer *)right;
    TEST_REQUIRE(iSequentialContainer.Add(left_sc, "b") == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.GetElement(left_sc, 0) == NULL);
    TEST_REQUIRE(iSequentialContainer.Push(left_sc, (void *)"b") == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.Pop(left_sc, output) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.InsertAt(left_sc, 0, "b") == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.EraseAt(left_sc, 0) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.ReplaceAt(left_sc, 0, "b") == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.IndexOf(left_sc, "a", NULL, &index) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.Append(left_sc, right_sc) == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iSequentialContainer.GetElementSize(left_sc) == 0);
    TEST_REQUIRE(iSequentialContainer.GetElementSize((SequentialContainer *)wide) == 0);
    iSequentialContainer.Finalize(left_sc);
    iSequentialContainer.Finalize(right_sc);
    iSequentialContainer.Finalize((SequentialContainer *)wide);
    left = NULL;
    right = NULL;
    wide = NULL;
    return 0;

cleanup:
    if (left) iSequentialContainer.Finalize((SequentialContainer *)left);
    if (right) iSequentialContainer.Finalize((SequentialContainer *)right);
    if (wide) iSequentialContainer.Finalize((SequentialContainer *)wide);
    return -1;
}

static int test_null_arguments(void)
{
    SequentialContainer *null_sc = NULL;
    Iterator *iterator = NULL;
    int value = 1;
    size_t index = 0;

    TEST_REQUIRE(iSequentialContainer.Size(null_sc) == 0);
    TEST_REQUIRE(iSequentialContainer.GetFlags(null_sc) == 0);
    TEST_REQUIRE(iSequentialContainer.SetFlags(null_sc, 0) == 0);
    TEST_REQUIRE(iSequentialContainer.Clear(null_sc) < 0);
    TEST_REQUIRE(iSequentialContainer.Contains(null_sc, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.Erase(null_sc, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.EraseAll(null_sc, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.Finalize(null_sc) < 0);
    iSequentialContainer.Apply(null_sc, NULL, NULL);
    TEST_REQUIRE(iSequentialContainer.Equal(null_sc, null_sc) < 0);
    TEST_REQUIRE(iSequentialContainer.Copy(null_sc) == NULL);
    TEST_REQUIRE(iSequentialContainer.SetErrorFunction(null_sc, NULL) != NULL);
    TEST_REQUIRE(iSequentialContainer.Sizeof(null_sc) == 0);
    TEST_REQUIRE(iSequentialContainer.NewIterator(null_sc) == NULL);
    TEST_REQUIRE(iSequentialContainer.InitIterator(null_sc, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.DeleteIterator(iterator) < 0);
    TEST_REQUIRE(iSequentialContainer.SizeofIterator(null_sc) == 0);
    TEST_REQUIRE(iSequentialContainer.Save(null_sc, NULL, NULL, NULL) < 0);
    TEST_REQUIRE(iSequentialContainer.GetElementSize(null_sc) == 0);
    TEST_REQUIRE(iSequentialContainer.Add(null_sc, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.GetElement(null_sc, 0) == NULL);
    TEST_REQUIRE(iSequentialContainer.Push(null_sc, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.Pop(null_sc, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.InsertAt(null_sc, 0, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.EraseAt(null_sc, 0) < 0);
    TEST_REQUIRE(iSequentialContainer.ReplaceAt(null_sc, 0, &value) < 0);
    TEST_REQUIRE(iSequentialContainer.IndexOf(null_sc, &value, NULL, &index) < 0);
    TEST_REQUIRE(iSequentialContainer.Append(null_sc, null_sc) < 0);
    return 0;

cleanup:
    return -1;
}

static const TestCase tests[] = {
    {"operations", test_operations},
    {"append compatibility", test_append_and_compatibility},
    {"dlist/vector iterators", test_dlist_and_vector_iterators},
    {"variable strings", test_unsupported_variable_strings},
    {"null arguments", test_null_arguments},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {"sequential", tests, sizeof(tests) / sizeof(tests[0])};
    return &suite;
}
