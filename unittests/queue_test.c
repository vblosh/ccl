#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"

static const char *last_error_name;
static char last_error_name_storage[512];
static int last_error_code;

static void *capture_error(const char *name, int code, ...)
{
	if (name == NULL) {
		last_error_name = NULL;
	} else {
		strncpy(last_error_name_storage, name,
			sizeof(last_error_name_storage) - 1);
		last_error_name_storage[sizeof(last_error_name_storage) - 1] = '\0';
		last_error_name = last_error_name_storage;
	}
	last_error_code = code;
	return NULL;
}

static void clear_error(void)
{
	last_error_name = NULL;
	last_error_code = 0;
}

static int error_is(const char *name, int code)
{
	return last_error_name != NULL && last_error_code == code &&
		strcmp(last_error_name, name) == 0;
}

static int check_peeks(Queue *queue, size_t count, int front, int back)
{
	int result;

	TEST_REQUIRE(iQueue.Size(queue) == count);
	if (count == 0) {
		result = 0;
		TEST_REQUIRE(iQueue.Front(queue, &result) == 0);
		TEST_REQUIRE(iQueue.Back(queue, &result) == 0);
	} else {
		TEST_REQUIRE(iQueue.Front(queue, &result) == 1 && result == front);
		TEST_REQUIRE(iQueue.Back(queue, &result) == 1 && result == back);
	}
	return 0;

cleanup:
	return -1;
}

static int test_fifo_lifecycle_and_views(void)
{
	Queue *queue = NULL;
	List *items;
	int value;
	int result;
	size_t empty_size;

	queue = iQueue.Create(sizeof(int));
	TEST_REQUIRE(queue != NULL);
	empty_size = iQueue.Sizeof(queue);
	TEST_REQUIRE(empty_size > iQueue.Sizeof(NULL));
	TEST_REQUIRE(check_peeks(queue, 0, 0, 0) == 0);
	result = 1234;
	TEST_REQUIRE(iQueue.Dequeue(queue, &result) == 0 && result == 1234);

	value = 10;
	TEST_REQUIRE(iQueue.Enqueue(queue, &value) == 1);
	value = 99; /* Enqueue copies the value. */
	TEST_REQUIRE(check_peeks(queue, 1, 10, 10) == 0);
	TEST_REQUIRE(iQueue.Sizeof(queue) > empty_size);
	value = 20;
	TEST_REQUIRE(iQueue.Enqueue(queue, &value) == 1);
	value = 30;
	TEST_REQUIRE(iQueue.Enqueue(queue, &value) == 1);
	TEST_REQUIRE(check_peeks(queue, 3, 10, 30) == 0);

	items = iQueue.GetData(queue);
	TEST_REQUIRE(items != NULL && items == iQueue.GetData(queue));
	TEST_REQUIRE(iList.Size(items) == 3);
	TEST_REQUIRE(*(int *)iList.GetElement(items, 0) == 10);
	TEST_REQUIRE(*(int *)iList.GetElement(items, 1) == 20);
	TEST_REQUIRE(*(int *)iList.GetElement(items, 2) == 30);
	TEST_REQUIRE(iQueue.Sizeof(queue) ==
			iQueue.Sizeof(NULL) + iList.Sizeof(items));

	result = 0;
	TEST_REQUIRE(iQueue.Dequeue(queue, &result) == 1 && result == 10);
	TEST_REQUIRE(check_peeks(queue, 2, 20, 30) == 0);
	TEST_REQUIRE(iQueue.Dequeue(queue, &result) == 1 && result == 20);
	TEST_REQUIRE(check_peeks(queue, 1, 30, 30) == 0);
	TEST_REQUIRE(iQueue.Dequeue(queue, &result) == 1 && result == 30);
	TEST_REQUIRE(check_peeks(queue, 0, 0, 0) == 0);
	TEST_REQUIRE(iQueue.Finalize(queue) == 1);
	queue = NULL;
	return 0;

cleanup:
	if (queue != NULL)
		iQueue.Finalize(queue);
	return -1;
}

