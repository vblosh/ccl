#include "containers.h"
#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int destructor_calls;
static int error_calls;
static void *last_extra;

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

static void *failure_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size)
        return NULL;
    return failure_malloc(count * size);
}

static void *failure_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return failure_malloc(size);
    return realloc(ptr, size);
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

static void *quiet_error(const char *operation, int code, ...)
{
    (void)operation;
    (void)code;
    ++error_calls;
    return NULL;
}

static int counting_destructor(void *data)
{
    if (data != NULL)
        ++destructor_calls;
    return 1;
}

static int collect(const void *data, void *arg)
{
    int *state = arg;
    state[state[0] + 1] = *(const int *)data;
    ++state[0];
    return 1;
}

static int stop_after_two(const void *data, void *arg)
{
    int *calls = arg;
    (void)data;
    ++*calls;
    return *calls < 2;
}

static int descending_compare(const void *left, const void *right,
                              CompareInfo *info)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    if (info != NULL)
        last_extra = info->ExtraArgs;
    return (b > a) - (b < a);
}

static int test_lifecycle_and_order(void)
{
    RedBlackTree *tree = NULL;
    Iterator *iterator = NULL;
    int values[] = {30, 20, 10, 40, 50, 25, 5, 15, 35, 45};
    int collected[16] = {0};
    int key;
    int calls = 0;
    int i;
    int result = -1;

    tree = iRedBlackTree.Create(sizeof(int), sizeof(int));
    TEST_REQUIRE(tree != NULL);
    TEST_REQUIRE(iRedBlackTree.GetElementSize(tree) == sizeof(int));
    TEST_REQUIRE(iRedBlackTree.Size(tree) == 0);
    TEST_REQUIRE(iRedBlackTree.GetFlags(tree) == 0);
    TEST_REQUIRE(iRedBlackTree.SetFlags(tree, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iRedBlackTree.SetFlags(tree, 0) == CONTAINER_READONLY);
    iRedBlackTree.SetErrorFunction(tree, quiet_error);
    TEST_REQUIRE(iRedBlackTree.SetDestructor(tree, counting_destructor) == NULL);

    for (i = 0; i < (int)(sizeof(values) / sizeof(values[0])); ++i)
        TEST_REQUIRE(iRedBlackTree.Add(tree, &values[i], &values[i]) == 1);
    key = values[0];
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == -1);
    TEST_REQUIRE(iRedBlackTree.Size(tree) == sizeof(values) / sizeof(values[0]));
    key = 25;
    TEST_REQUIRE(*(int *)iRedBlackTree.Find(tree, &key, NULL) == 25);
    key = 100;
    TEST_REQUIRE(iRedBlackTree.Find(tree, &key, NULL) == NULL);
    TEST_REQUIRE(iRedBlackTree.Apply(tree, collect, collected) == 1);
    TEST_REQUIRE(collected[0] == 10);
    TEST_REQUIRE(collected[1] == 5 && collected[2] == 10 && collected[10] == 50);
    TEST_REQUIRE(iRedBlackTree.Apply(tree, stop_after_two, &calls) == 0);
    TEST_REQUIRE(calls == 2);

    iterator = iRedBlackTree.NewIterator(tree);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(*(int *)iterator->GetFirst(iterator) == 5);
    TEST_REQUIRE(iterator->GetPosition(iterator) == 0);
    TEST_REQUIRE(*(int *)iterator->GetNext(iterator) == 10);
    TEST_REQUIRE(*(int *)iterator->GetLast(iterator) == 50);
    TEST_REQUIRE(*(int *)iterator->GetPrevious(iterator) == 45);
    TEST_REQUIRE(*(int *)iterator->Seek(iterator, 4) == 25);
    TEST_REQUIRE(iterator->Replace(iterator, &key, 0) == CONTAINER_ERROR_NOTIMPLEMENTED);
    TEST_REQUIRE(iterator->GetNext(iterator) != NULL);
    TEST_REQUIRE(iterator->GetPrevious(iterator) != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(iterator->GetPrevious(iterator) == NULL);
    TEST_REQUIRE(iterator->GetLast(iterator) != NULL);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    TEST_REQUIRE(iRedBlackTree.DeleteIterator(iterator) == 1);
    iterator = NULL;

    key = 10;
    TEST_REQUIRE(iRedBlackTree.Erase(tree, &key, NULL) == 1);
    TEST_REQUIRE(iRedBlackTree.Erase(tree, &key, NULL) == 0);
    TEST_REQUIRE(iRedBlackTree.Size(tree) == 9);
    TEST_REQUIRE(destructor_calls == 1);
    TEST_REQUIRE(iRedBlackTree.Clear(tree) == 1);
    TEST_REQUIRE(destructor_calls == 10);
    TEST_REQUIRE(iRedBlackTree.Size(tree) == 0);
    key = 7;
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == 1);
    TEST_REQUIRE(iRedBlackTree.Clear(tree) == 1);
    TEST_REQUIRE(destructor_calls == 11);
    result = 0;

cleanup:
    if (iterator != NULL)
        iRedBlackTree.DeleteIterator(iterator);
    if (tree != NULL)
        iRedBlackTree.Finalize(tree);
    return result;
}

