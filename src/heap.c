#include "containers.h"
#include "ccl_internal.h"

#include <limits.h>

#ifndef CHUNK_SIZE
#define CHUNK_SIZE 1000
#endif

/*
 * A heap slot has private metadata in front of the object returned to the
 * caller.  The old implementation used ListElement::Next for the free-list
 * link, which both corrupted arbitrary heap objects and made a freed slot
 * indistinguishable from a live one.  Keeping the metadata outside the
 * object also makes this allocator safe for priority-queue elements, which
 * are not ListElements.
 */
typedef struct HeapSlotMeta {
    uintptr_t state;
    void *next;
} HeapSlotMeta;

#define HEAP_SLOT_LIVE ((uintptr_t)0x484541505F4C4956ULL)
#define HEAP_SLOT_FREE ((uintptr_t)0x484541505F465245ULL)

static size_t heap_alignment(void)
{
    return (size_t)_Alignof(max_align_t);
}

static int checked_add(size_t left, size_t right, size_t *result)
{
    if (left > SIZE_MAX - right)
        return 0;
    *result = left + right;
    return 1;
}

static int checked_mul(size_t left, size_t right, size_t *result)
{
    if (left != 0 && right > SIZE_MAX / left)
        return 0;
    *result = left * right;
    return 1;
}

static int checked_align(size_t value, size_t alignment, size_t *result)
{
    size_t remainder;
    size_t padding;

    if (alignment == 0)
        return 0;
    remainder = value % alignment;
    if (remainder == 0) {
        *result = value;
        return 1;
    }
    padding = alignment - remainder;
    return checked_add(value, padding, result);
}

static size_t heap_metadata_size(void)
{
    size_t result;

    if (!checked_align(sizeof(HeapSlotMeta), heap_alignment(), &result))
        return 0;
    return result;
}

static int heap_slot_stride(const ContainerHeap *heap, size_t *result)
{
    size_t metadata = heap_metadata_size();

    if (heap == NULL || metadata == 0)
        return 0;
    return checked_add(metadata, heap->ElementSize, result);
}

static int allocator_is_valid(const ContainerAllocator *allocator)
{
    return allocator != NULL && allocator->malloc != NULL &&
           allocator->calloc != NULL && allocator->realloc != NULL &&
           allocator->free != NULL;
}

static int heap_is_valid(const ContainerHeap *heap)
{
    return heap != NULL && heap->VTable == &iHeap &&
           allocator_is_valid(heap->Allocator);
}

static void *heap_slot_object(ContainerHeap *heap, size_t index)
{
    size_t block = index / (size_t)CHUNK_SIZE;
    size_t position = index % (size_t)CHUNK_SIZE;
    size_t stride;
    size_t offset;

    if (heap == NULL || heap->Heap == NULL || block >= heap->BlockCount ||
        heap->Heap[block] == NULL || !heap_slot_stride(heap, &stride) ||
        !checked_mul(position, stride, &offset))
        return NULL;
    return heap->Heap[block] + offset + heap_metadata_size();
}

static HeapSlotMeta *heap_slot_metadata(ContainerHeap *heap, size_t index)
{
    void *object = heap_slot_object(heap, index);

    if (object == NULL)
        return NULL;
    return (HeapSlotMeta *)((char *)object - heap_metadata_size());
}

/* Number of slots whose backing block has actually been allocated. */
static size_t heap_allocated_count(const ContainerHeap *heap)
{
    size_t prefix;

    if (heap == NULL || heap->Heap == NULL || heap->BlockCount == 0 ||
        heap->CurrentBlock >= heap->BlockCount ||
        heap->Heap[heap->CurrentBlock] == NULL)
        return 0;
    if (!checked_mul((size_t)heap->CurrentBlock, (size_t)CHUNK_SIZE, &prefix))
        return 0;
    if (!checked_add(prefix, (size_t)heap->BlockIndex, &prefix))
        return 0;
    return prefix;
}

