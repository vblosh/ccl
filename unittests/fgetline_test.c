#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

typedef struct {
	int realloc_calls;
	int free_calls;
	int fail_at;
} AllocatorState;

static AllocatorState *active_allocator_state;
static int captured_errors;

static void *test_malloc(size_t size)
{
	return malloc(size);
}

static void *test_calloc(size_t count, size_t size)
{
	return calloc(count, size);
}

static void *test_realloc(void *ptr, size_t size)
{
	active_allocator_state->realloc_calls++;
	if (active_allocator_state->fail_at > 0 &&
		active_allocator_state->realloc_calls == active_allocator_state->fail_at)
		return NULL;
	return realloc(ptr, size);
}

static void test_free(void *ptr)
{
	if (ptr != NULL)
		active_allocator_state->free_calls++;
	free(ptr);
}

static ContainerAllocator test_allocator(AllocatorState *state)
{
	ContainerAllocator result;
	active_allocator_state = state;
	result.malloc = test_malloc;
	result.free = test_free;
	result.realloc = test_realloc;
	result.calloc = test_calloc;
	return result;
}

static void *capture_error(const char *name, int code, ...)
{
	(void)name;
	(void)code;
	captured_errors++;
	return NULL;
}

static FILE *narrow_stream(const unsigned char *data, size_t size)
{
	FILE *stream = tmpfile();
	if (stream == NULL)
		return NULL;
	if (size != 0 && fwrite(data, 1, size, stream) != size) {
		fclose(stream);
		return NULL;
	}
	rewind(stream);
	return stream;
}

static FILE *wide_stream(const wchar_t *data, size_t size)
{
	FILE *stream = tmpfile();
	size_t i;
	if (stream == NULL)
		return NULL;
	if (fwide(stream, 1) < 0) {
		fclose(stream);
		return NULL;
	}
	for (i = 0; i < size; i++) {
		if (fputwc(data[i], stream) == WEOF) {
			fclose(stream);
			return NULL;
		}
	}
	rewind(stream);
	return stream;
}

static int run_narrow_case(const unsigned char *input, size_t input_size,
					   int expected_length, const unsigned char *expected)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	char *line = NULL;
	int capacity = 0;
	int result = -1;

	stream = narrow_stream(input, input_size);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == expected_length);
	if (expected_length >= 0) {
		TEST_REQUIRE(memcmp(line, expected, (size_t)expected_length) == 0);
		TEST_REQUIRE(line[expected_length] == '\0');
	}
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int run_wide_case(const wchar_t *input, size_t input_size,
					 int expected_length, const wchar_t *expected)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	wchar_t *line = NULL;
	int capacity = 0;
	int result = -1;

	stream = wide_stream(input, input_size);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(WGetLine(&line, &capacity, stream, &allocator) == expected_length);
	if (expected_length >= 0) {
		TEST_REQUIRE(wmemcmp(line, expected, (size_t)expected_length) == 0);
		TEST_REQUIRE(line[expected_length] == L'\0');
	}
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int test_empty_and_basic_narrow_lines(void)
{
	static const unsigned char empty_line[] = "\n";
	static const unsigned char one_line[] = "x\n";
	static const unsigned char ordinary_line[] = "ordinary line\n";
	static const unsigned char final_line[] = "final line";

	if (run_narrow_case(empty_line, sizeof(empty_line) - 1, 0,
					(unsigned char *)"") != 0)
		return -1;
	if (run_narrow_case(one_line, sizeof(one_line) - 1, 1,
					(unsigned char *)"x") != 0)
		return -1;
	if (run_narrow_case(ordinary_line, sizeof(ordinary_line) - 1, 13,
					(unsigned char *)"ordinary line") != 0)
		return -1;
	if (run_narrow_case(final_line, sizeof(final_line) - 1, 10,
					(unsigned char *)"final line") != 0)
		return -1;
	return 0;
}

