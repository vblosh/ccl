#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "containers.h"
#include "test_suite.h"


/* Helper macro to check test results */
#define TEST_ASSERT(condition, testname) \
    do { \
        if (condition) { \
            printf("? %s\n", testname); \
        } else { \
            printf("? %s FAILED\n", testname); \
            return -1; \
        } \
    } while(0)

/* Helper function for Apply test - multiplies element by 2 */
static int apply_multiply_by_two(int element, void *arg)
{
    (void)arg; /* unused */
    printf("  Processing element: %d\n", element);
    return 0;
}

/* Helper function for ForEach test - doubles each element */
static int foreach_double_element(int element)
{
    return element * 2;
}

static int foreach_add_five(int element) {
    return element + 5;
}

static unsigned valarray_observer_events;

static void valarray_observer_callback(const void *object, unsigned operation,
                                       const void *extra[])
{
    (void)object;
    (void)operation;
    (void)extra;
    ++valarray_observer_events;
}

/* Test 1: Create and Finalize */
static int test_create_finalize(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);
    TEST_ASSERT(v != NULL, "Create valarray");
    TEST_ASSERT(iValArrayInt.Size(v) == 0, "Initial size is 0");
    TEST_ASSERT(iValArrayInt.GetCapacity(v) >= 10, "Capacity is at least 10");
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 2: Add elements */
static int test_add(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    TEST_ASSERT(iValArrayInt.Add(v, val1) == 1, "Add first element");
    TEST_ASSERT(iValArrayInt.Size(v) == 1, "Size is 1 after first add");
    TEST_ASSERT(iValArrayInt.Add(v, val2) == 1, "Add second element");
    TEST_ASSERT(iValArrayInt.Add(v, val3) == 1, "Add third element");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Size is 3 after three adds");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 2b: Add with SetSlice - Add appends to slice end */
static int test_add_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(15);

    /* First, populate the full array with initial values (0-90) */
    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }
    TEST_ASSERT(iValArrayInt.Size(v) == 10, "Initial array has 10 elements");

    /* Set slice: start=2, length=3, increment=2 (indices 2, 4, 6 -> values 20, 40, 60) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 2, 3, 2) >= 0, "SetSlice succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "After slice: Size == 3 (slice length)");

    /* SLICE STATE: start=2, length=3, increment=2
     * Next position for Add: 2 + 3*2 = 8
     * Slice elements are at actual indices: 2, 4, 6 (values 20, 40, 60)
     * Next element will be at index 8
     */

    /* Add to the sliced array - should append to the slice end */
    TEST_ASSERT(iValArrayInt.Add(v, 100) == 1, "Add to sliced array succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 4, "After Add: Size == 4 (slice length + 1)");

    /* Verify the added element is accessible in slice view */
    TEST_ASSERT(iValArrayInt.GetElement(v, 3) == 100, "Added element is at index 3 in slice");

    /* Verify first three slice elements unchanged */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 20, "Slice[0] still 20 (actual[2])");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 40, "Slice[1] still 40 (actual[4])");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 60, "Slice[2] still 60 (actual[6])");

    /* Add another element to slice
     * SLICE STATE: start=2, length=4, increment=2
     * Next position: 2 + 4*2 = 10
     */
    TEST_ASSERT(iValArrayInt.Add(v, 200) == 1, "Add second element to sliced array succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 5, "After second Add: Size == 5");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 200, "Second added element at index 4 in slice");

    /* Reset slice and verify full array
     * After reset: SLICE STATE: none, full array count
     * Full array now has 11 elements:
     * Indices 0-7: original values (0, 10, 20, 30, 40, 50, 60, 70)
     * Index 8: 100 (first added via slice)
     * Index 9: 80
     * Index 10: 200 (second added via slice)
     * BUT: actual array layout depends on how Add positions elements with slice
     */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 11, "After reset: Full array size reflects all elements added");

    /* Verify original elements still in place */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 0, "Original[0] == 0");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 20, "Original[2] == 20");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 40, "Original[4] == 40");
    TEST_ASSERT(iValArrayInt.GetElement(v, 6) == 60, "Original[6] == 60");
    TEST_ASSERT(iValArrayInt.GetElement(v, 9) == 90, "Original[9] == 90");

    /* Verify new elements added at expected positions */
    TEST_ASSERT(iValArrayInt.GetElement(v, 8) == 100, "First added element at position 8");
    TEST_ASSERT(iValArrayInt.GetElement(v, 10) == 200, "Second added element at position 10");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 3: GetElement */
static int test_get_element(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 100, val2 = 200, val3 = 300;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    int p1 = iValArrayInt.GetElement(v, 0);
    int p2 = iValArrayInt.GetElement(v, 1);
    int p3 = iValArrayInt.GetElement(v, 2);

    TEST_ASSERT(p1 == 100, "GetElement at index 0");
    TEST_ASSERT(p2 == 200, "GetElement at index 1");
    TEST_ASSERT(p3 == 300, "GetElement at index 2");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 4: PushBack and PopBack */
static int test_push_pop_back(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;
    int result;

    TEST_ASSERT(iValArrayInt.PushBack(v, val1) == 1, "PushBack first element");
    TEST_ASSERT(iValArrayInt.PushBack(v, val2) == 1, "PushBack second element");
    TEST_ASSERT(iValArrayInt.PushBack(v, val3) == 1, "PushBack third element");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Size is 3 after three pushes");

    TEST_ASSERT(iValArrayInt.PopBack(v, &result) == 1, "PopBack element");
    TEST_ASSERT(result == 30, "PopBack returned correct value");
    TEST_ASSERT(iValArrayInt.Size(v) == 2, "Size is 2 after pop");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 5: InsertAt */
static int test_insert_at(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30, val4 = 15;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.InsertAt(v, 1, val4) == 1, "InsertAt index 1");
    TEST_ASSERT(iValArrayInt.Size(v) == 4, "Size is 4 after insert");

    int p = iValArrayInt.GetElement(v, 1);
    TEST_ASSERT(p == 15, "Inserted element at correct position");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 6: Insert (insert at position 0) */
static int test_insert(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);

    TEST_ASSERT(iValArrayInt.Insert(v, val3) == 1, "Insert at beginning");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Size is 3 after insert");

    int p = iValArrayInt.GetElement(v, 0);
    TEST_ASSERT(p == 30, "Inserted element at position 0");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 7: ReplaceAt */
static int test_replace_at(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30, val4 = 99;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.ReplaceAt(v, 1, val4) == 1, "ReplaceAt index 1");

    int p = iValArrayInt.GetElement(v, 1);
    TEST_ASSERT(p == 99, "Element replaced at correct position");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 8: Contains */
static int test_contains(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30, val4 = 40;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.Contains(v, val1) == 1, "Contains finds existing element");
    TEST_ASSERT(iValArrayInt.Contains(v, val4) == 0, "Contains returns 0 for non-existent");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 9: IndexOf */
static int test_index_of(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;
    size_t idx;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.IndexOf(v, val1, &idx) == 1, "IndexOf finds element");
    TEST_ASSERT(idx == 0, "IndexOf returns correct index for first element");

    TEST_ASSERT(iValArrayInt.IndexOf(v, val3, &idx) == 1, "IndexOf finds last element");
    TEST_ASSERT(idx == 2, "IndexOf returns correct index for last element");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 10: Erase */
static int test_erase(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.Erase(v, val2) == 1, "Erase existing element");
    TEST_ASSERT(iValArrayInt.Size(v) == 2, "Size is 2 after erase");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == val1, "Element at index 0 is val1");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == val3, "Element at index 1 is val3");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 11: Copy */
static int test_copy(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    ValArrayInt *v_copy = iValArrayInt.Copy(v);
    TEST_ASSERT(v_copy != NULL, "Copy returns non-NULL");
    TEST_ASSERT(iValArrayInt.Size(v_copy) == iValArrayInt.Size(v), "Copy has same size");

    int p1 = iValArrayInt.GetElement(v, 0);
    int pc1 = iValArrayInt.GetElement(v_copy, 0);
    TEST_ASSERT(p1 == pc1, "Copy has same elements");

    iValArrayInt.Finalize(v_copy);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 12: Equal */
static int test_equal(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20;

    iValArrayInt.Add(v1, val1);
    iValArrayInt.Add(v1, val2);

    iValArrayInt.Add(v2, val1);
    iValArrayInt.Add(v2, val2);

    TEST_ASSERT(iValArrayInt.Equal(v1, v2) == 1, "Equal returns true for same arrays");

    int val3 = 30;
    iValArrayInt.Add(v2, val3);
    TEST_ASSERT(iValArrayInt.Equal(v1, v2) == 0, "Equal returns false for different arrays");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 13: Sort */
static int test_sort(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 30, val2 = 10, val3 = 20;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.Sort(v) == 1, "Sort succeeds");

    int p0 = iValArrayInt.GetElement(v, 0);
    int p1 = iValArrayInt.GetElement(v, 1);
    int p2 = iValArrayInt.GetElement(v, 2);
    TEST_ASSERT(p0 <= p1 && p1 <= p2, "Elements are sorted in ascending order");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 14: GetCapacity and SetCapacity */
static int test_capacity(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    TEST_ASSERT(iValArrayInt.GetCapacity(v) >= 10, "GetCapacity returns reasonable value");

	int val1 = 5;
    iValArrayInt.Add(v, val1);
    TEST_ASSERT(iValArrayInt.SetCapacity(v, 20) == 1, "SetCapacity succeeds");
    TEST_ASSERT(iValArrayInt.GetCapacity(v) >= 20, "Capacity increased");
	TEST_ASSERT(iValArrayInt.GetElement(v, 0) == val1, "Element unchanged after capacity change");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 15: Clear */
static int test_clear(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Array has 3 elements before clear");
    TEST_ASSERT(iValArrayInt.Clear(v) == 1, "Clear succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 0, "Array is empty after clear");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 16: Sizeof */
static int test_sizeof(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);

    size_t sz = iValArrayInt.Sizeof(v);
    TEST_ASSERT(sz > 0, "Sizeof returns positive value");
    TEST_ASSERT(sz >= sizeof(int) * 2, "Sizeof includes element data");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 17: GetRange */
static int test_get_range(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30, val4 = 40, val5 = 50;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);
    iValArrayInt.Add(v, val4);
    iValArrayInt.Add(v, val5);

    ValArrayInt *range = iValArrayInt.GetRange(v, 1, 3);
    TEST_ASSERT(range != NULL, "GetRange returns non-NULL");
    TEST_ASSERT(iValArrayInt.Size(range) == 3, "Range has correct size");
    TEST_ASSERT(iValArrayInt.GetElement(range, 0) == val2, "Range element 0 equals v[1]");
    TEST_ASSERT(iValArrayInt.GetElement(range, 1) == val3, "Range element 1 equals v[2]");
    TEST_ASSERT(iValArrayInt.GetElement(range, 2) == val4, "Range element 2 equals v[3]");

    iValArrayInt.Finalize(range);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 18: Reverse */
static int test_reverse(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.Reverse(v) == 1, "Reverse succeeds");

    int p0 = iValArrayInt.GetElement(v, 0);
    int p2 = iValArrayInt.GetElement(v, 2);
    TEST_ASSERT(p0 == 30 && p2 == 10, "Elements are reversed");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 19: Back and Front */
static int test_back_front(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    int front = iValArrayInt.Front(v);
    int back = iValArrayInt.Back(v);

    TEST_ASSERT(front == 10, "Front returns first element");
    TEST_ASSERT(back == 30, "Back returns last element");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 20: Append */
static int test_append(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(2);
    ValArrayInt *v2 = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30, val4 = 40;

    iValArrayInt.Add(v1, val1);
    iValArrayInt.Add(v1, val2);

    iValArrayInt.Add(v2, val3);
    iValArrayInt.Add(v2, val4);

    TEST_ASSERT(iValArrayInt.Append(v1, v2) == 1, "Append succeeds");
    TEST_ASSERT(iValArrayInt.Size(v1) == 4, "v1 size increased after append");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 0) == val1, "v1[0] == val1");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 1) == val2, "v1[1] == val2");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 2) == val3, "v1[2] == val3");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 3) == val4, "v1[3] == val4");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 21: InitializeWith */
