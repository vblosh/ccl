#include "containers.h"
#include "test_support.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The public BloomFilter is opaque.  This mirror is used only to verify the
   two allocation sizes reported by a counting allocator. */
typedef struct {
    size_t count;
    size_t max_elements;
    size_t hash_functions;
    size_t bits;
    ContainerAllocator *allocator;
    unsigned char *bitset;
    unsigned seeds[1];
} BloomLayout;

typedef struct {
    size_t calls;
    size_t frees;
    size_t sizes[4];
    size_t fail_at;
} AllocationState;

static AllocationState allocation_state;
static const char *last_operation;
static int last_error;

static void *counting_malloc(size_t size)
{
    void *result;
    ++allocation_state.calls;
    if (allocation_state.calls <= sizeof(allocation_state.sizes) /
                                      sizeof(allocation_state.sizes[0]))
        allocation_state.sizes[allocation_state.calls - 1] = size;
    if (allocation_state.fail_at != 0 &&
        allocation_state.calls >= allocation_state.fail_at)
        return NULL;
    result = malloc(size);
    return result;
}

static void counting_free(void *value)
{
    if (value != NULL)
        ++allocation_state.frees;
    free(value);
}

static void *counting_realloc(void *value, size_t size)
{
    return realloc(value, size);
}

static void *counting_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size)
        return NULL;
    return counting_malloc(count * size);
}

static ContainerAllocator counting_allocator = {
    counting_malloc,
    counting_free,
    counting_realloc,
    counting_calloc
};

static void *capture_error(const char *operation, int code, ...)
{
    last_operation = operation;
    last_error = code;
    return NULL;
}

static void reset_observations(void)
{
    memset(&allocation_state, 0, sizeof(allocation_state));
    last_operation = NULL;
    last_error = 0;
}

static int error_is(const char *operation, int code)
{
    return last_operation != NULL && strcmp(last_operation, operation) == 0 &&
           last_error == code;
}

static size_t expected_bits(size_t elements, double probability)
{
    long double value = roundl(-(long double)elements *
                               logl((long double)probability) /
                               0.4804530139182014246671025263266214588214L);
    if (value < 1.0L)
        value = 1.0L;
    return (size_t)value;
}

static size_t expected_hashes(size_t elements, size_t bits)
{
    long double value = roundl(0.7L * (long double)bits /
                               (long double)elements);
    if (value < 1.0L)
        value = 1.0L;
    return (size_t)value;
}

static int test_dimensions_and_formula(void)
{
    ContainerAllocator *old_allocator = NULL;
    BloomFilter *filter = NULL;
    const size_t elements[] = {1, 100, 1000};
    const double probabilities[] = {0.5, 0.01, 0.001};
    size_t i;
    int result = -1;

    old_allocator = iAllocator.Change(&counting_allocator);
    for (i = 0; i < sizeof(elements) / sizeof(elements[0]); ++i) {
        size_t bits = expected_bits(elements[i], probabilities[i]);
        size_t hashes = expected_hashes(elements[i], bits);
        size_t bit_bytes = bits / 8 + (bits % 8 != 0);
        size_t object_bytes = sizeof(BloomLayout) +
                              (hashes - 1) * sizeof(unsigned);
        reset_observations();
        filter = iBloomFilter.Create(elements[i], probabilities[i]);
        TEST_REQUIRE(filter != NULL);
        TEST_REQUIRE(allocation_state.calls == 2);
        TEST_REQUIRE(allocation_state.sizes[0] == object_bytes);
        TEST_REQUIRE(allocation_state.sizes[1] == bit_bytes);
        TEST_REQUIRE(iBloomFilter.CalculateSpace(elements[i], probabilities[i]) ==
                     object_bytes + bit_bytes);
        iBloomFilter.Finalize(filter);
        filter = NULL;
    }
    result = 0;

cleanup:
    if (filter != NULL)
        iBloomFilter.Finalize(filter);
    iAllocator.Change(old_allocator);
    return result;
}