static int test_empty_and_basic_wide_lines(void)
{
	static const wchar_t empty_line[] = L"\n";
	static const wchar_t one_line[] = L"x\n";
	static const wchar_t ordinary_line[] = L"ordinary line\n";
	static const wchar_t final_line[] = L"final line";

	if (run_wide_case(empty_line, sizeof(empty_line) / sizeof(*empty_line) - 1,
				  0, L"") != 0)
		return -1;
	if (run_wide_case(one_line, sizeof(one_line) / sizeof(*one_line) - 1,
				  1, L"x") != 0)
		return -1;
	if (run_wide_case(ordinary_line,
				  sizeof(ordinary_line) / sizeof(*ordinary_line) - 1,
				  13, L"ordinary line") != 0)
		return -1;
	if (run_wide_case(final_line, sizeof(final_line) / sizeof(*final_line) - 1,
				  10, L"final line") != 0)
		return -1;
	return 0;
}

static int test_empty_file_allocates_usable_buffer(void)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	char *line = NULL;
	int capacity = 0;
	int result = -1;

	stream = narrow_stream(NULL, 0);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == EOF);
	TEST_REQUIRE(line != NULL);
	TEST_REQUIRE(capacity == BUFSIZ);
	TEST_REQUIRE(state.realloc_calls == 1);
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int test_embedded_nul_is_counted(void)
{
	static const unsigned char input[] = {'a', '\0', 'b', '\n'};
	static const unsigned char expected[] = {'a', '\0', 'b'};
	wchar_t wide_input[] = {L'a', L'\0', L'b', L'\n'};
	wchar_t wide_expected[] = {L'a', L'\0', L'b'};

	if (run_narrow_case(input, sizeof(input), 3, expected) != 0)
		return -1;
	if (run_wide_case(wide_input, sizeof(wide_input) / sizeof(*wide_input),
				  3, wide_expected) != 0)
		return -1;
	return 0;
}

static int test_caller_buffers_are_reused(void)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	static const unsigned char narrow_input[] = "caller buffer\n";
	static const wchar_t wide_input[] = L"wide caller\n";
	FILE *stream = NULL;
	char narrow_storage[64];
	wchar_t wide_storage[64];
	char *line = narrow_storage;
	wchar_t *wide_line = wide_storage;
	int capacity = (int)sizeof(narrow_storage);
	int wide_capacity = (int)(sizeof(wide_storage) / sizeof(*wide_storage));
	int result = -1;

	stream = narrow_stream(narrow_input, sizeof(narrow_input) - 1);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == 13);
	TEST_REQUIRE(line == narrow_storage);
	TEST_REQUIRE(strcmp(line, "caller buffer") == 0);
	TEST_REQUIRE(state.realloc_calls == 0);
	fclose(stream);
	stream = wide_stream(wide_input, sizeof(wide_input) / sizeof(*wide_input) - 1);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(WGetLine(&wide_line, &wide_capacity, stream, &allocator) == 11);
	TEST_REQUIRE(wide_line == wide_storage);
	TEST_REQUIRE(wcscmp(wide_line, L"wide caller") == 0);
	TEST_REQUIRE(state.realloc_calls == 0);
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	return result;
}

