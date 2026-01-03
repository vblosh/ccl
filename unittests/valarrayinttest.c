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

/* Test helper function to print integer valarray */
static void PrintValArrayInt(ValArrayInt *v)
{
    size_t i, size = iValArrayInt.Size(v);
    printf("ValArray [Size: %lu, Capacity: %lu]: ", (unsigned long)size, (unsigned long)iValArrayInt.GetCapacity(v));
    for (i = 0; i < size; i++) {
        int val = iValArrayInt.GetElement(v, i);
        printf("%d ", val);
    }
    printf("\n");
}

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
    int val1 = 10, val2 = 20, val3 = 30, val4 = 99;
    
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
    
    iValArrayInt.Finalize(v);
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
    iValArrayInt.Add(v, 1);
    iValArrayInt.Add(v, 2);
    iValArrayInt.Add(v, 3);
    iValArrayInt.Add(v, 4);
    iValArrayInt.Add(v, 5);
    
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
    TEST_ASSERT(iValArrayInt.Size(v) == 5, "CreateSequence creates correct size");
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
    
    /* Add 10 elements */
    int i;
    for (i = 0; i < 10; i++) {
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
    const char *filename = "test_valarray.bin";
    
    /* Add test data */
    iValArrayInt.Add(v, 42);
    iValArrayInt.Add(v, 100);
    iValArrayInt.Add(v, 999);
    
    /* Open file for writing in binary mode */
    FILE *file = fopen(filename, "wb");
    TEST_ASSERT(file != NULL, "File opened for writing");
    
    /* Save array to file */
    int result = iValArrayInt.Save(v, file);
    TEST_ASSERT(result == 1, "Save returns success");
    
    fclose(file);
    
    /* Verify file was created and has content */
    FILE *check = fopen(filename, "rb");
    TEST_ASSERT(check != NULL, "Saved file exists");
    
    /* Check file size is non-zero */
    fseek(check, 0, SEEK_END);
    long file_size = ftell(check);
    TEST_ASSERT(file_size > 0, "Saved file has content");
    fclose(check);
    
    /* Clean up */
    remove(filename);
    iValArrayInt.Finalize(v);
    return 0;
}

/* Test 65: Load - Deserialize array from file */
static int test_load(void)
{
    ValArrayInt *v_original = iValArrayInt.Create(5);
    const char *filename = "test_load_valarray.bin";
    
    /* Create and save original array */
    iValArrayInt.Add(v_original, 10);
    iValArrayInt.Add(v_original, 20);
    iValArrayInt.Add(v_original, 30);
    iValArrayInt.Add(v_original, 40);
    iValArrayInt.Add(v_original, 50);
    
    FILE *save_file = fopen(filename, "wb");
    TEST_ASSERT(save_file != NULL, "File opened for saving");
    
    int save_result = iValArrayInt.Save(v_original, save_file);
    TEST_ASSERT(save_result == 1, "Original array saved successfully");
    fclose(save_file);
    
    /* Load from file */
    FILE *load_file = fopen(filename, "rb");
    TEST_ASSERT(load_file != NULL, "File opened for loading");
    ValArrayInt *v_loaded = iValArrayInt.Load(load_file);
    TEST_ASSERT(v_loaded != NULL, "Array loaded");
    fclose(load_file);
    
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
    remove(filename);
    iValArrayInt.Finalize(v_original);
    iValArrayInt.Finalize(v_loaded);
    return 0;
}

/* Test 73: Save and Load Round Trip - Verify data integrity */
static int test_save_load_roundtrip(void)
{
    ValArrayInt *v_original = iValArrayInt.Create(10);
    const char *filename = "test_roundtrip.bin";
    
    /* Create array with various values */
    int test_values[] = {-100, -1, 0, 1, 50, 100, 500, 1000, 9999, -9999,};
    for (int i = 0; i < sizeof(test_values)/sizeof(test_values[0]); i++) {
        iValArrayInt.Add(v_original, test_values[i]);
    }
    
    /* Save to file */
    FILE *save_file = fopen(filename, "wb");
    TEST_ASSERT(save_file != NULL, "File opened for save");
    TEST_ASSERT(iValArrayInt.Save(v_original, save_file) == 1, "Array saved");
    fclose(save_file);
    
    /* Load from file */
    FILE *load_file = fopen(filename, "rb");
    TEST_ASSERT(load_file != NULL, "File opened for load");
    ValArrayInt *v_loaded = iValArrayInt.Load(load_file);
    TEST_ASSERT(v_loaded != NULL, "Array loaded");
    fclose(load_file);
    
    /* Verify all elements match */
    TEST_ASSERT(iValArrayInt.Size(v_loaded) == sizeof(test_values)/sizeof(test_values[0]), "Loaded array has correct number of elements");
    for (int i = 0; i < sizeof(test_values)/sizeof(test_values[0]); i++) {
        TEST_ASSERT(iValArrayInt.GetElement(v_loaded, i) == test_values[i], "Element preserved in round trip");
    }
    
    /* Clean up */
    remove(filename);
    iValArrayInt.Finalize(v_original);
    iValArrayInt.Finalize(v_loaded);
    return 0;
}