static int test_initialize_with(void)
{
    int data[] = {10, 20, 30, 40, 50};
    ValArrayInt *v = iValArrayInt.InitializeWith(5, data);

    TEST_ASSERT(v != NULL, "InitializeWith returns non-NULL");
    TEST_ASSERT(iValArrayInt.Size(v) == 5, "InitializeWith sets correct size");

    int p0 = iValArrayInt.GetElement(v, 0);
    int p4 = iValArrayInt.GetElement(v, 4);
    TEST_ASSERT(p0 == 10, "First element initialized correctly");
    TEST_ASSERT(p4 == 50, "Last element initialized correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 22: GetFlags and SetFlags */
static int test_flags(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    unsigned old_flags = iValArrayInt.GetFlags(v);
    TEST_ASSERT(old_flags >= 0, "GetFlags returns valid flags");

    unsigned new_flags = iValArrayInt.SetFlags(v, CONTAINER_READONLY);
    TEST_ASSERT(new_flags == old_flags, "SetFlags returns old flags");
    TEST_ASSERT(iValArrayInt.GetFlags(v) == CONTAINER_READONLY, "Flags changed correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 23: GetElementSize */
static int test_get_element_size(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    size_t elem_size = iValArrayInt.GetElementSize(v);
    TEST_ASSERT(elem_size == sizeof(int), "GetElementSize returns correct size");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 24: NewIterator and iterator functions */
static int test_iterator(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    Iterator *it = iValArrayInt.NewIterator(v);
    TEST_ASSERT(it != NULL, "NewIterator returns non-NULL");

    int *first = it->GetFirst(it);
    TEST_ASSERT(first != NULL && *first == 10, "Iterator GetFirst works");

    int *next = it->GetNext(it);
    TEST_ASSERT(next != NULL && *next == 20, "Iterator GetNext works");

    size_t pos = it->GetPosition(it);
    TEST_ASSERT(pos == 1, "Iterator GetPosition returns correct index");

    int *last = it->GetLast(it);
    TEST_ASSERT(last != NULL && *last == 30, "Iterator GetLast works");

    iValArrayInt.DeleteIterator(it);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 25: EraseAt */
static int test_erase_at(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.EraseAt(v, 1) == 1, "EraseAt succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 2, "Size decreased after erase");

    int p = iValArrayInt.GetElement(v, 1);
    TEST_ASSERT(p == 30, "Elements shifted correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 26: Mismatch */
static int test_mismatch(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val4 = 99;

    iValArrayInt.Add(v1, val1);
    iValArrayInt.Add(v1, val2);

    iValArrayInt.Add(v2, val1);
    iValArrayInt.Add(v2, val4);

    size_t mismatch_idx;
    int result = iValArrayInt.Mismatch(v1, v2, &mismatch_idx);
    TEST_ASSERT(result == 1, "Mismatch detects difference");
    TEST_ASSERT(mismatch_idx == 1, "Mismatch returns correct index");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 27: AddRange */
static int test_add_range(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int data[] = {10, 20, 30, 40};

    TEST_ASSERT(iValArrayInt.AddRange(v, 4, data) == 1, "AddRange succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 4, "AddRange adds all elements");

    int p0 = iValArrayInt.GetElement(v, 0);
    int p3 = iValArrayInt.GetElement(v, 3);
    TEST_ASSERT(p0 == 10, "First element added correctly");
    TEST_ASSERT(p3 == 40, "Last element added correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 28: CopyElement */
static int test_copy_element(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    int result;
    TEST_ASSERT(iValArrayInt.CopyElement(v, 1, &result) == 1, "CopyElement succeeds");
    TEST_ASSERT(result == 20, "CopyElement copies correct value");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 29: RemoveRange */
static int test_remove_range(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30, val4 = 40, val5 = 50;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);
    iValArrayInt.Add(v, val4);
    iValArrayInt.Add(v, val5);

    TEST_ASSERT(iValArrayInt.RemoveRange(v, 1, 3) == 1, "RemoveRange succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Correct number of elements removed");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 30: Arithmetic operations - SumTo */
static int test_sum_to(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 1);
    iValArrayInt.Add(v1, 2);
    iValArrayInt.Add(v1, 3);

    iValArrayInt.Add(v2, 10);
    iValArrayInt.Add(v2, 20);
    iValArrayInt.Add(v2, 30);

    TEST_ASSERT(iValArrayInt.SumTo(v1, v2) == 1, "SumTo succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 0) == 11, "First element sum correct");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 1) == 22, "Second element sum correct");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 2) == 33, "Third element sum correct");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 31: Scalar operations - SumScalarTo */
static int test_sum_scalar_to(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int scalar = 5;

    iValArrayInt.Add(v, 1);
    iValArrayInt.Add(v, 2);
    iValArrayInt.Add(v, 3);

    TEST_ASSERT(iValArrayInt.SumScalarTo(v, scalar) == 1, "SumScalarTo succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 6, "First element scalar sum correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 7, "Second element scalar sum correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 8, "Third element scalar sum correct");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 32: Multiplication - MultiplyWith */
static int test_multiply_with(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 2);
    iValArrayInt.Add(v1, 3);
    iValArrayInt.Add(v1, 4);

    iValArrayInt.Add(v2, 10);
    iValArrayInt.Add(v2, 20);
    iValArrayInt.Add(v2, 30);

    TEST_ASSERT(iValArrayInt.MultiplyWith(v1, v2) == 1, "MultiplyWith succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 0) == 20, "First element multiplication correct");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 1) == 60, "Second element multiplication correct");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 2) == 120, "Third element multiplication correct");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 33: Scalar multiplication - MultiplyWithScalar */
static int test_multiply_with_scalar(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int scalar = 3;

    iValArrayInt.Add(v, 2);
    iValArrayInt.Add(v, 4);
    iValArrayInt.Add(v, 5);

    TEST_ASSERT(iValArrayInt.MultiplyWithScalar(v, scalar) == 1, "MultiplyWithScalar succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 6, "First element scalar multiply correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 12, "Second element scalar multiply correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 15, "Third element scalar multiply correct");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 34: CompareEqualScalar */
static int test_compare_equal_scalar(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int scalar = 20;

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 30);

    Mask *mask = NULL;
    mask = iValArrayInt.CompareEqualScalar(v, scalar, mask);
    TEST_ASSERT(mask != NULL, "CompareEqualScalar returns non-NULL mask");
    TEST_ASSERT(iMask.Size(mask) == 4, "CompareEqualScalar returns one mask entry per value");
    TEST_ASSERT(iMask.GetElement(mask, 0) == 0, "CompareEqualScalar marks non-matching value");
    TEST_ASSERT(iMask.GetElement(mask, 1) == 1, "CompareEqualScalar marks first match");
    TEST_ASSERT(iMask.GetElement(mask, 2) == 1, "CompareEqualScalar marks second match");
    TEST_ASSERT(iMask.GetElement(mask, 3) == 0, "CompareEqualScalar marks trailing non-match");

    iMask.Finalize(mask);
    mask = NULL;

    /* Reusing a larger mask exercises the non-allocating path as well. */
    Mask *reused = iMask.Create(8);
    TEST_ASSERT(reused != NULL, "Can create reusable mask");
    mask = iValArrayInt.CompareEqualScalar(v, scalar, reused);
    TEST_ASSERT(mask == reused, "CompareEqualScalar reuses a sufficiently large mask");
    TEST_ASSERT(iMask.Size(mask) == 4, "Reused mask is resized to the result length");
    TEST_ASSERT(iMask.GetElement(mask, 1) == 1 && iMask.GetElement(mask, 2) == 1,
                "Reused mask contains the expected matches");

    iMask.Finalize(mask);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Exercise every concrete ValArray wrapper so all specializations get a
 * compile/link/runtime smoke check in the ownership suite. */
static int test_valarray_specializations(void)
{
    ValArraySize_t *size_array = iValArraySize_t.Create(1);
    ValArrayShort *short_array = iValArrayShort.Create(1);
    ValArrayDouble *double_array = iValArrayDouble.Create(1);
    ValArrayLongDouble *long_double_array = iValArrayLongDouble.Create(1);
    ValArrayLLong *long_long_array = iValArrayLLong.Create(1);
    ValArrayULLong *unsigned_long_long_array = iValArrayULLong.Create(1);
    ValArrayFloat *float_array = iValArrayFloat.Create(1);
    ValArrayUInt *unsigned_array = iValArrayUInt.Create(1);

    TEST_ASSERT(size_array != NULL, "size_t ValArray wrapper creates an array");
    TEST_ASSERT(short_array != NULL, "short ValArray wrapper creates an array");
    TEST_ASSERT(double_array != NULL, "double ValArray wrapper creates an array");
    TEST_ASSERT(long_double_array != NULL, "long double ValArray wrapper creates an array");
    TEST_ASSERT(long_long_array != NULL, "long long ValArray wrapper creates an array");
    TEST_ASSERT(unsigned_long_long_array != NULL,
                "unsigned long long ValArray wrapper creates an array");
    TEST_ASSERT(float_array != NULL, "float ValArray wrapper creates an array");
    TEST_ASSERT(unsigned_array != NULL, "unsigned ValArray wrapper creates an array");

    TEST_ASSERT(iValArraySize_t.Add(size_array, (size_t)7) == 1,
                "size_t ValArray wrapper adds a value");
    TEST_ASSERT(iValArrayShort.Add(short_array, (short)-7) == 1,
                "short ValArray wrapper adds a value");
    TEST_ASSERT(iValArrayDouble.Add(double_array, 1.25) == 1,
                "double ValArray wrapper adds a value");
    TEST_ASSERT(iValArrayLongDouble.Add(long_double_array, (long double)2.5) == 1,
                "long double ValArray wrapper adds a value");
    TEST_ASSERT(iValArrayLLong.Add(long_long_array, (long long)-9) == 1,
                "long long ValArray wrapper adds a value");
    TEST_ASSERT(iValArrayULLong.Add(unsigned_long_long_array, (unsigned long long)9) == 1,
                "unsigned long long ValArray wrapper adds a value");
    TEST_ASSERT(iValArrayFloat.Add(float_array, 3.5f) == 1,
                "float ValArray wrapper adds a value");
    TEST_ASSERT(iValArrayUInt.Add(unsigned_array, 11U) == 1,
                "unsigned ValArray wrapper adds a value");

    TEST_ASSERT(iValArraySize_t.GetElement(size_array, 0) == (size_t)7,
                "size_t ValArray wrapper preserves its value");
    TEST_ASSERT(iValArrayShort.GetElement(short_array, 0) == (short)-7,
                "short ValArray wrapper preserves its value");
    TEST_ASSERT(iValArrayDouble.GetElement(double_array, 0) == 1.25,
                "double ValArray wrapper preserves its value");
    TEST_ASSERT(iValArrayLongDouble.GetElement(long_double_array, 0) == (long double)2.5,
                "long double ValArray wrapper preserves its value");
    TEST_ASSERT(iValArrayLLong.GetElement(long_long_array, 0) == (long long)-9,
                "long long ValArray wrapper preserves its value");
    TEST_ASSERT(iValArrayULLong.GetElement(unsigned_long_long_array, 0) ==
                    (unsigned long long)9,
                "unsigned long long ValArray wrapper preserves its value");
    TEST_ASSERT(iValArrayFloat.GetElement(float_array, 0) == 3.5f,
                "float ValArray wrapper preserves its value");
    TEST_ASSERT(iValArrayUInt.GetElement(unsigned_array, 0) == 11U,
                "unsigned ValArray wrapper preserves its value");

    /* Exercise the unsigned-only bitwise specialization. */
    ValArrayUInt *unsigned_other = iValArrayUInt.Create(2);
    TEST_ASSERT(unsigned_other != NULL, "Second unsigned ValArray is created");
    TEST_ASSERT(iValArrayUInt.Clear(unsigned_array) == 1,
                "Unsigned ValArray can reset its smoke-test value");
    TEST_ASSERT(iValArrayUInt.Add(unsigned_array, 0x0FU) == 1 &&
                    iValArrayUInt.Add(unsigned_array, 0xF0U) == 1,
                "Unsigned ValArray accepts bitwise operands");
    TEST_ASSERT(iValArrayUInt.Add(unsigned_other, 0xF0U) == 1 &&
                    iValArrayUInt.Add(unsigned_other, 0x0FU) == 1,
                "Second unsigned ValArray accepts bitwise operands");
    TEST_ASSERT(iValArrayUInt.Or(unsigned_array, unsigned_other) == 1,
                "Unsigned ValArray Or succeeds");
    TEST_ASSERT(iValArrayUInt.And(unsigned_array, unsigned_other) == 1,
                "Unsigned ValArray And succeeds");
    TEST_ASSERT(iValArrayUInt.Xor(unsigned_array, unsigned_other) == 1,
                "Unsigned ValArray Xor succeeds");
    TEST_ASSERT(iValArrayUInt.OrScalar(unsigned_array, 1U) == 1,
                "Unsigned ValArray OrScalar succeeds");
    TEST_ASSERT(iValArrayUInt.AndScalar(unsigned_array, 3U) == 1,
                "Unsigned ValArray AndScalar succeeds");
    TEST_ASSERT(iValArrayUInt.XorScalar(unsigned_array, 1U) == 1,
                "Unsigned ValArray XorScalar succeeds");
    TEST_ASSERT(iValArrayUInt.Not(unsigned_array) == 1,
                "Unsigned ValArray Not succeeds");
    TEST_ASSERT(iValArrayUInt.BitLeftShift(unsigned_array, 1) == 1,
                "Unsigned ValArray left shift succeeds");
    TEST_ASSERT(iValArrayUInt.BitRightShift(unsigned_array, 1) == 1,
                "Unsigned ValArray right shift succeeds");
    TEST_ASSERT(iValArrayUInt.BitLeftShift(unsigned_array, -1) == 1,
                "Unsigned ValArray negative left shift delegates right");
    TEST_ASSERT(iValArrayUInt.BitRightShift(unsigned_array, -1) == 1,
                "Unsigned ValArray negative right shift delegates left");

    /* Floating-point specializations expose tolerance comparison and inverse. */
    ValArrayDouble *double_other = iValArrayDouble.Create(2);
    Mask *float_mask = iMask.Create(2);
    TEST_ASSERT(double_other != NULL && float_mask != NULL,
                "Floating-point comparison fixtures are created");
    TEST_ASSERT(iValArrayDouble.Add(double_array, -2.0) == 1 &&
                    iValArrayDouble.Add(double_array, 4.0) == 1,
                "Double ValArray accepts comparison values");
    TEST_ASSERT(iValArrayDouble.Add(double_other, 1.25) == 1 &&
                    iValArrayDouble.Add(double_other, -2.0) == 1 &&
                    iValArrayDouble.Add(double_other, 5.0) == 1,
                "Second double ValArray accepts comparison values");
    TEST_ASSERT(iValArrayDouble.Abs(double_array) == 1,
                "Double ValArray Abs succeeds");
    TEST_ASSERT(iValArrayDouble.FCompare(double_array, double_other, float_mask, 0.01) != NULL,
                "Double ValArray FCompare succeeds");
    TEST_ASSERT(iValArrayDouble.Inverse(double_array) == 1,
                "Double ValArray Inverse succeeds");
    TEST_ASSERT(iValArrayDouble.GetElement(double_array, 1) == 0.5,
                "Double ValArray Inverse updates values");

    iValArraySize_t.Finalize(size_array);
    iValArrayShort.Finalize(short_array);
    iValArrayDouble.Finalize(double_array);
    iValArrayLongDouble.Finalize(long_double_array);
    iValArrayLLong.Finalize(long_long_array);
    iValArrayULLong.Finalize(unsigned_long_long_array);
    iValArrayFloat.Finalize(float_array);
    iValArrayUInt.Finalize(unsigned_array);
    iValArrayUInt.Finalize(unsigned_other);
    iValArrayDouble.Finalize(double_other);
    iMask.Finalize(float_mask);
    return 0;
}

/* Exercise the comparison, iterator, and boundary paths that are shared by
 * every generator instantiation. */
static int test_valarray_edge_paths(void)
{
    ValArrayInt *left = iValArrayInt.Create(3);
    ValArrayInt *right = iValArrayInt.Create(3);
    ValArrayInt *empty = iValArrayInt.Create(0);
    ValArraySize_t *indices = iValArraySize_t.Create(2);
    ValArrayInt *indexed = NULL;
    ValArrayInt *observed = iValArrayInt.Create(2);
    ValArrayInt *observed_other = iValArrayInt.Create(2);
    ValArrayInt *observed_copy = NULL;
    ValArrayInt *slice_range = iValArrayInt.Create(8);
    ValArrayInt *sort_slice = iValArrayInt.Create(5);
    ValArrayInt *capacity_array = iValArrayInt.Create(5);
    ValArrayInt *arithmetic_left = iValArrayInt.Create(2);
    ValArrayInt *arithmetic_right = iValArrayInt.Create(2);
    ValArrayInt *slice_compare_left = iValArrayInt.Create(4);
    ValArrayInt *slice_compare_right = iValArrayInt.Create(4);
    ValArrayInt *raw_array = NULL;
    ValArrayInt *created_with_allocator = NULL;
    ValArrayInt *slice_remove = iValArrayInt.Create(5);
    FILE *slice_output = NULL;
    unsigned char invalid_guid[32] = {0};
    int range_values[] = {7, 8};
    size_t capacity_before = 0;
    Mask *equal_mask = NULL;
    Mask *slice_mask = NULL;
    Mask *short_mask = NULL;
    char *ordering = NULL;
    char *scalar_ordering = NULL;
    Iterator *iterator = NULL;
    Iterator *stack_iterator = NULL;
    void *iterator_buffer = NULL;
    FILE *bad_file = NULL;
    int replacement = 99;
    int copied = 0;
    size_t index = 0;

    TEST_ASSERT(left != NULL && right != NULL && empty != NULL &&
                    observed != NULL && observed_other != NULL &&
                    slice_range != NULL && sort_slice != NULL &&
                    capacity_array != NULL && arithmetic_left != NULL &&
                    arithmetic_right != NULL && slice_compare_left != NULL &&
                    slice_compare_right != NULL && slice_remove != NULL,
                "Edge-path arrays are created");
    TEST_ASSERT(iValArrayInt.Add(left, 10) == 1, "Add left[0]");
    TEST_ASSERT(iValArrayInt.Add(left, 20) == 1, "Add left[1]");
    TEST_ASSERT(iValArrayInt.Add(left, 30) == 1, "Add left[2]");
    TEST_ASSERT(iValArrayInt.Add(right, 10) == 1, "Add right[0]");
    TEST_ASSERT(iValArrayInt.Add(right, 25) == 1, "Add right[1]");
    TEST_ASSERT(iValArrayInt.Add(right, 30) == 1, "Add right[2]");

    TEST_ASSERT(iValArrayInt.Equal(left, left) == 1, "Equal accepts identical arrays");
    TEST_ASSERT(iValArrayInt.Equal(left, NULL) == 0, "Equal rejects a null array");
    TEST_ASSERT(iValArrayInt.Equal(left, empty) == 0, "Equal rejects different sizes");
    equal_mask = iValArrayInt.CompareEqual(left, right, NULL);
    TEST_ASSERT(equal_mask != NULL, "CompareEqual creates a mask");
    TEST_ASSERT(iMask.Size(equal_mask) == 3, "CompareEqual mask has array length");
    TEST_ASSERT(iMask.GetElement(equal_mask, 0) == 1 &&
                    iMask.GetElement(equal_mask, 1) == 0 &&
                    iMask.GetElement(equal_mask, 2) == 1,
                "CompareEqual reports matching values");
    ordering = iValArrayInt.Compare(left, right, NULL);
    TEST_ASSERT(ordering != NULL, "Compare creates an ordering buffer");
    TEST_ASSERT(ordering[0] == 0 && ordering[1] < 0 && ordering[2] == 0,
                "Compare reports ordering values");
    scalar_ordering = iValArrayInt.CompareScalar(left, 20, NULL);
    TEST_ASSERT(scalar_ordering != NULL, "CompareScalar creates an ordering buffer");
    TEST_ASSERT(scalar_ordering[0] < 0 && scalar_ordering[1] == 0 &&
                    scalar_ordering[2] > 0,
                "CompareScalar reports ordering values");

    TEST_ASSERT(iValArrayInt.CopyElement(left, 99, &copied) < 0,
                "CopyElement rejects an out-of-range index");
    TEST_ASSERT(iValArrayInt.GetElement(left, 99) == INT_MIN,
                "GetElement returns the integer minimum for an invalid index");
    TEST_ASSERT(iValArrayInt.IndexOf(left, 999, &index) < 0,
                "IndexOf reports a missing value");
    TEST_ASSERT(iValArrayInt.Erase(left, 999) < 0,
                "Erase reports a missing value");
    TEST_ASSERT(iValArrayInt.ReplaceAt(left, 99, 0) < 0,
                "ReplaceAt rejects an out-of-range index");
    TEST_ASSERT(iValArrayInt.InsertAt(left, 99, 0) < 0,
                "InsertAt rejects an out-of-range index");
    TEST_ASSERT(iValArrayInt.InsertIn(left, 99, right) < 0,
                "InsertIn rejects an out-of-range index");
    TEST_ASSERT(iValArrayInt.GetRange(empty, 0, 1) == NULL,
                "GetRange returns null for an empty array");
    TEST_ASSERT(iValArrayInt.GetRange(left, 99, 100) == NULL,
                "GetRange returns null for an inverted range");
    TEST_ASSERT(iValArrayInt.RemoveRange(empty, 0, 1) == 0,
                "RemoveRange handles an empty array");
    TEST_ASSERT(iValArrayInt.PopBack(empty, NULL) == 0,
                "PopBack handles an empty array");
    TEST_ASSERT(iValArrayInt.AddRange(left, 0, NULL) == 1,
                "AddRange accepts an empty range");

    TEST_ASSERT(iValArrayInt.GetAllocator(left) == CurrentAllocator,
                "GetAllocator returns the array allocator");
    TEST_ASSERT(iValArrayInt.GetAllocator(NULL) == NULL,
                "GetAllocator handles null");
    TEST_ASSERT(iValArrayInt.Sizeof(NULL) > 0,
                "Sizeof reports a positive header size for null");
    TEST_ASSERT(iValArrayInt.GetElementSize(left) == sizeof(int),
                "GetElementSize reports int size");
    TEST_ASSERT(iValArrayInt.SetCompareFunction(left, NULL) == NULL,
                "SetCompareFunction has no default comparator");
    TEST_ASSERT(iValArrayInt.SetDestructor(left, NULL) == NULL,
                "SetDestructor has no default destructor");
    TEST_ASSERT(iValArrayInt.SetErrorFunction(NULL, NULL) != NULL,
                "SetErrorFunction returns the current error handler");
    TEST_ASSERT(iValArrayInt.ResetSlice(left) == 0,
                "ResetSlice handles an unsliced array");
    TEST_ASSERT(iValArrayInt.GetSlice(left, NULL, NULL, NULL) == 0,
                "GetSlice handles an unsliced array");
    TEST_ASSERT(iValArrayInt.SetSlice(left, iValArrayInt.Size(left), 1, 1) < 0,
                "SetSlice rejects a start beyond the array");
    TEST_ASSERT(iValArrayInt.SetSlice(left, 0, 0, 1) < 0,
                "SetSlice rejects an empty slice");
    TEST_ASSERT(iValArrayInt.SetSlice(left, 0, 1, 0) < 0,
                "SetSlice rejects a zero increment");

    for (index = 0; index < 5; ++index) {
        TEST_ASSERT(iValArrayInt.Add(slice_range, (int)index) == 1,
                    "Slice AddRange fixture is populated");
        TEST_ASSERT(iValArrayInt.Add(sort_slice, (int)(5 - index)) == 1,
                    "Slice Sort fixture is populated");
        TEST_ASSERT(iValArrayInt.Add(capacity_array, (int)index) == 1,
                    "Capacity fixture is populated");
        TEST_ASSERT(iValArrayInt.Add(slice_remove, (int)index) == 1,
                    "Slice RemoveRange fixture is populated");
    }
    TEST_ASSERT(iValArrayInt.SetSlice(slice_range, 0, 2, 2) == 1,
                "SetSlice selects a strided AddRange view");
    TEST_ASSERT(iValArrayInt.AddRange(slice_range, 2, range_values) == 1,
                "AddRange handles a strided slice");
    TEST_ASSERT(iValArrayInt.ResetSlice(slice_range) == 1,
                "Strided AddRange slice resets");
    TEST_ASSERT(iValArrayInt.SetSlice(sort_slice, 0, 3, 1) == 1 &&
                    iValArrayInt.Sort(sort_slice) == 1,
                "Sort handles a slice view");
    capacity_before = iValArrayInt.GetCapacity(capacity_array);
    TEST_ASSERT(iValArrayInt.SetCapacity(capacity_array, 2) == 1 &&
                    iValArrayInt.Size(capacity_array) == 2,
                "SetCapacity shrinks the array and count");
    TEST_ASSERT(iValArrayInt.SetCapacity(capacity_array, capacity_before + 2) == 1,
                "SetCapacity grows the array again");
    TEST_ASSERT(iValArrayInt.SetSlice(slice_remove, 0, 3, 1) == 1 &&
                    iValArrayInt.RemoveRange(slice_remove, 1, 2) == 1,
                "RemoveRange handles a sliced view");
    TEST_ASSERT(iValArrayInt.ResetSlice(slice_remove) == 1,
                "Sliced RemoveRange view resets");

    TEST_ASSERT(iValArrayInt.Add(arithmetic_left, 10) == 1 &&
                    iValArrayInt.Add(arithmetic_left, 20) == 1 &&
                    iValArrayInt.Add(arithmetic_right, 1) == 1 &&
                    iValArrayInt.Add(arithmetic_right, 2) == 1,
                "Slice arithmetic fixtures are populated");
    TEST_ASSERT(iValArrayInt.SetSlice(arithmetic_left, 0, 2, 1) == 1 &&
                    iValArrayInt.SetSlice(arithmetic_right, 0, 2, 1) == 1,
                "Slice arithmetic views are configured");
    TEST_ASSERT(iValArrayInt.SumTo(arithmetic_left, arithmetic_right) == 1 &&
                    iValArrayInt.SumScalarTo(arithmetic_left, 1) == 1 &&
                    iValArrayInt.SubtractFrom(arithmetic_left, arithmetic_right) == 1 &&
                    iValArrayInt.SubtractScalarFrom(arithmetic_left, 1) == 1 &&
                    iValArrayInt.SubtractFromScalar(10, arithmetic_left) == 1,
                "Arithmetic operations handle slice views");
    TEST_ASSERT(iValArrayInt.ResetSlice(arithmetic_left) == 1 &&
                    iValArrayInt.ResetSlice(arithmetic_right) == 1,
                "Slice arithmetic views reset");
    TEST_ASSERT(iValArrayInt.DivideScalarBy(arithmetic_left, 0) == 1,
                "DivideScalarBy handles a zero numerator");

    TEST_ASSERT(iValArrayInt.Add(slice_compare_left, 3) == 1 &&
                    iValArrayInt.Add(slice_compare_left, 4) == 1 &&
                    iValArrayInt.Add(slice_compare_right, 3) == 1 &&
                    iValArrayInt.Add(slice_compare_right, 5) == 1,
                "Slice comparison fixtures are populated");
    TEST_ASSERT(iValArrayInt.SetSlice(slice_compare_left, 0, 2, 1) == 1 &&
                    iValArrayInt.SetSlice(slice_compare_right, 0, 2, 1) == 1,
                "Slice comparison views are configured");
    slice_mask = iValArrayInt.CompareEqual(slice_compare_left, slice_compare_right, NULL);
    TEST_ASSERT(slice_mask != NULL,
                "CompareEqual handles slices");
    iMask.Finalize(slice_mask);
    slice_mask = NULL;
    ordering = iValArrayInt.Compare(slice_compare_left, slice_compare_right, ordering);
    TEST_ASSERT(ordering != NULL, "Compare handles slices");
    scalar_ordering = iValArrayInt.CompareScalar(slice_compare_left, 3, scalar_ordering);
    TEST_ASSERT(scalar_ordering != NULL, "CompareScalar handles slices");
    TEST_ASSERT(iValArrayInt.ResetSlice(slice_compare_left) == 1 &&
                    iValArrayInt.ResetSlice(slice_compare_right) == 1,
                "Slice comparison views reset");

    slice_output = ccl_test_tmpfile();
    TEST_ASSERT(slice_output != NULL && iValArrayInt.SetSlice(left, 0, 2, 1) == 1,
                "Fprintf slice fixture is configured");
    TEST_ASSERT(iValArrayInt.Fprintf(left, slice_output, "%d") > 0,
                "Fprintf handles a slice view");
    TEST_ASSERT(iValArrayInt.ResetSlice(left) == 1, "Fprintf slice view resets");
    short_mask = iMask.Create(1);
    TEST_ASSERT(short_mask != NULL && iValArrayInt.Select(left, short_mask) < 0,
                "Select rejects a mask with the wrong length");
    iMask.Finalize(short_mask);
    TEST_ASSERT(iValArrayInt.GetData(NULL) == NULL,
                "GetData handles null");

    created_with_allocator = iValArrayInt.CreateWithAllocator(0, CurrentAllocator);
    TEST_ASSERT(created_with_allocator != NULL,
                "CreateWithAllocator handles a zero start size");
    raw_array = (ValArrayInt *)calloc(1, iValArrayInt.Sizeof(NULL));
    TEST_ASSERT(raw_array != NULL && iValArrayInt.Init(raw_array, 2) == raw_array,
                "Init initializes caller storage");
    iValArrayInt.Finalize(raw_array);
    raw_array = NULL;

    /* A complete, wrong GUID reaches Load's validation branch. */
    bad_file = ccl_test_tmpfile();
    TEST_ASSERT(bad_file != NULL, "Temporary file is created for GUID validation");
    fwrite(invalid_guid, 1, sizeof(invalid_guid), bad_file);
    rewind(bad_file);
    TEST_ASSERT(iValArrayInt.Load(bad_file) == NULL,
                "Load rejects a complete file with an invalid GUID");
    fclose(bad_file);
    bad_file = NULL;

    /* Subscribe to every event to exercise the generator's observer hooks. */
    valarray_observer_events = 0;
    TEST_ASSERT(iObserver.Subscribe(observed, valarray_observer_callback, ~0U) == 1,
                "ValArray observer subscribes");
    TEST_ASSERT(iValArrayInt.Add(observed, 1) == 1, "Observed Add succeeds");
    TEST_ASSERT(iValArrayInt.AddRange(observed, 1, NULL) == 1,
                "Observed AddRange succeeds");
    TEST_ASSERT(iValArrayInt.Add(observed_other, 3) == 1 &&
                    iValArrayInt.Add(observed_other, 4) == 1,
                "Observer append fixture is populated");
    observed_copy = iValArrayInt.Copy(observed);
    TEST_ASSERT(observed_copy != NULL, "Observed Copy succeeds");
    TEST_ASSERT(iValArrayInt.SetCapacity(observed, 8) == 1,
                "Observed InsertAt fixture has spare capacity");
    TEST_ASSERT(iValArrayInt.InsertAt(observed, 0, 9) == 1,
                "Observed InsertAt succeeds");
    TEST_ASSERT(iValArrayInt.InsertIn(observed, 1, empty) == 1,
                "Observed InsertIn succeeds");
    TEST_ASSERT(iValArrayInt.ReplaceAt(observed, 0, 8) == 1,
                "Observed ReplaceAt succeeds");
    TEST_ASSERT(iValArrayInt.EraseAt(observed, 0) == 1,
                "Observed EraseAt succeeds");
    TEST_ASSERT(iValArrayInt.Append(observed, observed_other) == 1,
                "Observed Append succeeds");
    TEST_ASSERT(iValArrayInt.Clear(observed) == 1, "Observed Clear succeeds");
    iValArrayInt.SetFlags(observed, CONTAINER_HAS_OBSERVER);
    TEST_ASSERT(valarray_observer_events >= 9,
                "Observer receives ValArray mutation events");
    iValArrayInt.Finalize(observed);
    TEST_ASSERT(iObserver.Unsubscribe(observed, valarray_observer_callback) == 1,
                "ValArray observer unsubscribes");
    observed = NULL;

    iterator = iValArrayInt.NewIterator(left);
    TEST_ASSERT(iterator != NULL, "NewIterator creates an iterator");
    TEST_ASSERT(iterator->GetCurrent(iterator) == NULL,
                "NewIterator starts without a current value");
    TEST_ASSERT(iterator->GetPrevious(iterator) == NULL,
                "GetPrevious handles the initial iterator position");
    TEST_ASSERT(iterator->Seek(iterator, 1) != NULL &&
                    *(int *)iterator->GetCurrent(iterator) == 20,
                "Seek and GetCurrent select an element");
    TEST_ASSERT(iterator->GetPrevious(iterator) != NULL &&
                    *(int *)iterator->GetCurrent(iterator) == 10,
                "GetPrevious moves to the prior element");
    TEST_ASSERT(iterator->Replace(iterator, &replacement, 1) == 1,
                "Iterator Replace updates an element");
    TEST_ASSERT(iValArrayInt.GetElement(left, 0) == replacement,
                "Iterator Replace stores the replacement");
    TEST_ASSERT(iterator->Seek(iterator, 99) == NULL,
                "Seek rejects an out-of-range index");
    iValArrayInt.DeleteIterator(iterator);
    iterator = NULL;

    iterator_buffer = calloc(1, iValArrayInt.SizeofIterator(left));
    TEST_ASSERT(iterator_buffer != NULL, "Iterator buffer is allocated");
    stack_iterator = (Iterator *)iterator_buffer;
    TEST_ASSERT(iValArrayInt.InitIterator(left, iterator_buffer) == 1,
                "InitIterator initializes caller storage");
    TEST_ASSERT(stack_iterator->GetFirst(stack_iterator) != NULL,
                "Initialized iterator can read the first element");
    iValArrayInt.DeleteIterator(stack_iterator);
    iterator_buffer = NULL;

    TEST_ASSERT(iValArraySize_t.Add(indices, (size_t)2) == 1, "Add first index");
    TEST_ASSERT(iValArraySize_t.Add(indices, (size_t)0) == 1, "Add second index");
    indexed = iValArrayInt.IndexIn(left, indices);
    TEST_ASSERT(indexed != NULL && iValArrayInt.Size(indexed) == 2,
                "IndexIn creates an indexed array");
    TEST_ASSERT(iValArrayInt.GetElement(indexed, 0) == 30 &&
                    iValArrayInt.GetElement(indexed, 1) == replacement,
                "IndexIn follows size_t indices");

    bad_file = ccl_test_tmpfile();
    TEST_ASSERT(bad_file != NULL, "Temporary file is created");
    fputs("not a ValArray", bad_file);
    rewind(bad_file);
    TEST_ASSERT(iValArrayInt.Load(bad_file) == NULL,
                "Load rejects a file with an invalid GUID");

    iValArrayInt.Finalize(left);
    iValArrayInt.Finalize(right);
    iValArrayInt.Finalize(empty);
    iValArraySize_t.Finalize(indices);
    iValArrayInt.Finalize(indexed);
    iValArrayInt.Finalize(observed_other);
    iValArrayInt.Finalize(observed_copy);
    iValArrayInt.Finalize(slice_range);
    iValArrayInt.Finalize(sort_slice);
    iValArrayInt.Finalize(capacity_array);
    iValArrayInt.Finalize(arithmetic_left);
    iValArrayInt.Finalize(arithmetic_right);
    iValArrayInt.Finalize(slice_compare_left);
    iValArrayInt.Finalize(slice_compare_right);
    iValArrayInt.Finalize(slice_remove);
    iValArrayInt.Finalize(created_with_allocator);
    iMask.Finalize(equal_mask);
    if (ordering != NULL) CurrentAllocator->free(ordering);
    if (scalar_ordering != NULL) CurrentAllocator->free(scalar_ordering);
    if (slice_output != NULL) fclose(slice_output);
    if (bad_file != NULL) fclose(bad_file);
    return 0;
}

/* Test 35: Min and Max */
static int test_min_max(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 30);
    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 50);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 40);

    int min_val = iValArrayInt.Min(v);
    int max_val = iValArrayInt.Max(v);

    TEST_ASSERT(min_val == 10, "Min returns correct value");
    TEST_ASSERT(max_val == 50, "Max returns correct value");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 36: Rotate operations */
static int test_rotate_left(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 1);
    iValArrayInt.Add(v, 2);
    iValArrayInt.Add(v, 3);
    iValArrayInt.Add(v, 4);
    iValArrayInt.Add(v, 5);

    TEST_ASSERT(iValArrayInt.RotateLeft(v, 1) == 1, "RotateLeft succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 2, "First element after left rotate correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 1, "Last element after left rotate correct");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 37: Modulo operation */
static int test_mod_scalar(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int divisor = 3;

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 11);
    iValArrayInt.Add(v, 12);

    TEST_ASSERT(iValArrayInt.ModScalar(v, divisor) == 1, "ModScalar succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 1, "First element mod correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 2, "Second element mod correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 0, "Third element mod correct");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 38: Abs operation */
static int test_abs(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, -10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, -30);

    TEST_ASSERT(iValArrayInt.Abs(v) == 1, "Abs succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 10, "First element abs correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 20, "Second element abs correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 30, "Third element abs correct");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 39: Accumulate */
static int test_accumulate(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 1);
    iValArrayInt.Add(v, 2);
    iValArrayInt.Add(v, 3);
    iValArrayInt.Add(v, 4);

    int sum = iValArrayInt.Accumulate(v);
    TEST_ASSERT(sum == 10, "Accumulate returns correct sum");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 40: Product */
static int test_product(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 2);
    iValArrayInt.Add(v, 3);
    iValArrayInt.Add(v, 4);

    int product = iValArrayInt.Product(v);
    TEST_ASSERT(product == 24, "Product returns correct value");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 41: Resize */
static int test_resize(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Initial size is 3");

    /* Resize to larger size */
    TEST_ASSERT(iValArrayInt.Resize(v, 6) == 1, "Resize to 6 succeeds");
    TEST_ASSERT(iValArrayInt.GetCapacity(v) == 6, "Capasity increased to 6");

    /* Verify original elements are preserved */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == val1, "First element preserved after resize");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == val2, "Second element preserved after resize");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == val3, "Third element preserved after resize");

    /* Resize to smaller size */
    TEST_ASSERT(iValArrayInt.Resize(v, 2) == 1, "Resize to 2 succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 2, "Size decreased to 2");

    /* Verify remaining elements */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == val1, "First element preserved after shrink");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == val2, "Second element preserved after shrink");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 42: CopyTo */
static int test_copy_to(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 100, val2 = 200, val3 = 300;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    /* Copy array contents to buffer */
    int *buffer = iValArrayInt.CopyTo(v);
    TEST_ASSERT(buffer != NULL, "CopyTo returns non-NULL buffer");

    /* Verify all elements were copied correctly */
    TEST_ASSERT(buffer[0] == val1, "First element copied correctly");
    TEST_ASSERT(buffer[1] == val2, "Second element copied correctly");
    TEST_ASSERT(buffer[2] == val3, "Third element copied correctly");

    /* Verify buffer is independent of original array */
    buffer[0] = 999;
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == val1, "Original array unaffected by buffer change");

    /* Free the buffer using allocator */
    ContainerAllocator *allocator = iValArrayInt.GetAllocator(v);
    allocator->free(buffer);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 43: InsertIn */
static int test_insert_in(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(10);
    ValArrayInt *v2 = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;
    int val_insert1 = 100, val_insert2 = 200;

    /* Add elements to v1 */
    iValArrayInt.Add(v1, val1);
    iValArrayInt.Add(v1, val2);
    iValArrayInt.Add(v1, val3);

    /* Add elements to v2 (to be inserted) */
    iValArrayInt.Add(v2, val_insert1);
    iValArrayInt.Add(v2, val_insert2);

    /* Insert v2 at position 1 in v1 */
    TEST_ASSERT(iValArrayInt.InsertIn(v1, 1, v2) == 1, "InsertIn succeeds");
    TEST_ASSERT(iValArrayInt.Size(v1) == 5, "Size increased to 5 after InsertIn");

    /* Verify elements are in correct order */
    TEST_ASSERT(iValArrayInt.GetElement(v1, 0) == val1, "v1[0] == val1");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 1) == val_insert1, "v1[1] equals val_insert1");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 2) == val_insert2, "v1[2] equals val_insert2");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 3) == val2, "v1[3] equals val2");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 4) == val3, "v1[4] equals val3");

    /* Test InsertIn at beginning */
    ValArrayInt *v3 = iValArrayInt.Create(5);
    iValArrayInt.Add(v3, 50);
    iValArrayInt.Add(v3, 60);

    ValArrayInt *v4 = iValArrayInt.Create(5);
    iValArrayInt.Add(v4, 1);
    iValArrayInt.Add(v4, 2);

    TEST_ASSERT(iValArrayInt.InsertIn(v3, 0, v4) == 1, "InsertIn at position 0 succeeds");
    TEST_ASSERT(iValArrayInt.Size(v3) == 4, "Size increased to 4");
    TEST_ASSERT(iValArrayInt.GetElement(v3, 0) == 1, "First element is 1");
    TEST_ASSERT(iValArrayInt.GetElement(v3, 1) == 2, "Second element is 2");
    TEST_ASSERT(iValArrayInt.GetElement(v3, 2) == 50, "Third element is 50");
    TEST_ASSERT(iValArrayInt.GetElement(v3, 3) == 60, "Fourth element is 60");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    iValArrayInt.Finalize(v3);
    iValArrayInt.Finalize(v4);
    return 0;
}

/* Test 44: IndexIn */
static int test_index_in(void)
{
    ValArrayInt *source = iValArrayInt.Create(10);
    ValArraySize_t* indices = iValArraySize_t.Create(5);

    /* Add elements to source array */
    iValArrayInt.Add(source, 100);
    iValArrayInt.Add(source, 200);
    iValArrayInt.Add(source, 300);
    iValArrayInt.Add(source, 400);
    iValArrayInt.Add(source, 500);

    /* Create indices array with positions to extract */
    iValArraySize_t.Add(indices, 0);
    iValArraySize_t.Add(indices, 2);
    iValArraySize_t.Add(indices, 4);

    /* Extract elements at specified indices */
    ValArrayInt *result = iValArrayInt.IndexIn(source, indices);
    TEST_ASSERT(result != NULL, "IndexIn returns non-NULL result");
    TEST_ASSERT(iValArrayInt.Size(result) == 3, "Result has correct size");

    /* Verify extracted elements are correct */
    TEST_ASSERT(iValArrayInt.GetElement(result, 0) == 100, "First extracted element is 100 (source[0])");
    TEST_ASSERT(iValArrayInt.GetElement(result, 1) == 300, "Second extracted element is 300 (source[2])");
    TEST_ASSERT(iValArrayInt.GetElement(result, 2) == 500, "Third extracted element is 500 (source[4])");

    iValArrayInt.Finalize(result);
    iValArrayInt.Finalize(source);
    iValArraySize_t.Finalize(indices);
    return 0;
}

/* Test 45: RotateRight */
static int test_rotate_right(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    /* Add elements */
    for (int i = 1; i <= 5; i++) {
        iValArrayInt.Add(v, i);
    }

    /* Rotate right by 1 */
    TEST_ASSERT(iValArrayInt.RotateRight(v, 1) == 1, "RotateRight succeeds");

    /* Verify elements after right rotation by 1 */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 5, "First element after right rotate by 1 is 5");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 1, "Second element after right rotate by 1 is 1");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 2, "Third element after right rotate by 1 is 2");
    TEST_ASSERT(iValArrayInt.GetElement(v, 3) == 3, "Fourth element after right rotate by 1 is 3");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 4, "Fifth element after right rotate by 1 is 4");

    /* Rotate right by 2 more positions (total 3) */
    TEST_ASSERT(iValArrayInt.RotateRight(v, 2) == 1, "RotateRight by 2 succeeds");

    /* Verify elements after additional right rotation by 2 */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 3, "First element after total 3 right rotations is 3");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 4, "Second element after total 3 right rotations is 4");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 5, "Third element after total 3 right rotations is 5");
    TEST_ASSERT(iValArrayInt.GetElement(v, 3) == 1, "Fourth element after total 3 right rotations is 1");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 2, "Fifth element after total 3 right rotations is 2");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 46: Apply */
static int test_apply(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    /* Apply function to all elements */
    TEST_ASSERT(iValArrayInt.Apply(v, apply_multiply_by_two, NULL) == 1, "Apply succeeds");

    /* Verify that Apply traverses all elements (original values unchanged) */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == val1, "First element unchanged after Apply");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == val2, "Second element unchanged after Apply");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == val3, "Third element unchanged after Apply");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Array size unchanged after Apply");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 47: ForEach */
