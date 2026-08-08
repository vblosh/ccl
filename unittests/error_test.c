#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "containers.h"
#include "test_support.h"

/*
 * Successful AddError calls intentionally use a static pool.  The error
 * registry has no removal API, so using malloc here would turn the registry's
 * documented process lifetime into a leak under LeakSanitizer.
 */
typedef union {
	max_align_t alignment;
	unsigned char bytes[65536];
} ErrorTestStorage;

typedef struct {
	size_t offset;
	int fail_calloc;
	int fail_malloc;
	int calloc_calls;
	int malloc_calls;
	int free_calls;
} ErrorTestAllocatorState;

static ErrorTestStorage storage;
static ErrorTestAllocatorState allocator_state;
static ContainerAllocator error_test_allocator;
static ContainerAllocator *saved_allocator;
static ErrorFunction saved_handler;

static void *error_test_pool_alloc(size_t size, int clear)
{
	size_t alignment = sizeof(max_align_t);
	size_t aligned_offset = (allocator_state.offset + alignment - 1) /
		alignment * alignment;
	void *result;

	if (aligned_offset > sizeof(storage.bytes) ||
		size > sizeof(storage.bytes) - aligned_offset)
		return NULL;
	result = storage.bytes + aligned_offset;
	allocator_state.offset = aligned_offset + size;
	if (clear)
		memset(result, 0, size);
	return result;
}

static void *error_test_malloc(size_t size)
{
	allocator_state.malloc_calls++;
	if (allocator_state.fail_malloc)
		return NULL;
	return error_test_pool_alloc(size, 0);
}

static void *error_test_calloc(size_t count, size_t size)
{
	allocator_state.calloc_calls++;
	if (allocator_state.fail_calloc ||
		(count != 0 && size > (size_t)-1 / count))
		return NULL;
	return error_test_pool_alloc(count * size, 1);
}

static void *error_test_realloc(void *ptr, size_t size)
{
	(void)ptr;
	(void)size;
	return NULL;
}

static void error_test_free(void *ptr)
{
	if (ptr != NULL)
		allocator_state.free_calls++;
}

static void reset_allocator_counters(void)
{
	allocator_state.fail_calloc = 0;
	allocator_state.fail_malloc = 0;
	allocator_state.calloc_calls = 0;
	allocator_state.malloc_calls = 0;
	allocator_state.free_calls = 0;
}

static void begin_fixture(void)
{
	memset(&allocator_state, 0, sizeof(allocator_state));
	error_test_allocator.malloc = error_test_malloc;
	error_test_allocator.free = error_test_free;
	error_test_allocator.realloc = error_test_realloc;
	error_test_allocator.calloc = error_test_calloc;
	saved_allocator = iAllocator.Change(&error_test_allocator);
	saved_handler = iError.SetErrorFunction(NULL);
}

static void end_fixture(void)
{
	iError.SetErrorFunction(saved_handler);
	iAllocator.Change(saved_allocator);
}

static int test_builtin_messages(void)
{
	static const struct {
		int code;
		const char *message;
	} expected[] = {
		{CONTAINER_ERROR_NOTFOUND, "Object not found"},
		{CONTAINER_ERROR_INDEX, "Index error"},
		{CONTAINER_ERROR_READONLY, "Object is read only"},
		{CONTAINER_ERROR_FILEOPEN, "Error opening the file"},
		{CONTAINER_ERROR_WRONGFILE, "Not a container stream"},
		{CONTAINER_ERROR_NOTIMPLEMENTED,
		 "Function not implemented for this container type"},
		{CONTAINER_INTERNAL_ERROR, "Internal error in container library"},
		{CONTAINER_ERROR_OBJECT_CHANGED, "Iterator used with modified object"},
		{CONTAINER_ERROR_NOT_EMPTY, "container is not empty"},
		{CONTAINER_ERROR_FILE_READ, "read error in input stream"},
		{CONTAINER_ERROR_FILE_WRITE, "write error in output stream"},
		{CONTAINER_FULL, "Container is full"},
		{CONTAINER_ASSERTION_FAILED, "Assertion failed"},
		{CONTAINER_ERROR_BADARG, "Bad argument"},
		{CONTAINER_ERROR_NOMEMORY, "Insufficient memory"},
		{CONTAINER_ERROR_NOENT, "File not found"},
		{CONTAINER_ERROR_INCOMPATIBLE, "Incompatible element sizes"},
		{CONTAINER_ERROR_BADPOINTER, "Debug_malloc: BAD POINTER******"},
		{CONTAINER_ERROR_BUFFEROVERFLOW, "Debug_malloc: BUFFER OVERFLOW******"},
		{CONTAINER_ERROR_DIVISION_BY_ZERO, "Division by zero"},
		{CONTAINER_ERROR_WRONGELEMENT, "Wrong element passed to a list"},
		{CONTAINER_ERROR_BADMASK, "Incorrect mask length"},
		{CONTAINER_ERROR_WRONG_ITERATOR, "Wrong iterator"}
	};
	size_t i;
	int result = -1;

	begin_fixture();
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		TEST_REQUIRE(strcmp(iError.StrError(expected[i].code),
					expected[i].message) == 0);
	}
	TEST_REQUIRE(strcmp(iError.StrError(1234567), "Unknown error") == 0);
	result = 0;

cleanup:
	end_fixture();
	return result;
}

static int custom_error_calls;
static const char *custom_error_name;
static int custom_error_code;

