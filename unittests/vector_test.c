#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "ccl_internal.h"

static int compare_int(const void *left,const void *right,CompareInfo *info)
{
	(void)info;
	return *(const int *)left - *(const int *)right;
}

static int increment_int(void *value,void *arg)
{
	*(int *)value += arg ? *(int *)arg : 1;
	return 1;
}

static int save_raw_int(const void *value,void *arg,FILE *stream)
{
	(void)arg;
	return fwrite(value,sizeof(int),1,stream) == 1;
}

static int load_raw_int(void *value,void *arg,FILE *stream)
{
	(void)arg;
	return fread(value,sizeof(int),1,stream) == 1;
}

static int destructor_calls;

static int destroy_owned(void *value)
{
	struct owned { int *value; };
	struct owned *owned = value;
	if (owned->value != NULL) {
		++destructor_calls;
		free(owned->value);
		owned->value = NULL;
	}
	return 1;
}

static int value_at(Vector *v,size_t index)
{
	return *(int *)iVector.GetElement(v,index);
}

static int fail_malloc;
static int fail_realloc;
static int fail_calloc;
static int malloc_countdown = -1;

static void *vector_test_malloc(size_t size)
{
	if (malloc_countdown == 0) return NULL;
	if (malloc_countdown > 0) malloc_countdown--;
	return fail_malloc ? NULL : malloc(size);
}

static void vector_test_free(void *memory)
{
	free(memory);
}

static void *vector_test_realloc(void *memory, size_t size)
{
	return fail_realloc ? NULL : realloc(memory, size);
}

static void *vector_test_calloc(size_t count, size_t size)
{
	return fail_calloc ? NULL : calloc(count, size);
}

static ContainerAllocator vector_test_allocator = {
	vector_test_malloc,
	vector_test_free,
	vector_test_realloc,
	vector_test_calloc,
};

static int observer_notification_count;
static void vector_observer_callback(const void *object, unsigned event, const void *ExtraInfo[])
{
	(void)object;
	(void)event;
	(void)ExtraInfo;
	++observer_notification_count;
}

static int fallback_value = 999;
static void *vector_copy_element_error_handler(const char *fn, int code, ...)
{
	(void)fn;
	(void)code;
	return &fallback_value;
}

static int failing_save(const void *value, void *arg, FILE *stream)
{
	(void)value;
	(void)arg;
	(void)stream;
	return 0;
}

static int failing_load(void *value, void *arg, FILE *stream)
{
	(void)value;
	(void)arg;
	(void)stream;
	return 0;
}