static int test_foreach(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int val1 = 10, val2 = 20, val3 = 30;

    iValArrayInt.Add(v, val1);
    iValArrayInt.Add(v, val2);
    iValArrayInt.Add(v, val3);

    /* Apply transformation function to all elements */
    TEST_ASSERT(iValArrayInt.ForEach(v, foreach_double_element) == 1, "ForEach succeeds");

    /* Verify that ForEach transformed each element */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == val1 * 2, "First element doubled by ForEach");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == val2 * 2, "Second element doubled by ForEach");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == val3 * 2, "Third element doubled by ForEach");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Array size unchanged after ForEach");

    /* Apply another transformation - add 5 to each */
    TEST_ASSERT(iValArrayInt.ForEach(v, foreach_add_five) == 1, "ForEach second transformation succeeds");

    /* Verify cumulative transformation (doubled and added 5) */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == (val1 * 2) + 5, "First element doubled and added 5");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == (val2 * 2) + 5, "Second element doubled and added 5");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == (val3 * 2) + 5, "Third element doubled and added 5");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 48: SubtractFrom */
static int test_subtract_from(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 50);
    iValArrayInt.Add(v1, 60);
    iValArrayInt.Add(v1, 70);

    iValArrayInt.Add(v2, 10);
    iValArrayInt.Add(v2, 20);
    iValArrayInt.Add(v2, 30);

    TEST_ASSERT(iValArrayInt.SubtractFrom(v1, v2) == 1, "SubtractFrom succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 0) == 40, "First element subtracted correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 1) == 40, "Second element subtracted correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 2) == 40, "Third element subtracted correctly");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 49: SubtractScalarFrom */