static int test_clear_and_reuse(void)
{
	Queue *queue = NULL;
	int values[] = { 4, 5, 6 };
	int value;

	queue = iQueue.Create(sizeof(int));
	TEST_REQUIRE(queue != NULL);
	TEST_REQUIRE(iQueue.Clear(queue) == 1);
	TEST_REQUIRE(check_peeks(queue, 0, 0, 0) == 0);
	TEST_REQUIRE(iQueue.Enqueue(queue, &values[0]) == 1);
	TEST_REQUIRE(iQueue.Enqueue(queue, &values[1]) == 1);
	TEST_REQUIRE(iQueue.Enqueue(queue, &values[2]) == 1);
	TEST_REQUIRE(iQueue.Clear(queue) == 1);
	TEST_REQUIRE(check_peeks(queue, 0, 0, 0) == 0);

	value = 77;
	TEST_REQUIRE(iQueue.Enqueue(queue, &value) == 1);
	value = 0;
	TEST_REQUIRE(iQueue.Dequeue(queue, &value) == 1 && value == 77);
	TEST_REQUIRE(iQueue.Clear(queue) == 1);
	TEST_REQUIRE(iQueue.Finalize(queue) == 1);
	queue = NULL;
	return 0;

cleanup:
	if (queue != NULL)
		iQueue.Finalize(queue);
	return -1;
}

