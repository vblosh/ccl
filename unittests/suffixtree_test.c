#include "containers.h"
#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *quiet_error(const char *operation, int code, ...)
{
    (void)operation;
    (void)code;
    return NULL;
}

static int naive_find(const unsigned char *text, size_t text_len,
                      const unsigned char *key, size_t key_len)
{
    size_t i;

    if (key_len == 0)
        return 1;
    if (key_len > text_len)
        return CONTAINER_ERROR_NOTFOUND;
    for (i = 0; i + key_len <= text_len; ++i) {
        if (memcmp(text + i, key, key_len) == 0)
            return (int)i + 1;
    }
    return CONTAINER_ERROR_NOTFOUND;
}

static int valid_position(const unsigned char *text, size_t text_len,
                          const unsigned char *key, size_t key_len, int got)
{
    if (key_len == 0)
        return got == 1;
    if (got < 1 || (size_t)got + key_len - 1 > text_len)
        return 0;
    return memcmp(text + got - 1, key, key_len) == 0;
}

static int check_all_substrings(SuffixTree *tree, const unsigned char *text,
                                size_t length)
{
    size_t begin;
    size_t end;
    char key[128];

    for (begin = 0; begin <= length; ++begin) {
        for (end = begin; end <= length; ++end) {
            size_t key_len = end - begin;
            int got;
            memcpy(key, text + begin, key_len);
            key[key_len] = '\0';
            got = iSuffixTree.Find(tree, key);
            if (!valid_position(text, length, (const unsigned char *)key,
                                key_len, got))
                return 0;
        }
    }
    return 1;
}

static int test_boundaries_and_copy(void)
{
    static const char *const inputs[] = {
        "", "a", "ab", "aaaa", "banana", "mississippi", "abcde"
    };
    size_t input_index;
    int result = 1;
    SuffixTree *tree = NULL;
    char source[64];
    char key[64];

    for (input_index = 0;
         input_index < sizeof(inputs) / sizeof(inputs[0]); ++input_index) {
        size_t length = strlen(inputs[input_index]);
        strcpy(source, inputs[input_index]);
        tree = iSuffixTree.Create(source);
        TEST_REQUIRE(tree != NULL);
        TEST_REQUIRE(iSuffixTree.Find(tree, "") == 1);
        TEST_REQUIRE(check_all_substrings(tree, (const unsigned char *)source,
                                          length));
        strcpy(key, source);
        if (length != 0) {
            key[length] = 'x';
            key[length + 1] = '\0';
        } else {
            strcpy(key, "x");
        }
        TEST_REQUIRE(iSuffixTree.Find(tree, key) == CONTAINER_ERROR_NOTFOUND);

        /* Creation owns a copy, so caller storage may be changed immediately. */
        if (length != 0)
            source[0] = source[0] == 'q' ? 'p' : 'q';
        TEST_REQUIRE(iSuffixTree.Find(tree, (char *)inputs[input_index]) > 0 ||
                     length == 0);
        TEST_REQUIRE(iSuffixTree.Clear(tree) == 1);
        TEST_REQUIRE(iSuffixTree.Clear(tree) == 1);
        TEST_REQUIRE(iSuffixTree.Find(tree, "x") == CONTAINER_ERROR_NOTFOUND);
        TEST_REQUIRE(iSuffixTree.Find(tree, "") == 1);
        TEST_REQUIRE(iSuffixTree.Sizeof(tree) >= sizeof(SuffixTree *));
        iSuffixTree.Finalize(tree);
        tree = NULL;
    }
    result = 0;

cleanup:
    if (tree != NULL)
        iSuffixTree.Finalize(tree);
    return result;
}