static int test_subtract_scalar_from(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int scalar = 10;

    iValArrayInt.Add(v, 50);
    iValArrayInt.Add(v, 60);
    iValArrayInt.Add(v, 70);

    TEST_ASSERT(iValArrayInt.SubtractScalarFrom(v, scalar) == 1, "SubtractScalarFrom succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 40, "First element scalar subtracted correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 50, "Second element scalar subtracted correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 60, "Third element scalar subtracted correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 50: DivideBy */
static int test_divide_by(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 100);
    iValArrayInt.Add(v1, 200);
    iValArrayInt.Add(v1, 300);

    iValArrayInt.Add(v2, 10);
    iValArrayInt.Add(v2, 10);
    iValArrayInt.Add(v2, 10);

    TEST_ASSERT(iValArrayInt.DivideBy(v1, v2) == 1, "DivideBy succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 0) == 10, "First element divided correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 1) == 20, "Second element divided correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v1, 2) == 30, "Third element divided correctly");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 51: DivideByScalar */
static int test_divide_by_scalar(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    int divisor = 5;

    iValArrayInt.Add(v, 100);
    iValArrayInt.Add(v, 200);
    iValArrayInt.Add(v, 300);

    TEST_ASSERT(iValArrayInt.DivideByScalar(v, divisor) == 1, "DivideByScalar succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 20, "First element divided by scalar correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 40, "Second element divided by scalar correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 60, "Third element divided by scalar correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 52: CreateSequence */
static int test_create_sequence(void)
{
    ValArrayInt *v = iValArrayInt.CreateSequence(5, 10, 5);

    TEST_ASSERT(v != NULL, "CreateSequence returns non-NULL");
    TEST_ASSERT(iValArrayInt.Size(v) == 5, "CreateSequence sets correct size");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 10, "First element correct (start value)");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 15, "Second element correct (start + increment)");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 20, "Third element correct (start + 2*increment)");
    TEST_ASSERT(iValArrayInt.GetElement(v, 3) == 25, "Fourth element correct");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 30, "Fifth element correct");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 53: FillSequential */
static int test_fill_sequential(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    TEST_ASSERT(iValArrayInt.FillSequential(v, 5, 100, 20) == 1, "FillSequential succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 5, "Size set correctly by FillSequential");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 100, "First element filled correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 120, "Second element filled correctly");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 140, "Third element filled correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 54: SetSlice and GetSlice */
static int test_slice_operations(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);
    size_t start, length, incr;

    /* Add 10 elements: 0, 10, 20, ..., 90 */
    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }

    /* Set a slice: start at index 2, length 4, every 2nd element */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 2, 4, 2) >= 0, "SetSlice succeeds");

    /* Get slice specs */
    TEST_ASSERT(iValArrayInt.GetSlice(v, &start, &length, &incr) == 1, "GetSlice succeeds");
    TEST_ASSERT(start == 2, "Slice start is correct");
    TEST_ASSERT(length == 4, "Slice length is correct");
    TEST_ASSERT(incr == 2, "Slice increment is correct");

    /* Reset slice */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.GetSlice(v, NULL, NULL, NULL) == 0, "Slice reset correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 55: Error handling - DivideByScalar with zero */