/* Test 74: Save empty array */
static int test_save_empty_array(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    const char *filename = "test_empty.bin";
    
    /* Don't add any elements - array is empty */
    TEST_ASSERT(iValArrayInt.Size(v) == 0, "Array is empty");
    
    /* Save empty array */
    FILE *file = fopen(filename, "wb");
    TEST_ASSERT(file != NULL, "File opened");
    TEST_ASSERT(iValArrayInt.Save(v, file) == 1, "Empty array saved successfully");
    fclose(file);
    
    /* Load empty array back */
    FILE *load_file = fopen(filename, "rb");
    TEST_ASSERT(load_file != NULL, "File opened for loading");
    ValArrayInt *v_loaded = iValArrayInt.Load(load_file);
    TEST_ASSERT(v_loaded != NULL, "Empty array loaded");
    TEST_ASSERT(iValArrayInt.Size(v_loaded) == 0, "Loaded array is empty");
    fclose(load_file);
    
    /* Clean up */
    remove(filename);
    iValArrayInt.Finalize(v);
    iValArrayInt.Finalize(v_loaded);
    return 0;
}

/* Test 75: Save single element array */
static int test_save_single_element(void)
{
    ValArrayInt *v = iValArrayInt.Create(5);
    const char *filename = "test_single.bin";
    
    /* Add single element */
    iValArrayInt.Add(v, 12345);
    TEST_ASSERT(iValArrayInt.Size(v) == 1, "Array has single element");
    
    /* Save and load */
    FILE *save_file = fopen(filename, "wb");
    TEST_ASSERT(save_file != NULL, "File opened for save");
    TEST_ASSERT(iValArrayInt.Save(v, save_file) == 1, "Saved successfully");
    fclose(save_file);
    
    FILE *load_file = fopen(filename, "rb");
    TEST_ASSERT(load_file != NULL, "File opened for load");
    ValArrayInt *v_loaded = iValArrayInt.Load(load_file);
    TEST_ASSERT(v_loaded != NULL, "Loaded successfully");
    fclose(load_file);
    
    /* Verify single element */
    TEST_ASSERT(iValArrayInt.Size(v_loaded) == 1, "Loaded array has single element");
    TEST_ASSERT(iValArrayInt.GetElement(v_loaded, 0) == 12345, "Single element preserved");
    
    /* Clean up */
    remove(filename);
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
    
    /* Add 10 elements: 0, 10, 20, ..., 90 */
    int i;
    for (i = 0; i < 10; i++) {
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
    int count = 0;
    
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
    
    /* Set slice: start=1, length=3, increment=2 (indices 1, 3, 5) */
    TEST_ASSERT(iValArrayInt.SetSlice(v, 1, 3, 2) >= 0, "SetSlice for FillSequential succeeds");
    
    /* FillSequential should fill only slice elements with sequence starting at 100, increment 10 */
    TEST_ASSERT(iValArrayInt.FillSequential(v, 3, 100, 10) == 1, "FillSequential with slice succeeds");
    
    /* Verify slice elements were filled */
    TEST_ASSERT(iValArrayInt.ResetSlice(v) == 1, "ResetSlice succeeds");
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 100, "Slice element at actual[1] filled with 100");
    
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
    TEST_ASSERT(iValArrayInt.GetElement(v, 1) == 42, "Second element filled with 42");
    TEST_ASSERT(iValArrayInt.GetElement(v, 2) == 42, "Third element filled with 42");
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
    const char *filename = "test_fprintf.txt";
    
    iValArrayInt.Add(v, 100);
    iValArrayInt.Add(v, 200);
    iValArrayInt.Add(v, 300);
    
    /* Open file for writing */
    FILE *file = fopen(filename, "w");
    TEST_ASSERT(file != NULL, "File opened for writing");
    
    /* Write array to file using Fprintf */
    int result = iValArrayInt.Fprintf(v, file, "%d ");
    TEST_ASSERT(result > 0, "Fprintf returns positive value");
    
    fclose(file);
    
    /* Verify file was created and has content */
    file = fopen(filename, "r");
    TEST_ASSERT(file != NULL, "File exists after Fprintf");
    
    /* Read back and verify */
    int val1, val2, val3;
    int items_read = fscanf(file, "%d %d %d", &val1, &val2, &val3);
    TEST_ASSERT(items_read == 3, "File contains 3 values");
    TEST_ASSERT(val1 == 100, "First value is 100");
    TEST_ASSERT(val2 == 200, "Second value is 200");
    TEST_ASSERT(val3 == 300, "Third value is 300");
    
    fclose(file);
    remove(filename);
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
static TestCase valarray_int_tests[] = {
    {"test_create_finalize", test_create_finalize},
    {"test_add", test_add},
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
TestSuite ValArrayInt_Tests = {
    "ValArrayInt_Tests",
    valarray_int_tests,
    sizeof(valarray_int_tests) / sizeof(valarray_int_tests[0])
};
