#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GenericContainer base;
    unsigned id;
} ObserverSubject;

typedef struct {
    size_t calloc_calls;
    size_t realloc_calls;
    int fail_calloc;
    int fail_realloc;
} ObserverAllocatorState;

static ObserverAllocatorState allocator_state;
static const char *last_error_operation;
static int last_error_code;

static void *observer_malloc(size_t size)
{
    return malloc(size);
}

static void observer_free(void *value)
{
    free(value);
}

static void *observer_realloc(void *value, size_t size)
{
    ++allocator_state.realloc_calls;
    if (allocator_state.fail_realloc)
        return NULL;
    return realloc(value, size);
}

static void *observer_calloc(size_t count, size_t size)
{
    ++allocator_state.calloc_calls;
    if (allocator_state.fail_calloc)
        return NULL;
    return calloc(count, size);
}

static ContainerAllocator observer_allocator = {
    observer_malloc,
    observer_free,
    observer_realloc,
    observer_calloc
};

static void *capture_error(const char *operation, int code, ...)
{
    last_error_operation = operation;
    last_error_code = code;
    return NULL;
}

static ErrorFunction begin_errors(void)
{
    last_error_operation = NULL;
    last_error_code = 0;
    return iError.SetErrorFunction(capture_error);
}

static int error_is(const char *operation, int code)
{
    return last_error_operation != NULL &&
           strcmp(last_error_operation, operation) == 0 &&
           last_error_code == code;
}

static void reset_allocator(void)
{
    memset(&allocator_state, 0, sizeof(allocator_state));
}

static void init_subject(ObserverSubject *subject, unsigned id)
{
    memset(subject, 0, sizeof(*subject));
    subject->id = id;
}

typedef struct {
    unsigned calls;
    const void *object;
    unsigned operation;
    const void *extra[2];
} CallbackState;

static CallbackState callback_a_state;
static CallbackState callback_b_state;

static void record_callback(CallbackState *state, const void *object,
                            unsigned operation, const void *extra[])
{
    ++state->calls;
    state->object = object;
    state->operation = operation;
    state->extra[0] = extra[0];
    state->extra[1] = extra[1];
}

static void callback_a(const void *object, unsigned operation,
                       const void *extra[])
{
    record_callback(&callback_a_state, object, operation, extra);
}

static void callback_b(const void *object, unsigned operation,
                       const void *extra[])
{
    record_callback(&callback_b_state, object, operation, extra);
}

static void reset_callbacks(void)
{
    memset(&callback_a_state, 0, sizeof(callback_a_state));
    memset(&callback_b_state, 0, sizeof(callback_b_state));
}

static void remove_self_and_other(const void *object, unsigned operation,
                                  const void *extra[]);
static void subscribe_during_notify(const void *object, unsigned operation,
                                    const void *extra[]);

static ObserverSubject *subscription_target;
static unsigned mutation_calls;
static unsigned integration_add_size;
static unsigned integration_clear_size;
static unsigned integration_finalize_size;

static void remove_self_and_other(const void *object, unsigned operation,
                                  const void *extra[])
{
    (void)operation;
    (void)extra;
    ++mutation_calls;
    iObserver.Unsubscribe((void *)object, remove_self_and_other);
    iObserver.Unsubscribe((void *)object, callback_b);
}

static void subscribe_during_notify(const void *object, unsigned operation,
                                    const void *extra[])
{
    (void)object;
    (void)operation;
    (void)extra;
    ++mutation_calls;
    (void)iObserver.Subscribe(subscription_target, callback_b, CCL_ADD);
}

static void integration_callback(const void *object, unsigned operation,
                                 const void *extra[])
{
    (void)extra;
    if (operation == CCL_ADD)
        integration_add_size = (unsigned)iList.Size((const List *)object);
    else if (operation == CCL_CLEAR)
        integration_clear_size = (unsigned)iList.Size((const List *)object);
    else if (operation == CCL_FINALIZE) {
        integration_finalize_size = (unsigned)iList.Size((const List *)object);
        /* A finalized object must not leave a stale relationship behind. */
        iObserver.Unsubscribe((void *)object, integration_callback);
    }
}

static void remove_all_test_observers(void)
{
    (void)iObserver.Unsubscribe(NULL, callback_a);
    (void)iObserver.Unsubscribe(NULL, callback_b);
    (void)iObserver.Unsubscribe(NULL, remove_self_and_other);
    (void)iObserver.Unsubscribe(NULL, subscribe_during_notify);
    (void)iObserver.Unsubscribe(NULL, integration_callback);
}