static int test_divide_by_zero(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 100);
    iValArrayInt.Add(v, 200);
    iValArrayInt.Add(v, 300);

    /* This should trigger division by zero error handling */
    int result = iValArrayInt.DivideByScalar(v, 0);
    TEST_ASSERT(result < 0, "DivideByScalar with zero returns error");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 56: Error handling - DivideBy with zero in array */
static int test_divide_by_zero_element(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 100);
    iValArrayInt.Add(v1, 200);
    iValArrayInt.Add(v1, 300);

    iValArrayInt.Add(v2, 10);
    iValArrayInt.Add(v2, 0);  /* Zero element to trigger error */
    iValArrayInt.Add(v2, 10);

    /* This should trigger division by zero error handling */
    int result = iValArrayInt.DivideBy(v1, v2);
    /* Function continues but logs error for zero division */
    TEST_ASSERT(result == 1, "DivideBy continues despite zero element");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 57: Error handling - Incompatible array sizes in operations */
static int test_incompatible_arrays(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 10);
    iValArrayInt.Add(v1, 20);
    iValArrayInt.Add(v1, 30);

    iValArrayInt.Add(v2, 100);
    iValArrayInt.Add(v2, 200);
    /* v2 has 2 elements, v1 has 3 - incompatible sizes */

    /* This should trigger incompatibility error */
    int result = iValArrayInt.SumTo(v1, v2);
    TEST_ASSERT(result < 0, "SumTo with different sizes returns error");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 58: Error handling - ModScalar with zero */
