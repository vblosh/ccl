#include "containers.h"
#include "test_support.h"

#include <stdlib.h>
#include <string.h>

static int error_calls;
static int destructor_calls;
static int destructor_sum;
static void *extra_seen;

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

static void *failure_realloc(void *value, size_t size)
{
    if (value == NULL)
        return failure_malloc(size);
    return realloc(value, size);
}

static void failure_free(void *value)
{
    free(value);
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

static int descending_compare(const void *left, const void *right,
                              CompareInfo *info)
{
    int result;
    int a = *(const int *)left;
    int b = *(const int *)right;
    result = (a > b) - (a < b);
    if (info != NULL)
        extra_seen = info->ExtraArgs;
    return -result;
}

static int destructor(void *value)
{
    ++destructor_calls;
    if (value != NULL)
        destructor_sum += *(int *)value;
    return 1;
}

static int collect_values(const void *value, void *arg)
{
    int *state = arg;
    state[state[0] + 1] = *(const int *)value;
    ++state[0];
    return 1;
}

static int stop_after_two(const void *value, void *arg)
{
    int *calls = arg;
    (void)value;
    ++*calls;
    return *calls < 2;
}

static void cleanup_tree(BinarySearchTree **tree)
{
    if (*tree != NULL) {
        iBinarySearchTree.Finalize(*tree);
        *tree = NULL;
    }
}

static int test_default_add_rotations_and_apply(void)
{
    BinarySearchTree *tree = NULL;
    BinarySearchTree *zigzag = NULL;
    int values[] = {30, 20, 10, 40, 50, 25, 5, 15, 35, 45};
    int collected[12] = {0};
    int duplicate = 30;
    int i;
    int stopped = 0;
    int result = -1;

    tree = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    iBinarySearchTree.SetErrorFunction(tree, quiet_error);
    for (i = 0; i < (int)(sizeof(values) / sizeof(values[0])); ++i)
        TEST_REQUIRE(iBinarySearchTree.Add(tree, &values[i]) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &duplicate) == 1);
    TEST_REQUIRE(iBinarySearchTree.Size(tree) == 10);
    TEST_REQUIRE(iBinarySearchTree.Contains(tree, &values[0]) == 1);
    duplicate = 999;
    TEST_REQUIRE(iBinarySearchTree.Contains(tree, &duplicate) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iBinarySearchTree.Apply(tree, collect_values, collected) == 1);
    TEST_REQUIRE(collected[0] == 10);
    for (i = 2; i <= 10; ++i)
        TEST_REQUIRE(collected[i] > collected[i - 1]);
    TEST_REQUIRE(iBinarySearchTree.Apply(tree, stop_after_two, &stopped) == 0);
    TEST_REQUIRE(stopped == 2);
    TEST_REQUIRE(iBinarySearchTree.Apply(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBinarySearchTree.Apply(NULL, collect_values, collected) < 0);
    zigzag = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(zigzag != NULL);
    TEST_REQUIRE(iBinarySearchTree.Add(zigzag, &values[0]) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(zigzag, &values[2]) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(zigzag, &values[1]) == 0);
    result = 0;

cleanup:
    cleanup_tree(&tree);
    cleanup_tree(&zigzag);
    return result;
}

static int test_root_erase_and_rebalancing(void)
{
    BinarySearchTree *tree = NULL;
    int values[] = {50, 20, 80, 10, 30, 60, 90, 25, 35, 55, 70, 85, 95};
    int missing = 1234;
    int i;
    int result = -1;

    tree = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    for (i = 0; i < (int)(sizeof(values) / sizeof(values[0])); ++i)
        TEST_REQUIRE(iBinarySearchTree.Add(tree, &values[i]) == 0);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &missing, NULL) == 0);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &values[0], NULL) == 1);
    TEST_REQUIRE(iBinarySearchTree.Contains(tree, &values[0]) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iBinarySearchTree.Size(tree) == 12);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &values[1], NULL) == 1);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &values[2], NULL) == 1);
    for (i = 3; i < (int)(sizeof(values) / sizeof(values[0])); ++i)
        TEST_REQUIRE(iBinarySearchTree.Erase(tree, &values[i], NULL) == 1);
    TEST_REQUIRE(iBinarySearchTree.Size(tree) == 0);
    TEST_REQUIRE(iBinarySearchTree.Contains(tree, &missing) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &missing, NULL) == 0);
    result = 0;

cleanup:
    cleanup_tree(&tree);
    return result;
}