static int test_null_arguments_and_forwarded_errors(void)
{
	Queue *queue = NULL;
	int value = 41;
	int output = 0;
	size_t static_size;
	ErrorFunction previous;
	int handler_active = 0;

	previous = iError.SetErrorFunction(capture_error);
	handler_active = 1;

	clear_error();
	static_size = iQueue.Sizeof(NULL);
	TEST_REQUIRE(static_size > 0 && iQueue.Sizeof(NULL) == static_size);
	TEST_REQUIRE(last_error_name == NULL);

	clear_error();
	TEST_REQUIRE(iQueue.Size(NULL) == 0 && error_is("iQueue.Size", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Enqueue(NULL, &value) == CONTAINER_ERROR_BADARG &&
			error_is("iQueue.Enqueue", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Dequeue(NULL, &output) == CONTAINER_ERROR_BADARG &&
			error_is("iQueue.Dequeue", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Clear(NULL) == CONTAINER_ERROR_BADARG &&
			error_is("iQueue.Clear", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Finalize(NULL) == CONTAINER_ERROR_BADARG &&
			error_is("iQueue.Finalize", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Front(NULL, &output) == CONTAINER_ERROR_BADARG &&
			error_is("iQueue.Front", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Back(NULL, &output) == CONTAINER_ERROR_BADARG &&
			error_is("iQueue.Back", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.GetData(NULL) == NULL &&
			error_is("iQueue.GetData", CONTAINER_ERROR_BADARG));

	queue = iQueue.Create(sizeof(int));
	TEST_REQUIRE(queue != NULL);
	TEST_REQUIRE(iQueue.Enqueue(queue, &value) == 1);
	clear_error();
	TEST_REQUIRE(iQueue.Enqueue(queue, NULL) == CONTAINER_ERROR_BADARG &&
			error_is("iList.Add", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Front(queue, NULL) == CONTAINER_ERROR_BADARG &&
			error_is("iList.CopyElement", CONTAINER_ERROR_BADARG));
	clear_error();
	TEST_REQUIRE(iQueue.Back(queue, NULL) == CONTAINER_ERROR_BADARG &&
			error_is("iList.CopyElement", CONTAINER_ERROR_BADARG));
	TEST_REQUIRE(iQueue.Dequeue(queue, NULL) == 1);
	TEST_REQUIRE(iQueue.Dequeue(queue, NULL) == 0);
	TEST_REQUIRE(iQueue.Finalize(queue) == 1);
	queue = NULL;

	iError.SetErrorFunction(previous);
	handler_active = 0;
	return 0;

cleanup:
	if (handler_active)
		iError.SetErrorFunction(previous);
	if (queue != NULL)
		iQueue.Finalize(queue);
	return -1;
}

typedef struct {
	size_t malloc_calls;
	size_t calloc_calls;
	size_t free_calls;
	size_t fail_malloc_at;
	size_t fail_calloc_at;
} AllocatorState;

static AllocatorState *active_allocator_state;

static void *queue_test_malloc(size_t size)
{
	++active_allocator_state->malloc_calls;
	if (active_allocator_state->fail_malloc_at != 0 &&
		active_allocator_state->malloc_calls >= active_allocator_state->fail_malloc_at)
		return NULL;
	return malloc(size);
}

static void *queue_test_calloc(size_t count, size_t size)
{
	++active_allocator_state->calloc_calls;
	if (active_allocator_state->fail_calloc_at != 0 &&
		active_allocator_state->calloc_calls >= active_allocator_state->fail_calloc_at)
		return NULL;
	return calloc(count, size);
}

static void *queue_test_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

static void queue_test_free(void *ptr)
{
	if (ptr != NULL)
		++active_allocator_state->free_calls;
	free(ptr);
}

static int test_allocator_failures_and_ownership(void)
{
	AllocatorState state_a = { 0, 0, 0, 0, 0 };
	AllocatorState state_b = { 0, 0, 0, 0, 0 };
	ContainerAllocator allocator_a = {
		queue_test_malloc, queue_test_free, queue_test_realloc, queue_test_calloc
	};
	ContainerAllocator allocator_b = {
		queue_test_malloc, queue_test_free, queue_test_realloc, queue_test_calloc
	};
	ContainerAllocator invalid_allocator = {
		NULL, queue_test_free, queue_test_realloc, queue_test_calloc
	};
	ContainerAllocator *saved_current = CurrentAllocator;
	ErrorFunction previous;
	Queue *queue = NULL;
	int value = 13;
	int handler_active = 0;

	previous = iError.SetErrorFunction(capture_error);
	handler_active = 1;

	active_allocator_state = &state_a;
	state_a.fail_malloc_at = 1;
	TEST_REQUIRE(iQueue.CreateWithAllocator(sizeof(int), &allocator_a) == NULL);
	TEST_REQUIRE(state_a.malloc_calls == 1 && state_a.free_calls == 0);

	memset(&state_a, 0, sizeof(state_a));
	state_a.fail_calloc_at = 1;
	TEST_REQUIRE(iQueue.CreateWithAllocator(sizeof(int), &allocator_a) == NULL);
	TEST_REQUIRE(state_a.malloc_calls == 1 && state_a.calloc_calls == 1 &&
			state_a.free_calls == 1);

	memset(&state_a, 0, sizeof(state_a));
	TEST_REQUIRE(iQueue.CreateWithAllocator(sizeof(int), &invalid_allocator) == NULL);

	memset(&state_a, 0, sizeof(state_a));
	memset(&state_b, 0, sizeof(state_b));
	active_allocator_state = &state_a;
	queue = iQueue.CreateWithAllocator(sizeof(int), &allocator_a);
	TEST_REQUIRE(queue != NULL);
	TEST_REQUIRE(iList.GetAllocator(iQueue.GetData(queue)) == &allocator_a);
	TEST_REQUIRE(iQueue.Enqueue(queue, &value) == 1);
	CurrentAllocator = &allocator_b;
	active_allocator_state = &state_a;
	TEST_REQUIRE(iQueue.Finalize(queue) == 1);
	queue = NULL;
	TEST_REQUIRE(state_a.free_calls >= 3 && state_b.free_calls == 0);

	memset(&state_a, 0, sizeof(state_a));
	active_allocator_state = &state_a;
	CurrentAllocator = &allocator_a;
	queue = iQueue.CreateWithAllocator(sizeof(int), NULL);
	TEST_REQUIRE(queue != NULL);
	TEST_REQUIRE(iList.GetAllocator(iQueue.GetData(queue)) == &allocator_a);
	TEST_REQUIRE(iQueue.Finalize(queue) == 1);
	queue = NULL;

	CurrentAllocator = NULL;
	TEST_REQUIRE(iQueue.CreateWithAllocator(sizeof(int), NULL) == NULL);
	TEST_REQUIRE(iQueue.Create(sizeof(int)) == NULL);

	CurrentAllocator = saved_current;
	iError.SetErrorFunction(previous);
	handler_active = 0;
	active_allocator_state = NULL;
	return 0;

cleanup:
	CurrentAllocator = saved_current;
	if (queue != NULL)
		iQueue.Finalize(queue);
	if (handler_active)
		iError.SetErrorFunction(previous);
	active_allocator_state = NULL;
	return -1;
}

static uint32_t next_random(uint32_t *state)
{
	*state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
	return *state;
}

static int test_randomized_model(void)
{
	Queue *queue = NULL;
	int model[128];
	int output;
	int value;
	uint32_t random_state = UINT32_C(0x12345678);
	size_t count = 0;
	size_t step;

	queue = iQueue.Create(sizeof(int));
	TEST_REQUIRE(queue != NULL);
	for (step = 0; step < 2000; ++step) {
		unsigned operation = (unsigned)(next_random(&random_state) % 5);
		if (count == sizeof(model) / sizeof(model[0]) && operation == 0)
			operation = 1;
		switch (operation) {
		case 0:
			value = (int)(next_random(&random_state) & UINT32_C(0x7fffffff));
			TEST_REQUIRE(iQueue.Enqueue(queue, &value) == 1);
			model[count++] = value;
			break;
		case 1:
			output = -1;
			if (count == 0) {
				TEST_REQUIRE(iQueue.Dequeue(queue, &output) == 0 && output == -1);
			} else {
				TEST_REQUIRE(iQueue.Dequeue(queue, &output) == 1 &&
					output == model[0]);
				memmove(model, model + 1, (count - 1) * sizeof(model[0]));
				--count;
			}
			break;
		case 2:
			output = -1;
			if (count == 0)
				TEST_REQUIRE(iQueue.Front(queue, &output) == 0 && output == -1);
			else
				TEST_REQUIRE(iQueue.Front(queue, &output) == 1 && output == model[0]);
			break;
		case 3:
			output = -1;
			if (count == 0)
				TEST_REQUIRE(iQueue.Back(queue, &output) == 0 && output == -1);
			else
				TEST_REQUIRE(iQueue.Back(queue, &output) == 1 &&
					output == model[count - 1]);
			break;
		default:
			TEST_REQUIRE(iQueue.Clear(queue) == 1);
			count = 0;
			break;
		}
		TEST_REQUIRE(iQueue.Size(queue) == count);
		TEST_REQUIRE(iList.Size(iQueue.GetData(queue)) == count);
	}

	TEST_REQUIRE(iQueue.Finalize(queue) == 1);
	queue = NULL;
	return 0;

cleanup:
	if (queue != NULL)
		iQueue.Finalize(queue);
	return -1;
}

static const TestCase tests[] = {
	{ "FIFO lifecycle, copied values and list view", test_fifo_lifecycle_and_views },
	{ "clear and reuse", test_clear_and_reuse },
	{ "NULL arguments and forwarded errors", test_null_arguments_and_forwarded_errors },
	{ "allocator failures and ownership", test_allocator_failures_and_ownership },
	{ "randomized FIFO model", test_randomized_model }
};

static const TestSuite suite = {
	"queue", tests, sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
	return &suite;
}