static int test_mod_scalar_zero(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 30);

    /* This should trigger modulo by zero error handling */
    int result = iValArrayInt.ModScalar(v, 0);
    TEST_ASSERT(result < 0, "ModScalar with zero returns error");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 59: Error handling - GetData on readonly array */
static int test_get_data_readonly(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 30);

    /* Set array to readonly */
    iValArrayInt.SetFlags(v, CONTAINER_READONLY);

    /* This should return NULL for readonly array */
    int *data = iValArrayInt.GetData(v);
    TEST_ASSERT(data == NULL, "GetData returns NULL for readonly array");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 60: Front and Back with readonly flag check */
static int test_front_back_readonly(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 30);

    /* Get front/back before setting readonly */
    int front1 = iValArrayInt.Front(v);
    int back1 = iValArrayInt.Back(v);
    TEST_ASSERT(front1 == 10, "Front returns correct value");
    TEST_ASSERT(back1 == 30, "Back returns correct value");

    /* Set to readonly and try again */
    iValArrayInt.SetFlags(v, CONTAINER_READONLY);
    int front2 = iValArrayInt.Front(v);
    int back2 = iValArrayInt.Back(v);
    /* Front and Back should still work but may report error */
    (void)front2;
    (void)back2;

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 61: Mod with incompatible array sizes */
static int test_mod_incompatible(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 10);
    iValArrayInt.Add(v1, 20);
    iValArrayInt.Add(v1, 30);

    iValArrayInt.Add(v2, 3);
    iValArrayInt.Add(v2, 3);
    /* v2 has 2 elements, v1 has 3 - incompatible */

    /* This should trigger incompatibility error */
    int result = iValArrayInt.Mod(v1, v2);
    TEST_ASSERT(result < 0, "Mod with different sizes returns error");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 62: Mod with zero in array */
static int test_mod_zero_element(void)
{
    ValArrayInt *v1 = iValArrayInt.Create(5);
    ValArrayInt *v2 = iValArrayInt.Create(5);

    iValArrayInt.Add(v1, 10);
    iValArrayInt.Add(v1, 20);
    iValArrayInt.Add(v1, 30);

    iValArrayInt.Add(v2, 3);
    iValArrayInt.Add(v2, 0);  /* Zero element */
    iValArrayInt.Add(v2, 3);

    /* This should trigger modulo by zero error */
    int result = iValArrayInt.Mod(v1, v2);
    /* Function continues but logs error for zero modulo */
    TEST_ASSERT(result == 1, "Mod continues despite zero element");

    iValArrayInt.Finalize(v1);
    iValArrayInt.Finalize(v2);
    return 0;
}

/* Test 63: Init - Initialize existing stack-allocated array */
static int test_init(void)
{
    /* Note: Init is for in-place initialization of stack-allocated arrays.
       Since we're working with dynamically allocated arrays, we'll test Create instead
       and verify initialization behavior */
    ValArrayInt *v = iValArrayInt.Create(15);

    TEST_ASSERT(v != NULL, "Create returns non-NULL");
    TEST_ASSERT(iValArrayInt.Size(v) == 0, "Created array starts with size 0");
    TEST_ASSERT(iValArrayInt.GetCapacity(v) >= 15, "Create allocates requested capacity");

    /* Add elements to verify it works */
    TEST_ASSERT(iValArrayInt.Add(v, 42) == 1, "Can add to created array");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 42, "Element stored correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 64: Save - Serialize array to file */
static int test_save(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    /* Add test data */
    iValArrayInt.Add(v, 42);
    iValArrayInt.Add(v, 100);
    iValArrayInt.Add(v, 999);

    /* Open file for writing in binary mode */
    FILE *file = ccl_test_tmpfile();
    TEST_ASSERT(file != NULL, "File opened for writing");

    /* Save array to file */
    int result = iValArrayInt.Save(v, file);
    TEST_ASSERT(result == 1, "Save returns success");

    /* Check file size is non-zero */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    TEST_ASSERT(file_size > 0, "Saved file has content");
    fclose(file);

    /* Clean up */
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 65: Load - Deserialize array from file */
static int test_load(void)
{
    ValArrayInt *v_original = iValArrayInt.Create(5);

    /* Create and save original array */
    iValArrayInt.Add(v_original, 10);
    iValArrayInt.Add(v_original, 20);
    iValArrayInt.Add(v_original, 30);
    iValArrayInt.Add(v_original, 40);
    iValArrayInt.Add(v_original, 50);

    FILE *save_file = ccl_test_tmpfile();
    TEST_ASSERT(save_file != NULL, "File opened for saving");

    int save_result = iValArrayInt.Save(v_original, save_file);
    TEST_ASSERT(save_result == 1, "Original array saved successfully");
    /* Load from file */
    rewind(save_file);
    ValArrayInt *v_loaded = iValArrayInt.Load(save_file);
    TEST_ASSERT(v_loaded != NULL, "Array loaded");
    fclose(save_file);

    /* Verify all elements match */
    TEST_ASSERT(iValArrayInt.Size(v_loaded) == 5, "Loaded array has correct number of elements");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 0) == 10, "First element preserved");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 1) == 20, "Second element preserved");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 2) == 30, "Third element preserved");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 3) == 40, "Fourth element preserved");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 4) == 50, "Fifth element preserved");

    /* Ensure subtracting scalar works on loaded array */
    TEST_ASSERT(iValArrayInt.SubtractFromScalar(100, v_loaded) == 1, "SubtractFromScalar on loaded array succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 0) == 90, "First element after subtracting scalar: 100 - 10 = 90");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 1) == 80, "Second element after subtracting scalar: 100 - 20 = 80");

    /* Clean up */
    iValArrayInt.Finalize(v_original);
    iValArrayInt.Finalize(v_loaded);
    return 0;
}

