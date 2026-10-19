#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* §6.1 Container Conversions                                                */
/* ========================================================================= */

static ValArrayDouble *CastToDouble_Loop(ValArrayInt *src)
{
    if (src == NULL)
        return NULL;
    size_t siz = iValArrayInt.Size(src);
    ValArrayDouble *result = iValArrayDouble.Create(siz);
    if (result == NULL)
        return NULL;
    for (size_t i = 0; i < siz; i++) {
        double d = (double)iValArrayInt.GetElement(src, i);
        int r = iValArrayDouble.Add(result, d);
        if (r < 0)
            break;
    }
    return result;
}

static ValArrayDouble *CastToDouble_Iterator(ValArrayInt *src)
{
    if (src == NULL)
        return NULL;
    size_t siz = iValArrayInt.Size(src);
    ValArrayDouble *result = iValArrayDouble.Create(siz);
    if (result == NULL)
        return NULL;
    Iterator *it = iValArrayInt.NewIterator(src);
    if (it == NULL) {
        iValArrayDouble.Finalize(result);
        return NULL;
    }
    for (int *ip = it->GetFirst(it); ip != NULL; ip = it->GetNext(it)) {
        int r = iValArrayDouble.Add(result, (double)*ip);
        if (r < 0)
            break;
    }
    iValArrayInt.DeleteIterator(it);
    return result;
}

struct customer {
    long long NumericID;
    char *Name;
    long double Balance;
};

static int ApplyCustomerName(void *data, void *container)
{
    struct customer *pData = (struct customer *)data;
    strCollection *collection = (strCollection *)container;
    return istrCollection.Add(collection, pData->Name);
}

static strCollection *GetAllCustomerNames(Vector *CustomerDB)
{
    if (CustomerDB == NULL)
        return NULL;
    size_t siz = iVector.Size(CustomerDB);
    strCollection *result = istrCollection.Create(siz);
    if (result == NULL)
        return NULL;
    iVector.Apply(CustomerDB, ApplyCustomerName, result);
    return result;
}

static int test_container_conversions_loop_and_iterator(void)
{
    ValArrayInt *src = NULL;
    ValArrayDouble *dst_loop = NULL;
    ValArrayDouble *dst_iter = NULL;
    int result = -1;

    src = iValArrayInt.Create(5);
    TEST_REQUIRE(src != NULL);

    int test_values[] = { 10, -25, 0, 42, 1000 };
    for (size_t i = 0; i < 5; i++) {
        TEST_REQUIRE(iValArrayInt.Add(src, test_values[i]) == 1);
    }
    TEST_REQUIRE(iValArrayInt.Size(src) == 5);

    /* Test loop-based conversion */
    dst_loop = CastToDouble_Loop(src);
    TEST_REQUIRE(dst_loop != NULL);
    TEST_REQUIRE(iValArrayDouble.Size(dst_loop) == 5);
    for (size_t i = 0; i < 5; i++) {
        double val = iValArrayDouble.GetElement(dst_loop, i);
        TEST_REQUIRE(val == (double)test_values[i]);
    }

    /* Test iterator-based conversion */
    dst_iter = CastToDouble_Iterator(src);
    TEST_REQUIRE(dst_iter != NULL);
    TEST_REQUIRE(iValArrayDouble.Size(dst_iter) == 5);
    for (size_t i = 0; i < 5; i++) {
        double val = iValArrayDouble.GetElement(dst_iter, i);
        TEST_REQUIRE(val == (double)test_values[i]);
    }

    /* Test with empty input */
    ValArrayInt *empty_src = iValArrayInt.Create(0);
    TEST_REQUIRE(empty_src != NULL);
    ValArrayDouble *empty_loop = CastToDouble_Loop(empty_src);
    TEST_REQUIRE(empty_loop != NULL);
    TEST_REQUIRE(iValArrayDouble.Size(empty_loop) == 0);
    ValArrayDouble *empty_iter = CastToDouble_Iterator(empty_src);
    TEST_REQUIRE(empty_iter != NULL);
    TEST_REQUIRE(iValArrayDouble.Size(empty_iter) == 0);

    iValArrayDouble.Finalize(empty_loop);
    iValArrayDouble.Finalize(empty_iter);
    iValArrayInt.Finalize(empty_src);

    result = 0;

cleanup:
    if (dst_loop != NULL)
        iValArrayDouble.Finalize(dst_loop);
    if (dst_iter != NULL)
        iValArrayDouble.Finalize(dst_iter);
    if (src != NULL)
        iValArrayInt.Finalize(src);
    return result;
}