static HeapSlotMeta *find_slot(ContainerHeap *heap, const void *element,
                               size_t *index)
{
    size_t count;
    size_t i;

    if (heap == NULL || element == NULL)
        return NULL;
    count = heap_allocated_count(heap);
    for (i = 0; i < count; ++i) {
        void *object = heap_slot_object(heap, i);
        if (object != NULL && (uintptr_t)object == (uintptr_t)element) {
            if (index != NULL)
                *index = i;
            return heap_slot_metadata(heap, i);
        }
    }
    return NULL;
}

static int report_bad_argument(const char *name)
{
    iError.RaiseError(name, CONTAINER_ERROR_BADARG);
    return CONTAINER_ERROR_BADARG;
}

/* Allocate a new object from the pool, or reuse a freed slot. */
static void *newHeapObject(ContainerHeap *heap)
{
    size_t table_bytes;
    size_t block_bytes;
    size_t stride;
    size_t next_block;
    size_t new_count;
    size_t old_bytes;
    size_t object_index;
    char *table_tail;
    HeapSlotMeta *meta;
    void *object;

    if (!heap_is_valid(heap)) {
        report_bad_argument("iHeap.NewObject");
        return NULL;
    }
    if (!heap_slot_stride(heap, &stride) ||
        !checked_mul((size_t)CHUNK_SIZE, stride, &block_bytes))
        return NULL;

    if (heap->FreeList != NULL) {
        object = heap->FreeList;
        meta = (HeapSlotMeta *)((char *)object - heap_metadata_size());
        if (meta->state != HEAP_SLOT_FREE)
            return NULL;
        heap->FreeList = meta->next;
        meta->next = NULL;
        meta->state = HEAP_SLOT_LIVE;
        ++heap->timestamp;
        return object;
    }

    if (heap->Heap == NULL) {
        if (!checked_mul((size_t)CHUNK_SIZE, sizeof(char *), &table_bytes))
            return NULL;
        heap->Heap = heap->Allocator->calloc((size_t)CHUNK_SIZE,
                                              sizeof(char *));
        if (heap->Heap == NULL)
            return NULL;
        heap->BlockCount = (unsigned)CHUNK_SIZE;
        heap->CurrentBlock = 0;
        heap->BlockIndex = 0;
        heap->MemoryUsed = table_bytes;
    }

    /* A full block is retained as the last allocated block until the next
     * block is successfully allocated.  This keeps allocation failures
     * retryable without publishing a partially initialized state. */
    if (heap->BlockIndex >= (unsigned)CHUNK_SIZE) {
        if (heap->CurrentBlock == UINT_MAX)
            return NULL;
        next_block = (size_t)heap->CurrentBlock + 1;
        if (next_block >= (size_t)heap->BlockCount) {
            if ((size_t)heap->BlockCount > SIZE_MAX - (size_t)CHUNK_SIZE ||
                (size_t)heap->BlockCount + (size_t)CHUNK_SIZE > UINT_MAX)
                return NULL;
            new_count = (size_t)heap->BlockCount + (size_t)CHUNK_SIZE;
            if (!checked_mul(new_count, sizeof(char *), &table_bytes) ||
                !checked_mul((size_t)heap->BlockCount, sizeof(char *),
                             &old_bytes))
                return NULL;
            if (table_bytes - old_bytes > SIZE_MAX - heap->MemoryUsed)
                return NULL;
            table_tail = heap->Allocator->realloc(heap->Heap, table_bytes);
            if (table_tail == NULL)
                return NULL;
            memset(table_tail + old_bytes, 0, table_bytes - old_bytes);
            heap->Heap = (char **)table_tail;
            heap->MemoryUsed += table_bytes - old_bytes;
            heap->BlockCount = (unsigned)new_count;
        }
        if (heap->Heap[next_block] == NULL) {
            heap->Heap[next_block] = heap->Allocator->calloc(
                (size_t)CHUNK_SIZE, stride);
            if (heap->Heap[next_block] == NULL)
                return NULL;
            if (!checked_add(heap->MemoryUsed, block_bytes,
                             &heap->MemoryUsed)) {
                heap->Allocator->free(heap->Heap[next_block]);
                heap->Heap[next_block] = NULL;
                return NULL;
            }
        }
        heap->CurrentBlock = (unsigned)next_block;
        heap->BlockIndex = 0;
    } else if (heap->Heap[heap->CurrentBlock] == NULL) {
        heap->Heap[heap->CurrentBlock] = heap->Allocator->calloc(
            (size_t)CHUNK_SIZE, stride);
        if (heap->Heap[heap->CurrentBlock] == NULL)
            return NULL;
        if (!checked_add(heap->MemoryUsed, block_bytes, &heap->MemoryUsed)) {
            heap->Allocator->free(heap->Heap[heap->CurrentBlock]);
            heap->Heap[heap->CurrentBlock] = NULL;
            return NULL;
        }
    }

    if (!checked_mul((size_t)heap->CurrentBlock, (size_t)CHUNK_SIZE,
                     &object_index) ||
        !checked_add(object_index, (size_t)heap->BlockIndex, &object_index))
        return NULL;
    object = heap_slot_object(heap, object_index);
    if (object == NULL)
        return NULL;
    meta = (HeapSlotMeta *)((char *)object - heap_metadata_size());
    meta->state = HEAP_SLOT_LIVE;
    meta->next = NULL;
    ++heap->BlockIndex;
    ++heap->timestamp;
    return object;
}