static int test_delete_stress_and_key_sizes(void)
{
    RedBlackTree *tree = NULL;
    RedBlackTree *small = NULL;
    int values[320];
    unsigned char keys[320][3];
    unsigned char absent[3] = {0, 0, 0};
    int value;
    int i;
    int result = -1;

    tree = iRedBlackTree.Create(sizeof(int), sizeof(int));
    TEST_REQUIRE(tree != NULL);
    for (i = 0; i < 320; ++i) {
        values[i] = (i * 97) % 320;
        TEST_REQUIRE(iRedBlackTree.Add(tree, &values[i], &values[i]) == 1);
    }
    TEST_REQUIRE(iRedBlackTree.Size(tree) == 320);
    for (i = 0; i < 320; i += 2)
        TEST_REQUIRE(iRedBlackTree.Erase(tree, &values[i], NULL) == 1);
    for (i = 1; i < 320; i += 2)
        TEST_REQUIRE(iRedBlackTree.Erase(tree, &values[i], NULL) == 1);
    TEST_REQUIRE(iRedBlackTree.Size(tree) == 0);
    TEST_REQUIRE(iRedBlackTree.Erase(tree, &values[0], NULL) == 0);

    small = iRedBlackTree.Create(sizeof(int), 3);
    TEST_REQUIRE(small != NULL);
    for (i = 0; i < 320; ++i) {
        keys[i][0] = (unsigned char)(i >> 8);
        keys[i][1] = (unsigned char)i;
        keys[i][2] = (unsigned char)(255 - i);
        value = i * 3;
        TEST_REQUIRE(iRedBlackTree.Add(small, keys[i], &value) == 1);
    }
    value = 0;
    TEST_REQUIRE(iRedBlackTree.Find(small, keys[123], NULL) != NULL);
    TEST_REQUIRE(*(int *)iRedBlackTree.Find(small, keys[123], NULL) == 369);
    TEST_REQUIRE(iRedBlackTree.Find(small, absent, NULL) == NULL);
    result = 0;

cleanup:
    if (small != NULL)
        iRedBlackTree.Finalize(small);
    if (tree != NULL)
        iRedBlackTree.Finalize(tree);
    return result;
}