static int test_container_conversions_apply_customer_names(void)
{
    Vector *customer_db = NULL;
    strCollection *names = NULL;
    int result = -1;

    customer_db = iVector.Create(sizeof(struct customer), 3);
    TEST_REQUIRE(customer_db != NULL);

    struct customer c1 = { 101LL, "Alice Smith", 1250.75L };
    struct customer c2 = { 102LL, "Bob Jones", -45.50L };
    struct customer c3 = { 103LL, "Charlie Brown", 99999.00L };

    TEST_REQUIRE(iVector.Add(customer_db, &c1) == 1);
    TEST_REQUIRE(iVector.Add(customer_db, &c2) == 1);
    TEST_REQUIRE(iVector.Add(customer_db, &c3) == 1);
    TEST_REQUIRE(iVector.Size(customer_db) == 3);

    names = GetAllCustomerNames(customer_db);
    TEST_REQUIRE(names != NULL);
    TEST_REQUIRE(istrCollection.Size(names) == 3);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(names, 0), "Alice Smith") == 0);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(names, 1), "Bob Jones") == 0);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(names, 2), "Charlie Brown") == 0);

    result = 0;

cleanup:
    if (names != NULL)
        istrCollection.Finalize(names);
    if (customer_db != NULL)
        iVector.Finalize(customer_db);
    return result;
}

/* ========================================================================= */
/* §6.2 Infinite & Auto-Extending Arrays                                      */
/* ========================================================================= */

static int infinite_id_value;
static int zero_extensible_value;
static ErrorFunction oldVectorErrorFn;

static void *InfiniteIdentityFn(const char *msg, int errorCode, ...)
{
    va_list ap;
    (void)msg;
    if (errorCode != CONTAINER_ERROR_INDEX)
        return oldVectorErrorFn ? oldVectorErrorFn(msg, errorCode) : NULL;
    va_start(ap, errorCode);
    (void)va_arg(ap, Vector *);
    size_t idx = va_arg(ap, size_t);
    infinite_id_value = (int)idx;
    va_end(ap);
    return &infinite_id_value;
}

static void *ZeroExtensibleFn(const char *msg, int errorCode, ...)
{
    va_list ap;
    (void)msg;
    if (errorCode != CONTAINER_ERROR_INDEX)
        return oldVectorErrorFn ? oldVectorErrorFn(msg, errorCode) : NULL;
    va_start(ap, errorCode);
    (void)va_arg(ap, Vector *);
    (void)va_arg(ap, size_t);
    zero_extensible_value = 0;
    va_end(ap);
    return &zero_extensible_value;
}

static void *InsertExtensibleFn(const char *msg, int errorCode, ...)
{
    va_list ap;
    (void)msg;
    if (errorCode != CONTAINER_ERROR_INDEX)
        return oldVectorErrorFn ? oldVectorErrorFn(msg, errorCode) : NULL;
    va_start(ap, errorCode);
    Vector *v = va_arg(ap, Vector *);
    size_t idx = va_arg(ap, size_t);
    va_end(ap);
    iVector.Resize(v, idx);
    return (void *)1;
}