static int test_randomized_differential(void)
{
    BinarySearchTree *tree = NULL;
    int present[64] = {0};
    int collected[80] = {0};
    int order[64];
    int i;
    int result = -1;

    tree = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    for (i = 0; i < 64; ++i) {
        int value = (i * 37 + 11) % 64;
        int j;
        order[i] = value;
        TEST_REQUIRE(iBinarySearchTree.Add(tree, &value) == 0);
        present[value] = 1;
        memset(collected, 0, sizeof(collected));
        TEST_REQUIRE(iBinarySearchTree.Apply(tree, collect_values, collected) == 1);
        TEST_REQUIRE(collected[0] == i + 1);
        for (j = 2; j <= i + 1; ++j)
            TEST_REQUIRE(collected[j] > collected[j - 1]);
    }
    for (i = 63; i >= 0; --i) {
        int j;
        TEST_REQUIRE(iBinarySearchTree.Erase(tree, &order[i], NULL) == 1);
        present[order[i]] = 0;
        memset(collected, 0, sizeof(collected));
        TEST_REQUIRE(iBinarySearchTree.Apply(tree, collect_values, collected) == 1 ||
                     iBinarySearchTree.Size(tree) == 0);
        for (j = 2; j < (int)i; ++j)
            TEST_REQUIRE(collected[j] > collected[j - 1]);
    }
    TEST_REQUIRE(iBinarySearchTree.Size(tree) == 0);
    (void)present;
    result = 0;

cleanup:
    cleanup_tree(&tree);
    return result;
}

static int test_clear_reuse_metadata_and_destructor(void)
{
    BinarySearchTree *tree = NULL;
    CompareFunction old_compare;
    int one = 1, two = 2, three = 3;
    unsigned old_flags;
    int result = -1;

    destructor_calls = 0;
    destructor_sum = 0;
    tree = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    iBinarySearchTree.SetErrorFunction(tree, quiet_error);
    iBinarySearchTree.SetDestructor(tree, destructor);
    old_compare = iBinarySearchTree.SetCompareFunction(tree, descending_compare);
    TEST_REQUIRE(old_compare == iBinarySearchTree.Compare);
    old_flags = iBinarySearchTree.SetFlags(tree, 2);
    TEST_REQUIRE(old_flags == 0 && iBinarySearchTree.GetFlags(tree) == 2);
    TEST_REQUIRE(iBinarySearchTree.Insert(tree, &one, NULL) == 0);
    TEST_REQUIRE(iBinarySearchTree.Insert(tree, &two, (void *)0x1234) == 0);
    TEST_REQUIRE(extra_seen == (void *)0x1234);
    TEST_REQUIRE(iBinarySearchTree.Clear(tree) == 1);
    TEST_REQUIRE(destructor_calls == 2 && destructor_sum == 3);
    TEST_REQUIRE(iBinarySearchTree.Size(tree) == 0);
    TEST_REQUIRE(iBinarySearchTree.GetFlags(tree) == 2);
    TEST_REQUIRE(iBinarySearchTree.SetCompareFunction(tree, NULL) == descending_compare);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &three) == 0);
    TEST_REQUIRE(iBinarySearchTree.Clear(tree) == 1);
    TEST_REQUIRE(iBinarySearchTree.Clear(tree) == 1);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &two) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &one) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &three) == 0);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &two, NULL) == 1);
    TEST_REQUIRE(destructor_calls == 4);
    TEST_REQUIRE(iBinarySearchTree.Clear(tree) == 1);
    TEST_REQUIRE(iBinarySearchTree.SetDestructor(tree, NULL) == destructor);
    TEST_REQUIRE(destructor_calls == 6);
    result = 0;

cleanup:
    cleanup_tree(&tree);
    return result;
}

static int test_per_tree_comparators_and_insert_extra(void)
{
    BinarySearchTree *ascending = NULL;
    BinarySearchTree *descending = NULL;
    BinarySearchTree *same_left = NULL;
    BinarySearchTree *same_right = NULL;
    int one = 1, two = 2, three = 3;
    int four = 4;
    int result = -1;

    extra_seen = NULL;
    ascending = iBinarySearchTree.Create(sizeof(int));
    descending = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(ascending != NULL && descending != NULL);
    TEST_REQUIRE(iBinarySearchTree.SetCompareFunction(descending,
                                                       descending_compare) ==
                 iBinarySearchTree.Compare);
    TEST_REQUIRE(iBinarySearchTree.Add(ascending, &two) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(ascending, &one) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(descending, &two) == 0);
    TEST_REQUIRE(iBinarySearchTree.Insert(descending, &three, (void *)0xbeef) == 0);
    TEST_REQUIRE(extra_seen == (void *)0xbeef);
    TEST_REQUIRE(iBinarySearchTree.Contains(ascending, &one) == 1);
    TEST_REQUIRE(iBinarySearchTree.Contains(descending, &three) == 1);
    TEST_REQUIRE(iBinarySearchTree.SetCompareFunction(ascending, NULL) ==
                 iBinarySearchTree.Compare);
    TEST_REQUIRE(iBinarySearchTree.Equal(ascending, descending) == 0);
    same_left = iBinarySearchTree.Create(sizeof(int));
    same_right = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(same_left != NULL && same_right != NULL);
    TEST_REQUIRE(iBinarySearchTree.Add(same_left, &two) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(same_left, &one) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(same_left, &three) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(same_right, &two) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(same_right, &one) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(same_right, &three) == 0);
    TEST_REQUIRE(iBinarySearchTree.Equal(same_left, same_right) == 1);
    TEST_REQUIRE(iBinarySearchTree.Add(same_right, &four) == 0);
    TEST_REQUIRE(iBinarySearchTree.Equal(same_left, same_right) == 0);
    result = 0;

cleanup:
    cleanup_tree(&ascending);
    cleanup_tree(&descending);
    cleanup_tree(&same_left);
    cleanup_tree(&same_right);
    return result;
}