static int test_binary_unaligned_keys(void)
{
    BloomFilter *filter = NULL;
    unsigned char storage[96];
    unsigned char binary[] = {0x00, 0x7f, 0x00, 0xff, 0x01};
    const void *keys[] = {
        "a", "ab", "abc", "abcd", "abcde", binary,
        "a deliberately longer bloom-filter key"
    };
    const size_t lengths[] = {1, 2, 3, 4, 5, sizeof(binary), 39};
    size_t i;
    int result = -1;

    memset(storage, 0xa5, sizeof(storage));
    memcpy(storage + 1, "unaligned four-byte key", 23);
    filter = iBloomFilter.Create(32, 0.01);
    TEST_REQUIRE(filter != NULL);
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        TEST_REQUIRE(iBloomFilter.Add(filter, keys[i], lengths[i]) == i + 1);
        TEST_REQUIRE(iBloomFilter.Find(filter, keys[i], lengths[i]) == 1);
    }
    TEST_REQUIRE(iBloomFilter.Add(filter, storage + 1, 23) == 8);
    TEST_REQUIRE(iBloomFilter.Find(filter, storage + 1, 23) == 1);
    result = 0;

cleanup:
    if (filter != NULL)
        iBloomFilter.Finalize(filter);
    return result;
}

static int test_fill_clear_and_refill(void)
{
    BloomFilter *filter = NULL;
    const char *keys[] = {"one", "two", "three", "four"};
    size_t i;
    ErrorFunction old_error = NULL;
    int result = -1;

    old_error = iError.SetErrorFunction(capture_error);
    filter = iBloomFilter.Create(3, 0.01);
    TEST_REQUIRE(filter != NULL);
    for (i = 0; i < 3; ++i)
        TEST_REQUIRE(iBloomFilter.Add(filter, keys[i], strlen(keys[i])) == i + 1);
    TEST_REQUIRE(iBloomFilter.Add(filter, keys[3], strlen(keys[3])) == 0);
    TEST_REQUIRE(error_is("BloomFilter.Add", CONTAINER_FULL));
    TEST_REQUIRE(iBloomFilter.Clear(filter) == 1);
    for (i = 0; i < 3; ++i)
        TEST_REQUIRE(iBloomFilter.Find(filter, keys[i], strlen(keys[i])) == 0);
    for (i = 0; i < 3; ++i)
        TEST_REQUIRE(iBloomFilter.Add(filter, keys[i], strlen(keys[i])) == i + 1);
    result = 0;

cleanup:
    if (filter != NULL)
        iBloomFilter.Finalize(filter);
    iError.SetErrorFunction(old_error);
    return result;
}