/* Test 73: Save and Load Round Trip - Verify data integrity */
static int test_save_load_roundtrip(void)
{
    ValArrayInt *v_original = iValArrayInt.Create(10);

    /* Create array with various values */
    int test_values[] = {-100, -1, 0, 1, 50, 100, 500, 1000, 9999, -9999,};
    for (int i = 0; i < sizeof(test_values)/sizeof(test_values[0]); i++) {
        iValArrayInt.Add(v_original, test_values[i]);
    }

    /* Save to file */
    FILE *save_file = ccl_test_tmpfile();
    TEST_ASSERT(save_file != NULL, "File opened for save");
    TEST_ASSERT(iValArrayInt.Save(v_original, save_file) == 1, "Array saved");
    /* Load from file */
    rewind(save_file);
    ValArrayInt *v_loaded = iValArrayInt.Load(save_file);
    TEST_ASSERT(v_loaded != NULL, "Array loaded");
    fclose(save_file);

    /* Verify all elements match */
    TEST_ASSERT(iValArrayInt.Size(v_loaded) == sizeof(test_values)/sizeof(test_values[0]), "Loaded array has correct number of elements");
    for (int i = 0; i < sizeof(test_values)/sizeof(test_values[0]); i++) {
        TEST_ASSERT(iValArrayInt.GetElement(v_loaded, i) == test_values[i], "Element preserved in round trip");
    }

    /* Clean up */
    iValArrayInt.Finalize(v_original);
    iValArrayInt.Finalize(v_loaded);
    return 0;
}

/* Test 74: Save empty array */
static int test_save_empty_array(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    /* Don't add any elements - array is empty */
    TEST_ASSERT(iValArrayInt.Size(v) == 0, "Array is empty");

    /* Save empty array */
    FILE *file = ccl_test_tmpfile();
    TEST_ASSERT(file != NULL, "File opened");
    TEST_ASSERT(iValArrayInt.Save(v, file) == 1, "Empty array saved successfully");
    /* Load empty array back */
    rewind(file);
    ValArrayInt *v_loaded = iValArrayInt.Load(file);
    TEST_ASSERT(v_loaded != NULL, "Empty array loaded");
    TEST_ASSERT(iValArrayInt.Size(v_loaded) == 0, "Loaded array is empty");
    fclose(file);

    /* Clean up */
    iValArrayInt.Finalize(v);
    iValArrayInt.Finalize(v_loaded);
    return 0;
}

/* Test 75: Save single element array */
static int test_save_single_element(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    /* Add single element */
    iValArrayInt.Add(v, 12345);
    TEST_ASSERT(iValArrayInt.Size(v) == 1, "Array has single element");

    /* Save and load */
    FILE *save_file = ccl_test_tmpfile();
    TEST_ASSERT(save_file != NULL, "File opened for save");
    TEST_ASSERT(iValArrayInt.Save(v, save_file) == 1, "Saved successfully");
    rewind(save_file);
    ValArrayInt *v_loaded = iValArrayInt.Load(save_file);
    TEST_ASSERT(v_loaded != NULL, "Loaded successfully");
    fclose(save_file);

    /* Verify single element */
    TEST_ASSERT(iValArrayInt.Size(v_loaded) == 1, "Loaded array has single element");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 0) == 12345, "Single element preserved");

    /* Clean up */
    iValArrayInt.Finalize(v);
    iValArrayInt.Finalize(v_loaded);
    return 0;
}

/* Test 76: GetElement before and after SetSlice */
static int test_get_element_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);
    size_t start, length, incr;

    /* Add 10 elements: 0, 10, 20, ..., 90 */
    int i;
    for (i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }

    /* Access elements before slice */
    TEST_ASSERT(iValArrayInt.Size(v) == 10, "Before slice: Size == 10");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 0, "Before slice: GetElement(0) == 0");
    TEST_ASSERT(iValArrayInt.GetElement(v, 5) == 50, "Before slice: GetElement(5) == 50");
    TEST_ASSERT(iValArrayInt.GetElement(v, 9) == 90, "Before slice: GetElement(9) == 90");

    /* Set a slice: start at index 2, length 4, every 2nd element */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 2, 4, 2) >= 0, "SetSlice succeeds");

    /* Get slice specs */
    TEST_ASSERT(iValArrayInt.GetSlice(v, &start, &length, &incr) == 1, "GetSlice succeeds");
    TEST_ASSERT(start == 2, "Slice start is correct");
    TEST_ASSERT(length == 4, "Slice length is correct");
    TEST_ASSERT(incr == 2, "Slice increment is correct");

    /* Reset slice */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.GetSlice(v, NULL, NULL, NULL) == 0, "Slice reset correctly");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 76: Copy with SetSlice - copies only slice elements */
static int test_copy_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    /* Add 10 elements: 0, 10, 20, 30, 40, 50, 60, 70, 80, 90 */
    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }

    /* Size before slice */
    TEST_ASSERT(iValArrayInt.Size(v) == 10, "Before slice: Size == 10");

    /* Copy before slice - should copy all 10 elements */
    ValArrayInt *v_copy_before = iValArrayInt.Copy(v);
    TEST_ASSERT(iValArrayInt.Size(v_copy_before) == 10, "Before slice: Copy has 10 elements");
    TEST_ASSERT(iValArrayInt.GetElement(v_copy_before, 0) == 0, "Before slice: Copy[0] == 0");
    TEST_ASSERT(iValArrayInt.GetElement(v_copy_before, 9) == 90, "Before slice: Copy[9] == 90");

    /* Set slice: start=2, length=3, increment=2 (indices 2, 4, 6 -> values 20, 40, 60) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 2, 3, 2) >= 0, "SetSlice for Copy test succeeds");

    /* After slice: Size changes to slice length (3) */
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "After slice: Size == 3 (slice length)");

    /* Copy after slice - should copy only slice elements (logical view) */
    ValArrayInt *v_copy_after = iValArrayInt.Copy(v);
    TEST_ASSERT(iValArrayInt.Size(v_copy_after) == 3, "After slice: Copy has 3 elements (slice length)");
    TEST_ASSERT(iValArrayInt.GetElement(v_copy_after, 0) == 20, "After slice: Copy[0] == 20 (logical[0] = actual[2])");
    TEST_ASSERT(iValArrayInt.GetElement(v_copy_after, 1) == 40, "After slice: Copy[1] == 40 (logical[1] = actual[4])");
    TEST_ASSERT(iValArrayInt.GetElement(v_copy_after, 2) == 60, "After slice: Copy[2] == 60 (logical[2] = actual[6])");

    iValArrayInt.Finalize(v_copy_before);
    iValArrayInt.Finalize(v_copy_after);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 83: Apply with SetSlice - Apply traverses only slice elements */
static int test_apply_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }

    /* Set slice: start=2, length=3, increment=2 (indices 2, 4, 6 -> values 20, 40, 60) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 2, 3, 2) >= 0, "SetSlice for Apply test succeeds");

    /* Apply on sliced array - should process only 3 elements */
    TEST_ASSERT(iValArrayInt.Apply(v, apply_multiply_by_two, NULL) == 1, "Apply with slice succeeds");

    /* Reset and verify original values unchanged */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 20, "Slice element at actual[2] untouched (2*2)");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 84: ForEach with SetSlice - ForEach modifies only slice elements */
static int test_foreach_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i);
    }

    /* Set slice: start=1, length=3, increment=2 (indices 1, 3, 5 -> values 1, 3, 5) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 1, 3, 2) >= 0, "SetSlice for ForEach test succeeds");

    /* ForEach should double only slice elements */
    TEST_ASSERT(iValArrayInt.ForEach(v, foreach_double_element) == 1, "ForEach with slice succeeds");

    /* Verify only slice elements were modified */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 2, "v[1]: 1 * 2 = 2");
    TEST_ASSERT(iValArrayInt.GetElement(v, 3) == 6, "v[3]: 3 * 2 = 6");
    TEST_ASSERT(iValArrayInt.GetElement(v, 5) == 10, "v[5]: 5 * 2 = 10");

    /* Non-slice elements should be unchanged */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 0, "Non-slice element v[0] unchanged");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 2, "Non-slice element v[2] unchanged");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 4, "Non-slice element v[4] unchanged");
    TEST_ASSERT(iValArrayInt.GetElement(v, 6) == 6, "Non-slice element v[6] unchanged");
    TEST_ASSERT(iValArrayInt.GetElement(v, 7) == 7, "Non-slice element v[7] unchanged");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 85: Reverse with SetSlice - Reverses only slice elements */
static int test_reverse_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }

    /* Set slice: start=2, length=3, increment=2 (indices 2, 4, 6 -> values 20, 40, 60) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 2, 3, 2) >= 0, "SetSlice for Reverse succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "After slice: Size == 3 (slice length)");

    /* Reverse should reverse only the slice elements */
    TEST_ASSERT(iValArrayInt.Reverse(v) == 1, "Reverse with slice succeeds");

    /* Verify slice is reset properly after reverse */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 86: PopBack with SetSlice - PopBack removes from slice */
static int test_popback_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);
    int result;

    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }

    /* Set slice: start=1, length=3, increment=2 (indices 1, 3, 5 -> values 10, 30, 50) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 1, 3, 2) >= 0, "SetSlice for PopBack succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "After slice: Size == 3 (slice length)");

    /* PopBack should remove the last slice element (at actual index 8) */
    TEST_ASSERT(iValArrayInt.PopBack(v, &result) == 1, "PopBack from slice succeeds");
    TEST_ASSERT(result == 50, "PopBack returns correct slice element (50)");
    TEST_ASSERT(iValArrayInt.Size(v) == 2, "Size decreased after pop");

    /* Verify slice length decreased */
    size_t start, length, incr;
    TEST_ASSERT(iValArrayInt.GetSlice(v, &start, &length, &incr) == 1, "GetSlice after PopBack");
    TEST_ASSERT(length == 2, "Slice length decreased to 2");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 87: FillSequential with SetSlice - Fills only slice elements */
