#include "test_support.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"

typedef struct {
	int key;
	unsigned id;
	unsigned checksum;
	unsigned char payload[9];
} SortRecord;

typedef struct {
	CompareInfo *expected;
	const void *expected_left;
	const void *expected_right;
	int descending;
	size_t calls;
	int bad_context;
} CompareState;

static int compare_record(const void *left, const void *right,
						 CompareInfo *info)
{
	const SortRecord *a = (const SortRecord *)left;
	const SortRecord *b = (const SortRecord *)right;
	CompareState *state;
	int result;

	if (info == NULL) return 0;
	state = (CompareState *)info->ExtraArgs;
	if (state == NULL || info != state->expected ||
		info->ContainerLeft != state->expected_left ||
		info->ContainerRight != state->expected_right)
		if (state != NULL) state->bad_context = 1;
	if (state != NULL) ++state->calls;
	result = (a->key > b->key) - (a->key < b->key);
	return state != NULL && state->descending ? -result : result;
}

static int compare_byte_record(const void *left, const void *right,
						   CompareInfo *info)
{
	const unsigned char *a = (const unsigned char *)left;
	const unsigned char *b = (const unsigned char *)right;
	CompareState *state = info == NULL ? NULL : (CompareState *)info->ExtraArgs;
	int result;

	if (state == NULL || info != state->expected)
		if (state != NULL) state->bad_context = 1;
	if (state != NULL) ++state->calls;
	result = (a[0] > b[0]) - (a[0] < b[0]);
	return state != NULL && state->descending ? -result : result;
}

static int compare_int(const void *left, const void *right,
						CompareInfo *info)
{
	(void)info;
	return (*(const int *)left > *(const int *)right) -
		(*(const int *)left < *(const int *)right);
}

static int records_sorted(const SortRecord *records, size_t count, int descending)
{
	size_t i;
	for (i = 1; i < count; ++i) {
		if ((!descending && records[i - 1].key > records[i].key) ||
			(descending && records[i - 1].key < records[i].key))
			return 0;
	}
	return 1;
}

static int bytes_sorted(const unsigned char *records, size_t count, size_t width,
						int descending)
{
	size_t i;
	for (i = 1; i < count; ++i) {
		const unsigned char *previous = records + (i - 1) * width;
		const unsigned char *current = records + i * width;
		if ((!descending && previous[0] > current[0]) ||
			(descending && previous[0] < current[0]))
			return 0;
	}
	return 1;
}