static int test_range_capacity_and_append(void)
{
	Vector *v = NULL, *other = NULL, *range = NULL, *copy = NULL;
	Vector *indices = NULL, *selected = NULL;
	Mask *mask = NULL, *comparison = NULL;
	int values[] = { 1, 2, 3, 4, 5, 6 };
	int extra[] = { 7, 8 };
	int expected[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	int scalar = 4;
	size_t i, index = 0, mismatch = 0;
	size_t before_select;
	int result = 1;

	v = iVector.Create(sizeof(int),1);
	other = iVector.InitializeWith(sizeof(int),2,extra);
	TEST_REQUIRE(v != NULL && other != NULL);
	TEST_REQUIRE(iVector.AddRange(v,6,values) == 1);
	TEST_REQUIRE(iVector.Size(v) == 6 && iVector.GetCapacity(v) >= 6);
	TEST_REQUIRE(iVector.SetCapacity(v,12) == 1);
	for (i=0; i<6; ++i) TEST_REQUIRE(value_at(v,i) == values[i]);
	TEST_REQUIRE(iVector.SetCapacity(v,4) == 1 && iVector.Size(v) == 4);
	for (i=0; i<4; ++i) TEST_REQUIRE(value_at(v,i) == values[i]);
	TEST_REQUIRE(iVector.Resize(v,7) == 1 && iVector.Size(v) == 7);
	TEST_REQUIRE(value_at(v,4) == 0 && value_at(v,6) == 0);
	TEST_REQUIRE(iVector.Append(v,other) == 1 && iVector.Size(v) == 9);
	TEST_REQUIRE(value_at(v,7) == 7 && value_at(v,8) == 8);
	TEST_REQUIRE(iVector.Append(other,other) == 1 && iVector.Size(other) == 4);
	TEST_REQUIRE(value_at(other,2) == 7 && value_at(other,3) == 8);

	/* Appending a vector to itself must snapshot the source before realloc. */
	TEST_REQUIRE(iVector.Clear(v) == 1);
	TEST_REQUIRE(iVector.AddRange(v,6,values) == 1);
	TEST_REQUIRE(iVector.Append(v,v) == 1 && iVector.Size(v) == 12);
	for (i=0; i<12; ++i) TEST_REQUIRE(value_at(v,i) == values[i % 6]);
	TEST_REQUIRE(iVector.AddRange(v,2,iVector.GetElement(v,0)) == 1);
	TEST_REQUIRE(value_at(v,12) == 1 && value_at(v,13) == 2);

	range = iVector.GetRange(v,2,5);
	TEST_REQUIRE(range != NULL && iVector.Size(range) == 4);
	for (i=0; i<4; ++i) TEST_REQUIRE(value_at(range,i) == value_at(v,i+2));
	TEST_REQUIRE(iVector.GetRange(v,5,2) == NULL);
	TEST_REQUIRE(iVector.RemoveRange(v,2,5) == 1);
	TEST_REQUIRE(iVector.Size(v) == 11 && value_at(v,2) == 6 && value_at(v,6) == 4);

	TEST_REQUIRE(iVector.InsertAt(v,2,&scalar) == 1);
	TEST_REQUIRE(value_at(v,2) == 4);
	TEST_REQUIRE(iVector.InsertIn(v,3,range) == 1);
	TEST_REQUIRE(iVector.Size(v) == 16);
	TEST_REQUIRE(iVector.EraseAt(v,2) == 1);
	TEST_REQUIRE(iVector.Erase(v,&scalar) == 1);
	TEST_REQUIRE(iVector.EraseAll(v,&scalar) == 1);
	TEST_REQUIRE(iVector.Contains(v,&scalar,NULL) == 0);

	copy = iVector.Copy(v);
	TEST_REQUIRE(copy != NULL && iVector.Equal(v,copy) == 1);
	TEST_REQUIRE(iVector.IndexOf(v,&values[0],NULL,&index) == 1);
	TEST_REQUIRE(index < iVector.Size(v));
	TEST_REQUIRE(iVector.Contains(v,&values[1],NULL) == 1);
	TEST_REQUIRE(iVector.Mismatch(v,copy,&mismatch) == 0);

	indices = iVector.Create(sizeof(size_t),2);
	{
		size_t a = 0, b = 2;
		TEST_REQUIRE(iVector.Add(indices,&a) == 1 && iVector.Add(indices,&b) == 1);
	}
	selected = iVector.IndexIn(v,indices);
	TEST_REQUIRE(selected != NULL && iVector.Size(selected) == 2);
	iVector.Finalize(selected);
	selected = NULL;
	TEST_REQUIRE(iVector.SearchWithKey(v,0,sizeof(int),3,&values[5],&index) == 1 && index >= 3);

	mask = iMask.Create(iVector.Size(v));
	TEST_REQUIRE(mask != NULL);
	for (i=0; i<iVector.Size(v); ++i)
		TEST_REQUIRE(iMask.SetElement(mask,i,(i % 2) == 0) == 1);
	selected = iVector.SelectCopy(v,mask);
	TEST_REQUIRE(selected != NULL && iVector.Size(selected) == (iVector.Size(v)+1)/2);
	iVector.Finalize(selected); selected = NULL;
	before_select = iVector.Size(v);
	TEST_REQUIRE(iVector.Select(v,mask) == 1 && iVector.Size(v) == (before_select+1)/2);

	/* Reusable masks retain their logical length after comparison. */
	comparison = iVector.CompareEqual(v,v,mask);
	TEST_REQUIRE(comparison == mask && iMask.Size(comparison) == iVector.Size(v));
	comparison = iVector.CompareEqualScalar(v,&scalar,mask);
	TEST_REQUIRE(comparison == mask && iMask.Size(comparison) == iVector.Size(v));
	(void)iVector.SetCompareFunction(v,compare_int);
	comparison = iVector.CompareEqualScalar(v,&scalar,mask);
	TEST_REQUIRE(comparison != NULL && iMask.Size(comparison) == iVector.Size(v));
	result = 0;

cleanup:
	if (comparison != NULL && comparison != mask) iMask.Finalize(comparison);
	if (mask != NULL) iMask.Finalize(mask);
	if (selected != NULL) iVector.Finalize(selected);
	if (indices != NULL) iVector.Finalize(indices);
	if (copy != NULL) iVector.Finalize(copy);
	if (range != NULL) iVector.Finalize(range);
	if (other != NULL) iVector.Finalize(other);
	if (v != NULL) iVector.Finalize(v);
	(void)expected;
	return result;
}

static int test_owned_selection_and_removal(void)
{
	typedef struct { int *value; } Owned;
	Vector *v = NULL;
	Mask *mask = NULL;
	Owned owned;
	size_t i;
	int result = 1;

	destructor_calls = 0;
	v = iVector.Create(sizeof(Owned),4);
	TEST_REQUIRE(v != NULL);
	iVector.SetDestructor(v,destroy_owned);
	for (i=0; i<4; ++i) {
		owned.value = malloc(sizeof(*owned.value));
		TEST_REQUIRE(owned.value != NULL);
		*owned.value = (int)i;
		TEST_REQUIRE(iVector.Add(v,&owned) == 1);
	}
	mask = iMask.Create(4);
	TEST_REQUIRE(mask != NULL);
	TEST_REQUIRE(iMask.SetElement(mask,0,0) == 1 &&
	            iMask.SetElement(mask,1,1) == 1 &&
	            iMask.SetElement(mask,2,0) == 1 &&
	            iMask.SetElement(mask,3,1) == 1);
	TEST_REQUIRE(iVector.Select(v,mask) == 1 && iVector.Size(v) == 2);
	TEST_REQUIRE(destructor_calls == 2);
	TEST_REQUIRE(iVector.Resize(v,1) == 1 && destructor_calls == 3);
	TEST_REQUIRE(iVector.SetCapacity(v,0) == 1 && iVector.Size(v) == 0);
	TEST_REQUIRE(destructor_calls == 4);
	result = 0;

cleanup:
	if (mask != NULL) iMask.Finalize(mask);
	if (v != NULL) iVector.Finalize(v);
	return result;
}

static int test_iterators_and_persistence(void)
{
	Vector *v = NULL, *loaded = NULL;
	Iterator *heap = NULL, *placement = NULL;
	FILE *stream = NULL;
	unsigned char *storage = NULL;
	int values[] = { 10, 20, 30 };
	int replacement = 25;
	int result = 1;

	v = iVector.InitializeWith(sizeof(int),3,values);
	TEST_REQUIRE(v != NULL);
	heap = iVector.NewIterator(v);
	TEST_REQUIRE(heap != NULL && *(int *)heap->GetNext(heap) == 10);
	TEST_REQUIRE(heap->GetNext(heap) != NULL && heap->GetPosition(heap) == 1);
	TEST_REQUIRE(*(int *)heap->GetLast(heap) == 30);
	TEST_REQUIRE(*(int *)heap->GetPrevious(heap) == 20);
	TEST_REQUIRE(*(int *)heap->Seek(heap,0) == 10);
	TEST_REQUIRE(heap->GetCurrent(heap) != NULL);
	TEST_REQUIRE(heap->Replace(heap,&replacement,1) == 1 && value_at(v,0) == 25);
	iVector.DeleteIterator(heap); heap = NULL;

	storage = malloc(iVector.SizeofIterator(v));
	TEST_REQUIRE(storage != NULL && iVector.InitIterator(v,storage) == 1);
	placement = (Iterator *)storage;
	TEST_REQUIRE(*(int *)placement->GetNext(placement) == 25);
	TEST_REQUIRE(iVector.DeleteIterator(placement) == 1);
	placement = NULL;

	heap = iVector.NewIterator(v);
	TEST_REQUIRE(heap != NULL);
	TEST_REQUIRE(iVector.Add(v,&replacement) == 1);
	TEST_REQUIRE(heap->GetNext(heap) == NULL);
	iVector.DeleteIterator(heap); heap = NULL;

	stream = ccl_test_tmpfile();
	TEST_REQUIRE(stream != NULL && iVector.Save(v,stream,NULL,NULL) == 1);
	rewind(stream);
	loaded = iVector.Load(stream,NULL,NULL);
	TEST_REQUIRE(loaded != NULL && iVector.Equal(v,loaded) == 1);
	result = 0;

cleanup:
	if (heap != NULL) iVector.DeleteIterator(heap);
	if (placement != NULL) iVector.DeleteIterator(placement);
	free(storage);
	if (stream != NULL) fclose(stream);
	if (loaded != NULL) iVector.Finalize(loaded);
	if (v != NULL) iVector.Finalize(v);
	return result;
}

static int test_readonly_and_edges(void)
{
	Vector *v = NULL;
	int value = 1;
	int result = 1;

	TEST_REQUIRE(iVector.Create(0,1) == NULL);
	TEST_REQUIRE(iVector.CreateWithAllocator(sizeof(int),1,NULL) == NULL);
	v = iVector.Create(sizeof(int),0);
	TEST_REQUIRE(v != NULL);
	TEST_REQUIRE(iVector.Add(v,&value) == 1);
	iVector.SetFlags(v,CONTAINER_READONLY);
	TEST_REQUIRE(iVector.Add(v,&value) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Resize(v,2) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Select(v,NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iVector.SetCapacity(v,10) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Append(v,v) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.GetElement(v,0) == NULL);
	iVector.SetFlags(v,0);
	TEST_REQUIRE(iVector.Clear(v) == 1 && iVector.Size(v) == 0);
	TEST_REQUIRE(iVector.Resize(v,0) == 0);
	result = 0;

cleanup:
	if (v != NULL) iVector.Finalize(v);
	return result;
}

static int test_remaining_surface(void)
{
	Vector *v = NULL, *copy = NULL, *other = NULL, *loaded = NULL;
	void **copied = NULL;
	Vector placement;
	Iterator *iterator = NULL;
	FILE *stream = NULL;
	int values[] = { 3, 1, 2 }, value = 4, delta = 1, out = 0;
	size_t index = 0, mismatch = 0, iterator_size;
	int result = 1;

	v = iVector.Create(sizeof(int),2);
	TEST_REQUIRE(v != NULL);
	TEST_REQUIRE(iVector.GetAllocator(v) != NULL);
	TEST_REQUIRE(iVector.Sizeof(v) >= sizeof(Vector));
	TEST_REQUIRE(iVector.SizeofIterator(v) > sizeof(Iterator));
	(void)iVector.SetErrorFunction(v,NULL);
	(void)iVector.SetCompareFunction(v,NULL);
	TEST_REQUIRE(iVector.AddRange(v,0,NULL) == 1);
	TEST_REQUIRE(iVector.SetCapacity(v,iVector.GetCapacity(v)) == 0);
	TEST_REQUIRE(iVector.Add(v,&values[0]) == 1);
	TEST_REQUIRE(iVector.PushBack(v,&values[1]) == 1);
	TEST_REQUIRE(iVector.Insert(v,&values[2]) == 1);
	TEST_REQUIRE(iVector.Front(v) != NULL && value_at(v,0) == 2);
	TEST_REQUIRE(iVector.Back(v) != NULL && value_at(v,2) == 1);
	TEST_REQUIRE(iVector.CopyElement(v,1,&out) == 1 && out == 3);
	TEST_REQUIRE(iVector.CopyElement(v,99,&out) < 0);
	TEST_REQUIRE(iVector.CopyElement(v,0,NULL) < 0);
	copied = iVector.CopyTo(v);
	TEST_REQUIRE(copied != NULL && copied[0] != NULL && *(int *)copied[0] == 2);
	for (index=0; copied[index] != NULL; ++index) free(copied[index]);
	free(copied); copied = NULL;

	TEST_REQUIRE(iVector.ReplaceAt(v,1,&value) == 1 && value_at(v,1) == 4);
	TEST_REQUIRE(iVector.PopBack(v,&out) == 1 && out == 1);
	TEST_REQUIRE(iVector.PopBack(v,NULL) == 1);
	TEST_REQUIRE(iVector.PopBack(v,NULL) == 1);
	TEST_REQUIRE(iVector.PopBack(v,NULL) == 0);
	TEST_REQUIRE(iVector.EraseAt(v,99) < 0);
	TEST_REQUIRE(iVector.AddRange(v,3,values) == 1);
	{
		Vector *clamped = iVector.GetRange(v,1,999);
		TEST_REQUIRE(clamped != NULL && iVector.Size(clamped) == iVector.Size(v)-1);
		iVector.Finalize(clamped);
	}
	TEST_REQUIRE(iVector.RemoveRange(v,0,0) == 0);
	TEST_REQUIRE(iVector.Apply(v,increment_int,&delta) == 1);
	TEST_REQUIRE(value_at(v,0) == 4);
	TEST_REQUIRE(iVector.SetCompareFunction(v,compare_int) != NULL);
	TEST_REQUIRE(iVector.Sort(v) == 1);
	TEST_REQUIRE(iVector.Reverse(v) == 1);
	TEST_REQUIRE(iVector.RotateLeft(v,1) == 1);
	TEST_REQUIRE(iVector.RotateRight(v,7) == 1);
	TEST_REQUIRE(iVector.Reverse(v) == 1);
	TEST_REQUIRE(iVector.RotateLeft(v,0) == 0);
	TEST_REQUIRE(iVector.RotateRight(v,0) == 0);
	TEST_REQUIRE(iVector.Reserve(v,iVector.GetCapacity(v)+5) == 1);
	copy = iVector.Copy(v);
	other = iVector.InitializeWith(sizeof(int),iVector.Size(v),iVector.GetData(v));
	TEST_REQUIRE(copy != NULL && other != NULL);
	(void)iVector.SetCompareFunction(other,compare_int);
	TEST_REQUIRE(iVector.Equal(v,copy) == 1 || iVector.Equal(v,copy) == 0);
	TEST_REQUIRE(iVector.Mismatch(v,other,&mismatch) == 0 || mismatch < iVector.Size(v));
	TEST_REQUIRE(iVector.IndexOf(v,&value,NULL,NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iVector.Contains(v,&value,NULL) == 1 || iVector.Contains(v,&value,NULL) == 0);
	TEST_REQUIRE(iVector.SearchWithKey(v,99,sizeof(int),0,&value,&index) == 0);
	TEST_REQUIRE(iVector.InsertIn(v,99,other) == CONTAINER_ERROR_INDEX);
	TEST_REQUIRE(iVector.GetRange(v,99,100) == NULL);

	iterator = iVector.NewIterator(v);
	TEST_REQUIRE(iterator != NULL);
	iterator_size = iVector.SizeofIterator(v);
	TEST_REQUIRE(iterator_size >= sizeof(struct VectorIterator));
	TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
	TEST_REQUIRE(iterator->Replace(iterator,NULL,0) == 1);
	iVector.DeleteIterator(iterator); iterator = NULL;

	stream = ccl_test_tmpfile();
	TEST_REQUIRE(stream != NULL && iVector.Save(v,stream,save_raw_int,NULL) == 1);
	rewind(stream);
	loaded = iVector.Load(stream,load_raw_int,NULL);
	TEST_REQUIRE(loaded != NULL && iVector.Size(loaded) == iVector.Size(v));
	fclose(stream); stream = NULL;

	memset(&placement,0,sizeof(placement));
	TEST_REQUIRE(iVector.Init(&placement,sizeof(int),1) == &placement);
	TEST_REQUIRE(iVector.Add(&placement,&value) == 1);
	iVector.Clear(&placement);
	placement.Allocator->free(placement.contents);
	placement.contents = NULL;
	result = 0;

cleanup:
	if (stream != NULL) fclose(stream);
	if (iterator != NULL) iVector.DeleteIterator(iterator);
	if (copied != NULL) {
		for (index=0; copied[index] != NULL; ++index) free(copied[index]);
		free(copied);
	}
	if (loaded != NULL) iVector.Finalize(loaded);
	if (other != NULL) iVector.Finalize(other);
	if (copy != NULL) iVector.Finalize(copy);
	if (v != NULL) iVector.Finalize(v);
	return result;
}

static int test_invalid_and_readonly_branches(void)
{
	Vector *v = NULL, *other = NULL;
	Mask *mask = NULL, *short_mask = NULL;
	Iterator *iterator = NULL;
	FILE *stream = NULL;
	int value = 1;
	int init_values[3] = { 1, 1, 1 };
	int result = 1;

	TEST_REQUIRE(iVector.Size(NULL) == 0);
	TEST_REQUIRE(iVector.GetFlags(NULL) == 0);
	TEST_REQUIRE(iVector.SetFlags(NULL,0) == 0);
	TEST_REQUIRE(iVector.Clear(NULL) < 0);
	TEST_REQUIRE(iVector.Contains(NULL,&value,NULL) < 0);
	TEST_REQUIRE(iVector.Erase(NULL,&value) < 0);
	TEST_REQUIRE(iVector.EraseAll(NULL,&value) < 0);
	TEST_REQUIRE(iVector.Finalize(NULL) < 0);
	TEST_REQUIRE(iVector.Apply(NULL,increment_int,NULL) < 0);
	TEST_REQUIRE(iVector.Equal(NULL,NULL) == 1);
	TEST_REQUIRE(iVector.Copy(NULL) == NULL);
	TEST_REQUIRE(iVector.SetErrorFunction(NULL,NULL) != NULL);
	TEST_REQUIRE(iVector.Sizeof(NULL) == sizeof(Vector));
	TEST_REQUIRE(iVector.NewIterator(NULL) == NULL);
	TEST_REQUIRE(iVector.InitIterator(NULL,NULL) < 0);
	TEST_REQUIRE(iVector.DeleteIterator(NULL) < 0);
	TEST_REQUIRE(iVector.Save(NULL,NULL,NULL,NULL) < 0);
	TEST_REQUIRE(iVector.Load(NULL,NULL,NULL) == NULL);
	TEST_REQUIRE(iVector.GetElementSize(NULL) == 0);
	TEST_REQUIRE(iVector.Add(NULL,&value) < 0);
	TEST_REQUIRE(iVector.GetElement(NULL,0) == NULL);
	TEST_REQUIRE(iVector.PushBack(NULL,&value) < 0);
	TEST_REQUIRE(iVector.PopBack(NULL,&value) < 0);
	TEST_REQUIRE(iVector.InsertAt(NULL,0,&value) < 0);
	TEST_REQUIRE(iVector.EraseAt(NULL,0) < 0);
	TEST_REQUIRE(iVector.ReplaceAt(NULL,0,&value) < 0);
	TEST_REQUIRE(iVector.IndexOf(NULL,&value,NULL,NULL) < 0);
	TEST_REQUIRE(iVector.Insert(NULL,&value) < 0);
	TEST_REQUIRE(iVector.InsertIn(NULL,0,NULL) < 0);
	TEST_REQUIRE(iVector.IndexIn(NULL,NULL) == NULL);
	TEST_REQUIRE(iVector.GetCapacity(NULL) == 0);
	TEST_REQUIRE(iVector.SetCapacity(NULL,0) < 0);
	TEST_REQUIRE(iVector.SetCompareFunction(NULL,NULL) == NULL);
	TEST_REQUIRE(iVector.Sort(NULL) < 0);
	TEST_REQUIRE(iVector.CreateWithAllocator(sizeof(int),1,NULL) == NULL);
	TEST_REQUIRE(iVector.Init(NULL,sizeof(int),1) == NULL);
	TEST_REQUIRE(iVector.AddRange(NULL,1,&value) < 0);
	TEST_REQUIRE(iVector.GetRange(NULL,0,0) == NULL);
	TEST_REQUIRE(iVector.CopyElement(NULL,0,&value) < 0);
	TEST_REQUIRE(iVector.CopyTo(NULL) == NULL);
	TEST_REQUIRE(iVector.Reverse(NULL) < 0);
	TEST_REQUIRE(iVector.Append(NULL,NULL) < 0);
	TEST_REQUIRE(iVector.Mismatch(NULL,NULL,NULL) < 0);
	TEST_REQUIRE(iVector.GetAllocator(NULL) == NULL);
	TEST_REQUIRE(iVector.SetDestructor(NULL,NULL) == NULL);
	TEST_REQUIRE(iVector.SearchWithKey(NULL,0,1,0,&value,NULL) < 0);
	{
		Vector *empty = iVector.InitializeWith(sizeof(int),0,NULL);
		TEST_REQUIRE(empty != NULL);
		iVector.Finalize(empty);
	}
	TEST_REQUIRE(iVector.Select(NULL,NULL) < 0);
	TEST_REQUIRE(iVector.SelectCopy(NULL,NULL) == NULL);
	TEST_REQUIRE(iVector.Resize(NULL,0) < 0);
	TEST_REQUIRE(iVector.InitializeWith(sizeof(int),1,NULL) == NULL);
	TEST_REQUIRE(iVector.GetData(NULL) == NULL);
	TEST_REQUIRE(iVector.Back(NULL) == NULL);
	TEST_REQUIRE(iVector.Front(NULL) == NULL);
	TEST_REQUIRE(iVector.RemoveRange(NULL,0,0) < 0);
	TEST_REQUIRE(iVector.RotateLeft(NULL,0) < 0);
	TEST_REQUIRE(iVector.RotateRight(NULL,0) < 0);
	TEST_REQUIRE(iVector.CompareEqual(NULL,NULL,NULL) == NULL);
	TEST_REQUIRE(iVector.CompareEqualScalar(NULL,&value,NULL) == NULL);
	TEST_REQUIRE(iVector.Reserve(NULL,1) < 0);

	v = iVector.InitializeWith(sizeof(int),3,init_values);
	other = iVector.Create(sizeof(long),1);
	TEST_REQUIRE(v != NULL && other != NULL);
	mask = iMask.Create(3);
	short_mask = iMask.Create(1);
	TEST_REQUIRE(mask != NULL && short_mask != NULL);
	TEST_REQUIRE(iVector.Select(v,short_mask) == CONTAINER_ERROR_BADMASK);
	TEST_REQUIRE(iVector.SelectCopy(v,short_mask) == NULL);
	TEST_REQUIRE(iVector.CompareEqual(v,other,NULL) == NULL);
	TEST_REQUIRE(iVector.Mismatch(v,other,NULL) < 0);
	TEST_REQUIRE(iVector.InsertIn(v,0,other) == CONTAINER_ERROR_INCOMPATIBLE);
	TEST_REQUIRE(iVector.Append(v,other) == CONTAINER_ERROR_INCOMPATIBLE);
	TEST_REQUIRE(iVector.SearchWithKey(v,0,1,0,NULL,NULL) < 0);

	iVector.SetFlags(v,CONTAINER_READONLY);
	iterator = iVector.NewIterator(v);
	TEST_REQUIRE(iterator != NULL);
	TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
	TEST_REQUIRE(iterator->GetNext(iterator) != NULL);
	TEST_REQUIRE(iterator->GetLast(iterator) != NULL);
	TEST_REQUIRE(iterator->GetPrevious(iterator) != NULL);
	TEST_REQUIRE(iterator->Seek(iterator,1) != NULL);
	TEST_REQUIRE(iterator->GetCurrent(iterator) != NULL);
	iVector.DeleteIterator(iterator); iterator = NULL;
	TEST_REQUIRE(iVector.AddRange(v,1,&value) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Clear(v) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.InsertAt(v,0,&value) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.InsertIn(v,0,v) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Erase(v,&value) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.EraseAt(v,0) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.ReplaceAt(v,0,&value) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Sort(v) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Reverse(v) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.RotateLeft(v,1) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.RotateRight(v,1) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.Apply(v,increment_int,NULL) == 1);
	TEST_REQUIRE(iVector.GetData(v) == NULL);
	TEST_REQUIRE(iVector.Back(v) == NULL && iVector.Front(v) == NULL);
	TEST_REQUIRE(iVector.Select(v,mask) == CONTAINER_ERROR_READONLY);
	TEST_REQUIRE(iVector.SetCapacity(v,iVector.GetCapacity(v)) == CONTAINER_ERROR_READONLY);

	stream = ccl_test_tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(iVector.Load(stream,NULL,NULL) == NULL);
	rewind(stream);
	{
		unsigned char zeros[sizeof(guid)] = { 0 };
		fwrite(zeros,1,sizeof(zeros),stream);
		rewind(stream);
		TEST_REQUIRE(iVector.Load(stream,NULL,NULL) == NULL);
	}
	result = 0;

cleanup:
	if (stream != NULL) fclose(stream);
	if (iterator != NULL) iVector.DeleteIterator(iterator);
	if (short_mask != NULL) iMask.Finalize(short_mask);
	if (mask != NULL) iMask.Finalize(mask);
	if (other != NULL) iVector.Finalize(other);
	if (v != NULL) iVector.Finalize(v);
	return result;
}

static int test_vector_comparisons_and_rotations(void)
{
	Vector *v1 = NULL, *v2 = NULL, *v3 = NULL;
	Vector *indices = NULL, *selected = NULL;
	Mask *m = NULL;
	int vals1[] = { 1, 2, 3, 4, 5 };
	int vals2[] = { 1, 2, 3, 4, 6 };
	int vals3[] = { 1, 2, 3 };
	size_t mismatch = 0;
	int scalar = 3;
	int result = 1;

	v1 = iVector.InitializeWith(sizeof(int), 5, vals1);
	v2 = iVector.InitializeWith(sizeof(int), 5, vals2);
	v3 = iVector.InitializeWith(sizeof(int), 3, vals3);
	TEST_REQUIRE(v1 != NULL && v2 != NULL && v3 != NULL);

	/* Equal branches */
	TEST_REQUIRE(iVector.Equal(v1, v1) == 1);
	TEST_REQUIRE(iVector.Equal(NULL, v1) == 0);
	TEST_REQUIRE(iVector.Equal(v1, NULL) == 0);
	TEST_REQUIRE(iVector.Equal(v1, v3) == 0); /* different count */
	iVector.SetFlags(v2, CONTAINER_READONLY);
	TEST_REQUIRE(iVector.Equal(v1, v2) == 0); /* different Flags */
	iVector.SetFlags(v2, 0);
	{
		Vector *short_vec = iVector.Create(sizeof(short), 5);
		TEST_REQUIRE(iVector.Equal(v1, short_vec) == 0); /* different ElementSize */
		iVector.Finalize(short_vec);
	}
	{
		Vector *empty1 = iVector.Create(sizeof(int), 0);
		Vector *empty2 = iVector.Create(sizeof(int), 0);
		TEST_REQUIRE(iVector.Equal(empty1, empty2) == 1); /* count == 0 */
		iVector.Finalize(empty1);
		iVector.Finalize(empty2);
	}
	CompareFunction default_cmp = iVector.SetCompareFunction(v1, NULL);
	iVector.SetCompareFunction(v2, compare_int);
	TEST_REQUIRE(iVector.Equal(v1, v2) == 0); /* different CompareFn */
	iVector.SetCompareFunction(v1, compare_int);
	TEST_REQUIRE(iVector.Equal(v1, v2) == 0); /* custom CompareFn mismatch */
	*(int *)iVector.GetElement(v2, 4) = 5;
	TEST_REQUIRE(iVector.Equal(v1, v2) == 1); /* custom CompareFn match */
	iVector.SetCompareFunction(v1, default_cmp);
	iVector.SetCompareFunction(v2, default_cmp);
	*(int *)iVector.GetElement(v2, 4) = 6;
	TEST_REQUIRE(iVector.Equal(v1, v2) == 0); /* memcmp mismatch */

	/* Mismatch branches */
	TEST_REQUIRE(iVector.Mismatch(v1, v1, &mismatch) == 0);
	{
		Vector *short_vec = iVector.Create(sizeof(short), 5);
		TEST_REQUIRE(iVector.Mismatch(v1, short_vec, &mismatch) == CONTAINER_ERROR_INCOMPATIBLE);
		iVector.Finalize(short_vec);
	}
	{
		Vector *empty1 = iVector.Create(sizeof(int), 0);
		Vector *empty2 = iVector.Create(sizeof(int), 0);
		TEST_REQUIRE(iVector.Mismatch(empty1, empty2, &mismatch) == 1); /* siz == 0 */
		iVector.Finalize(empty1);
		iVector.Finalize(empty2);
	}
	TEST_REQUIRE(iVector.Mismatch(v1, v2, &mismatch) == 1 && mismatch == 4); /* elements differ */
	TEST_REQUIRE(iVector.Mismatch(v1, v3, &mismatch) == 1 && mismatch == 3); /* prefix matches, lengths differ */
	TEST_REQUIRE(iVector.Mismatch(v3, v1, &mismatch) == 1 && mismatch == 3);

	/* RotateLeft & RotateRight */
	{
		Vector *empty = iVector.Create(sizeof(int), 0);
		TEST_REQUIRE(iVector.RotateLeft(empty, 1) == 0);
		TEST_REQUIRE(iVector.RotateRight(empty, 1) == 0);
		iVector.Finalize(empty);
	}
	TEST_REQUIRE(iVector.RotateLeft(v1, 0) == 0);
	TEST_REQUIRE(iVector.RotateRight(v1, 0) == 0);
	TEST_REQUIRE(iVector.RotateLeft(v1, 5) == 1); /* multiple of count */
	TEST_REQUIRE(iVector.RotateRight(v1, 5) == 1);
	TEST_REQUIRE(iVector.RotateLeft(v1, 2) == 1); /* n = 2 exercises first partition loop */
	TEST_REQUIRE(iVector.RotateRight(v1, 2) == 1);
	for (size_t i = 0; i < 5; ++i) {
		TEST_REQUIRE(value_at(v1, i) == vals1[i]);
	}

	/* CompareEqual & CompareEqualScalar */
	TEST_REQUIRE(iVector.CompareEqual(v1, v3, NULL) == NULL); /* length mismatch */
	{
		Vector *short_vec = iVector.Create(sizeof(short), 5);
		TEST_REQUIRE(iVector.CompareEqual(v1, short_vec, NULL) == NULL);
		iVector.Finalize(short_vec);
	}
	m = iMask.Create(1); /* smaller than v1 length */
	m = iVector.CompareEqual(v1, v2, m); /* exercises bytearray resize in CompareEqual */
	TEST_REQUIRE(m != NULL && iMask.Size(m) == 5);
	iMask.Finalize(m);
	m = iMask.Create(1);
	m = iVector.CompareEqualScalar(v1, &scalar, m); /* exercises bytearray resize in CompareEqualScalar */
	TEST_REQUIRE(m != NULL && iMask.Size(m) == 5);
	iMask.Finalize(m);
	m = NULL;
	iVector.SetCompareFunction(v1, compare_int);
	m = iVector.CompareEqualScalar(v1, &scalar, NULL); /* custom CompareFn in CompareEqualScalar */
	TEST_REQUIRE(m != NULL);
	iMask.Finalize(m);
	m = NULL;
	iVector.SetCompareFunction(v1, default_cmp);

	/* IndexIn */
	TEST_REQUIRE(iVector.IndexIn(NULL, NULL) == NULL);
	TEST_REQUIRE(iVector.IndexIn(v1, v2) == NULL); /* v2 element size is int, not size_t */
	indices = iVector.Create(sizeof(size_t), 4);
	{
		size_t bad_idx = 999;
		iVector.Add(indices, &bad_idx);
		TEST_REQUIRE(iVector.IndexIn(v1, indices) == NULL); /* index out of range */
	}
	iVector.Clear(indices);
	{
		size_t idx0 = 0, idx2 = 2;
		iVector.Add(indices, &idx0);
		iVector.Add(indices, &idx2);
		selected = iVector.IndexIn(v1, indices);
		TEST_REQUIRE(selected != NULL && iVector.Size(selected) == 2);
		TEST_REQUIRE(value_at(selected, 0) == 1 && value_at(selected, 1) == 3);
		iVector.Finalize(selected);
		selected = NULL;
	}
	iVector.Finalize(indices);
	indices = NULL;

	result = 0;

cleanup:
	if (m) iMask.Finalize(m);
	if (indices) iVector.Finalize(indices);
	if (selected) iVector.Finalize(selected);
	if (v3) iVector.Finalize(v3);
	if (v2) iVector.Finalize(v2);
	if (v1) iVector.Finalize(v1);
	return result;
}

static int test_vector_iterators_exhaustive(void)
{
	Vector *v = NULL;
	Vector *empty = NULL;
	Iterator *it = NULL;
	Iterator *empty_it = NULL;
	struct VectorIterator dummy;
	int values[] = { 10, 20, 30 };
	int rep = 40;
	int result = 1;

	empty = iVector.Create(sizeof(int), 0);
	v = iVector.InitializeWith(sizeof(int), 3, values);
	TEST_REQUIRE(empty != NULL && v != NULL);

	/* Iterators on empty vector */
	empty_it = iVector.NewIterator(empty);
	TEST_REQUIRE(empty_it != NULL);
	TEST_REQUIRE(empty_it->GetFirst(empty_it) == NULL);
	TEST_REQUIRE(empty_it->GetLast(empty_it) == NULL);
	TEST_REQUIRE(empty_it->GetNext(empty_it) == NULL);
	TEST_REQUIRE(empty_it->Replace(empty_it, &rep, 1) == 0);
	iVector.DeleteIterator(empty_it);
	empty_it = NULL;

	it = iVector.NewIterator(v);
	TEST_REQUIRE(it != NULL);

	/* NULL iterator checks */
	TEST_REQUIRE(it->GetFirst(NULL) == NULL);
	TEST_REQUIRE(it->GetNext(NULL) == NULL);
	TEST_REQUIRE(it->GetLast(NULL) == NULL);
	TEST_REQUIRE(it->GetPrevious(NULL) == NULL);
	TEST_REQUIRE(it->GetCurrent(NULL) == NULL);
	TEST_REQUIRE(it->GetPosition(NULL) == (size_t)-1);
	TEST_REQUIRE(it->Seek(NULL, 0) == NULL);
	TEST_REQUIRE(iVector.DeleteIterator(NULL) < 0);

	/* Wrong magic number checks */
	memset(&dummy, 0, sizeof(dummy));
	dummy.Magic = 12345;
	dummy.AL = v;
	TEST_REQUIRE(it->GetFirst(&dummy.it) == NULL);
	TEST_REQUIRE(it->GetNext(&dummy.it) == NULL);
	TEST_REQUIRE(it->GetLast(&dummy.it) == NULL);
	TEST_REQUIRE(it->GetPrevious(&dummy.it) == NULL);
	TEST_REQUIRE(it->GetCurrent(&dummy.it) == NULL);
	TEST_REQUIRE(it->GetPosition(&dummy.it) == (size_t)-1);
	TEST_REQUIRE(it->Seek(&dummy.it, 0) == NULL);
	TEST_REQUIRE(it->Replace(&dummy.it, &rep, 1) == CONTAINER_ERROR_WRONG_ITERATOR);
	TEST_REQUIRE(iVector.DeleteIterator(&dummy.it) == CONTAINER_ERROR_WRONG_ITERATOR);

	/* Normal iterator operations and timestamp mismatch */
	TEST_REQUIRE(it->Seek(it, 100) == NULL); /* Seek out of bounds */
	TEST_REQUIRE(*(int *)it->GetFirst(it) == 10);
	TEST_REQUIRE(*(int *)it->GetLast(it) == 30);
	TEST_REQUIRE(*(int *)it->GetPrevious(it) == 20);
	TEST_REQUIRE(*(int *)it->GetPrevious(it) == 10);
	TEST_REQUIRE(it->GetPrevious(it) == NULL); /* at beginning */
	TEST_REQUIRE(it->Replace(it, NULL, 1) == 1); /* EraseAt via Replace(NULL) */
	TEST_REQUIRE(iVector.Size(v) == 2);
	TEST_REQUIRE(it->Replace(it, &rep, 0) == 1); /* ReplaceAt with direction 0 */

	/* Invalidate timestamp */
	TEST_REQUIRE(iVector.Add(v, &rep) == 1);
	TEST_REQUIRE(it->GetFirst(it) == NULL);
	TEST_REQUIRE(it->GetNext(it) == NULL);
	TEST_REQUIRE(it->GetPrevious(it) == NULL);
	TEST_REQUIRE(it->GetLast(it) == NULL);
	TEST_REQUIRE(it->GetCurrent(it) == NULL);
	TEST_REQUIRE(it->GetPosition(it) == (size_t)-1);
	TEST_REQUIRE(it->Seek(it, 0) == NULL);
	TEST_REQUIRE(it->Replace(it, &rep, 1) == CONTAINER_ERROR_OBJECT_CHANGED);
	iVector.DeleteIterator(it);
	it = NULL;

	/* Replace on read-only vector */
	iVector.SetFlags(v, CONTAINER_READONLY);
	it = iVector.NewIterator(v);
	TEST_REQUIRE(it != NULL);
	TEST_REQUIRE(it->GetFirst(it) != NULL);
	TEST_REQUIRE(it->Replace(it, &rep, 1) == CONTAINER_ERROR_READONLY);
	iVector.DeleteIterator(it);
	it = NULL;
	iVector.SetFlags(v, 0);

	result = 0;

cleanup:
	if (empty_it) iVector.DeleteIterator(empty_it);
	if (it) iVector.DeleteIterator(it);
	if (empty) iVector.Finalize(empty);
	if (v) iVector.Finalize(v);
	return result;
}

static int test_vector_observers_and_destructors(void)
{
	Vector *v = NULL, *other = NULL, *copy = NULL;
	int values[] = { 1, 2, 3, 4, 5 };
	int scalar = 99;
	int result = 1;

	v = iVector.InitializeWith(sizeof(int), 5, values);
	other = iVector.InitializeWith(sizeof(int), 3, values);
	TEST_REQUIRE(v != NULL && other != NULL);

	observer_notification_count = 0;
	TEST_REQUIRE(iObserver.Subscribe(v, vector_observer_callback, ~0U) == 1);

	TEST_REQUIRE(iVector.Add(v, &scalar) == 1);
	TEST_REQUIRE(iVector.AddRange(v, 2, values) == 1);
	TEST_REQUIRE(iVector.InsertAt(v, 1, &scalar) == 1);
	TEST_REQUIRE(iVector.InsertIn(v, 2, other) == 1);
	TEST_REQUIRE(iVector.ReplaceAt(v, 0, &scalar) == 1);
	TEST_REQUIRE(iVector.EraseAt(v, 0) == 1);
	copy = iVector.Copy(v);
	TEST_REQUIRE(copy != NULL);
	iVector.Finalize(copy);
	copy = NULL;
	TEST_REQUIRE(iVector.Append(v, other) == 1);
	TEST_REQUIRE(iVector.Clear(v) == 1);
	TEST_REQUIRE(observer_notification_count >= 8);

	iVector.Finalize(v);
	v = NULL;
	iVector.Finalize(other);
	other = NULL;

	/* Custom DestructorFn */
	{
		struct owned { int *value; };
		Vector *ov = iVector.Create(sizeof(struct owned), 4);
		struct owned o1 = { malloc(sizeof(int)) };
		struct owned o2 = { malloc(sizeof(int)) };
		struct owned o3 = { malloc(sizeof(int)) };
		struct owned o4 = { malloc(sizeof(int)) };
		destructor_calls = 0;
		iVector.SetDestructor(ov, destroy_owned);
		iVector.Add(ov, &o1);
		iVector.Add(ov, &o2);
		iVector.Add(ov, &o3);
		iVector.Add(ov, &o4);

		/* RemoveRange with destructor */
		iVector.RemoveRange(ov, 1, 3);
		TEST_REQUIRE(destructor_calls == 2);

		/* ReplaceAt with destructor */
		{
			struct owned rep = { malloc(sizeof(int)) };
			iVector.ReplaceAt(ov, 0, &rep);
			TEST_REQUIRE(destructor_calls == 3);
		}

		/* Clear with destructor */
		iVector.Clear(ov);
		TEST_REQUIRE(destructor_calls == 5);
		iVector.Finalize(ov);
	}

	/* Custom dynamically-allocated VTable in Finalize */
	{
		Vector *dyn = iVector.Create(sizeof(int), 2);
		VectorInterface *custom_vtable = malloc(sizeof(VectorInterface));
		memcpy(custom_vtable, &iVector, sizeof(VectorInterface));
		dyn->VTable = custom_vtable;
		iVector.Finalize(dyn);
	}

	result = 0;

cleanup:
	if (copy) iVector.Finalize(copy);
	if (other) iVector.Finalize(other);
	if (v) iVector.Finalize(v);
	return result;
}

static int test_vector_allocator_and_error_paths(void)
{
	Vector *v = NULL, *v2 = NULL;
	int values[] = { 1, 2, 3, 4, 5 };
	int out = 0;
	size_t key_res = 0;
	FILE *stream = NULL;
	int result = 1;

	/* CreateWithAllocator error branches */
	fail_malloc = 1;
	TEST_REQUIRE(iVector.CreateWithAllocator(sizeof(int), 5, &vector_test_allocator) == NULL);
	fail_malloc = 0;

	/* malloc_countdown: 1st malloc (header) succeeds, 2nd malloc (contents) fails */
	malloc_countdown = 1;
	TEST_REQUIRE(iVector.CreateWithAllocator(sizeof(int), 5, &vector_test_allocator) == NULL);
	malloc_countdown = -1;

	/* elementsize * DEFAULT_START_SIZE overflow */
	TEST_REQUIRE(iVector.Create(SIZE_MAX / 2, 0) == NULL);

	/* Init error branches */
	{
		Vector uninit;
		TEST_REQUIRE(iVector.Init(NULL, sizeof(int), 2) == NULL);
		TEST_REQUIRE(iVector.Init(&uninit, SIZE_MAX / 2, 0) == NULL);
	}

	/* Reverse branches */
	{
		Vector *empty = iVector.Create(sizeof(int), 0);
		TEST_REQUIRE(iVector.Reverse(empty) == 1);
		iVector.Finalize(empty);
	}
	v = iVector.CreateWithAllocator(sizeof(int), 3, &vector_test_allocator);
	iVector.AddRange(v, 3, values);
	fail_malloc = 1;
	TEST_REQUIRE(iVector.Reverse(v) == CONTAINER_ERROR_NOMEMORY);
	fail_malloc = 0;
	iVector.Finalize(v);
	v = NULL;

	/* GetRange branches */
	{
		Vector *empty = iVector.Create(sizeof(int), 0);
		TEST_REQUIRE(iVector.GetRange(empty, 0, 1) == NULL);
		iVector.Finalize(empty);
	}
	v = iVector.CreateWithAllocator(sizeof(int), 3, &vector_test_allocator);
	iVector.AddRange(v, 3, values);
	fail_malloc = 1;
	TEST_REQUIRE(iVector.GetRange(v, 0, 2) == NULL);
	fail_malloc = 0;
	iVector.Finalize(v);
	v = NULL;

	/* Copy branches */
	{
		Vector *empty = iVector.Create(sizeof(int), 0);
		Vector *empty_copy = iVector.Copy(empty);
		TEST_REQUIRE(empty_copy != NULL && iVector.Size(empty_copy) == 0);
		iVector.Finalize(empty_copy);
		iVector.Finalize(empty);
	}
	v = iVector.CreateWithAllocator(sizeof(int), 3, &vector_test_allocator);
	iVector.AddRange(v, 3, values);
	fail_malloc = 1;
	TEST_REQUIRE(iVector.Copy(v) == NULL);
	fail_malloc = 0;
	malloc_countdown = 1; /* header succeeds, contents fail */
	TEST_REQUIRE(iVector.Copy(v) == NULL);
	malloc_countdown = -1;
	iVector.Finalize(v);
	v = NULL;

	/* CopyTo malloc failure during elements */
	v = iVector.CreateWithAllocator(sizeof(int), 3, &vector_test_allocator);
	iVector.AddRange(v, 3, values);
	malloc_countdown = 2; /* header array + 1st element succeed, 2nd fails */
	TEST_REQUIRE(iVector.CopyTo(v) == NULL);
	malloc_countdown = -1;
	iVector.Finalize(v);
	v = NULL;

	/* SelectCopy malloc failure */
	v = iVector.CreateWithAllocator(sizeof(int), 3, &vector_test_allocator);
	iVector.AddRange(v, 3, values);
	{
		Mask *m = iMask.Create(3);
		TEST_REQUIRE(m != NULL);
		m->data[0] = 1;
		m->data[1] = 1;
		m->data[2] = 1;
		fail_malloc = 1;
		TEST_REQUIRE(iVector.SelectCopy(v, m) == NULL);
		fail_malloc = 0;
		iMask.Finalize(m);
	}
	iVector.Finalize(v);
	v = NULL;

	/* Apply with read-only vector malloc failure */
	v = iVector.CreateWithAllocator(sizeof(int), 3, &vector_test_allocator);
	iVector.AddRange(v, 3, values);
	iVector.SetFlags(v, CONTAINER_READONLY);
	fail_malloc = 1;
	TEST_REQUIRE(iVector.Apply(v, increment_int, NULL) == CONTAINER_ERROR_NOMEMORY);
	fail_malloc = 0;
	iVector.Finalize(v);
	v = NULL;

	/* SearchWithKey branches */
	v = iVector.InitializeWith(sizeof(int), 3, values);
	TEST_REQUIRE(iVector.SearchWithKey(v, 0, sizeof(int), 10, &values[0], &key_res) == 0); /* startidx >= count */
	TEST_REQUIRE(iVector.SearchWithKey(v, 0, sizeof(int), 0, NULL, &key_res) == CONTAINER_ERROR_BADARG);
	{
		int not_found = 999;
		TEST_REQUIRE(iVector.SearchWithKey(v, 0, sizeof(int), 0, &not_found, &key_res) == 0);
	}

	/* Contains & IndexOf branches */
	TEST_REQUIRE(iVector.Contains(v, NULL, NULL) == CONTAINER_ERROR_BADARG);
	{
		int extra = 42;
		TEST_REQUIRE(iVector.Contains(v, &values[0], &extra) == 1);
	}
	TEST_REQUIRE(iVector.IndexOf(v, NULL, NULL, &key_res) == CONTAINER_ERROR_BADARG);
	{
		int not_found = 999;
		TEST_REQUIRE(iVector.IndexOf(v, &not_found, NULL, &key_res) == CONTAINER_ERROR_NOTFOUND);
	}

	/* Add / AddRange / Append / InsertAt / InsertIn / Erase bad args */
	TEST_REQUIRE(iVector.Add(v, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iVector.AddRange(v, 1, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iVector.AddRange(v, SIZE_MAX, values) == CONTAINER_ERROR_NOMEMORY);
	TEST_REQUIRE(iVector.Append(v, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iVector.InsertAt(v, 0, NULL) == CONTAINER_ERROR_BADARG);
	TEST_REQUIRE(iVector.InsertAt(v, 100, &values[0]) == CONTAINER_ERROR_INDEX);
	TEST_REQUIRE(iVector.InsertIn(v, 0, NULL) == CONTAINER_ERROR_BADARG);
	{
		Vector *short_vec = iVector.Create(sizeof(short), 2);
		TEST_REQUIRE(iVector.InsertIn(v, 0, short_vec) == CONTAINER_ERROR_INCOMPATIBLE);
		iVector.Finalize(short_vec);
	}
	TEST_REQUIRE(iVector.Erase(v, NULL) == CONTAINER_ERROR_BADARG);
	{
		int first_val = values[0];
		TEST_REQUIRE(iVector.EraseAll(v, &first_val) == 1); /* EraseAll matching at index 0 */
	}

	/* PopBack on readonly */
	iVector.SetFlags(v, CONTAINER_READONLY);
	TEST_REQUIRE(iVector.PopBack(v, NULL) == CONTAINER_ERROR_READONLY);
	iVector.SetFlags(v, 0);

	/* Front and Back on empty */
	{
		Vector *empty = iVector.Create(sizeof(int), 0);
		TEST_REQUIRE(iVector.Front(empty) == NULL);
		TEST_REQUIRE(iVector.Back(empty) == NULL);
		iVector.Finalize(empty);
	}

	/* GetFlags and GetElement */
	TEST_REQUIRE(iVector.GetFlags(v) == 0);
	TEST_REQUIRE(iVector.GetElement(v, 999) == NULL);

	/* SetCapacity errors */
	TEST_REQUIRE(iVector.SetCapacity(v, SIZE_MAX) == CONTAINER_ERROR_NOMEMORY);
	iVector.Finalize(v);
	v = NULL;

	v = iVector.CreateWithAllocator(sizeof(int), 2, &vector_test_allocator);
	fail_malloc = 1;
	TEST_REQUIRE(iVector.SetCapacity(v, 10) == CONTAINER_ERROR_NOMEMORY);
	fail_malloc = 0;
	iVector.Finalize(v);
	v = NULL;

	/* ResizeTo errors */
	v = iVector.CreateWithAllocator(sizeof(int), 2, &vector_test_allocator);
	TEST_REQUIRE(iVector.Resize(v, 0) == 0); /* newSize == count (0) */
	TEST_REQUIRE(iVector.Resize(v, 2) == 1);
	TEST_REQUIRE(iVector.Resize(v, 2) == 0); /* newSize == count (2) */
	fail_realloc = 1;
	TEST_REQUIRE(iVector.Resize(v, 10) == CONTAINER_ERROR_NOMEMORY);
	fail_realloc = 0;
	iVector.Finalize(v);
	v = NULL;

	/* CopyElement custom error handler fallback */
	v = iVector.InitializeWith(sizeof(int), 2, values);
	iVector.SetErrorFunction(v, vector_copy_element_error_handler);
	TEST_REQUIRE(iVector.CopyElement(v, 100, &out) == 1 && out == 999);
	iVector.Finalize(v);
	v = NULL;

	/* Save & Load error handling */
	v = iVector.InitializeWith(sizeof(int), 3, values);
	TEST_REQUIRE(iVector.Save(v, NULL, NULL, NULL) == CONTAINER_ERROR_BADARG);

	stream = ccl_test_tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(iVector.Save(v, stream, failing_save, NULL) == CONTAINER_ERROR_FILE_WRITE);
	fclose(stream);

	/* Load with failing loadFn */
	stream = ccl_test_tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(iVector.Save(v, stream, NULL, NULL) == 1);
	rewind(stream);
	TEST_REQUIRE(iVector.Load(stream, failing_load, NULL) == NULL);
	fclose(stream);

	/* Load with bad version */
	stream = ccl_test_tmpfile();
	TEST_REQUIRE(stream != NULL);
	TEST_REQUIRE(iVector.Save(v, stream, NULL, NULL) == 1);
	rewind(stream);
	fseek(stream, sizeof(guid), SEEK_SET);
	{
		uint32_t bad_version = 0xdeadbeef;
		fwrite(&bad_version, sizeof(bad_version), 1, stream);
	}
	rewind(stream);
	TEST_REQUIRE(iVector.Load(stream, NULL, NULL) == NULL);
	fclose(stream);

	/* NewIterator NOMEMORY */
	v2 = iVector.CreateWithAllocator(sizeof(int), 2, &vector_test_allocator);
	fail_calloc = 1;
	TEST_REQUIRE(iVector.NewIterator(v2) == NULL);
	fail_calloc = 0;
	iVector.Finalize(v2);
	v2 = NULL;

	/* RemoveRange edge cases */
	TEST_REQUIRE(iVector.RemoveRange(v, 0, 0) == 0); /* count unchanged */
	TEST_REQUIRE(iVector.RemoveRange(v, 10, 20) == 0); /* start >= count */
	TEST_REQUIRE(iVector.RemoveRange(v, 0, 100) == 1); /* end > count */
	TEST_REQUIRE(iVector.Size(v) == 0);
	TEST_REQUIRE(iVector.RemoveRange(v, 0, 1) == 0); /* empty */

	iVector.Finalize(v);
	v = NULL;

	result = 0;

cleanup:
	fail_malloc = 0;
	fail_realloc = 0;
	fail_calloc = 0;
	malloc_countdown = -1;
	if (v2) iVector.Finalize(v2);
	if (v) iVector.Finalize(v);
	return result;
}

static const TestCase tests[] = {
	{ "range, capacity, append, and selection", test_range_capacity_and_append },
	{ "owned selection and removal", test_owned_selection_and_removal },
	{ "iterators and persistence", test_iterators_and_persistence },
	{ "read-only and edge paths", test_readonly_and_edges },
	{ "remaining public surface", test_remaining_surface },
	{ "invalid and read-only branches", test_invalid_and_readonly_branches },
	{ "comparisons and rotations", test_vector_comparisons_and_rotations },
	{ "iterators exhaustive", test_vector_iterators_exhaustive },
	{ "observers and destructors", test_vector_observers_and_destructors },
	{ "allocator and error paths", test_vector_allocator_and_error_paths },
};

static const TestSuite suite = {
	"vector",
	tests,
	sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
	return &suite;
}