static int test_invalid_arguments(void)
{
    ObserverSubject subject;
    ContainerAllocator *saved_allocator = NULL;
    ErrorFunction saved_error = NULL;
    size_t allocations_before;
    unsigned original_flags;
    int result = -1;

    init_subject(&subject, 1);
    subject.base.Flags = 0x100U;
    original_flags = subject.base.Flags;
    reset_allocator();
    saved_allocator = iAllocator.Change(&observer_allocator);
    saved_error = begin_errors();
    allocations_before = allocator_state.calloc_calls +
                         allocator_state.realloc_calls;

    TEST_REQUIRE(iObserver.Subscribe(NULL, callback_a, CCL_ADD) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iObserver.Subscribe", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(subject.base.Flags == original_flags);
    TEST_REQUIRE(iObserver.Subscribe(&subject, NULL, CCL_ADD) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iObserver.Subscribe(&subject, callback_a, 0) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(subject.base.Flags == original_flags);
    TEST_REQUIRE(iObserver.Notify(NULL, CCL_ADD, NULL, NULL) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(error_is("iObserver.Notify", CONTAINER_ERROR_BADARG));
    TEST_REQUIRE(iObserver.Notify(&subject, 0, NULL, NULL) ==
                 CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(callback_a_state.calls == 0);
    TEST_REQUIRE(iObserver.Unsubscribe(NULL, NULL) == 0);
    TEST_REQUIRE(allocations_before == allocator_state.calloc_calls +
                 allocator_state.realloc_calls);

    result = 0;
cleanup:
    if (saved_error != NULL)
        (void)iError.SetErrorFunction(saved_error);
    if (saved_allocator != NULL)
        (void)iAllocator.Change(saved_allocator);
    return result;
}

static int test_initial_allocation_failure(void)
{
    ObserverSubject subject;
    ContainerAllocator *saved_allocator = NULL;
    ErrorFunction saved_error = NULL;
    unsigned original_flags;
    int result = -1;

    init_subject(&subject, 2);
    subject.base.Flags = 0x400U;
    original_flags = subject.base.Flags;
    reset_allocator();
    allocator_state.fail_calloc = 1;
    saved_allocator = iAllocator.Change(&observer_allocator);
    saved_error = begin_errors();

    TEST_REQUIRE(iObserver.Subscribe(&subject, callback_a, CCL_ADD) ==
                 CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(error_is("iObserver.Subscribe", CONTAINER_ERROR_NOMEMORY));
    TEST_REQUIRE(subject.base.Flags == original_flags);
    TEST_REQUIRE(allocator_state.calloc_calls == 1);

    allocator_state.fail_calloc = 0;
    TEST_REQUIRE(iObserver.Subscribe(&subject, callback_a, CCL_ADD) == 1);
    TEST_REQUIRE((subject.base.Flags & CONTAINER_HAS_OBSERVER) != 0);
    TEST_REQUIRE(iObserver.Unsubscribe(&subject, callback_a) == 1);
    TEST_REQUIRE(allocator_state.calloc_calls == 2);
    result = 0;
cleanup:
    remove_all_test_observers();
    if (saved_error != NULL)
        (void)iError.SetErrorFunction(saved_error);
    if (saved_allocator != NULL)
        (void)iAllocator.Change(saved_allocator);
    return result;
}

static int test_delivery_and_unsubscribe(void)
{
    ObserverSubject a, b, c, reused;
    int extra_one = 11;
    int extra_two = 22;
    int result = -1;

    remove_all_test_observers();
    reset_callbacks();
    init_subject(&a, 10);
    init_subject(&b, 11);
    init_subject(&c, 12);
    init_subject(&reused, 13);

    TEST_REQUIRE(iObserver.Subscribe(&a, callback_a, CCL_ADD | CCL_CLEAR) == 1);
    TEST_REQUIRE((a.base.Flags & CONTAINER_HAS_OBSERVER) != 0);
    TEST_REQUIRE(iObserver.Notify(&a, CCL_ADD, &extra_one, &extra_two) == 1);
    TEST_REQUIRE(callback_a_state.calls == 1 && callback_a_state.object == &a &&
                 callback_a_state.operation == CCL_ADD &&
                 callback_a_state.extra[0] == &extra_one &&
                 callback_a_state.extra[1] == &extra_two);
    TEST_REQUIRE(iObserver.Notify(&a, CCL_PUSH, NULL, NULL) == 0);
    TEST_REQUIRE(callback_a_state.calls == 1);

    TEST_REQUIRE(iObserver.Subscribe(&a, callback_a, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Subscribe(&a, callback_b, CCL_CLEAR) == 1);
    TEST_REQUIRE(iObserver.Notify(&a, CCL_ADD, NULL, NULL) == 2);
    TEST_REQUIRE(iObserver.Notify(&a, CCL_CLEAR, NULL, NULL) == 2);
    TEST_REQUIRE(iObserver.Subscribe(&b, callback_a, CCL_PUSH) == 1);
    TEST_REQUIRE(iObserver.Subscribe(&c, callback_b, CCL_ADD | CCL_PUSH) == 1);
    TEST_REQUIRE(iObserver.Notify(&a, CCL_ADD | CCL_PUSH, NULL, NULL) == 2);
    TEST_REQUIRE(iObserver.Notify(&b, CCL_ADD, NULL, NULL) == 0);
    TEST_REQUIRE(iObserver.Notify(&b, CCL_PUSH, NULL, NULL) == 1);
    TEST_REQUIRE(iObserver.Notify(&c, CCL_PUSH, NULL, NULL) == 1);

    TEST_REQUIRE(iObserver.Unsubscribe(&a, callback_b) == 1);
    TEST_REQUIRE(iObserver.Unsubscribe(&a, callback_b) == 0);
    TEST_REQUIRE(iObserver.Unsubscribe(&a, callback_a) == 2);
    TEST_REQUIRE(iObserver.Unsubscribe(&a, callback_a) == 0);
    TEST_REQUIRE(iObserver.Unsubscribe(&b, NULL) == 1);
    TEST_REQUIRE(iObserver.Unsubscribe(NULL, callback_b) == 1);
    TEST_REQUIRE(iObserver.Unsubscribe(NULL, NULL) == 0);
    TEST_REQUIRE(iObserver.Unsubscribe(&c, callback_a) == 0);
    TEST_REQUIRE(iObserver.Subscribe(&reused, callback_a, CCL_ADD) == 1);
    reset_callbacks();
    TEST_REQUIRE(iObserver.Notify(&reused, CCL_ADD, NULL, NULL) == 1);
    TEST_REQUIRE(callback_a_state.calls == 1);
    result = 0;
cleanup:
    remove_all_test_observers();
    return result;
}

static int test_capacity_growth_and_reuse(void)
{
    ObserverSubject subjects[29];
    ObserverSubject unrelated;
    size_t i;
    int result = -1;

    remove_all_test_observers();
    reset_callbacks();
    for (i = 0; i < sizeof(subjects) / sizeof(subjects[0]); ++i)
        init_subject(&subjects[i], (unsigned)(100 + i));
    init_subject(&unrelated, 999);

    for (i = 0; i < 25; ++i)
        TEST_REQUIRE(iObserver.Subscribe(&subjects[i], callback_a, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Notify(&subjects[24], CCL_ADD, NULL, NULL) == 1);
    allocator_state.fail_realloc = 1;
    subjects[25].base.Flags = 0x800U;
    TEST_REQUIRE(iObserver.Subscribe(&subjects[25], callback_b, CCL_ADD) ==
                 CONTAINER_ERROR_NOMEMORY);
    TEST_REQUIRE(subjects[25].base.Flags == 0x800U);
    TEST_REQUIRE(error_is("iObserver.Subscribe", CONTAINER_ERROR_NOMEMORY));
    TEST_REQUIRE(allocator_state.realloc_calls >= 1);
    allocator_state.fail_realloc = 0;
    TEST_REQUIRE(iObserver.Subscribe(&subjects[25], callback_b, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Notify(&subjects[25], CCL_ADD, NULL, NULL) == 1);
    TEST_REQUIRE(iObserver.Notify(&unrelated, CCL_ADD, NULL, NULL) == 0);

    TEST_REQUIRE(iObserver.Unsubscribe(&subjects[0], callback_a) == 1);
    TEST_REQUIRE(iObserver.Unsubscribe(&subjects[12], callback_a) == 1);
    TEST_REQUIRE(iObserver.Unsubscribe(&subjects[25], callback_b) == 1);
    TEST_REQUIRE(iObserver.Subscribe(&subjects[26], callback_b, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Subscribe(&subjects[27], callback_b, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Subscribe(&subjects[28], callback_b, CCL_ADD) == 1);
    for (i = 1; i < 25; ++i) {
        if (i == 12)
            continue;
        TEST_REQUIRE(iObserver.Notify(&subjects[i], CCL_ADD, NULL, NULL) == 1);
    }
    TEST_REQUIRE(iObserver.Notify(&subjects[26], CCL_ADD, NULL, NULL) == 1);
    TEST_REQUIRE(iObserver.Notify(&subjects[27], CCL_ADD, NULL, NULL) == 1);
    TEST_REQUIRE(iObserver.Notify(&subjects[28], CCL_ADD, NULL, NULL) == 1);
    result = 0;
cleanup:
    allocator_state.fail_realloc = 0;
    remove_all_test_observers();
    return result;
}

static int test_reentrant_notification(void)
{
    ObserverSubject object;
    ObserverSubject target;
    ObserverSubject fill[49];
    size_t i;
    int result = -1;

    remove_all_test_observers();
    reset_callbacks();
    mutation_calls = 0;
    init_subject(&object, 200);
    init_subject(&target, 201);
    TEST_REQUIRE(iObserver.Subscribe(&object, remove_self_and_other, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Subscribe(&object, callback_b, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Notify(&object, CCL_ADD, NULL, NULL) == 1);
    TEST_REQUIRE(mutation_calls == 1 && callback_b_state.calls == 0);
    TEST_REQUIRE(iObserver.Notify(&object, CCL_ADD, NULL, NULL) == 0);

    /* vsize is 50 after the preceding growth test.  Fill its current extent
       so the subscription from the final callback grows the table to 75. */
    remove_all_test_observers();
    mutation_calls = 0;
    for (i = 0; i < sizeof(fill) / sizeof(fill[0]); ++i) {
        init_subject(&fill[i], (unsigned)(300 + i));
        TEST_REQUIRE(iObserver.Subscribe(&fill[i], callback_b, CCL_ADD) == 1);
    }
    init_subject(&object, 400);
    init_subject(&target, 401);
    subscription_target = &target;
    TEST_REQUIRE(iObserver.Subscribe(&object, subscribe_during_notify, CCL_ADD) == 1);
    TEST_REQUIRE(iObserver.Notify(&object, CCL_ADD, NULL, NULL) == 1);
    TEST_REQUIRE(mutation_calls == 1);
    TEST_REQUIRE(callback_b_state.calls == 0);
    TEST_REQUIRE(iObserver.Notify(&target, CCL_ADD, NULL, NULL) == 1);
    result = 0;
cleanup:
    remove_all_test_observers();
    return result;
}

static int test_list_integration(void)
{
    List *list = NULL;
    int value = 7;
    int result = -1;

    remove_all_test_observers();
    integration_add_size = 0;
    integration_clear_size = 0;
    integration_finalize_size = 0;
    list = iList.Create(sizeof(value));
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iObserver.Subscribe(list, integration_callback,
                                     CCL_ADD | CCL_CLEAR | CCL_FINALIZE) == 1);
    TEST_REQUIRE(iList.Add(list, &value) == 1);
    TEST_REQUIRE(integration_add_size == 1);
    TEST_REQUIRE(iList.Clear(list) == 1);
    TEST_REQUIRE(integration_clear_size == 1);
    TEST_REQUIRE(iList.Finalize(list) == 1);
    list = NULL;
    TEST_REQUIRE(integration_finalize_size == 0);
    result = 0;
cleanup:
    if (list != NULL) {
        (void)iObserver.Unsubscribe(list, integration_callback);
        (void)iList.Finalize(list);
    }
    remove_all_test_observers();
    return result;
}

static const TestCase observer_tests[] = {
    {"invalid arguments", test_invalid_arguments},
    {"initial allocation failure", test_initial_allocation_failure},
    {"delivery and unsubscribe", test_delivery_and_unsubscribe},
    {"capacity growth and reuse", test_capacity_growth_and_reuse},
    {"reentrant notification", test_reentrant_notification},
    {"list integration", test_list_integration},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "observer",
        observer_tests,
        sizeof(observer_tests) / sizeof(observer_tests[0])
    };
    return &suite;
}
