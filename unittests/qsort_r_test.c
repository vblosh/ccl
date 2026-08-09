/* Keep the platform declaration visible: this is the ABI regression test. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int CclQsortCompare(void *, const void *, const void *);
extern void ccl_qsort_r(void *, size_t, size_t, void *, CclQsortCompare *);

typedef struct {
	int key;
	unsigned id;
	unsigned char payload[5];
} SortRecord;

typedef struct {
	void *expected_thunk;
	int descending;
	size_t calls;
	int bad_thunk;
} CompareState;

static int compare_key(int left, int right, int descending)
{
	int result = (left > right) - (left < right);
	return descending ? -result : result;
}

static int ccl_compare(void *thunk, const void *left, const void *right)
{
	CompareState *state = (CompareState *)thunk;
	const SortRecord *a = (const SortRecord *)left;
	const SortRecord *b = (const SortRecord *)right;

	if (state == NULL || thunk != state->expected_thunk) {
		if (state != NULL) state->bad_thunk = 1;
		return 0;
	}
	++state->calls;
	return compare_key(a->key, b->key, state->descending);
}

static int ccl_compare_bytes(void *thunk, const void *left, const void *right)
{
	CompareState *state = (CompareState *)thunk;
	const unsigned char *a = (const unsigned char *)left;
	const unsigned char *b = (const unsigned char *)right;

	if (state == NULL || thunk != state->expected_thunk) {
		if (state != NULL) state->bad_thunk = 1;
		return 0;
	}
	++state->calls;
	return compare_key(a[0], b[0], state->descending);
}

static int native_compare(const void *left, const void *right, void *thunk)
{
	CompareState *state = (CompareState *)thunk;
	const int *a = (const int *)left;
	const int *b = (const int *)right;
	if (state != NULL) ++state->calls;
	return compare_key(*a, *b, state != NULL && state->descending);
}

static int records_sorted(const SortRecord *records, size_t count, int descending)
{
	size_t i;
	for (i = 1; i < count; ++i)
		if (compare_key(records[i - 1].key, records[i].key, descending) > 0)
			return 0;
	return 1;
}

static int bytes_sorted(const unsigned char *records, size_t count, size_t width,
						int descending)
{
	size_t i;
	for (i = 1; i < count; ++i) {
		const unsigned char *previous = records + (i - 1) * width;
		const unsigned char *current = records + i * width;
		if (compare_key(previous[0], current[0], descending) > 0)
			return 0;
	}
	return 1;
}

static int same_bytes(const unsigned char *before, const unsigned char *after,
					 size_t count, size_t width)
{
	unsigned char matched[128] = {0};
	size_t i, j;

	if (count > sizeof(matched)) return 0;
	for (i = 0; i < count; ++i) {
		for (j = 0; j < count; ++j) {
			if (!matched[j] &&
				memcmp(after + i * width, before + j * width, width) == 0) {
				matched[j] = 1;
				break;
			}
		}
		if (j == count) return 0;
	}
	return 1;
}

static void fill_records(SortRecord *records, size_t count, int pattern)
{
	unsigned seed = 0x31415926U;
	size_t i, j;
	for (i = 0; i < count; ++i) {
		if (pattern == 0) {
			seed = seed * 1664525U + 1013904223U;
			records[i].key = (int)((seed >> 16) % 19U);
		}
		else if (pattern == 1) records[i].key = (int)i;
		else if (pattern == 2) records[i].key = (int)(count - i);
		else if (pattern == 3) records[i].key = (int)(i < count / 2 ? i : count - i);
		else records[i].key = (int)(i & 1U ? 4 : 1);
		records[i].id = (unsigned)(i + 1);
		for (j = 0; j < sizeof(records[i].payload); ++j)
			records[i].payload[j] = (unsigned char)(i * 9U + j * 13U + 7U);
	}
}

static int run_record_case(size_t count, int pattern, int descending)
{
	SortRecord records[128];
	SortRecord before[128];
	CompareState state;

	fill_records(records, count, pattern);
	memcpy(before, records, count * sizeof(records[0]));
	state.expected_thunk = &state;
	state.descending = descending;
	state.calls = 0;
	state.bad_thunk = 0;
	ccl_qsort_r(records, count, sizeof(records[0]), &state, ccl_compare);
	return records_sorted(records, count, descending) &&
		same_bytes((const unsigned char *)before, (const unsigned char *)records,
				count, sizeof(records[0])) && !state.bad_thunk &&
		(count < 2 || state.calls != 0) ? 0 : -1;
}

static int run_byte_case(size_t count, size_t width, int descending)
{
	unsigned char records[128 * 13];
	unsigned char before[128 * 13];
	CompareState state;
	size_t i, j;

	for (i = 0; i < count; ++i) {
		records[i * width] = (unsigned char)((i * 29U + 3U) % 23U);
		for (j = 1; j < width; ++j)
			records[i * width + j] = (unsigned char)(i * 3U + j * 11U);
	}
	memcpy(before, records, count * width);
	state.expected_thunk = &state;
	state.descending = descending;
	state.calls = 0;
	state.bad_thunk = 0;
	ccl_qsort_r(records, count, width, &state, ccl_compare_bytes);
	return bytes_sorted(records, count, width, descending) &&
		same_bytes(before, records, count, width) && !state.bad_thunk &&
	(count < 2 || state.calls != 0) ? 0 : -1;
}

static int test_sizes_and_patterns(void)
{
	static const size_t counts[] = {0, 1, 2, 6, 7, 8, 40, 41, 96};
	size_t i;

	for (i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
		TEST_REQUIRE(run_record_case(counts[i], 0, 0) == 0);
		TEST_REQUIRE(run_record_case(counts[i], 1, 1) == 0);
		TEST_REQUIRE(run_record_case(counts[i], 2, 0) == 0);
		TEST_REQUIRE(run_record_case(counts[i], 3, 1) == 0);
		TEST_REQUIRE(run_record_case(counts[i], 4, 0) == 0);
	}
	return 0;

cleanup:
	return -1;
}

static int test_element_widths_and_noops(void)
{
	static const size_t widths[] = {1, sizeof(long), 13};
	unsigned char untouched[16];
	unsigned char before[16];
	size_t i;

	memset(untouched, 0xc7, sizeof(untouched));
	memcpy(before, untouched, sizeof(untouched));
	ccl_qsort_r(untouched, 0, 1, NULL, ccl_compare_bytes);
	ccl_qsort_r(untouched, 1, 1, NULL, ccl_compare_bytes);
	ccl_qsort_r(untouched, 4, 0, NULL, ccl_compare_bytes);
	TEST_REQUIRE(memcmp(untouched, before, sizeof(untouched)) == 0);
	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
		TEST_REQUIRE(run_byte_case(41, widths[i], (int)(i & 1U)) == 0);
	}
	return 0;

cleanup:
	return -1;
}

static int test_native_qsort_r_abi(void)
{
	int values[] = {7, 1, 4, 1, 9, 2};
	int expected[] = {1, 1, 2, 4, 7, 9};
	CompareState state;

	state.expected_thunk = &state;
	state.descending = 0;
	state.calls = 0;
	state.bad_thunk = 0;
	/* With _GNU_SOURCE this call has glibc's (left, right, arg) ABI. */
	qsort_r(values, sizeof(values) / sizeof(values[0]), sizeof(values[0]),
			native_compare, &state);
	TEST_REQUIRE(memcmp(values, expected, sizeof(values)) == 0);
	TEST_REQUIRE(state.calls != 0);
	return 0;

cleanup:
	return -1;
}

static const TestCase tests[] = {
	{"sizes and patterns", test_sizes_and_patterns},
	{"element widths and no-ops", test_element_widths_and_noops},
	{"native qsort_r ABI", test_native_qsort_r_abi}
};

const TestSuite *ccl_get_test_suite(void)
{
	static const TestSuite suite = {
		"ccl_qsort_r", tests, sizeof(tests) / sizeof(tests[0])
	};
	return &suite;
}