static int test_binary_and_randomized(void)
{
    unsigned char source[32];
    unsigned char key[32];
    unsigned int state = 0x13579bdfU;
    size_t trial;
    int result = 1;
    SuffixTree *tree = NULL;

    source[0] = 1;
    source[1] = 127;
    source[2] = 128;
    source[3] = 255;
    source[4] = 1;
    source[5] = 255;
    source[6] = '\0';
    tree = iSuffixTree.Create((char *)source);
    TEST_REQUIRE(tree != NULL);
    TEST_REQUIRE(check_all_substrings(tree, source, 6));
    iSuffixTree.Finalize(tree);
    tree = NULL;

    for (trial = 0; trial < 120; ++trial) {
        size_t source_len = (size_t)(state % 12U);
        size_t i;
        state = state * 1664525U + 1013904223U;
        for (i = 0; i < source_len; ++i) {
            state = state * 1664525U + 1013904223U;
            source[i] = (unsigned char)(1U + state % 255U);
        }
        source[source_len] = '\0';
        tree = iSuffixTree.Create((char *)source);
        TEST_REQUIRE(tree != NULL);
        for (i = 0; i < 20; ++i) {
            size_t key_len;
            int expected;
            int got;
            size_t j;
            state = state * 1664525U + 1013904223U;
            key_len = (size_t)(state % 15U);
            for (j = 0; j < key_len; ++j) {
                state = state * 1664525U + 1013904223U;
                key[j] = (unsigned char)(1U + state % 255U);
            }
            key[key_len] = '\0';
            expected = naive_find(source, source_len, key, key_len);
            got = iSuffixTree.Find(tree, (char *)key);
            TEST_REQUIRE((expected == CONTAINER_ERROR_NOTFOUND &&
                          got == CONTAINER_ERROR_NOTFOUND) ||
                         (expected != CONTAINER_ERROR_NOTFOUND &&
                          valid_position(source, source_len, key, key_len, got)));
        }
        iSuffixTree.Finalize(tree);
        tree = NULL;
    }
    result = 0;

cleanup:
    if (tree != NULL)
        iSuffixTree.Finalize(tree);
    return result;
}

static int apply_count;

static int count_nodes(void *node, void *arg)
{
    int *count = (int *)arg;
    TEST_REQUIRE(node != NULL);
    ++*count;
    ++apply_count;
    return 1;

cleanup:
    return 0;
}

static int stop_apply(void *node, void *arg)
{
    int *limit = (int *)arg;
    (void)node;
    --*limit;
    return *limit > 0;
}