static int test_invalid_dimensions_and_arguments(void)
{
    BloomFilter *filter = NULL;
    ErrorFunction old_error;
    const double invalid[] = {
        0.0, 1.0, -DBL_MIN, 1.0 + DBL_EPSILON, NAN, INFINITY, -INFINITY
    };
    size_t i;
    int result = -1;

    old_error = iError.SetErrorFunction(capture_error);
    TEST_REQUIRE(iBloomFilter.Create(0, 0.5) == NULL);
    TEST_REQUIRE(error_is("BloomFilter.Create", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iBloomFilter.CalculateSpace(0, 0.5) == 0);
    TEST_REQUIRE(error_is("BloomFilter.CalculateSpace", CONTAINER_ERROR_BADARG));
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        TEST_REQUIRE(iBloomFilter.Create(10, invalid[i]) == NULL);
        TEST_REQUIRE(error_is("BloomFilter.Create", CONTAINER_ERROR_BADARG));
        TEST_REQUIRE(iBloomFilter.CalculateSpace(10, invalid[i]) == 0);
        TEST_REQUIRE(error_is("BloomFilter.CalculateSpace", CONTAINER_ERROR_BADARG));
    }

    filter = iBloomFilter.Create(1, nextafter(1.0, 0.0));
    TEST_REQUIRE(filter != NULL);
    TEST_REQUIRE(iBloomFilter.Add(filter, "edge", 4) == 1);
    TEST_REQUIRE(iBloomFilter.Find(filter, "edge", 4) == 1);
    iBloomFilter.Finalize(filter);
    filter = NULL;
    filter = iBloomFilter.Create(1, DBL_MIN);
    TEST_REQUIRE(filter != NULL);
    TEST_REQUIRE(iBloomFilter.Add(filter, "edge", 4) == 1);

    TEST_REQUIRE(iBloomFilter.Add(NULL, "x", 1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("BloomFilter.Add", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iBloomFilter.Add(filter, NULL, 1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("BloomFilter.Add", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iBloomFilter.Add(filter, "x", 0) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("BloomFilter.Add", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iBloomFilter.Find(NULL, "x", 1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iBloomFilter.Find", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iBloomFilter.Find(filter, NULL, 1) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBloomFilter.Find(filter, "x", 0) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBloomFilter.Clear(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iBloomFilter.Clear", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iBloomFilter.Finalize(NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iBloomFilter.Finalize", CONTAINER_ERROR_BADARG));
    result = 0;

cleanup:
    if (filter != NULL)
        iBloomFilter.Finalize(filter);
    iError.SetErrorFunction(old_error);
    return result;
}

static int test_allocator_failures_and_ownership(void)
{
    ContainerAllocator *old_allocator = NULL;
    BloomFilter *filter = NULL;
    ErrorFunction old_error;
    int result = -1;

    old_error = iError.SetErrorFunction(capture_error);
    reset_observations();
    allocation_state.fail_at = 1;
    old_allocator = iAllocator.Change(&counting_allocator);
    TEST_REQUIRE(iBloomFilter.Create(10, 0.01) == NULL);
    TEST_REQUIRE(allocation_state.calls == 1 && allocation_state.frees == 0);
    TEST_REQUIRE(error_is("BloomFilter.Create", CONTAINER_ERROR_NOMEMORY));

    reset_observations();
    allocation_state.fail_at = 2;
    TEST_REQUIRE(iBloomFilter.Create(10, 0.01) == NULL);
    TEST_REQUIRE(allocation_state.calls == 2 && allocation_state.frees == 1);
    TEST_REQUIRE(error_is("BloomFilter.Create", CONTAINER_ERROR_NOMEMORY));

    reset_observations();
    allocation_state.fail_at = 0;
    filter = iBloomFilter.Create(10, 0.01);
    TEST_REQUIRE(filter != NULL);
    TEST_REQUIRE(allocation_state.calls == 2);
    iAllocator.Change(old_allocator);
    TEST_REQUIRE(iBloomFilter.Finalize(filter) == 1);
    filter = NULL;
    TEST_REQUIRE(allocation_state.frees == 2);
    result = 0;

cleanup:
    if (filter != NULL)
        iBloomFilter.Finalize(filter);
    iAllocator.Change(old_allocator);
    iError.SetErrorFunction(old_error);
    return result;
}

static int test_checked_size_boundaries(void)
{
    ErrorFunction old_error;
    int result = -1;

    old_error = iError.SetErrorFunction(capture_error);
    TEST_REQUIRE(iBloomFilter.CalculateSpace(SIZE_MAX, 0.5) == 0);
    TEST_REQUIRE(error_is("BloomFilter.CalculateSpace", CONTAINER_ERROR_NOMEMORY));
    TEST_REQUIRE(iBloomFilter.Create(SIZE_MAX, 0.5) == NULL);
    TEST_REQUIRE(error_is("BloomFilter.Create", CONTAINER_ERROR_NOMEMORY));
    result = 0;

cleanup:
    iError.SetErrorFunction(old_error);
    return result;
}

static const TestCase bloom_tests[] = {
    {"dimensions and formula", test_dimensions_and_formula},
    {"binary and unaligned keys", test_binary_unaligned_keys},
    {"fill, clear, and refill", test_fill_clear_and_refill},
    {"invalid dimensions and arguments", test_invalid_dimensions_and_arguments},
    {"allocator failures and ownership", test_allocator_failures_and_ownership},
    {"checked size boundaries", test_checked_size_boundaries}
};

static const TestSuite bloom_suite = {
    "bloom",
    bloom_tests,
    sizeof(bloom_tests) / sizeof(bloom_tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &bloom_suite;
}
