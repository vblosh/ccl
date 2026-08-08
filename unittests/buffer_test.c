#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "containers.h"
#include "test_support.h"

static int error_count;
static int last_error;

static void *capture_error(const char *name, int code, ...)
{
	(void)name;
	error_count++;
	last_error = code;
	return NULL;
}

static int test_stream_basic(void)
{
	StreamBuffer *buffer = NULL;
	StreamBuffer *default_buffer = NULL;
	char output[16];
	const char input[] = "abcd";
	int result = -1;

	buffer = iStreamBuffer.Create(4);
	TEST_REQUIRE(buffer != NULL);
	default_buffer = iStreamBuffer.Create(0);
	TEST_REQUIRE(default_buffer != NULL && iStreamBuffer.Size(default_buffer) == 1024);
	TEST_REQUIRE(iStreamBuffer.Size(buffer) == 4);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == 0);
	TEST_REQUIRE(iStreamBuffer.GetData(buffer)[0] == 0);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, (void *)input, 4) == 4);
	TEST_REQUIRE(iStreamBuffer.Size(buffer) == 4);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == 4);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, (void *)"ef", 2) == 2);
	TEST_REQUIRE(iStreamBuffer.Size(buffer) >= 6);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == 6);
	TEST_REQUIRE(iStreamBuffer.SetPosition(buffer, 0) == 1);
	memset(output, 0, sizeof(output));
	TEST_REQUIRE(iStreamBuffer.Read(buffer, output, 6) == 6);
	TEST_REQUIRE(memcmp(output, "abcdef", 6) == 0);
	TEST_REQUIRE(iStreamBuffer.SetPosition(buffer, 2) == 1);
	TEST_REQUIRE(iStreamBuffer.Read(buffer, output, iStreamBuffer.Size(buffer)) ==
			iStreamBuffer.Size(buffer) - 2);
	TEST_REQUIRE(iStreamBuffer.Read(buffer, output, 1) == 0);
	TEST_REQUIRE(iStreamBuffer.SetPosition(buffer, 999) == 1);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == iStreamBuffer.Size(buffer));
	TEST_REQUIRE(iStreamBuffer.Clear(buffer) == 1);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == 0);
	for (size_t i = 0; i < iStreamBuffer.Size(buffer); i++)
		TEST_REQUIRE(iStreamBuffer.GetData(buffer)[i] == 0);
	result = 0;

cleanup:
	if (buffer != NULL)
		iStreamBuffer.Finalize(buffer);
	if (default_buffer != NULL)
		iStreamBuffer.Finalize(default_buffer);
	return result;
}

static int test_stream_resize_and_overflow(void)
{
	StreamBuffer *buffer = NULL;
	char output[8];
	const char input[] = "12345678";
	ErrorFunction saved_handler;
	int result = -1;

	error_count = 0;
	last_error = 0;
	saved_handler = iError.SetErrorFunction(capture_error);
	buffer = iStreamBuffer.CreateWithAllocator(8, iAllocator.GetCurrent());
	TEST_REQUIRE(buffer != NULL);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, (void *)input, 8) == 8);
	TEST_REQUIRE(iStreamBuffer.Resize(buffer, 8) == 0);
	TEST_REQUIRE(iStreamBuffer.SetPosition(buffer, 8) == 1);
	TEST_REQUIRE(iStreamBuffer.Resize(buffer, 1) == 1);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == 1);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, (void *)"Z", 1) == 1);
	TEST_REQUIRE(iStreamBuffer.Size(buffer) >= 2);
	TEST_REQUIRE(iStreamBuffer.SetPosition(buffer, 0) == 1);
	memset(output, 0, sizeof(output));
	TEST_REQUIRE(iStreamBuffer.Read(buffer, output, 2) == 2);
	TEST_REQUIRE(output[0] == '1' && output[1] == 'Z');
	TEST_REQUIRE(iStreamBuffer.Resize(buffer, 0) == 1);
	TEST_REQUIRE(iStreamBuffer.Size(buffer) == 0);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == 0);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, (void *)"x", 1) == 1);
	TEST_REQUIRE(iStreamBuffer.GetData(buffer)[0] == 'x');
	TEST_REQUIRE(iStreamBuffer.SetPosition(buffer, 1) == 1);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, (void *)"x", SIZE_MAX) == 0);
	TEST_REQUIRE(last_error == CONTAINER_ERROR_BUFFEROVERFLOW);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, NULL, 1) == 0);
	TEST_REQUIRE(last_error == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iStreamBuffer.Write(buffer, NULL, 0) == 0);
	result = 0;

cleanup:
	iError.SetErrorFunction(saved_handler);
	if (buffer != NULL)
		iStreamBuffer.Finalize(buffer);
	return result;
}

