#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int error_calls;
static int destructor_calls;
static void *last_extra;
static const void *last_left;
static const void *last_right;

static void *quiet_error(const char *operation, int code, ...)
{
    (void)operation;
    (void)code;
    ++error_calls;
    return NULL;
}

static int int_compare(const void *left, const void *right, CompareInfo *info)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    if (info != NULL) {
        last_extra = info->ExtraArgs;
        last_left = info->ContainerLeft;
        last_right = info->ContainerRight;
    }
    return (a > b) - (a < b);
}

static int counting_destructor(void *value)
{
    (void)value;
    ++destructor_calls;
    return 1;
}

static int collect_int(const void *value, void *arg)
{
    int *out = arg;
    out[out[0] + 1] = *(const int *)value;
    ++out[0];
    return 1;
}

static void cleanup_tree(TreeMap **tree)
{
    if (*tree != NULL) {
        iTreeMap.Finalize(*tree);
        *tree = NULL;
    }
}

static int test_default_small_elements_and_range(void)
{
    TreeMap *tree = NULL;
    TreeMap *other = NULL;
    unsigned char values[4][3] = {{1, 0, 0}, {2, 0, 0},
                                  {3, 0, 0}, {4, 0, 0}};
    unsigned char duplicate[3] = {2, 0, 0};
    int result = -1;

    tree = iTreeMap.Create(3);
    other = iTreeMap.Create(3);
    TEST_REQUIRE(tree != NULL && other != NULL);
    TEST_REQUIRE(iTreeMap.GetElementSize(tree) == 3);
    TEST_REQUIRE(iTreeMap.Add(tree, values[0], NULL) == 1);
    TEST_REQUIRE(iTreeMap.AddRange(tree, 3, values[1], NULL) == 1);
    TEST_REQUIRE(iTreeMap.Size(tree) == 4);
    TEST_REQUIRE(iTreeMap.Contains(tree, duplicate, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Add(tree, duplicate, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Size(tree) == 4);
    TEST_REQUIRE(iTreeMap.AddRange(other, 4, values[0], NULL) == 1);
    TEST_REQUIRE(iTreeMap.Equal(tree, other) == 1);
    ((unsigned char *)iTreeMap.GetElement(other, values[3], NULL))[2] = 9;
    TEST_REQUIRE(iTreeMap.Equal(tree, other) == 0);
    result = 0;

cleanup:
    cleanup_tree(&tree);
    cleanup_tree(&other);
    return result;
}

static int test_custom_compare_context_and_insert(void)
{
    TreeMap *tree = NULL;
    TreeMap *copy = NULL;
    int values[] = {4, 1, 9};
    int range[] = {7, 2, 8};
    int replacement = 9;
    int extra = 123;
    int collected[8] = {0};
    int result = -1;

    last_extra = NULL;
    last_left = NULL;
    last_right = NULL;
    tree = iTreeMap.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    iTreeMap.SetErrorFunction(tree, quiet_error);
    TEST_REQUIRE(iTreeMap.SetCompareFunction(tree, int_compare) != NULL);
    TEST_REQUIRE(iTreeMap.Add(tree, &values[0], &extra) == 1);
    TEST_REQUIRE(iTreeMap.Add(tree, &values[1], &extra) == 1);
    TEST_REQUIRE(iTreeMap.Add(tree, &values[2], &extra) == 1);
    TEST_REQUIRE(iTreeMap.AddRange(tree, 3, range, &extra) == 1);
    TEST_REQUIRE(iTreeMap.Size(tree) == 6);
    TEST_REQUIRE(last_extra == &extra);
    TEST_REQUIRE(last_left == tree && last_right == tree);
    TEST_REQUIRE(*(int *)iTreeMap.GetElement(tree, &replacement, &extra) == 9);
    TEST_REQUIRE(iTreeMap.Apply(tree, collect_int, collected) == 1);
    TEST_REQUIRE(collected[0] == 6 && collected[1] == 1 && collected[6] == 9);

    TEST_REQUIRE(iTreeMap.Insert(tree, &replacement, &extra) == 1);
    TEST_REQUIRE(iTreeMap.Size(tree) == 6);
    copy = iTreeMap.Copy(tree);
    TEST_REQUIRE(copy != NULL);
    TEST_REQUIRE(iTreeMap.Equal(tree, copy) == 1);
    result = 0;

cleanup:
    cleanup_tree(&tree);
    cleanup_tree(&copy);
    return result;
}

static int test_destructor_erase_replace_clear(void)
{
    TreeMap *tree = NULL;
    int one = 1;
    int two = 2;
    int three = 3;
    Iterator *it = NULL;
    int result = -1;

    destructor_calls = 0;
    tree = iTreeMap.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    iTreeMap.SetErrorFunction(tree, quiet_error);
    iTreeMap.SetDestructor(tree, counting_destructor);
    TEST_REQUIRE(iTreeMap.Add(tree, &one, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Add(tree, &two, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Erase(tree, &one, NULL) == 1);
    TEST_REQUIRE(destructor_calls == 1);
    TEST_REQUIRE(iTreeMap.Insert(tree, &three, NULL) == 0);
    TEST_REQUIRE(iTreeMap.Insert(tree, &two, NULL) == 1);
    TEST_REQUIRE(destructor_calls == 2);
    it = iTreeMap.NewIterator(tree);
    TEST_REQUIRE(it != NULL && *(int *)it->GetFirst(it) == 2);
    TEST_REQUIRE(it->Replace(it, &one, 1) == 1);
    TEST_REQUIRE(iTreeMap.Contains(tree, &one, NULL) == 1);
    TEST_REQUIRE(destructor_calls == 3);
    TEST_REQUIRE(iTreeMap.Clear(tree) == 1);
    TEST_REQUIRE(destructor_calls == 5);
    TEST_REQUIRE(iTreeMap.Add(tree, &three, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Clear(tree) == 1);
    TEST_REQUIRE(destructor_calls == 6);
    result = 0;

cleanup:
    if (it != NULL)
        iTreeMap.DeleteIterator(it);
    cleanup_tree(&tree);
    return result;
}

static int test_iterators_readonly_and_initialize(void)
{
    TreeMap *tree = NULL;
    Iterator *it = NULL;
    unsigned char buffer[sizeof(struct TreeMapIterator)];
    int values[] = {3, 1, 2};
    int replacement = 4;
    int result = -1;

    tree = iTreeMap.InitializeWith(sizeof(int), 3, values);
    TEST_REQUIRE(tree != NULL);
    it = iTreeMap.NewIterator(tree);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(*(int *)it->GetLast(it) == 3);
    TEST_REQUIRE(*(int *)it->GetPrevious(it) == 2);
    TEST_REQUIRE(*(int *)it->GetFirst(it) == 1);
    TEST_REQUIRE(it->GetPosition(it) == 0);
    TEST_REQUIRE(*(int *)it->Seek(it, 2) == 3);
    TEST_REQUIRE(it->GetPosition(it) == 2);
    TEST_REQUIRE(it->Replace(it, NULL, 0) == 1);
    TEST_REQUIRE(iTreeMap.Size(tree) == 2);
    TEST_REQUIRE(iTreeMap.InitIterator(tree, buffer) == 1);
    TEST_REQUIRE(*(int *)((Iterator *)buffer)->GetFirst((Iterator *)buffer) == 1);
    TEST_REQUIRE(iTreeMap.DeleteIterator((Iterator *)buffer) == 1);
    TEST_REQUIRE(iTreeMap.SetFlags(tree, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iTreeMap.Add(tree, &replacement, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iTreeMap.Clear(tree) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(it->GetCurrent(it) == NULL);
    result = 0;

cleanup:
    if (it != NULL)
        iTreeMap.DeleteIterator(it);
    cleanup_tree(&tree);
    return result;
}

static int test_save_load(void)
{
    TreeMap *tree = NULL;
    TreeMap *loaded = NULL;
    FILE *stream = NULL;
    int values[] = {11, 4, 19};
    int result = -1;

    tree = iTreeMap.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    TEST_REQUIRE(iTreeMap.AddRange(tree, 3, values, NULL) == 1);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iTreeMap.Save(tree, stream, NULL, NULL) == 1);
    rewind(stream);
    loaded = iTreeMap.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL);
    TEST_REQUIRE(iTreeMap.Equal(tree, loaded) == 1);
    result = 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    cleanup_tree(&tree);
    cleanup_tree(&loaded);
    return result;
}

static int failing_save(const void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int failing_load(void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int test_rebalance_errors_and_failures(void)
{
    TreeMap *tree = NULL;
    TreeMap *empty = NULL;
    TreeMap *source = NULL;
    Iterator *it = NULL;
    int values[160];
    int key;
    int i;
    FILE *stream = NULL;
    int result = -1;

    for (i = 0; i < 160; ++i)
        values[i] = i;
    tree = iTreeMap.Create(sizeof(int));
    empty = iTreeMap.Create(sizeof(int));
    source = iTreeMap.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL && empty != NULL && source != NULL);
    iTreeMap.SetErrorFunction(tree, quiet_error);
    TEST_REQUIRE(iTreeMap.SetCompareFunction(tree, int_compare) != NULL);
    TEST_REQUIRE(iTreeMap.AddRange(tree, 160, values, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Size(tree) == 160);
    for (i = 0; i < 160; i += 2) {
        key = i;
        TEST_REQUIRE(iTreeMap.Erase(tree, &key, NULL) == 1);
    }
    TEST_REQUIRE(iTreeMap.Size(tree) == 80);
    for (i = 1; i < 160; i += 2) {
        key = i;
        TEST_REQUIRE(iTreeMap.Erase(tree, &key, NULL) == 1);
    }
    TEST_REQUIRE(iTreeMap.Size(tree) == 0);
    TEST_REQUIRE(iTreeMap.Erase(tree, &key, NULL) == 0);
    TEST_REQUIRE(iTreeMap.Sizeof(tree) >= sizeof(TreeMap));
    TEST_REQUIRE(iTreeMap.GetElementSize(NULL) == (size_t)CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iTreeMap.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iTreeMap.InitIterator(NULL, values) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iTreeMap.InitIterator(empty, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iTreeMap.GetElement(tree, NULL, NULL) == NULL);
    TEST_REQUIRE(iTreeMap.SetCompareFunction(empty, int_compare) != NULL);
    TEST_REQUIRE(iTreeMap.Apply(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);

    it = iTreeMap.NewIterator(empty);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetPrevious(it) == NULL);
    TEST_REQUIRE(it->GetPosition(it) == (size_t)-1);
    iTreeMap.DeleteIterator(it);
    it = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iTreeMap.Add(source, &key, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Save(source, stream, failing_save, NULL) ==
                 CONTAINER_ERROR_FILE_WRITE);
    rewind(stream);
    TEST_REQUIRE(iTreeMap.Load(stream, failing_load, NULL) == NULL);
    fclose(stream);
    stream = NULL;
    result = 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (it != NULL)
        iTreeMap.DeleteIterator(it);
    cleanup_tree(&tree);
    cleanup_tree(&empty);
    cleanup_tree(&source);
    return result;
}

static int test_argument_and_iterator_matrix(void)
{
    TreeMap *tree = NULL;
    TreeMap *other = NULL;
    TreeMap *probe = NULL;
    Iterator *it = NULL;
    int value = 5;
    int other_value = 6;
    FILE *stream = NULL;
    unsigned char short_header[4] = {0, 0, 0, 0};
    int result = -1;

    error_calls = 0;
    probe = iTreeMap.CreateWithAllocator(sizeof(int), NULL);
    TEST_REQUIRE(probe != NULL);
    iTreeMap.Finalize(probe);
    probe = NULL;
    tree = iTreeMap.Create(sizeof(int));
    other = iTreeMap.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL && other != NULL);
    iTreeMap.SetErrorFunction(tree, quiet_error);
    iTreeMap.SetErrorFunction(other, quiet_error);
    TEST_REQUIRE(iTreeMap.Add(tree, &value, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Add(other, &other_value, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Contains(tree, &other_value, NULL) == 0);
    TEST_REQUIRE(iTreeMap.Erase(tree, &other_value, NULL) == 0);
    TEST_REQUIRE(iTreeMap.Add(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iTreeMap.AddRange(tree, 0, NULL, NULL) == 1);
    TEST_REQUIRE(iTreeMap.Insert(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iTreeMap.SetCompareFunction(tree, int_compare) != NULL);
    TEST_REQUIRE(iTreeMap.SetCompareFunction(tree, int_compare) != NULL);
    TEST_REQUIRE(iTreeMap.Equal(tree, other) == 0);
    TEST_REQUIRE(iTreeMap.SetFlags(tree, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iTreeMap.Erase(tree, &value, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iTreeMap.Insert(tree, &value, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iTreeMap.AddRange(tree, 1, &value, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iTreeMap.Equal(tree, other) == 0);
    TEST_REQUIRE(iTreeMap.Clear(tree) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iTreeMap.Sizeof(NULL) == sizeof(TreeMap));
    TEST_REQUIRE(iTreeMap.SetErrorFunction(NULL, quiet_error) == iError.RaiseError);
    TEST_REQUIRE(iTreeMap.SetDestructor(NULL, counting_destructor) == NULL);
    TEST_REQUIRE(iTreeMap.GetAllocator(NULL) == NULL);
    TEST_REQUIRE(iTreeMap.Equal(NULL, other) == 0);
    TEST_REQUIRE(iTreeMap.Save(NULL, NULL, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iTreeMap.Load(NULL, NULL, NULL) == NULL);

    iTreeMap.SetFlags(tree, 0);
    it = iTreeMap.NewIterator(tree);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) != NULL);
    TEST_REQUIRE(it->Replace(it, &other_value, 0) == 1);
    TEST_REQUIRE(it->GetCurrent(it) == NULL);
    TEST_REQUIRE(iTreeMap.Add(tree, &value, NULL) == 1);
    TEST_REQUIRE(it->GetFirst(it) == NULL);
    iTreeMap.DeleteIterator(it);
    it = NULL;

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(fwrite(short_header, sizeof(short_header), 1, stream) == 1);
    rewind(stream);
    TEST_REQUIRE(iTreeMap.Load(stream, NULL, NULL) == NULL);
    fclose(stream);
    stream = NULL;
    result = 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (it != NULL)
        iTreeMap.DeleteIterator(it);
    cleanup_tree(&tree);
    cleanup_tree(&other);
    cleanup_tree(&probe);
    return result;
}

static const TestCase scapegoat_tests[] = {
    {"default small elements and range", test_default_small_elements_and_range},
    {"custom comparator context and insert", test_custom_compare_context_and_insert},
    {"destructor erase replace clear", test_destructor_erase_replace_clear},
    {"iterators readonly and initialize", test_iterators_readonly_and_initialize},
    {"save and load", test_save_load},
    {"rebalance errors and failures", test_rebalance_errors_and_failures},
    {"argument and iterator matrix", test_argument_and_iterator_matrix},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "Scapegoat TreeMap",
        scapegoat_tests,
        sizeof(scapegoat_tests) / sizeof(scapegoat_tests[0])
    };
    return &suite;
}
