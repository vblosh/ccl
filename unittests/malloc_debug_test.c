#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "test_support.h"

typedef struct {
    int count;
    int code;
    const char *operation;
} ErrorCapture;

static ErrorCapture captured;

static void *capture_error(const char *operation, int code, ...)
{
    captured.count++;
    captured.code = code;
    captured.operation = operation;
    return NULL;
}

static void clear_capture(void)
{
    captured.count = 0;
    captured.code = 0;
    captured.operation = NULL;
}

static int test_basic_sizes_are_aligned_zeroed_and_writable(void)
{
    static const size_t sizes[] = {0, 1, sizeof(size_t) - 1,
                                   sizeof(size_t), sizeof(size_t) + 1};
    size_t i;
    unsigned char *memory = NULL;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t size = sizes[i];
        size_t j;

        memory = (unsigned char *)iDebugMalloc.malloc(size);
        TEST_REQUIRE(memory != NULL);
        TEST_REQUIRE(((uintptr_t)memory % sizeof(size_t)) == 0);
        for (j = 0; j < ((size + sizeof(size_t) - 1) &
                         ~(sizeof(size_t) - 1)); ++j)
            TEST_REQUIRE(memory[j] == 0);
        for (j = 0; j < size; ++j)
            memory[j] = (unsigned char)(j + 1);
        iDebugMalloc.free(memory);
        memory = NULL;
    }
    return 0;

cleanup:
    if (memory != NULL)
        iDebugMalloc.free(memory);
    return -1;
}

static int test_realloc_preserves_data_and_repairs_shrink_metadata(void)
{
    unsigned char *memory = NULL;
    unsigned char *resized = NULL;
    size_t i;

    memory = (unsigned char *)iDebugMalloc.malloc(64);
    TEST_REQUIRE(memory != NULL);
    for (i = 0; i < 64; ++i)
        memory[i] = (unsigned char)(0xa0 + i);

    resized = (unsigned char *)iDebugMalloc.realloc(memory, 64);
    TEST_REQUIRE(resized == memory);
    resized = (unsigned char *)iDebugMalloc.realloc(resized, 128);
    TEST_REQUIRE(resized != NULL);
    memory = resized;
    for (i = 0; i < 64; ++i)
        TEST_REQUIRE(resized[i] == (unsigned char)(0xa0 + i));
    for (i = 64; i < 128; ++i)
        TEST_REQUIRE(resized[i] == 0);

    resized = (unsigned char *)iDebugMalloc.realloc(memory, 16);
    TEST_REQUIRE(resized != NULL);
    memory = resized;
    for (i = 0; i < 16; ++i)
        TEST_REQUIRE(memory[i] == (unsigned char)(0xa0 + i));
    iDebugMalloc.free(memory);
    return 0;

cleanup:
    if (memory != NULL)
        iDebugMalloc.free(memory);
    return -1;
}

static int test_realloc_zero_returns_valid_zero_size_block(void)
{
    void *memory = iDebugMalloc.malloc(16);
    void *resized;

    TEST_REQUIRE(memory != NULL);
    resized = iDebugMalloc.realloc(memory, 0);
    TEST_REQUIRE(resized != NULL);
    memory = resized;
    iDebugMalloc.free(resized);
    memory = NULL;
    return 0;

cleanup:
    if (memory != NULL)
        iDebugMalloc.free(memory);
    return -1;
}

static int test_null_operations_are_noops_or_allocate(void)
{
    void *memory = NULL;

    iDebugMalloc.free(NULL);
    memory = iDebugMalloc.realloc(NULL, 8);
    TEST_REQUIRE(memory != NULL);
    iDebugMalloc.free(memory);
    return 0;

cleanup:
    if (memory != NULL)
        iDebugMalloc.free(memory);
    return -1;
}

static int test_size_overflow_is_rejected_and_original_survives(void)
{
    unsigned char *memory = NULL;
    unsigned char *resized = NULL;

    TEST_REQUIRE(iDebugMalloc.malloc(SIZE_MAX) == NULL);
    TEST_REQUIRE(iDebugMalloc.malloc(SIZE_MAX - sizeof(size_t)) == NULL);
    TEST_REQUIRE(iDebugMalloc.malloc(SIZE_MAX - 7) == NULL);

    memory = (unsigned char *)iDebugMalloc.malloc(8);
    TEST_REQUIRE(memory != NULL);
    memory[0] = 0x5a;
    resized = (unsigned char *)iDebugMalloc.realloc(memory, SIZE_MAX);
    TEST_REQUIRE(resized == NULL);
    TEST_REQUIRE(memory[0] == 0x5a);
    iDebugMalloc.free(memory);
    return 0;

cleanup:
    if (memory != NULL)
        iDebugMalloc.free(memory);
    return -1;
}