static int test_infinite_identity_array(void)
{
    Vector *v = NULL;
    int result = -1;

    v = iVector.Create(sizeof(int), 1);
    TEST_REQUIRE(v != NULL);

    oldVectorErrorFn = iVector.SetErrorFunction(v, InfiniteIdentityFn);

    /* Verify out-of-bounds indices return identity value */
    for (int i = 20; i < 30; i++) {
        int *val = (int *)iVector.GetElement(v, (size_t)i);
        TEST_REQUIRE(val != NULL);
        TEST_REQUIRE(*val == i);
    }

    /* Restore error handler */
    (void)iVector.SetErrorFunction(v, oldVectorErrorFn);
    oldVectorErrorFn = NULL;

    iVector.Finalize(v);
    v = NULL;
    result = 0;

cleanup:
    if (v != NULL) {
        if (oldVectorErrorFn != NULL)
            (void)iVector.SetErrorFunction(v, oldVectorErrorFn);
        iVector.Finalize(v);
    }
    oldVectorErrorFn = NULL;
    return result;
}

static int test_zero_extensible_array(void)
{
    Vector *v = NULL;
    int result = -1;

    v = iVector.Create(sizeof(int), 2);
    TEST_REQUIRE(v != NULL);

    int val0 = 42;
    int val1 = 84;
    TEST_REQUIRE(iVector.Add(v, &val0) == 1);
    TEST_REQUIRE(iVector.Add(v, &val1) == 1);
    TEST_REQUIRE(iVector.Size(v) == 2);

    oldVectorErrorFn = iVector.SetErrorFunction(v, ZeroExtensibleFn);

    /* In-bounds elements return stored values */
    int *stored0 = (int *)iVector.GetElement(v, 0);
    int *stored1 = (int *)iVector.GetElement(v, 1);
    TEST_REQUIRE(stored0 != NULL && *stored0 == 42);
    TEST_REQUIRE(stored1 != NULL && *stored1 == 84);

    /* Out-of-bounds elements return 0 */
    size_t out_indices[] = { 2, 5, 10, 50, 100 };
    for (size_t i = 0; i < sizeof(out_indices) / sizeof(out_indices[0]); i++) {
        int *val = (int *)iVector.GetElement(v, out_indices[i]);
        TEST_REQUIRE(val != NULL);
        TEST_REQUIRE(*val == 0);
    }

    /* Restore error handler */
    (void)iVector.SetErrorFunction(v, oldVectorErrorFn);
    oldVectorErrorFn = NULL;

    iVector.Finalize(v);
    v = NULL;
    result = 0;

cleanup:
    if (v != NULL) {
        if (oldVectorErrorFn != NULL)
            (void)iVector.SetErrorFunction(v, oldVectorErrorFn);
        iVector.Finalize(v);
    }
    oldVectorErrorFn = NULL;
    return result;
}

static int test_insert_extensible_array(void)
{
    Vector *v = NULL;
    int result = -1;

    v = iVector.Create(sizeof(int), 1);
    TEST_REQUIRE(v != NULL);

    int val0 = 77;
    TEST_REQUIRE(iVector.Add(v, &val0) == 1);
    TEST_REQUIRE(iVector.Size(v) == 1);

    oldVectorErrorFn = iVector.SetErrorFunction(v, InsertExtensibleFn);

    /* Sparse insertion at out-of-bounds index 10 */
    int val10 = 123;
    TEST_REQUIRE(iVector.InsertAt(v, 10, &val10) == 1);
    TEST_REQUIRE(iVector.Size(v) == 11);

    /* Verify elements: index 0 is 77, indices 1..9 are 0, index 10 is 123 */
    int *e0 = (int *)iVector.GetElement(v, 0);
    TEST_REQUIRE(e0 != NULL && *e0 == 77);
    for (size_t i = 1; i < 10; i++) {
        int *zi = (int *)iVector.GetElement(v, i);
        TEST_REQUIRE(zi != NULL && *zi == 0);
    }
    int *e10 = (int *)iVector.GetElement(v, 10);
    TEST_REQUIRE(e10 != NULL && *e10 == 123);

    /* Another sparse insertion at out-of-bounds index 20 */
    int val20 = 456;
    TEST_REQUIRE(iVector.InsertAt(v, 20, &val20) == 1);
    TEST_REQUIRE(iVector.Size(v) == 21);

    int *e20 = (int *)iVector.GetElement(v, 20);
    TEST_REQUIRE(e20 != NULL && *e20 == 456);
    int *e15 = (int *)iVector.GetElement(v, 15);
    TEST_REQUIRE(e15 != NULL && *e15 == 0);

    /* Restore error handler */
    (void)iVector.SetErrorFunction(v, oldVectorErrorFn);
    oldVectorErrorFn = NULL;

    iVector.Finalize(v);
    v = NULL;
    result = 0;

cleanup:
    if (v != NULL) {
        if (oldVectorErrorFn != NULL)
            (void)iVector.SetErrorFunction(v, oldVectorErrorFn);
        iVector.Finalize(v);
    }
    oldVectorErrorFn = NULL;
    return result;
}