static int test_equal_merge_and_allocator_ownership(void)
{
    BinarySearchTree *left = NULL;
    BinarySearchTree *right = NULL;
    BinarySearchTree *merge = NULL;
    BinarySearchTree *empty_left = NULL;
    BinarySearchTree *empty_right = NULL;
    BinarySearchTree *incompatible = NULL;
    int one = 1, two = 2, three = 3;
    long four = 4;
    int result = -1;

    left = iBinarySearchTree.Create(sizeof(int));
    right = iBinarySearchTree.Create(sizeof(int));
    empty_left = iBinarySearchTree.Create(sizeof(int));
    empty_right = iBinarySearchTree.Create(sizeof(int));
    incompatible = iBinarySearchTree.Create(sizeof(long));
    TEST_REQUIRE(left != NULL && right != NULL && empty_left != NULL &&
                 empty_right != NULL && incompatible != NULL);
    TEST_REQUIRE(iBinarySearchTree.Equal(empty_left, empty_right) == 1);
    TEST_REQUIRE(iBinarySearchTree.Equal(empty_left, left) == 1);
    TEST_REQUIRE(iBinarySearchTree.Add(left, &one) == 0);
    TEST_REQUIRE(iBinarySearchTree.Equal(empty_left, left) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(right, &three) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(incompatible, &four) == 0);
    TEST_REQUIRE(iBinarySearchTree.Merge(left, incompatible, &two) == NULL);
    merge = iBinarySearchTree.Merge(left, right, &two);
    TEST_REQUIRE(merge != NULL);
    TEST_REQUIRE(iBinarySearchTree.Size(left) == 0 && iBinarySearchTree.Size(right) == 0);
    TEST_REQUIRE(iBinarySearchTree.Size(merge) == 3);
    TEST_REQUIRE(iBinarySearchTree.Contains(merge, &one) == 1);
    TEST_REQUIRE(iBinarySearchTree.Contains(merge, &two) == 1);
    TEST_REQUIRE(iBinarySearchTree.Contains(merge, &three) == 1);
    {
        BinarySearchTree *temporary = iBinarySearchTree.Merge(empty_left, empty_right, &two);
        TEST_REQUIRE(temporary != NULL);
        iBinarySearchTree.Finalize(temporary);
    }
    result = 0;

cleanup:
    cleanup_tree(&merge);
    cleanup_tree(&left);
    cleanup_tree(&right);
    cleanup_tree(&empty_left);
    cleanup_tree(&empty_right);
    cleanup_tree(&incompatible);
    return result;
}

static int test_iterator_readonly_and_errors(void)
{
    BinarySearchTree *tree = NULL;
    Iterator *iterator = NULL;
    int one = 1, two = 2, three = 3, four = 4;
    int result = -1;

    tree = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    iBinarySearchTree.SetErrorFunction(tree, quiet_error);
    TEST_REQUIRE(iBinarySearchTree.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iBinarySearchTree.DeleteIterator(NULL) == CONTAINER_ERROR_BADARG);
    iterator = iBinarySearchTree.NewIterator(tree);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) == NULL);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &two) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &one) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &three) == 0);
    TEST_REQUIRE(iterator->GetFirst(iterator) == NULL);
    iBinarySearchTree.DeleteIterator(iterator);
    iterator = iBinarySearchTree.NewIterator(tree);
    TEST_REQUIRE(*(int *)iterator->GetFirst(iterator) == 1);
    TEST_REQUIRE(*(int *)iterator->GetNext(iterator) == 2);
    TEST_REQUIRE(*(int *)iterator->GetLast(iterator) == 3);
    TEST_REQUIRE(*(int *)iterator->GetPrevious(iterator) == 2);
    TEST_REQUIRE(*(int *)iterator->Seek(iterator, 0) == 1);
    TEST_REQUIRE(iterator->Seek(iterator, 99) == NULL);
    TEST_REQUIRE(iterator->Replace(iterator, &four, 1) == CONTAINER_ERROR_NOTIMPLEMENTED);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &four) == 0);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    iBinarySearchTree.DeleteIterator(iterator);
    iterator = NULL;
    iBinarySearchTree.SetFlags(tree, CONTAINER_READONLY);
    iterator = iBinarySearchTree.NewIterator(tree);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(*(int *)iterator->GetFirst(iterator) == 1);
    TEST_REQUIRE(*(int *)iterator->GetCurrent(iterator) == 1);
    TEST_REQUIRE(iterator->GetPosition(iterator) == 0);
    iBinarySearchTree.DeleteIterator(iterator);
    iterator = NULL;
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &four) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBinarySearchTree.Insert(tree, &four, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &one, NULL) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBinarySearchTree.Clear(tree) == CONTAINER_ERROR_READONLY);
    result = 0;