static int test_calloc_overflow_and_zeroing(void)
{
    unsigned char *memory = NULL;
    size_t i;

    TEST_REQUIRE(iDebugMalloc.calloc(SIZE_MAX / 2 + 1, 2) == NULL);
    memory = (unsigned char *)iDebugMalloc.calloc(0, SIZE_MAX);
    TEST_REQUIRE(memory != NULL);
    iDebugMalloc.free(memory);
    memory = (unsigned char *)iDebugMalloc.calloc(4, sizeof(*memory));
    TEST_REQUIRE(memory != NULL);
    for (i = 0; i < 4; ++i)
        TEST_REQUIRE(memory[i] == 0);
    iDebugMalloc.free(memory);
    memory = (unsigned char *)iDebugMalloc.calloc(4, 0);
    TEST_REQUIRE(memory != NULL);
    iDebugMalloc.free(memory);
    return 0;

cleanup:
    if (memory != NULL)
        iDebugMalloc.free(memory);
    return -1;
}

static int test_bad_pointer_and_double_free_are_safe(void)
{
    unsigned char foreign;
    void *memory = NULL;
    size_t *header;
    ErrorFunction old_error = NULL;

    old_error = iError.SetErrorFunction(capture_error);
    clear_capture();
    iDebugMalloc.free(&foreign);
    TEST_REQUIRE(captured.count == 1);
    TEST_REQUIRE(captured.code == CONTAINER_ERROR_BADPOINTER);
    TEST_REQUIRE(strcmp(captured.operation, "Free") == 0);

    memory = iDebugMalloc.malloc(8);
    TEST_REQUIRE(memory != NULL);
    header = (size_t *)((unsigned char *)memory - 2 * sizeof(size_t));
    header[0] = 0;
    clear_capture();
    iDebugMalloc.free(memory);
    TEST_REQUIRE(captured.count == 1);
    TEST_REQUIRE(captured.code == CONTAINER_ERROR_BADPOINTER);
    header[0] = (size_t)0xdeadbeef;
    iDebugMalloc.free(memory);
    clear_capture();
    iDebugMalloc.free(memory);
    TEST_REQUIRE(captured.count == 1);
    TEST_REQUIRE(captured.code == CONTAINER_ERROR_BADPOINTER);
    TEST_REQUIRE(strcmp(captured.operation, "Free") == 0);
    iError.SetErrorFunction(old_error);
    return 0;

cleanup:
    if (old_error != NULL)
        iError.SetErrorFunction(old_error);
    if (memory != NULL)
        iDebugMalloc.free(memory);
    return -1;
}

static int test_footer_overwrite_is_reported_without_invalid_access(void)
{
    unsigned char *memory = NULL;
    size_t *footer;
    ErrorFunction old_error = NULL;

    memory = (unsigned char *)iDebugMalloc.malloc(16);
    TEST_REQUIRE(memory != NULL);
    footer = (size_t *)(memory + 16);
    *footer = 0;

    old_error = iError.SetErrorFunction(capture_error);
    clear_capture();
    iDebugMalloc.free(memory);
    TEST_REQUIRE(captured.count == 1);
    TEST_REQUIRE(captured.code == CONTAINER_ERROR_BUFFEROVERFLOW);
    TEST_REQUIRE(strcmp(captured.operation, "Free") == 0);
    iError.SetErrorFunction(old_error);

    *footer = (size_t)0xbeefdead;
    iDebugMalloc.free(memory);
    return 0;

cleanup:
    if (old_error != NULL)
        iError.SetErrorFunction(old_error);
    if (memory != NULL) {
        *footer = (size_t)0xbeefdead;
        iDebugMalloc.free(memory);
    }
    return -1;
}

static int test_realloc_bad_pointer_is_rejected(void)
{
    unsigned char foreign;
    ErrorFunction old_error;

    old_error = iError.SetErrorFunction(capture_error);
    clear_capture();
    TEST_REQUIRE(iDebugMalloc.realloc(&foreign, 12) == NULL);
    TEST_REQUIRE(captured.count == 1);
    TEST_REQUIRE(captured.code == CONTAINER_ERROR_BADPOINTER);
    TEST_REQUIRE(strcmp(captured.operation, "Realloc") == 0);
    iError.SetErrorFunction(old_error);
    return 0;

cleanup:
    iError.SetErrorFunction(old_error);
    return -1;
}

static const TestCase tests[] = {
    {"basic sizes are aligned, zeroed, and writable",
     test_basic_sizes_are_aligned_zeroed_and_writable},
    {"realloc preserves data and repairs shrink metadata",
     test_realloc_preserves_data_and_repairs_shrink_metadata},
    {"realloc zero returns a valid zero-size block",
     test_realloc_zero_returns_valid_zero_size_block},
    {"null operations are safe",
     test_null_operations_are_noops_or_allocate},
    {"size overflow is rejected and original survives",
     test_size_overflow_is_rejected_and_original_survives},
    {"calloc overflow and zeroing",
     test_calloc_overflow_and_zeroing},
    {"bad pointer and double free are safe",
     test_bad_pointer_and_double_free_are_safe},
    {"footer overwrite is reported safely",
     test_footer_overwrite_is_reported_without_invalid_access},
    {"realloc bad pointer is rejected",
     test_realloc_bad_pointer_is_rejected},
};

static const TestSuite suite = {
    "malloc_debug",
    tests,
    sizeof(tests) / sizeof(tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