static int test_custom_compare_readonly_and_errors(void)
{
    RedBlackTree *tree = NULL;
    Iterator *iterator = NULL;
    int values[] = {1, 2, 3};
    int key = 2;
    int extra = 42;
    int result = -1;

    tree = iRedBlackTree.Create(sizeof(int), sizeof(int));
    TEST_REQUIRE(tree != NULL);
    iRedBlackTree.SetErrorFunction(tree, quiet_error);
    TEST_REQUIRE(iRedBlackTree.SetCompareFunction(tree, descending_compare) != NULL);
    TEST_REQUIRE(iRedBlackTree.Add(tree, &values[1], &values[1]) == 1);
    TEST_REQUIRE(iRedBlackTree.Insert(tree, &values[0], &values[0], &extra) == 1);
    TEST_REQUIRE(iRedBlackTree.Insert(tree, &values[2], &values[2], &extra) == 1);
    TEST_REQUIRE(*(int *)iRedBlackTree.Find(tree, &key, &extra) == 2);
    TEST_REQUIRE(last_extra == &extra);
    TEST_REQUIRE(iRedBlackTree.SetCompareFunction(tree, NULL) == descending_compare);

    TEST_REQUIRE(iRedBlackTree.SetFlags(tree, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iRedBlackTree.Insert(tree, &key, &key, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iRedBlackTree.Erase(tree, &key, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iRedBlackTree.Clear(tree) == CONTAINER_ERROR_READONLY);
    iterator = iRedBlackTree.NewIterator(tree);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(*(int *)iterator->GetFirst(iterator) == 3);
    TEST_REQUIRE(iterator->GetCurrent(iterator) != NULL);
    TEST_REQUIRE(iRedBlackTree.DeleteIterator(iterator) == 1);
    iterator = NULL;
    TEST_REQUIRE(iRedBlackTree.SetDestructor(tree, NULL) == NULL);

    TEST_REQUIRE(iRedBlackTree.Add(NULL, &key, &key) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRedBlackTree.Find(NULL, &key, NULL) == NULL);
    TEST_REQUIRE(iRedBlackTree.Erase(NULL, &key, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRedBlackTree.Apply(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRedBlackTree.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iRedBlackTree.DeleteIterator(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iRedBlackTree.Sizeof(NULL) >= sizeof(RedBlackTree *));
    TEST_REQUIRE(iRedBlackTree.GetElementSize(NULL) == 0);
    TEST_REQUIRE(iRedBlackTree.SetDestructor(NULL, counting_destructor) == NULL);
    TEST_REQUIRE(iRedBlackTree.Create(0, sizeof(int)) == NULL);
    TEST_REQUIRE(iRedBlackTree.Create(sizeof(int), 0) == NULL);
    TEST_REQUIRE(error_calls >= 5);
    result = 0;

cleanup:
    if (iterator != NULL)
        iRedBlackTree.DeleteIterator(iterator);
    if (tree != NULL)
        iRedBlackTree.Finalize(tree);
    return result;
}

static int test_iterator_invalidation(void)
{
    RedBlackTree *tree = NULL;
    Iterator *iterator = NULL;
    int key = 11;
    int result = -1;

    tree = iRedBlackTree.Create(sizeof(int), sizeof(int));
    TEST_REQUIRE(tree != NULL);
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == 1);
    iterator = iRedBlackTree.NewIterator(tree);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    ++key;
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == 1);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(iterator->GetPosition(iterator) == (size_t)-1);
    result = 0;

cleanup:
    if (iterator != NULL)
        iRedBlackTree.DeleteIterator(iterator);
    if (tree != NULL)
        iRedBlackTree.Finalize(tree);
    return result;
}

static int test_allocation_failures(void)
{
    ContainerAllocator *saved_allocator;
    RedBlackTree *tree = NULL;
    int key = 4;
    int result = -1;

    failure_state.calls = 0;
    failure_state.fail_at = 1;
    saved_allocator = iAllocator.Change(&failure_allocator);
    TEST_REQUIRE(iRedBlackTree.Create(sizeof(int), sizeof(int)) == NULL);
    failure_state.fail_at = 0;
    tree = iRedBlackTree.Create(sizeof(int), sizeof(int));
    TEST_REQUIRE(tree != NULL);
    failure_state.fail_at = failure_state.calls + 1;
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(iRedBlackTree.Size(tree) == 0);
    failure_state.fail_at = failure_state.calls + 2;
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(iRedBlackTree.Size(tree) == 0);
    failure_state.fail_at = 0;
    TEST_REQUIRE(iRedBlackTree.Add(tree, &key, &key) == 1);
    result = 0;

cleanup:
    failure_state.fail_at = 0;
    if (tree != NULL)
        iRedBlackTree.Finalize(tree);
    iAllocator.Change(saved_allocator);
    return result;
}

static const TestCase redblacktree_tests[] = {
    {"lifecycle and ordered operations", test_lifecycle_and_order},
    {"deletion stress and variable key sizes", test_delete_stress_and_key_sizes},
    {"custom comparison, readonly, and errors", test_custom_compare_readonly_and_errors},
    {"iterator invalidation", test_iterator_invalidation},
    {"allocation failures", test_allocation_failures}
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "Red-black tree",
        redblacktree_tests,
        sizeof(redblacktree_tests) / sizeof(redblacktree_tests[0])
    };
    return &suite;
}