static int FreeObject(ContainerHeap *heap, void *element)
{
    HeapSlotMeta *meta;

    if (!heap_is_valid(heap) || element == NULL)
        return report_bad_argument("iHeap.FreeObject");
    meta = find_slot(heap, element, NULL);
    if (meta == NULL || meta->state != HEAP_SLOT_LIVE)
        return report_bad_argument("iHeap.FreeObject");
    meta->state = HEAP_SLOT_FREE;
    meta->next = heap->FreeList;
    heap->FreeList = element;
    ++heap->timestamp;
    return 1;
}

static void Clear(ContainerHeap *heap)
{
    size_t i;

    if (!heap_is_valid(heap)) {
        iError.RaiseError("iHeap.Clear", CONTAINER_ERROR_BADARG);
        return;
    }
    if (heap->Heap != NULL) {
        for (i = 0; i <= (size_t)heap->CurrentBlock &&
                    i < (size_t)heap->BlockCount; ++i) {
            if (heap->Heap[i] != NULL)
                heap->Allocator->free(heap->Heap[i]);
        }
        heap->Allocator->free(heap->Heap);
    }
    heap->BlockCount = 0;
    heap->CurrentBlock = 0;
    heap->BlockIndex = 0;
    heap->Heap = NULL;
    heap->FreeList = NULL;
    heap->MemoryUsed = 0;
    ++heap->timestamp;
}

static void DestroyHeap(ContainerHeap *heap)
{
    if (!heap_is_valid(heap)) {
        iError.RaiseError("iHeap.Finalize", CONTAINER_ERROR_BADARG);
        return;
    }
    Clear(heap);
    heap->Allocator->free(heap);
}

static size_t GetHeapSize(ContainerHeap *heap)
{
    if (heap == NULL)
        return 0;
    return heap->MemoryUsed;
}