cleanup:
    if (iterator != NULL)
        iBinarySearchTree.DeleteIterator(iterator);
    cleanup_tree(&tree);
    return result;
}

static int test_nulls_and_finalize_destructor(void)
{
    BinarySearchTree *tree = NULL;
    BinarySearchTree *zero = NULL;
    int value = 7;
    int result = -1;

    error_calls = 0;
    destructor_calls = 0;
    tree = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(tree != NULL);
    iBinarySearchTree.SetErrorFunction(tree, quiet_error);
    iBinarySearchTree.SetDestructor(tree, destructor);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBinarySearchTree.Insert(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBinarySearchTree.Contains(tree, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBinarySearchTree.Sizeof(NULL) >= sizeof(BinarySearchTree *));
    TEST_REQUIRE(iBinarySearchTree.SetCompareFunction(NULL, NULL) == NULL);
    TEST_REQUIRE(iBinarySearchTree.SetDestructor(NULL, destructor) == NULL);
    zero = iBinarySearchTree.Create(0);
    TEST_REQUIRE(zero != NULL);
    TEST_REQUIRE(iBinarySearchTree.Add(zero, NULL) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(zero, NULL) == 1);
    TEST_REQUIRE(iBinarySearchTree.Contains(zero, NULL) == 1);
    TEST_REQUIRE(iBinarySearchTree.Add(tree, &value) == 0);
    TEST_REQUIRE(iBinarySearchTree.Erase(tree, &value, NULL) == 1);
    TEST_REQUIRE(destructor_calls == 1);
    TEST_REQUIRE(error_calls >= 4);
    result = 0;

cleanup:
    cleanup_tree(&tree);
    cleanup_tree(&zero);
    return result;
}

static int test_allocation_failures(void)
{
    BinarySearchTree *left = NULL;
    BinarySearchTree *right = NULL;
    BinarySearchTree *merge = NULL;
    ContainerAllocator *saved_allocator;
    int one = 1;
    int three = 3;
    int result = -1;

    failure_state.calls = 0;
    failure_state.fail_at = 1;
    saved_allocator = iAllocator.Change(&failure_allocator);
    TEST_REQUIRE(iBinarySearchTree.Create(sizeof(int)) == NULL);
    failure_state.fail_at = 0;
    left = iBinarySearchTree.Create(sizeof(int));
    right = iBinarySearchTree.Create(sizeof(int));
    TEST_REQUIRE(left != NULL && right != NULL);
    TEST_REQUIRE(iBinarySearchTree.Add(left, &one) == 0);
    TEST_REQUIRE(iBinarySearchTree.Add(right, &three) == 0);
    failure_state.fail_at = failure_state.calls + 1;
    TEST_REQUIRE(iBinarySearchTree.Merge(left, right, &one) == NULL);
    failure_state.fail_at = failure_state.calls + 2;
    TEST_REQUIRE(iBinarySearchTree.Merge(left, right, &one) == NULL);
    iAllocator.Change(saved_allocator);
    result = 0;

cleanup:
    iAllocator.Change(saved_allocator);
    if (merge != NULL)
        cleanup_tree(&merge);
    cleanup_tree(&left);
    cleanup_tree(&right);
    return result;
}

static const TestCase searchtree_tests[] = {
    {"default add, rotations, and apply", test_default_add_rotations_and_apply},
    {"root erase and rebalancing", test_root_erase_and_rebalancing},
    {"randomized differential", test_randomized_differential},
    {"clear reuse metadata and destructor", test_clear_reuse_metadata_and_destructor},
    {"per-tree comparators and insert extra", test_per_tree_comparators_and_insert_extra},
    {"equal merge and allocator ownership", test_equal_merge_and_allocator_ownership},
    {"iterator readonly and errors", test_iterator_readonly_and_errors},
    {"nulls and finalize destructor", test_nulls_and_finalize_destructor},
    {"allocation failures", test_allocation_failures},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "Binary search tree",
        searchtree_tests,
        sizeof(searchtree_tests) / sizeof(searchtree_tests[0])
    };
    return &suite;
}