/* ========================================================================= */
/* §5.11.2 Priority Queue Complete Example                                   */
/* ========================================================================= */

static int test_priority_queue_complete_example(void)
{
    PQueue *pq = NULL;
    int result = -1;

    pq = iPQueue.Create(20);
    TEST_REQUIRE(pq != NULL);

    int priorities[] = { 470, 101, 282, 16, 984, 1457, 1144, 2007, 1622, 1458 };
    const size_t num_items = sizeof(priorities) / sizeof(priorities[0]);

    for (size_t i = 0; i < num_items; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", priorities[i]);
        int r = iPQueue.Push(pq, priorities[i], buf);
        TEST_REQUIRE(r == 1);
    }

    TEST_REQUIRE(iPQueue.Size(pq) == num_items);
    TEST_REQUIRE(iPQueue.Sizeof(pq) >= iPQueue.Sizeof(NULL));
    TEST_REQUIRE(iPQueue.Sizeof(NULL) > 0);

    size_t count = iPQueue.Size(pq);
    intptr_t prev_key = -1;
    for (size_t i = 0; i < count; i++) {
        char buf[32];
        intptr_t key = iPQueue.Pop(pq, buf);
        TEST_REQUIRE(key != INT_MAX);
        TEST_REQUIRE(key > prev_key);

        char expected[32];
        snprintf(expected, sizeof(expected), "%ld", (long)key);
        TEST_REQUIRE(strcmp(buf, expected) == 0);

        prev_key = key;
    }

    TEST_REQUIRE(iPQueue.Size(pq) == 0);
    TEST_REQUIRE(iPQueue.Finalize(pq) == 1);
    pq = NULL;
    result = 0;

cleanup:
    if (pq != NULL)
        iPQueue.Finalize(pq);
    return result;
}

/* ========================================================================= */
/* §5.15.1 StreamBuffer Record Pack/Unpack                                   */
/* ========================================================================= */

static int test_streambuffer_record_pack_unpack(void)
{
    StreamBuffer *sb = NULL;
    int result = -1;

    sb = iStreamBuffer.Create(10);
    TEST_REQUIRE(sb != NULL);

    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "item %d", i + 1);
        size_t len = 1 + strlen(buf);
        size_t written = iStreamBuffer.Write(sb, buf, len);
        TEST_REQUIRE(written == len);
    }

    char term = 0;
    size_t term_written = iStreamBuffer.Write(sb, &term, 1);
    TEST_REQUIRE(term_written == 1);

    /* Total bytes: (9 items * 7 bytes) + (1 item * 8 bytes) + 1 term byte = 72 */
    TEST_REQUIRE(iStreamBuffer.GetPosition(sb) == 72);

    TEST_REQUIRE(iStreamBuffer.SetPosition(sb, 0) == 1);
    TEST_REQUIRE(iStreamBuffer.GetPosition(sb) == 0);

    const char *p = iStreamBuffer.GetData(sb);
    TEST_REQUIRE(p != NULL);

    int record_count = 0;
    while (*p != '\0') {
        char expected[32];
        snprintf(expected, sizeof(expected), "item %d", record_count + 1);
        TEST_REQUIRE(strcmp(p, expected) == 0);
        p += 1 + strlen(p);
        record_count++;
    }
    TEST_REQUIRE(record_count == 10);
    TEST_REQUIRE(*p == '\0');

    TEST_REQUIRE(iStreamBuffer.Finalize(sb) == 1);
    sb = NULL;
    result = 0;