static int test_stream_files(void)
{
	StreamBuffer *buffer = NULL;
	StreamBuffer *from_file = NULL;
	FILE *stream = NULL;
	FILE *output = NULL;
	char path[128];
	unsigned char input[] = { 0, 1, 2, 0xff, 4 };
	unsigned char output_data[8];
	int result = -1;

	(void)snprintf(path, sizeof(path), "/tmp/ccl_buffer_%ld.bin", (long)getpid());
	output = fopen(path, "wb");
	TEST_REQUIRE(output != NULL);
	TEST_REQUIRE(fwrite(input, 1, sizeof(input), output) == sizeof(input));
	TEST_REQUIRE(fclose(output) == 0);
	output = NULL;
	from_file = iStreamBuffer.CreateFromFile(path);
	TEST_REQUIRE(from_file != NULL);
	TEST_REQUIRE(iStreamBuffer.Size(from_file) == sizeof(input) + 1);
	TEST_REQUIRE(memcmp(iStreamBuffer.GetData(from_file), input, sizeof(input)) == 0);
	TEST_REQUIRE(iStreamBuffer.GetData(from_file)[sizeof(input)] == 0);

	buffer = iStreamBuffer.Create(5);
	TEST_REQUIRE(buffer != NULL);
	stream = tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(fwrite(input, 1, 2, stream) == 2);
	rewind(stream);
	TEST_REQUIRE(iStreamBuffer.ReadFromFile(buffer, stream) == 2);
	TEST_REQUIRE(iStreamBuffer.GetPosition(buffer) == 0);
	TEST_REQUIRE(memcmp(iStreamBuffer.GetData(buffer), input, 2) == 0);
	TEST_REQUIRE(iStreamBuffer.ReadFromFile(buffer, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iStreamBuffer.WriteToFile(buffer, NULL) == CONTAINER_ERROR_BADARG);
	output = tmpfile();
	TEST_REQUIRE(output != NULL);
	TEST_REQUIRE(iStreamBuffer.WriteToFile(buffer, output) == 5);
	rewind(output);
	memset(output_data, 0, sizeof(output_data));
	TEST_REQUIRE(fread(output_data, 1, 5, output) == 5);
	TEST_REQUIRE(memcmp(output_data, input, 2) == 0);
	result = 0;

cleanup:
	if (stream != NULL)
		fclose(stream);
	if (output != NULL)
		fclose(output);
	if (buffer != NULL)
		iStreamBuffer.Finalize(buffer);
	if (from_file != NULL)
		iStreamBuffer.Finalize(from_file);
	(void)unlink(path);
	return result;
}

typedef struct {
	int malloc_calls;
	int realloc_calls;
	int free_calls;
	int fail_realloc;
	int fail_malloc_call;
} AllocatorState;

static AllocatorState *active_allocator_state;

static void *buffer_malloc(size_t size)
{
	active_allocator_state->malloc_calls++;
	if (active_allocator_state->fail_malloc_call > 0 &&
		active_allocator_state->malloc_calls == active_allocator_state->fail_malloc_call)
		return NULL;
	return malloc(size);
}

static void *buffer_realloc(void *ptr, size_t size)
{
	active_allocator_state->realloc_calls++;
	if (active_allocator_state->fail_realloc)
		return NULL;
	return realloc(ptr, size);
}

static void buffer_free(void *ptr)
{
	if (ptr != NULL)
		active_allocator_state->free_calls++;
	free(ptr);
}

static void *buffer_calloc(size_t n, size_t size)
{
	return calloc(n, size);
}

static int test_allocator_retention_and_failure(void)
{
	AllocatorState state = {0, 0, 0, 0, 0};
	ContainerAllocator allocator = {
		buffer_malloc, buffer_free, buffer_realloc, buffer_calloc
	};
	ContainerAllocator *saved = iAllocator.GetCurrent();
	StreamBuffer *stream = NULL;
	CircularBuffer *ring = NULL;
	char value = 'q';
	int circular_value = 17;
	int result = -1;

	active_allocator_state = &state;
	state.fail_malloc_call = 1;
	TEST_REQUIRE(iStreamBuffer.CreateWithAllocator(4, &allocator) == NULL);
	state.malloc_calls = 0;
	state.fail_malloc_call = 2;
	TEST_REQUIRE(iStreamBuffer.CreateWithAllocator(4, &allocator) == NULL);
	state.fail_malloc_call = 0;
	stream = iStreamBuffer.CreateWithAllocator(4, &allocator);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(iStreamBuffer.Write(stream, &value, 1) == 1);
	state.fail_realloc = 1;
	TEST_REQUIRE(iStreamBuffer.SetPosition(stream, 4) == 1);
	TEST_REQUIRE(iStreamBuffer.Write(stream, &value, 1) == 0);
	TEST_REQUIRE(iStreamBuffer.Resize(stream, 100) == CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(iStreamBuffer.Size(stream) == 4);
	TEST_REQUIRE(iStreamBuffer.GetData(stream)[0] == value);
	state.fail_realloc = 0;
	iAllocator.Change(&allocator);
	TEST_REQUIRE(iStreamBuffer.Finalize(stream) == 1);
	stream = NULL;
	TEST_REQUIRE(state.free_calls >= 2);
	/* Circular creation must retain its allocator after CurrentAllocator changes. */
	iAllocator.Change(saved);
	ring = iCircularBuffer.CreateWithAllocator(sizeof(int), 2, &allocator);
	TEST_REQUIRE(ring != NULL);
	TEST_REQUIRE(iCircularBuffer.Sizeof(ring) > iCircularBuffer.Size(ring));
	TEST_REQUIRE(iCircularBuffer.Add(ring, &circular_value) == 1);
	TEST_REQUIRE(iCircularBuffer.Clear(ring) == 1);
	TEST_REQUIRE(iCircularBuffer.Finalize(ring) == 1);
	ring = NULL;
	state.malloc_calls = 0;
	state.fail_malloc_call = 1;
	TEST_REQUIRE(iCircularBuffer.CreateWithAllocator(sizeof(int), 2, &allocator) == NULL);
	state.malloc_calls = 0;
	state.fail_malloc_call = 2;
	TEST_REQUIRE(iCircularBuffer.CreateWithAllocator(sizeof(int), 2, &allocator) == NULL);
	state.fail_malloc_call = 0;
	TEST_REQUIRE(iCircularBuffer.CreateWithAllocator(2, SIZE_MAX, &allocator) == NULL);
	result = 0;

cleanup:
	if (stream != NULL)
		iStreamBuffer.Finalize(stream);
	if (ring != NULL)
		iCircularBuffer.Finalize(ring);
	iAllocator.Change(saved);
	return result;
}

static int destructor_values[32];
static size_t destructor_count;

static int record_destructor(void *value)
{
	if (destructor_count < sizeof(destructor_values) / sizeof(destructor_values[0]))
		destructor_values[destructor_count++] = *(int *)value;
	return 1;
}

static int test_circular_fifo_and_destructors(void)
{
	CircularBuffer *ring = NULL;
	CircularBuffer *single = NULL;
	CircularBuffer *wrapped = NULL;
	int value;
	int result = -1;

	destructor_count = 0;
	ring = iCircularBuffer.Create(sizeof(int), 3);
	TEST_REQUIRE(ring != NULL);
	TEST_REQUIRE(iCircularBuffer.SetDestructor(ring, record_destructor) == NULL);
	for (value = 1; value <= 3; value++)
		TEST_REQUIRE(iCircularBuffer.Add(ring, &value) == 1);
	TEST_REQUIRE(iCircularBuffer.Size(ring) == 3);
	value = 4;
	TEST_REQUIRE(iCircularBuffer.Add(ring, &value) == 0);
	TEST_REQUIRE(iCircularBuffer.Size(ring) == 3);
	TEST_REQUIRE(iCircularBuffer.PeekFront(ring, &value) == 1 && value == 2);
	TEST_REQUIRE(iCircularBuffer.PopFront(ring, NULL) == 1);
	TEST_REQUIRE(iCircularBuffer.PopFront(ring, &value) == 1 && value == 3);
	TEST_REQUIRE(iCircularBuffer.PopFront(ring, &value) == 1 && value == 4);
	TEST_REQUIRE(iCircularBuffer.PopFront(ring, &value) == 0);
	TEST_REQUIRE(iCircularBuffer.PeekFront(ring, &value) == 0);
	TEST_REQUIRE(destructor_count == 4);
	TEST_REQUIRE(destructor_values[0] == 1 && destructor_values[1] == 2 &&
			destructor_values[2] == 3 && destructor_values[3] == 4);
	TEST_REQUIRE(iCircularBuffer.SetDestructor(ring, NULL) == record_destructor);
	TEST_REQUIRE(iCircularBuffer.SetDestructor(ring, record_destructor) == NULL);
	single = iCircularBuffer.Create(sizeof(int), 1);
	TEST_REQUIRE(single != NULL);
	value = 1;
	TEST_REQUIRE(iCircularBuffer.Add(single, &value) == 1);
	value = 2;
	TEST_REQUIRE(iCircularBuffer.Add(single, &value) == 0);
	TEST_REQUIRE(iCircularBuffer.Size(single) == 1);
	TEST_REQUIRE(iCircularBuffer.PeekFront(single, &value) == 1 && value == 2);
	TEST_REQUIRE(iCircularBuffer.PopFront(single, &value) == 1 && value == 2);
	TEST_REQUIRE(iCircularBuffer.Size(single) == 0);

	destructor_count = 0;
	wrapped = iCircularBuffer.Create(sizeof(int), 3);
	TEST_REQUIRE(wrapped != NULL);
	TEST_REQUIRE(iCircularBuffer.SetDestructor(wrapped, record_destructor) == NULL);
	for (value = 1; value <= 3; value++)
		TEST_REQUIRE(iCircularBuffer.Add(wrapped, &value) == 1);
	TEST_REQUIRE(iCircularBuffer.PopFront(wrapped, &value) == 1 && value == 1);
	for (value = 4; value <= 4; value++)
		TEST_REQUIRE(iCircularBuffer.Add(wrapped, &value) == 1);
	TEST_REQUIRE(iCircularBuffer.Size(wrapped) == 3);
	TEST_REQUIRE(iCircularBuffer.Clear(wrapped) == 1);
	TEST_REQUIRE(iCircularBuffer.Size(wrapped) == 0);
	TEST_REQUIRE(destructor_count == 4);
	TEST_REQUIRE(destructor_values[0] == 1 && destructor_values[1] == 2 &&
			destructor_values[2] == 3 && destructor_values[3] == 4);
	/* A newly added item after Clear is destroyed by Finalize. */
	value = 5;
	TEST_REQUIRE(iCircularBuffer.Add(wrapped, &value) == 1);
	TEST_REQUIRE(iCircularBuffer.Finalize(wrapped) == 1);
	wrapped = NULL;
	TEST_REQUIRE(destructor_count == 5);
	result = 0;

cleanup:
	if (ring != NULL)
		iCircularBuffer.Finalize(ring);
	if (single != NULL)
		iCircularBuffer.Finalize(single);
	if (wrapped != NULL)
		iCircularBuffer.Finalize(wrapped);
	return result;
}

static int test_buffer_bad_arguments(void)
{
	StreamBuffer *stream = NULL;
	CircularBuffer *ring = NULL;
	int value = 1;
	int result = -1;

	error_count = 0;
	TEST_REQUIRE(iStreamBuffer.GetPosition(NULL) == 0);
	TEST_REQUIRE(iStreamBuffer.GetData(NULL) == NULL);
	TEST_REQUIRE(iStreamBuffer.Size(NULL) > 0);
	TEST_REQUIRE(iCircularBuffer.Size(NULL) == iCircularBuffer.Sizeof(NULL));
	TEST_REQUIRE(iStreamBuffer.CreateWithAllocator(1, NULL) == NULL);
	TEST_REQUIRE(iStreamBuffer.CreateFromFile(NULL) == NULL);
	TEST_REQUIRE(iStreamBuffer.Read(NULL, &value, 1) == 0);
	TEST_REQUIRE(iStreamBuffer.Write(NULL, &value, 1) == 0);
	TEST_REQUIRE(iStreamBuffer.Resize(NULL, 1) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iStreamBuffer.Clear(NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iStreamBuffer.Finalize(NULL) == CONTAINER_ERROR_BADARG);
	ring = iCircularBuffer.Create(sizeof(int), 1);
	TEST_REQUIRE(ring != NULL);
	TEST_REQUIRE(iCircularBuffer.CreateWithAllocator(0, 1, iAllocator.GetCurrent()) == NULL);
	TEST_REQUIRE(iCircularBuffer.CreateWithAllocator(1, 0, iAllocator.GetCurrent()) == NULL);
	TEST_REQUIRE(iCircularBuffer.CreateWithAllocator(1, 1, NULL) == NULL);
	TEST_REQUIRE(iCircularBuffer.Add(ring, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iCircularBuffer.PeekFront(ring, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iCircularBuffer.PopFront(ring, NULL) == 0);
	TEST_REQUIRE(iCircularBuffer.SetDestructor(NULL, NULL) == NULL);
	TEST_REQUIRE(iCircularBuffer.Finalize(NULL) == CONTAINER_ERROR_BADARG);
	stream = iStreamBuffer.Create(1);
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(iStreamBuffer.ReadFromFile(stream, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iStreamBuffer.WriteToFile(stream, NULL) == CONTAINER_ERROR_BADARG);
	result = 0;

cleanup:
	if (stream != NULL)
		iStreamBuffer.Finalize(stream);
	if (ring != NULL)
		iCircularBuffer.Finalize(ring);
	return result;
}

static const TestCase buffer_tests[] = {
	{ "stream basic", test_stream_basic },
	{ "stream resize and overflow", test_stream_resize_and_overflow },
	{ "stream files", test_stream_files },
	{ "allocator retention and failure", test_allocator_retention_and_failure },
	{ "circular fifo and destructors", test_circular_fifo_and_destructors },
	{ "bad arguments", test_buffer_bad_arguments },
};

const TestSuite *ccl_get_test_suite(void)
{
	static const TestSuite suite = {
		"buffer", buffer_tests, sizeof(buffer_tests) / sizeof(buffer_tests[0])
	};
	return &suite;
}
