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

static const TestCase tests[] = {
	{ "range, capacity, append, and selection", test_range_capacity_and_append },
	{ "owned selection and removal", test_owned_selection_and_removal },
	{ "iterators and persistence", test_iterators_and_persistence },
	{ "read-only and edge paths", test_readonly_and_edges },
	{ "remaining public surface", test_remaining_surface },
	{ "invalid and read-only branches", test_invalid_and_readonly_branches }
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