cleanup:
    if (sb != NULL)
        iStreamBuffer.Finalize(sb);
    return result;
}

/* ========================================================================= */
/* §4.7.3 Observer Pattern with ValArrayInt                                   */
/* ========================================================================= */

static unsigned obs_call_count;
static unsigned obs_add_count;
static unsigned obs_finalize_count;
static const void *obs_last_object;
static int obs_last_add_value;

static void observer_pattern_callback(const void *ObservedObject,
                                      unsigned operation,
                                      const void *ExtraInfo[])
{
    obs_call_count++;
    obs_last_object = ObservedObject;
    if (operation == CCL_ADD) {
        obs_add_count++;
        if (ExtraInfo != NULL && ExtraInfo[0] != NULL) {
            obs_last_add_value = *(const int *)ExtraInfo[0];
        }
    } else if (operation == CCL_FINALIZE) {
        obs_finalize_count++;
    }
}

static int test_observer_valarrayint_pattern(void)
{
    ValArrayInt *vInt = NULL;
    int result = -1;

    obs_call_count = 0;
    obs_add_count = 0;
    obs_finalize_count = 0;
    obs_last_object = NULL;
    obs_last_add_value = 0;

    vInt = iValArrayInt.CreateSequence(24, 0, 1);
    TEST_REQUIRE(vInt != NULL);
    TEST_REQUIRE(iValArrayInt.Size(vInt) == 24);
    for (size_t i = 0; i < 24; i++) {
        TEST_REQUIRE(iValArrayInt.GetElement(vInt, i) == (int)i);
    }

    int sub = iObserver.Subscribe(vInt, observer_pattern_callback,
                                  CCL_ADD | CCL_FINALIZE);
    TEST_REQUIRE(sub > 0);

    TEST_REQUIRE(iValArrayInt.Add(vInt, 4096) == 1);
    TEST_REQUIRE(obs_call_count == 1);
    TEST_REQUIRE(obs_add_count == 1);
    TEST_REQUIRE(obs_finalize_count == 0);
    TEST_REQUIRE(obs_last_object == vInt);
    TEST_REQUIRE(obs_last_add_value == 4096);
    TEST_REQUIRE(iValArrayInt.Size(vInt) == 25);
    TEST_REQUIRE(iValArrayInt.GetElement(vInt, 24) == 4096);

    TEST_REQUIRE(iValArrayInt.Finalize(vInt) == 1);
    vInt = NULL;
    TEST_REQUIRE(obs_call_count == 2);
    TEST_REQUIRE(obs_add_count == 1);
    TEST_REQUIRE(obs_finalize_count == 1);

    (void)iObserver.Unsubscribe(NULL, observer_pattern_callback);
    result = 0;

cleanup:
    if (vInt != NULL)
        iValArrayInt.Finalize(vInt);
    (void)iObserver.Unsubscribe(NULL, observer_pattern_callback);
    return result;
}

/* ========================================================================= */
/* Test Suite Registration                                                   */
/* ========================================================================= */

static const TestCase extensions_tests[] = {
    { "conversions_loop_and_iterator", test_container_conversions_loop_and_iterator },
    { "conversions_apply_customer_names", test_container_conversions_apply_customer_names },
    { "infinite_identity_array", test_infinite_identity_array },
    { "zero_extensible_array", test_zero_extensible_array },
    { "insert_extensible_array", test_insert_extensible_array },
    { "priority_queue_complete_example", test_priority_queue_complete_example },
    { "streambuffer_record_pack_unpack", test_streambuffer_record_pack_unpack },
    { "observer_valarrayint_pattern", test_observer_valarrayint_pattern },
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "extensions",
        extensions_tests,
        sizeof(extensions_tests) / sizeof(extensions_tests[0])
    };
    return &suite;
}