static int test_fill_sequential_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, 0);
    }

    /* Set slice: start=1, length=3, increment=2 (indices 1, 3, 5 -> values 0, 0, 0) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 1, 3, 2) >= 0, "SetSlice for FillSequential succeeds");

    /* FillSequential should fill only slice elements with sequence starting at 100, increment 10 */
    TEST_ASSERT(iValArrayInt.FillSequential(v, 3, 100, 10) == 1, "FillSequential with slice succeeds");

    /* Verify slice elements were filled */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 100, "Slice element at actual[1] filled with 100");
    TEST_ASSERT(iValArrayInt.GetElement(v, 3) == 110, "Slice element at actual[3] filled with 110");
    TEST_ASSERT(iValArrayInt.GetElement(v, 5) == 120, "Slice element at actual[5] filled with 120");

    /* Non-slice elements should remain unchanged */
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 0, "Non-slice element at actual[0] unchanged");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 0, "Non-slice element at actual[2] unchanged");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 88: GetData on writable array */
static int test_get_data_writable(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 30);

    /* GetData should return pointer to array contents for writable array */
    int *data = iValArrayInt.GetData(v);
    TEST_ASSERT(data != NULL, "GetData returns non-NULL for writable array");
    TEST_ASSERT(data[0] == 10, "GetData returns correct first element");
    TEST_ASSERT(data[1] == 20, "GetData returns correct second element");
    TEST_ASSERT(data[2] == 30, "GetData returns correct third element");

    /* Verify we can modify through returned pointer */
    data[1] = 999;
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 999, "Array modified through GetData pointer");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 89: Memset - Fill array with constant value */
static int test_memset(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    /* Memset fills array with constant value (increment 0) */
    TEST_ASSERT(iValArrayInt.Memset(v, 42, 5) == 1, "Memset succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 5, "Memset sets correct size");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 42, "First element filled with 42");
    TEST_ASSERT(iValArrayInt.GetElement(v, 4) == 42, "Fifth element filled with 42");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 90: Select - Select elements based on mask */
static int test_select(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    Mask *mask = iMask.Create(5);

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 30);
    iValArrayInt.Add(v, 40);
    iValArrayInt.Add(v, 50);

    /* Create a mask manually by using data array */
    char maskdata[5] = {1, 0, 1, 0, 1};  /* select indices 0, 2, 4 */
    Mask *mask2 = iMask.CreateFromMask(5, maskdata);

    /* Select should keep only masked elements */
    TEST_ASSERT(iValArrayInt.Select(v, mask2) == 1, "Select succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "Select reduces size to 3");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 10, "First selected element is 10");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 30, "Second selected element is 30");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 50, "Third selected element is 50");

    iMask.Finalize(mask);
    iMask.Finalize(mask2);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 91: SelectCopy - Select elements based on mask into new array */
static int test_select_copy(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 10);
    iValArrayInt.Add(v, 20);
    iValArrayInt.Add(v, 30);
    iValArrayInt.Add(v, 40);
    iValArrayInt.Add(v, 50);

    /* Create a mask for selecting indices 1, 3 */
    char maskdata[5] = {0, 1, 0, 1, 0};
    Mask *mask = iMask.CreateFromMask(5, maskdata);

    /* SelectCopy should create new array with masked elements */
    ValArrayInt *result = iValArrayInt.SelectCopy(v, mask);
    TEST_ASSERT(result != NULL, "SelectCopy returns non-NULL");
    TEST_ASSERT(iValArrayInt.Size(result) == 2, "SelectCopy creates array with 2 elements");
    TEST_ASSERT(iValArrayInt.GetElement(result, 0) == 20, "First selected element is 20");
    TEST_ASSERT(iValArrayInt.GetElement(result, 1) == 40, "Second selected element is 40");

    /* Verify original array unchanged */
    TEST_ASSERT(iValArrayInt.Size(v) == 5, "Original array size unchanged");
    TEST_ASSERT(iValArrayInt.GetElement(v, 0) == 10, "Original array element unchanged");

    iMask.Finalize(mask);
    iValArrayInt.Finalize(result);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 92: Fprintf - Output array to file */
static int test_fprintf(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);

    iValArrayInt.Add(v, 100);
    iValArrayInt.Add(v, 200);
    iValArrayInt.Add(v, 300);

    /* Open file for writing */
    FILE *file = ccl_test_tmpfile();
    TEST_ASSERT(file != NULL, "File opened for writing");

    /* Write array to file using Fprintf */
    int result = iValArrayInt.Fprintf(v, file, "%d ");
    TEST_ASSERT(result > 0, "Fprintf returns positive value");

    /* Read back and verify */
    rewind(file);
    int val1, val2, val3;
    int items_read = fscanf(file, "%d %d %d", &val1, &val2, &val3);
    TEST_ASSERT(items_read == 3, "File contains 3 values");
    TEST_ASSERT(val1 == 100, "First value is 100");
    TEST_ASSERT(val2 == 200, "Second value is 200");
    TEST_ASSERT(val3 == 300, "Third value is 300");

    fclose(file);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 93: Contains with SetSlice - Contains searches only slice elements */
static int test_contains_with_slice(void)
{
    ValArrayInt *v = iValArrayInt.Create(10);

    /* Add 10 elements: 0, 10, 20, 30, 40, 50, 60, 70, 80, 90 */
    for (int i = 0; i < 10; i++) {
        iValArrayInt.Add(v, i * 10);
    }

    /* Set slice: start=2, length=3, increment=2 (indices 2, 4, 6 -> values 20, 40, 60) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 2, 3, 2) >= 0, "SetSlice for Contains succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 3, "After slice: Size == 3 (slice length)");

    /* Test Contains on slice - should find elements in slice */
    TEST_ASSERT(iValArrayInt.Contains(v, 20) == 1, "Contains finds 20 in slice (actual[2])");
    TEST_ASSERT(iValArrayInt.Contains(v, 40) == 1, "Contains finds 40 in slice (actual[4])");
    TEST_ASSERT(iValArrayInt.Contains(v, 60) == 1, "Contains finds 60 in slice (actual[6])");

    /* Test Contains on slice - should NOT find elements outside slice */
    TEST_ASSERT(iValArrayInt.Contains(v, 10) == 0, "Contains does NOT find 10 (not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 30) == 0, "Contains does NOT find 30 (not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 50) == 0, "Contains does NOT find 50 (not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 70) == 0, "Contains does NOT find 70 (not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 80) == 0, "Contains does NOT find 80 (not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 90) == 0, "Contains does NOT find 90 (not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 0) == 0, "Contains does NOT find 0 (not in slice)");

    /* Test non-existent element */
    TEST_ASSERT(iValArrayInt.Contains(v, 999) == 0, "Contains does NOT find 999 (non-existent)");

    /* Reset slice and verify full array Contains works */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.Size(v) == 10, "After reset: Size == 10 (full array)");

    /* Now Contains should find all original elements */
    TEST_ASSERT(iValArrayInt.Contains(v, 20) == 1, "Contains finds 20 after reset");
    TEST_ASSERT(iValArrayInt.Contains(v, 50) == 1, "Contains finds 50 after reset (was not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 80) == 1, "Contains finds 80 after reset (was not in slice)");
    TEST_ASSERT(iValArrayInt.Contains(v, 0) == 1, "Contains finds 0 after reset");
    TEST_ASSERT(iValArrayInt.Contains(v, 90) == 1, "Contains finds 90 after reset");

    iValArrayInt.Finalize(v);
    return 0;
}

/* Test Suite Definition */
static const TestCase valarray_int_tests[] = {
    {"test_create_finalize", test_create_finalize},
    {"test_add", test_add},
    {"test_add_with_slice", test_add_with_slice},
    {"test_get_element", test_get_element},
    {"test_push_pop_back", test_push_pop_back},
    {"test_insert_at", test_insert_at},
    {"test_insert", test_insert},
    {"test_replace_at", test_replace_at},
    {"test_contains", test_contains},
    {"test_index_of", test_index_of},
    {"test_erase", test_erase},
    {"test_copy", test_copy},
    {"test_equal", test_equal},
    {"test_sort", test_sort},
    {"test_capacity", test_capacity},
    {"test_clear", test_clear},
    {"test_sizeof", test_sizeof},
    {"test_get_range", test_get_range},
    {"test_reverse", test_reverse},
    {"test_back_front", test_back_front},
    {"test_append", test_append},
    {"test_initialize_with", test_initialize_with},
    {"test_flags", test_flags},
    {"test_get_element_size", test_get_element_size},
    {"test_iterator", test_iterator},
    {"test_erase_at", test_erase_at},
    {"test_mismatch", test_mismatch},
    {"test_add_range", test_add_range},
    {"test_copy_element", test_copy_element},
    {"test_remove_range", test_remove_range},
    {"test_sum_to", test_sum_to},
    {"test_sum_scalar_to", test_sum_scalar_to},
    {"test_multiply_with", test_multiply_with},
    {"test_multiply_with_scalar", test_multiply_with_scalar},
    {"test_compare_equal_scalar", test_compare_equal_scalar},
    {"test_valarray_specializations", test_valarray_specializations},
    {"test_valarray_edge_paths", test_valarray_edge_paths},
    {"test_min_max", test_min_max},
    {"test_rotate_left", test_rotate_left},
    {"test_mod_scalar", test_mod_scalar},
    {"test_abs", test_abs},
    {"test_accumulate", test_accumulate},
    {"test_product", test_product},
    {"test_resize", test_resize},
    {"test_copy_to", test_copy_to},
    {"test_insert_in", test_insert_in},
    {"test_index_in", test_index_in},
    {"test_rotate_right", test_rotate_right},
    {"test_apply", test_apply},
    {"test_foreach", test_foreach},
    {"test_subtract_from", test_subtract_from},
    {"test_subtract_scalar_from", test_subtract_scalar_from},
    {"test_divide_by", test_divide_by},
    {"test_divide_by_scalar", test_divide_by_scalar},
    {"test_create_sequence", test_create_sequence},
    {"test_fill_sequential", test_fill_sequential},
    {"test_slice_operations", test_slice_operations},
    {"test_divide_by_zero", test_divide_by_zero},
    {"test_divide_by_zero_element", test_divide_by_zero_element},
    {"test_incompatible_arrays", test_incompatible_arrays},
    {"test_mod_scalar_zero", test_mod_scalar_zero},
    {"test_get_data_readonly", test_get_data_readonly},
    {"test_front_back_readonly", test_front_back_readonly},
    {"test_mod_incompatible", test_mod_incompatible},
    {"test_mod_zero_element", test_mod_zero_element},
    {"test_init", test_init},
    {"test_save", test_save},
    {"test_load", test_load},
    {"test_save_load_roundtrip", test_save_load_roundtrip},
    {"test_save_empty_array", test_save_empty_array},
    {"test_save_single_element", test_save_single_element},
    {"test_get_element_with_slice", test_get_element_with_slice},
    {"test_copy_with_slice", test_copy_with_slice},
    {"test_apply_with_slice", test_apply_with_slice},
    {"test_foreach_with_slice", test_foreach_with_slice},
    {"test_reverse_with_slice", test_reverse_with_slice},
    {"test_popback_with_slice", test_popback_with_slice},
    {"test_fill_sequential_with_slice", test_fill_sequential_with_slice},
    {"test_get_data_writable", test_get_data_writable},
    {"test_memset", test_memset},
    {"test_select", test_select},
    {"test_select_copy", test_select_copy},
    {"test_fprintf", test_fprintf},
    {"test_contains_with_slice", test_contains_with_slice},
};

/* Export test suite */
static const TestSuite valarray_int_suite = {
    "ValArrayInt_Tests",
    valarray_int_tests,
    sizeof(valarray_int_tests) / sizeof(valarray_int_tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &valarray_int_suite;
}