static int same_records(const unsigned char *before, const unsigned char *after,
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

static void fill_records(SortRecord *records, const int *keys, size_t count)
{
	size_t i, j;
	for (i = 0; i < count; ++i) {
		records[i].key = keys[i];
		records[i].id = (unsigned)(i + 1);
		records[i].checksum = (unsigned)(keys[i] * 31 + (int)i * 17);
		for (j = 0; j < sizeof(records[i].payload); ++j)
			records[i].payload[j] = (unsigned char)(i * 13 + j * 7 + 3);
	}
}

static int run_record_case(const int *keys, size_t count, int descending)
{
	SortRecord records[128];
	SortRecord before[128];
	CompareState state;
	CompareInfo info;
	static const int left_tag = 1;
	static const int right_tag = 2;

	if (count > sizeof(records) / sizeof(records[0])) return -1;
	fill_records(records, keys, count);
	memcpy(before, records, count * sizeof(records[0]));
	state.expected = &info;
	state.expected_left = &left_tag;
	state.expected_right = &right_tag;
	state.descending = descending;
	state.calls = 0;
	state.bad_context = 0;
	info.ExtraArgs = &state;
	/* The comparator intentionally checks these exact context values. */
	info.ContainerLeft = &left_tag;
	info.ContainerRight = &right_tag;
	qsortEx(records, count, sizeof(records[0]), compare_record, &info);
	return records_sorted(records, count, descending) &&
		 same_records((const unsigned char *)before, (const unsigned char *)records,
					 count, sizeof(records[0])) && state.calls != 0 &&
		 !state.bad_context ? 0 : -1;
}

static int test_noop_inputs(void)
{
	unsigned char bytes[12];
	unsigned char before[12];
	CompareInfo info;

	memset(bytes, 0xa5, sizeof(bytes));
	memcpy(before, bytes, sizeof(bytes));
	memset(&info, 0, sizeof(info));
	qsortEx(bytes, 0, 1, NULL, &info);
	qsortEx(bytes, 1, 1, NULL, &info);
	qsortEx(bytes, 4, 0, NULL, &info);
	TEST_REQUIRE(memcmp(bytes, before, sizeof(bytes)) == 0);
	return 0;

cleanup:
	return -1;
}

static int test_cutoff_and_pivot_cases(void)
{
	static const int reverse2[] = {2, 1};
	static const int reverse7[] = {7, 6, 5, 4, 3, 2, 1};
	static const int duplicate8[] = {3, 1, 3, 2, 1, 3, 2, 1};
	static const int reverse9[] = {9, 8, 7, 6, 5, 4, 3, 2, 1};
	/* The middle element is the minimum/maximum pivot respectively. */
	static const int minimum_middle[] = {7, 5, 6, 8, 0, 4, 3, 2, 1};
	static const int maximum_middle[] = {1, 2, 3, 4, 9, 5, 6, 7, 8};

	TEST_REQUIRE(run_record_case(reverse2, 2, 0) == 0);
	TEST_REQUIRE(run_record_case(reverse7, 7, 1) == 0);
	TEST_REQUIRE(run_record_case(duplicate8, 8, 0) == 0);
	TEST_REQUIRE(run_record_case(reverse9, 9, 1) == 0);
	TEST_REQUIRE(run_record_case(minimum_middle, 9, 0) == 0);
	TEST_REQUIRE(run_record_case(maximum_middle, 9, 1) == 0);
	return 0;

cleanup:
	return -1;
}

static int test_large_partitions_and_records(void)
{
	int keys[96];
	SortRecord records[96];
	SortRecord before[96];
	CompareState state;
	CompareInfo info;
	static const int left_tag = 3;
	static const int right_tag = 4;
	unsigned seed = 0x9e3779b9U;
	int i;

	for (i = 0; i < 96; ++i) {
		if (i < 48) keys[i] = i % 9;
		else keys[i] = (95 - i) % 17;
	}
	fill_records(records, keys, 96);
	memcpy(before, records, sizeof(records));
	state.expected = &info;
	state.expected_left = &left_tag;
	state.expected_right = &right_tag;
	state.descending = 0;
	state.calls = 0;
	state.bad_context = 0;
	info.ExtraArgs = &state;
	info.ContainerLeft = &left_tag;
	info.ContainerRight = &right_tag;
	/* qsortEx forwards the same CompareInfo pointer, not a copy. */
	qsortEx(records, 96, sizeof(records[0]), compare_record, &info);
	TEST_REQUIRE(records_sorted(records, 96, 0));
	TEST_REQUIRE(same_records((const unsigned char *)before,
						 (const unsigned char *)records, 96, sizeof(records[0])));
	TEST_REQUIRE(state.calls > 100 && !state.bad_context);
	for (i = 0; i < 96; ++i) {
		seed = seed * 1664525U + 1013904223U;
		keys[i] = (int)((seed >> 16) % 101U) - 50;
	}
	TEST_REQUIRE(run_record_case(keys, 96, 1) == 0);
	return 0;

cleanup:
	return -1;
}

static int test_byte_widths(void)
{
	static const size_t widths[] = {1, 3, sizeof(long), 17};
	unsigned char records[20 * 17];
	unsigned char before[20 * 17];
	CompareState state;
	CompareInfo info;
	static const int left_tag = 5;
	static const int right_tag = 6;
	unsigned i, j;

	for (j = 0; j < sizeof(widths) / sizeof(widths[0]); ++j) {
		size_t width = widths[j];
		for (i = 0; i < 20; ++i) {
			records[i * width] = (unsigned char)((19 - i) % 11);
			for (unsigned k = 1; k < width; ++k)
				records[i * width + k] = (unsigned char)(i * 5 + k);
		}
		memcpy(before, records, 20 * width);
		state.expected = &info;
		state.descending = (int)(j & 1U);
		state.calls = 0;
		state.bad_context = 0;
		info.ExtraArgs = &state;
		info.ContainerLeft = &left_tag;
		info.ContainerRight = &right_tag;
		qsortEx(records, 20, width, compare_byte_record, &info);
		TEST_REQUIRE(bytes_sorted(records, 20, width, state.descending));
		TEST_REQUIRE(same_records(before, records, 20, width));
		TEST_REQUIRE(state.calls != 0 && !state.bad_context);
	}
	return 0;

cleanup:
	return -1;
}

static int test_container_sort_integration(void)
{
	static const int values[] = {4, 1, 3, 1, 2};
	Vector *vector = NULL;
	List *list = NULL;
	Dlist *dlist = NULL;
	strCollection *strings = NULL;
	static const char *words[] = {"delta", "alpha", "charlie", "alpha"};
	static const int expected[] = {1, 1, 2, 3, 4};
	static const char *sorted_words[] = {"alpha", "alpha", "charlie", "delta"};
	size_t i;
	int result = -1;

	vector = iVector.Create(sizeof(int), 1);
	list = iList.Create(sizeof(int));
	dlist = iDlist.Create(sizeof(int));
	strings = istrCollection.Create(1);
	TEST_REQUIRE(vector != NULL && list != NULL && dlist != NULL && strings != NULL);
	TEST_REQUIRE(iVector.AddRange(vector, 5, values) == 1);
	TEST_REQUIRE(iList.AddRange(list, 5, values) == 1);
	TEST_REQUIRE(iDlist.AddRange(dlist, 5, values) == 1);
	iVector.SetCompareFunction(vector, compare_int);
	iList.SetCompareFunction(list, compare_int);
	iDlist.SetCompareFunction(dlist, compare_int);
	TEST_REQUIRE(iVector.Sort(vector) == 1 && iList.Sort(list) == 1 &&
				iDlist.Sort(dlist) == 1);
	for (i = 0; i < 5; ++i) {
		TEST_REQUIRE(*(int *)iVector.GetElement(vector, i) == expected[i]);
		TEST_REQUIRE(*(int *)iList.GetElement(list, i) == expected[i]);
		TEST_REQUIRE(*(int *)iDlist.GetElement(dlist, i) == expected[i]);
	}
	for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
		TEST_REQUIRE(istrCollection.Add(strings, words[i]) == 1);
	TEST_REQUIRE(istrCollection.Sort(strings) == 1);
	for (i = 0; i < sizeof(sorted_words) / sizeof(sorted_words[0]); ++i)
		TEST_REQUIRE(strcmp(istrCollection.GetElement(strings, i), sorted_words[i]) == 0);
	result = 0;

cleanup:
	if (strings != NULL) istrCollection.Finalize(strings);
	if (dlist != NULL) iDlist.Finalize(dlist);
	if (list != NULL) iList.Finalize(list);
	if (vector != NULL) iVector.Finalize(vector);
	return result;
}

static const TestCase tests[] = {
	{"no-op inputs", test_noop_inputs},
	{"cutoff and pivot cases", test_cutoff_and_pivot_cases},
	{"large partitions and records", test_large_partitions_and_records},
	{"byte widths", test_byte_widths},
	{"container sort integration", test_container_sort_integration}
};

const TestSuite *ccl_get_test_suite(void)
{
	static const TestSuite suite = {
		"qsortEx", tests, sizeof(tests) / sizeof(tests[0])
	};
	return &suite;
}