static void *custom_error(const char *name, int code, ...)
{
	custom_error_calls++;
	custom_error_name = name;
	custom_error_code = code;
	return (void *)name;
}

static int test_handler_replacement_and_null_error(void)
{
	ErrorFunction original;
	int result = -1;

	begin_fixture();
	original = saved_handler;
	custom_error_calls = 0;
	custom_error_name = NULL;
	custom_error_code = 0;
	TEST_REQUIRE(iError.SetErrorFunction(custom_error) == original);
	TEST_REQUIRE(iError.SetErrorFunction(NULL) == custom_error);
	TEST_REQUIRE(iError.RaiseError == custom_error);
	TEST_REQUIRE(iError.NullPtrError("error_test.null") ==
				CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(custom_error_calls == 1);
	TEST_REQUIRE(strcmp(custom_error_name, "error_test.null") == 0);
	TEST_REQUIRE(custom_error_code == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iError.SetErrorFunction(original) == custom_error);
	TEST_REQUIRE(iError.RaiseError == original);
	result = 0;

cleanup:
	end_fixture();
	return result;
}

static int test_default_and_empty_handlers(void)
{
	FILE *stream = NULL;
	char output[256];
	int saved_stderr = -1;
	int redirected = 0;
	int result = -1;

	begin_fixture();
	stream = ccl_test_tmpfile();
	TEST_REQUIRE(stream != NULL);
	saved_stderr = dup(fileno(stderr));
	TEST_REQUIRE(saved_stderr >= 0);
	TEST_REQUIRE(dup2(fileno(stream), fileno(stderr)) >= 0);
	redirected = 1;
	TEST_REQUIRE(iError.RaiseError("error_test.default",
						  CONTAINER_ERROR_DIVISION_BY_ZERO) == NULL);
	fflush(stderr);
	TEST_REQUIRE(fseek(stream, 0, SEEK_SET) == 0);
	TEST_REQUIRE(fgets(output, sizeof(output), stream) != NULL);
	TEST_REQUIRE(strcmp(output,
			"Container library: Error 'Division by zero' in function "
			"error_test.default\n") == 0);
	TEST_REQUIRE(iError.EmptyErrorFunction("error_test.empty",
							CONTAINER_ERROR_WRONG_ITERATOR, 1, 2, 3) == NULL);
	result = 0;

cleanup:
	if (redirected) {
		fflush(stderr);
		(void)dup2(saved_stderr, fileno(stderr));
	}
	if (saved_stderr >= 0)
		close(saved_stderr);
	if (stream != NULL)
		fclose(stream);
	end_fixture();
	return result;
}

static int test_user_message_copy_and_shadowing(void)
{
	char message[] = "caller-owned message";
	int result = -1;

	begin_fixture();
	TEST_REQUIRE(iError.AddError(1001, message) == 1);
	message[0] = 'X';
	TEST_REQUIRE(strcmp(iError.StrError(1001), "caller-owned message") == 0);
	TEST_REQUIRE(iError.AddError(1002, "first registration") == 1);
	TEST_REQUIRE(strcmp(iError.StrError(1002), "first registration") == 0);
	TEST_REQUIRE(iError.AddError(1002, "newest registration") == 1);
	TEST_REQUIRE(strcmp(iError.StrError(1002), "newest registration") == 0);
	TEST_REQUIRE(iError.AddError(CONTAINER_ERROR_INDEX,
							"custom index message") == 1);
	TEST_REQUIRE(strcmp(iError.StrError(CONTAINER_ERROR_INDEX),
					"custom index message") == 0);
	result = 0;

cleanup:
	end_fixture();
	return result;
}

static int test_add_error_failures_and_bad_argument(void)
{
	int result = -1;

	begin_fixture();
	allocator_state.fail_calloc = 1;
	TEST_REQUIRE(iError.AddError(2001, "node failure") ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(allocator_state.calloc_calls == 1);
	TEST_REQUIRE(allocator_state.malloc_calls == 0);
	TEST_REQUIRE(allocator_state.free_calls == 0);
	TEST_REQUIRE(strcmp(iError.StrError(2001), "Unknown error") == 0);

	reset_allocator_counters();
	allocator_state.fail_malloc = 1;
	TEST_REQUIRE(iError.AddError(2002, "message failure") ==
				CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(allocator_state.calloc_calls == 1);
	TEST_REQUIRE(allocator_state.malloc_calls == 1);
	TEST_REQUIRE(allocator_state.free_calls == 1);
	TEST_REQUIRE(strcmp(iError.StrError(2002), "Unknown error") == 0);

	reset_allocator_counters();
	TEST_REQUIRE(iError.AddError(2003, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(allocator_state.calloc_calls == 0);
	TEST_REQUIRE(allocator_state.malloc_calls == 0);
	TEST_REQUIRE(allocator_state.free_calls == 0);
	TEST_REQUIRE(strcmp(iError.StrError(2003), "Unknown error") == 0);
	result = 0;

cleanup:
	end_fixture();
	return result;
}

static const TestCase tests[] = {
	{"built-in error messages", test_builtin_messages},
	{"handler replacement and NULL error", test_handler_replacement_and_null_error},
	{"default and empty handlers", test_default_and_empty_handlers},
	{"user message copy and shadowing", test_user_message_copy_and_shadowing},
	{"AddError failures and bad argument", test_add_error_failures_and_bad_argument}
};

static const TestSuite suite = {
	"error",
	tests,
	sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
	return &suite;
}