static ContainerHeap *InitHeap(void *storage, size_t element_size,
                               const ContainerAllocator *allocator)
{
    ContainerHeap *heap = storage;
    size_t minimum;
    size_t normalized;

    if (heap == NULL)
        return (iError.NullPtrError("iHeap.InitHeap"), NULL);
    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (!allocator_is_valid(allocator)) {
        iError.RaiseError("iHeap.InitHeap", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    minimum = 2 * sizeof(void *);
    if (element_size < minimum)
        element_size = minimum;
    if (!checked_align(element_size, heap_alignment(), &normalized))
        return NULL;
    memset(heap, 0, sizeof(*heap));
    heap->VTable = &iHeap;
    heap->ElementSize = normalized;
    heap->Allocator = allocator;
    return heap;
}

static ContainerHeap *newHeap(size_t element_size,
                               const ContainerAllocator *allocator)
{
    ContainerHeap *heap;

    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (!allocator_is_valid(allocator)) {
        iError.RaiseError("iHeap.Create", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    heap = allocator->malloc(sizeof(ContainerHeap));
    if (heap == NULL)
        return NULL;
    if (InitHeap(heap, element_size, allocator) == NULL) {
        allocator->free(heap);
        return NULL;
    }
    return heap;
}

static int iterator_ready(struct HeapIterator *iterator, const char *name)
{
    if (iterator == NULL)
        return (iError.NullPtrError(name), 0);
    if (iterator->Magic != HEAP_MAGIC_NUMBER) {
        iError.RaiseError(name, CONTAINER_ERROR_WRONG_ITERATOR);
        return 0;
    }
    if (!heap_is_valid(iterator->Heap) ||
        iterator->timestamp != iterator->Heap->timestamp) {
        iError.RaiseError(name, CONTAINER_ERROR_OBJECT_CHANGED);
        return 0;
    }
    return 1;
}

static void iterator_no_current(struct HeapIterator *iterator)
{
    iterator->BlockNumber = SIZE_MAX;
    iterator->BlockPosition = SIZE_MAX;
}

static void iterator_set_index(struct HeapIterator *iterator, size_t index)
{
    iterator->BlockNumber = index / (size_t)CHUNK_SIZE;
    iterator->BlockPosition = index % (size_t)CHUNK_SIZE;
}

static size_t iterator_index(const struct HeapIterator *iterator)
{
    if (iterator->BlockNumber == SIZE_MAX || iterator->BlockPosition == SIZE_MAX)
        return SIZE_MAX;
    if (iterator->BlockNumber > SIZE_MAX / (size_t)CHUNK_SIZE)
        return SIZE_MAX;
    return iterator->BlockNumber * (size_t)CHUNK_SIZE +
           iterator->BlockPosition;
}

static int slot_is_live(ContainerHeap *heap, size_t index)
{
    HeapSlotMeta *meta = heap_slot_metadata(heap, index);
    return meta != NULL && meta->state == HEAP_SLOT_LIVE;
}

static void *find_live_forward(ContainerHeap *heap, size_t start,
                               size_t *found)
{
    size_t count = heap_allocated_count(heap);
    size_t i;

    if (start >= count)
        return NULL;
    for (i = start; i < count; ++i) {
        if (slot_is_live(heap, i)) {
            if (found != NULL)
                *found = i;
            return heap_slot_object(heap, i);
        }
    }
    return NULL;
}

static void *find_live_backward(ContainerHeap *heap, size_t start,
                                size_t *found)
{
    size_t count = heap_allocated_count(heap);
    size_t i;

    if (count == 0 || start >= count)
        return NULL;
    i = start;
    for (;;) {
        if (slot_is_live(heap, i)) {
            if (found != NULL)
                *found = i;
            return heap_slot_object(heap, i);
        }
        if (i == 0)
            break;
        --i;
    }
    return NULL;
}

static void *GetFirst(Iterator *base)
{
    struct HeapIterator *iterator = (struct HeapIterator *)base;
    size_t index;
    void *result;

    if (!iterator_ready(iterator, "Heap.GetFirst"))
        return NULL;
    iterator_no_current(iterator);
    result = find_live_forward(iterator->Heap, 0, &index);
    if (result != NULL)
        iterator_set_index(iterator, index);
    return result;
}

static void *GetNext(Iterator *base)
{
    struct HeapIterator *iterator = (struct HeapIterator *)base;
    size_t current;
    size_t start;
    size_t index;
    void *result;

    if (!iterator_ready(iterator, "Heap.GetNext"))
        return NULL;
    current = iterator_index(iterator);
    if (current == SIZE_MAX)
        start = 0;
    else if (current == SIZE_MAX - 1)
        return NULL;
    else
        start = current + 1;
    result = find_live_forward(iterator->Heap, start, &index);
    if (result != NULL)
        iterator_set_index(iterator, index);
    return result;
}

static void *GetPrevious(Iterator *base)
{
    struct HeapIterator *iterator = (struct HeapIterator *)base;
    size_t current;
    size_t index;
    void *result;

    if (!iterator_ready(iterator, "Heap.GetPrevious"))
        return NULL;
    current = iterator_index(iterator);
    if (current == SIZE_MAX || current == 0)
        return NULL;
    result = find_live_backward(iterator->Heap, current - 1, &index);
    if (result != NULL)
        iterator_set_index(iterator, index);
    return result;
}

static void *GetLast(Iterator *base)
{
    struct HeapIterator *iterator = (struct HeapIterator *)base;
    size_t count;
    size_t index;
    void *result;

    if (!iterator_ready(iterator, "Heap.GetLast"))
        return NULL;
    iterator_no_current(iterator);
    count = heap_allocated_count(iterator->Heap);
    result = find_live_backward(iterator->Heap, count == 0 ? 0 : count - 1,
                                &index);
    if (result != NULL)
        iterator_set_index(iterator, index);
    return result;
}

static void *GetCurrent(Iterator *base)
{
    struct HeapIterator *iterator = (struct HeapIterator *)base;
    size_t index;

    if (!iterator_ready(iterator, "Heap.GetCurrent"))
        return NULL;
    index = iterator_index(iterator);
    if (index == SIZE_MAX || !slot_is_live(iterator->Heap, index))
        return NULL;
    return heap_slot_object(iterator->Heap, index);
}

static size_t GetPosition(Iterator *base)
{
    struct HeapIterator *iterator = (struct HeapIterator *)base;

    if (!iterator_ready(iterator, "Heap.GetPosition"))
        return SIZE_MAX;
    return iterator_index(iterator);
}

static Iterator *NewIterator(ContainerHeap *heap)
{
    struct HeapIterator *iterator;

    if (!heap_is_valid(heap)) {
        iError.RaiseError("iHeap.NewIterator", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    iterator = heap->Allocator->calloc(1, sizeof(*iterator));
    if (iterator == NULL)
        return NULL;
    iterator->Heap = heap;
    iterator->timestamp = heap->timestamp;
    iterator->Magic = HEAP_MAGIC_NUMBER;
    iterator_no_current(iterator);
    iterator->it.GetFirst = GetFirst;
    iterator->it.GetNext = GetNext;
    iterator->it.GetPrevious = GetPrevious;
    iterator->it.GetCurrent = GetCurrent;
    iterator->it.GetLast = GetLast;
    iterator->it.GetPosition = GetPosition;
    return &iterator->it;
}

static int DeleteIterator(Iterator *base)
{
    struct HeapIterator *iterator;
    ContainerHeap *heap;

    if (base == NULL)
        return iError.NullPtrError("iHeap.DeleteIterator");
    iterator = (struct HeapIterator *)base;
    if (iterator->Magic != HEAP_MAGIC_NUMBER) {
        iError.RaiseError("iHeap.DeleteIterator", CONTAINER_ERROR_WRONG_ITERATOR);
        return CONTAINER_ERROR_WRONG_ITERATOR;
    }
    heap = iterator->Heap;
    iterator->Magic = 0;
    if (!heap_is_valid(heap))
        return CONTAINER_ERROR_WRONG_ITERATOR;
    heap->Allocator->free(base);
    return 1;
}

HeapInterface iHeap = {
    newHeap,
    newHeapObject,
    FreeObject,
    Clear,
    DestroyHeap,
    InitHeap,
    GetHeapSize,
    NewIterator,
    DeleteIterator
};