static int test_apply_print_and_nulls(void)
{
    SuffixTree *tree = NULL;
    FILE *stream = NULL;
    ErrorFunction saved_error;
    char output[1024];
    size_t read_count;
    int count = 0;
    int limit = 2;
    int result = 1;

    saved_error = iError.SetErrorFunction(quiet_error);
    TEST_REQUIRE(iSuffixTree.Create(NULL) == NULL);
    TEST_REQUIRE(iSuffixTree.CreateWithAllocator(NULL, CurrentAllocator) == NULL);
    TEST_REQUIRE(iSuffixTree.CreateWithAllocator((char *)"a", NULL) == NULL);
    TEST_REQUIRE(iSuffixTree.Find(NULL, (char *)"a") == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iSuffixTree.Find(NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iSuffixTree.Clear(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iSuffixTree.Finalize(NULL) == 0);
    TEST_REQUIRE(iSuffixTree.Apply(NULL, count_nodes, &count) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iSuffixTree.Apply(NULL, NULL, NULL) == CONTAINER_ERROR_BADARG);
    iSuffixTree.Print(NULL, NULL);
    iSuffixTree.Print(NULL, stdout);

    tree = iSuffixTree.Create((char *)"mississippi");
    TEST_REQUIRE(tree != NULL);
    TEST_REQUIRE(iSuffixTree.Apply(tree, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iSuffixTree.Apply(tree, count_nodes, &count) == 1);
    TEST_REQUIRE(count > 1 && apply_count == count);
    TEST_REQUIRE(iSuffixTree.Apply(tree, stop_apply, &limit) == 0);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    iSuffixTree.Print(tree, stream);
    fflush(stream);
    rewind(stream);
    read_count = fread(output, 1, sizeof(output) - 1, stream);
    output[read_count] = '\0';
    TEST_REQUIRE(strstr(output, "root") != NULL);
    TEST_REQUIRE(memchr(output, '\0', read_count) == NULL);
    fclose(stream);
    stream = NULL;
    iSuffixTree.Print(tree, NULL);
    TEST_REQUIRE(iSuffixTree.Clear(tree) == 1);
    iSuffixTree.Print(tree, stdout);
    TEST_REQUIRE(iSuffixTree.Apply(tree, count_nodes, &count) == 1);
    result = 0;

cleanup:
    iError.SetErrorFunction(saved_error);
    if (stream != NULL)
        fclose(stream);
    if (tree != NULL)
        iSuffixTree.Finalize(tree);
    return result;
}

typedef struct FailureAllocatorState {
    size_t calls;
    size_t frees;
    size_t fail_at;
} FailureAllocatorState;

static FailureAllocatorState failure_state;

static void *failure_malloc(size_t size)
{
    ++failure_state.calls;
    if (failure_state.fail_at != 0 &&
        failure_state.calls >= failure_state.fail_at)
        return NULL;
    return malloc(size);
}

static void *failure_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size)
        return NULL;
    ++failure_state.calls;
    if (failure_state.fail_at != 0 &&
        failure_state.calls >= failure_state.fail_at)
        return NULL;
    return calloc(count, size);
}

static void *failure_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void failure_free(void *ptr)
{
    if (ptr != NULL)
        ++failure_state.frees;
    free(ptr);
}

static int nested_active;
static int nested_ok;

static void exercise_nested_create(void)
{
    char nested_text[] = "banana";
    SuffixTree *nested;

    if (nested_active)
        return;
    nested_active = 1;
    nested = iSuffixTree.Create(nested_text);
    nested_ok = nested != NULL && iSuffixTree.Find(nested, (char *)"ana") > 0;
    if (nested != NULL)
        iSuffixTree.Finalize(nested);
    nested_active = 0;
}

static void *nested_malloc(size_t size)
{
    void *result = malloc(size);
    exercise_nested_create();
    return result;
}

static void *nested_calloc(size_t count, size_t size)
{
    void *result = calloc(count, size);
    exercise_nested_create();
    return result;
}

static void *nested_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void nested_free(void *ptr)
{
    free(ptr);
}

static int test_allocation_failures_and_two_trees(void)
{
    static ContainerAllocator allocator = {
        failure_malloc, failure_free, failure_realloc, failure_calloc
    };
    static ContainerAllocator nested_allocator = {
        nested_malloc, nested_free, nested_realloc, nested_calloc
    };
    char source[] = "mississippi";
    size_t fail_at;
    size_t successful_calls = 0;
    ErrorFunction saved_error;
    int result = 1;
    SuffixTree *left = NULL;
    SuffixTree *right = NULL;

    saved_error = iError.SetErrorFunction(quiet_error);
    failure_state.calls = 0;
    failure_state.fail_at = 0;
    left = iSuffixTree.CreateWithAllocator(source, &allocator);
    TEST_REQUIRE(left != NULL);
    successful_calls = failure_state.calls;
    iSuffixTree.Finalize(left);
    left = NULL;
    TEST_REQUIRE(failure_state.frees == successful_calls);

    for (fail_at = 1; fail_at <= successful_calls; ++fail_at) {
        failure_state.calls = 0;
        failure_state.frees = 0;
        failure_state.fail_at = fail_at;
        TEST_REQUIRE(iSuffixTree.CreateWithAllocator(source, &allocator) == NULL);
        TEST_REQUIRE(failure_state.frees == failure_state.calls - 1);
        TEST_REQUIRE(strcmp(source, "mississippi") == 0);
    }

    failure_state.fail_at = 0;
    failure_state.calls = failure_state.frees = 0;
    left = iSuffixTree.CreateWithAllocator((char *)"banana", &allocator);
    right = iSuffixTree.CreateWithAllocator((char *)"abracadabra", &allocator);
    TEST_REQUIRE(left != NULL && right != NULL);
    TEST_REQUIRE(iSuffixTree.Find(left, (char *)"ana") > 0);
    TEST_REQUIRE(iSuffixTree.Find(right, (char *)"cad") > 0);
    iSuffixTree.Finalize(left);
    left = NULL;
    TEST_REQUIRE(iSuffixTree.Find(right, (char *)"abra") > 0);
    nested_active = 0;
    nested_ok = 0;
    {
        SuffixTree *nested_outer =
            iSuffixTree.CreateWithAllocator((char *)"mississippi",
                                             &nested_allocator);
        TEST_REQUIRE(nested_outer != NULL && nested_ok);
        TEST_REQUIRE(iSuffixTree.Find(nested_outer, (char *)"issi") > 0);
        iSuffixTree.Finalize(nested_outer);
    }
    result = 0;

cleanup:
    iError.SetErrorFunction(saved_error);
    if (left != NULL)
        iSuffixTree.Finalize(left);
    if (right != NULL)
        iSuffixTree.Finalize(right);
    return result;
}

static const TestCase suffixtree_tests[] = {
    {"boundaries and copied source", test_boundaries_and_copy},
    {"binary and randomized differential", test_binary_and_randomized},
    {"apply, print, and nulls", test_apply_print_and_nulls},
    {"allocation failures and independent trees",
     test_allocation_failures_and_two_trees}
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "Suffix tree",
        suffixtree_tests,
        sizeof(suffixtree_tests) / sizeof(suffixtree_tests[0])
    };
    return &suite;
}