static int test_cross_capacity_growth(void)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	char *line = NULL;
	int capacity = 0;
	int i;
	int result = -1;

	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	for (i = 0; i < BUFSIZ * 2 + 17; i++)
		TEST_REQUIRE(fputc('g', stream) != EOF);
	rewind(stream);
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == BUFSIZ * 2 + 17);
	TEST_REQUIRE(capacity == BUFSIZ * 4);
	TEST_REQUIRE(state.realloc_calls == 3);
	TEST_REQUIRE(line[0] == 'g' && line[BUFSIZ * 2 + 16] == 'g');
	TEST_REQUIRE(line[BUFSIZ * 2 + 17] == '\0');
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int test_exact_capacity_narrow_lines(void)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	char *line = NULL;
	int capacity = 0;
	int i;
	int result = -1;

	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	for (i = 0; i < BUFSIZ; i++)
		TEST_REQUIRE(fputc('n', stream) != EOF);
	rewind(stream);
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == BUFSIZ);
	TEST_REQUIRE(capacity == BUFSIZ + 1);
	TEST_REQUIRE(line[BUFSIZ] == '\0');
	TEST_REQUIRE(state.realloc_calls == 2);
	allocator.free(line);
	line = NULL;
	fclose(stream);
	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	for (i = 0; i < BUFSIZ; i++)
		TEST_REQUIRE(fputc('d', stream) != EOF);
	TEST_REQUIRE(fputc('\n', stream) != EOF);
	rewind(stream);
	line = NULL;
	capacity = 0;
	state.realloc_calls = 0;
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == BUFSIZ);
	TEST_REQUIRE(capacity == BUFSIZ * 2);
	TEST_REQUIRE(line[BUFSIZ] == '\0');
	TEST_REQUIRE(state.realloc_calls == 2);
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int test_exact_capacity_wide_lines(void)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	wchar_t *line = NULL;
	int capacity = 0;
	int i;
	int result = -1;

	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(fwide(stream, 1) > 0);
	for (i = 0; i < BUFSIZ; i++)
		TEST_REQUIRE(fputwc(L'w', stream) != WEOF);
	rewind(stream);
	TEST_REQUIRE(WGetLine(&line, &capacity, stream, &allocator) == BUFSIZ);
	TEST_REQUIRE(capacity == BUFSIZ + 1);
	TEST_REQUIRE(line[BUFSIZ] == L'\0');
	TEST_REQUIRE(state.realloc_calls == 2);
	allocator.free(line);
	line = NULL;
	fclose(stream);
	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(fwide(stream, 1) > 0);
	for (i = 0; i < BUFSIZ; i++)
		TEST_REQUIRE(fputwc(L'd', stream) != WEOF);
	TEST_REQUIRE(fputwc(L'\n', stream) != WEOF);
	rewind(stream);
	capacity = 0;
	state.realloc_calls = 0;
	TEST_REQUIRE(WGetLine(&line, &capacity, stream, &allocator) == BUFSIZ);
	TEST_REQUIRE(capacity == BUFSIZ * 2);
	TEST_REQUIRE(line[BUFSIZ] == L'\0');
	TEST_REQUIRE(state.realloc_calls == 2);
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int test_allocation_failures(void)
{
	static const unsigned char short_input[] = "failure\n";
	AllocatorState state = {0, 0, 1};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	char *line = NULL;
	int capacity = 0;
	int i;
	int result = -1;

	stream = narrow_stream(short_input, sizeof(short_input) - 1);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(line == NULL);
	TEST_REQUIRE(state.realloc_calls == 1);
	fclose(stream);
	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	for (i = 0; i < BUFSIZ + 1; i++)
		TEST_REQUIRE(fputc('g', stream) != EOF);
	rewind(stream);
	line = NULL;
	capacity = 0;
	state.realloc_calls = 0;
	state.free_calls = 0;
	state.fail_at = 2;
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(line == NULL);
	TEST_REQUIRE(state.realloc_calls == 2 && state.free_calls == 1);
	fclose(stream);
	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	for (i = 0; i < BUFSIZ; i++)
		TEST_REQUIRE(fputc('f', stream) != EOF);
	rewind(stream);
	line = NULL;
	capacity = 0;
	state.realloc_calls = 0;
	state.free_calls = 0;
	state.fail_at = 2;
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(line == NULL);
	TEST_REQUIRE(state.realloc_calls == 2 && state.free_calls == 1);
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int test_wide_allocation_failures(void)
{
	AllocatorState state = {0, 0, 1};
	ContainerAllocator allocator = test_allocator(&state);
	FILE *stream = NULL;
	wchar_t *line = NULL;
	int capacity = 0;
	int i;
	int result = -1;

	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(fwide(stream, 1) > 0);
	TEST_REQUIRE(fputwc(L'x', stream) != WEOF);
	rewind(stream);
	TEST_REQUIRE(WGetLine(&line, &capacity, stream, &allocator) ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(line == NULL);
	fclose(stream);
	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(fwide(stream, 1) > 0);
	for (i = 0; i < BUFSIZ + 1; i++)
		TEST_REQUIRE(fputwc(L'g', stream) != WEOF);
	rewind(stream);
	line = NULL;
	capacity = 0;
	state.realloc_calls = 0;
	state.free_calls = 0;
	state.fail_at = 2;
	TEST_REQUIRE(WGetLine(&line, &capacity, stream, &allocator) ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(line == NULL);
	TEST_REQUIRE(state.realloc_calls == 2 && state.free_calls == 1);
	fclose(stream);
	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(fwide(stream, 1) > 0);
	for (i = 0; i < BUFSIZ; i++)
		TEST_REQUIRE(fputwc(L'f', stream) != WEOF);
	rewind(stream);
	line = NULL;
	capacity = 0;
	state.realloc_calls = 0;
	state.free_calls = 0;
	state.fail_at = 2;
	TEST_REQUIRE(WGetLine(&line, &capacity, stream, &allocator) ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(line == NULL);
	TEST_REQUIRE(state.realloc_calls == 2 && state.free_calls == 1);
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (line != NULL)
		allocator.free(line);
	return result;
}

static int test_invalid_arguments(void)
{
	AllocatorState state = {0, 0, 0};
	ContainerAllocator allocator = test_allocator(&state);
	ErrorFunction old_error = NULL;
	FILE *stream = NULL;
	char *line = NULL;
	char storage[8];
	int capacity;
	int result = -1;

	stream = narrow_stream(NULL, 0);
	TEST_REQUIRE(stream != NULL);
	old_error = iError.SetErrorFunction(capture_error);
	captured_errors = 0;
	capacity = 0;
	TEST_REQUIRE(GetLine(&line, &capacity, NULL, &allocator) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(GetLine(NULL, &capacity, stream, &allocator) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(GetLine(&line, NULL, stream, &allocator) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(GetLine(&line, &capacity, stream, NULL) == CONTAINER_ERROR_BADARG);
	line = storage;
	capacity = 0;
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == CONTAINER_ERROR_BADARG);
	line = NULL;
	capacity = -1;
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == CONTAINER_ERROR_BADARG);
	line = storage;
	capacity = -1;
	TEST_REQUIRE(GetLine(&line, &capacity, stream, &allocator) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(captured_errors == 7);
	iError.SetErrorFunction(old_error);
	result = 0;

cleanup:
	iError.SetErrorFunction(old_error);
	if (stream != NULL)
		fclose(stream);
	return result;
}

static int test_wide_non_ascii_content(void)
{
	const char *old_locale;
	char old_locale_copy[128];
	static const wchar_t input[] = {L'c', 0x03a9, L'f', L'\n'};
	static const wchar_t expected[] = {L'c', 0x03a9, L'f'};
	old_locale = setlocale(LC_CTYPE, NULL);
	if (old_locale != NULL) {
		strncpy(old_locale_copy, old_locale, sizeof(old_locale_copy) - 1);
		old_locale_copy[sizeof(old_locale_copy) - 1] = '\0';
	}
	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL &&
		setlocale(LC_CTYPE, "en_US.UTF-8") == NULL)
		return 0;
	if (run_wide_case(input, sizeof(input) / sizeof(*input) - 1, 3,
				  expected) != 0)
		return -1;
	if (old_locale != NULL)
		setlocale(LC_CTYPE, old_locale_copy);
	return 0;
}

static TestCase fgetline_tests[] = {
	{"empty and basic narrow lines", test_empty_and_basic_narrow_lines},
	{"empty and basic wide lines", test_empty_and_basic_wide_lines},
	{"empty file allocates usable buffer", test_empty_file_allocates_usable_buffer},
	{"embedded NUL is counted", test_embedded_nul_is_counted},
	{"caller buffers are reused", test_caller_buffers_are_reused},
	{"cross-capacity growth", test_cross_capacity_growth},
	{"exact-capacity narrow lines", test_exact_capacity_narrow_lines},
	{"exact-capacity wide lines", test_exact_capacity_wide_lines},
	{"narrow allocation failures", test_allocation_failures},
	{"wide allocation failures", test_wide_allocation_failures},
	{"invalid arguments", test_invalid_arguments},
	{"wide non-ASCII content", test_wide_non_ascii_content}
};

static TestSuite fgetline_suite = {
	"fgetline",
	fgetline_tests,
	(int)(sizeof(fgetline_tests) / sizeof(fgetline_tests[0]))
};

const TestSuite *ccl_get_test_suite(void)
{
	return &fgetline_suite;
}
