/*
 * Double-ended queue.
 *
 * A deque stores its logical left end in tail and its logical right end in
 * head.  Next links lead from tail to head; Previous links lead from head to
 * tail.  Keeping that convention in one direction is important: the old
 * implementation mixed the two directions and consequently only exposed the
 * newest element to most operations.
 */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "containers.h"
#include "ccl_internal.h"

struct deque_t {
    DequeInterface *VTable;
    size_t count;
    unsigned Flags;
    size_t ElementSize;
    DlistElement *head;
    DlistElement *tail;
    CompareFunction compare;
    ErrorFunction RaiseError;
    ContainerAllocator *Allocator;
    unsigned timestamp;
    DestructorFunction DestructorFn;
    unsigned owns_header;
};

typedef DlistElement *DequeNode;

static const guid DequeGuid = { 0x3b9f4d2a, 0x5e11, 0x4db7,
    { 0x91, 0x44, 0x6a, 0x2d, 0x77, 0xb8, 0x0c, 0xe1 } };
#define DEQUE_PERSIST_VERSION UINT32_C(1)

static int default_comparator(const void *left, const void *right,
                              CompareInfo *ExtraArgs)
{
    size_t size = ((const Deque *)ExtraArgs->ContainerLeft)->ElementSize;
    return memcmp(left, right, size);
}

static int DequeError(Deque *d, const char *name, int code)
{
    ErrorFunction fn = d != NULL ? d->RaiseError : NULL;
    if (fn == NULL)
        fn = iError.RaiseError;
    if (fn != NULL)
        (void)fn(name, code);
    return code;
}

static int NodeSize(const Deque *d, size_t *size)
{
    if (d == NULL || size == NULL)
        return CONTAINER_ERROR_BADARG;
    if (d->ElementSize > SIZE_MAX - sizeof(DlistElement))
        return CONTAINER_ERROR_NOMEMORY;
    *size = sizeof(DlistElement) + d->ElementSize;
    return 1;
}

static Deque *Init(Deque *d, size_t elementsize)
{
    if (d == NULL)
    {
        (void)DequeError(NULL, "iDeque.Init", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (elementsize == 0 || elementsize > SIZE_MAX - sizeof(DlistElement)) {
        (void)DequeError(NULL, "iDeque.Init", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (CurrentAllocator == NULL || CurrentAllocator->malloc == NULL ||
        CurrentAllocator->free == NULL) {
        (void)DequeError(NULL, "iDeque.Init", CONTAINER_ERROR_BADARG);
        return NULL;
    }

    memset(d, 0, sizeof(*d));
    d->VTable = &iDeque;
    d->ElementSize = elementsize;
    d->compare = default_comparator;
    d->RaiseError = iError.RaiseError;
    d->Allocator = CurrentAllocator;
    d->owns_header = 0;
    return d;
}

static Deque *Create(size_t elementsize)
{
    Deque *d;

    if (CurrentAllocator == NULL || CurrentAllocator->malloc == NULL ||
        CurrentAllocator->free == NULL) {
        (void)DequeError(NULL, "iDeque.Create", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (elementsize == 0 || elementsize > SIZE_MAX - sizeof(DlistElement)) {
        (void)DequeError(NULL, "iDeque.Create", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    d = CurrentAllocator->malloc(sizeof(*d));
    if (d == NULL) {
        (void)DequeError(NULL, "iDeque.Create", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if (Init(d, elementsize) == NULL) {
        CurrentAllocator->free(d);
        return NULL;
    }
    d->owns_header = 1;
    return d;
}

static int Finalize(Deque *d)
{
    unsigned flags;
    int result;
    ContainerAllocator *allocator;

    if (d == NULL)
        return iError.NullPtrError("iDeque.Finalize");
    flags = d->Flags;
    allocator = d->Allocator;
    result = iDeque.Clear(d);
    if (result < 0)
        return result;
    if (flags & CONTAINER_HAS_OBSERVER)
        (void)iObserver.Notify(d, CCL_FINALIZE, NULL, NULL);
    if (d->owns_header && allocator != NULL && allocator->free != NULL)
        allocator->free(d);
    return 1;
}

static int Add(Deque *d, const void *item)
{
    DequeNode node;
    size_t node_size;

    if (d == NULL)
        return iError.NullPtrError("iDeque.Add");
    if (item == NULL)
        return DequeError(d, "iDeque.Add", CONTAINER_ERROR_BADARG);
    if (d->count == SIZE_MAX)
        return DequeError(d, "iDeque.Add", CONTAINER_ERROR_NOMEMORY);
    if (NodeSize(d, &node_size) < 0)
        return DequeError(d, "iDeque.Add", CONTAINER_ERROR_NOMEMORY);
    node = d->Allocator->malloc(node_size);
    if (node == NULL)
        return DequeError(d, "iDeque.Add", CONTAINER_ERROR_NOMEMORY);

    node->Previous = d->head;
    node->Next = NULL;
    memcpy(node->Data, item, d->ElementSize);
    if (d->head != NULL)
        d->head->Next = node;
    else
        d->tail = node;
    d->head = node;
    ++d->count;
    ++d->timestamp;
    if (d->Flags & CONTAINER_HAS_OBSERVER)
        (void)iObserver.Notify(d, CCL_ADD, item, NULL);
    return 1;
}

static int AddLeft(Deque *d, void *item)
{
    DequeNode node;
    size_t node_size;

    if (d == NULL)
        return iError.NullPtrError("iDeque.AddLeft");
    if (item == NULL)
        return DequeError(d, "iDeque.AddLeft", CONTAINER_ERROR_BADARG);
    if (d->count == SIZE_MAX)
        return DequeError(d, "iDeque.AddLeft", CONTAINER_ERROR_NOMEMORY);
    if (NodeSize(d, &node_size) < 0)
        return DequeError(d, "iDeque.AddLeft", CONTAINER_ERROR_NOMEMORY);
    node = d->Allocator->malloc(node_size);
    if (node == NULL)
        return DequeError(d, "iDeque.AddLeft", CONTAINER_ERROR_NOMEMORY);

    node->Next = d->tail;
    node->Previous = NULL;
    memcpy(node->Data, item, d->ElementSize);
    if (d->tail != NULL)
        d->tail->Previous = node;
    else
        d->head = node;
    d->tail = node;
    ++d->count;
    ++d->timestamp;
    if (d->Flags & CONTAINER_HAS_OBSERVER)
        (void)iObserver.Notify(d, CCL_ADD, item, NULL);
    return 1;
}

static void DestroyNode(Deque *d, DequeNode node)
{
    if (node == NULL)
        return;
    if (d->DestructorFn != NULL)
        (void)d->DestructorFn(node->Data);
    d->Allocator->free(node);
}

static int Clear(Deque *d)
{
    DequeNode node, next;
    unsigned old_count;

    if (d == NULL)
        return iError.NullPtrError("iDeque.Clear");
    old_count = d->count != 0;
    if (d->Flags & CONTAINER_HAS_OBSERVER)
        (void)iObserver.Notify(d, CCL_CLEAR, NULL, NULL);

    node = d->tail;
    d->head = NULL;
    d->tail = NULL;
    d->count = 0;
    if (old_count)
        ++d->timestamp;
    while (node != NULL) {
        next = node->Next;
        DestroyNode(d, node);
        node = next;
    }
    return 0;
}

static int PopFront(Deque *d, void *outbuf)
{
    DequeNode node, previous;

    if (d == NULL)
        return iError.NullPtrError("iDeque.PopFront");
    if (outbuf == NULL)
        return DequeError(d, "iDeque.PopFront", CONTAINER_ERROR_BADARG);
    node = d->head;
    if (node == NULL)
        return 0;

    /* Copy while node is alive.  The destructor is allowed to inspect or
     * release resources represented by the element. */
    memcpy(outbuf, node->Data, d->ElementSize);
    previous = node->Previous;
    d->head = previous;
    if (previous != NULL)
        previous->Next = NULL;
    else
        d->tail = NULL;
    --d->count;
    ++d->timestamp;
    if (d->DestructorFn != NULL)
        (void)d->DestructorFn(node->Data);
    if (d->Flags & CONTAINER_HAS_OBSERVER)
        (void)iObserver.Notify(d, CCL_POP, node->Data, NULL);
    d->Allocator->free(node);
    return 1;
}

static int PeekFront(Deque *d, void *outbuf)
{
    if (d == NULL)
        return iError.NullPtrError("iDeque.Front");
    if (outbuf == NULL)
        return DequeError(d, "iDeque.Front", CONTAINER_ERROR_BADARG);
    if (d->head == NULL)
        return 0;
    memcpy(outbuf, d->head->Data, d->ElementSize);
    return 1;
}

static int PopBack(Deque *d, void *outbuf)
{
    DequeNode node, next;

    if (d == NULL)
        return iError.NullPtrError("iDeque.PopBack");
    if (outbuf == NULL)
        return DequeError(d, "iDeque.PopBack", CONTAINER_ERROR_BADARG);
    node = d->tail;
    if (node == NULL)
        return 0;

    memcpy(outbuf, node->Data, d->ElementSize);
    next = node->Next;
    d->tail = next;
    if (next != NULL)
        next->Previous = NULL;
    else
        d->head = NULL;
    --d->count;
    ++d->timestamp;
    if (d->DestructorFn != NULL)
        (void)d->DestructorFn(node->Data);
    if (d->Flags & CONTAINER_HAS_OBSERVER)
        (void)iObserver.Notify(d, CCL_POP, node->Data, NULL);
    d->Allocator->free(node);
    return 1;
}

static int PeekBack(Deque *d, void *outbuf)
{
    if (d == NULL)
        return iError.NullPtrError("iDeque.Back");
    if (outbuf == NULL)
        return DequeError(d, "iDeque.Back", CONTAINER_ERROR_BADARG);
    if (d->tail == NULL)
        return 0;
    memcpy(outbuf, d->tail->Data, d->ElementSize);
    return 1;
}

static int EraseInternal(Deque *d, const void *item, int all)
{
    DequeNode node, next, previous;
    CompareInfo ci;
    int removed = 0;

    if (d == NULL)
        return iError.NullPtrError(all ? "iDeque.EraseAll" : "iDeque.Erase");
    if (item == NULL)
        return DequeError(d, all ? "iDeque.EraseAll" : "iDeque.Erase",
                          CONTAINER_ERROR_BADARG);
    ci.ContainerLeft = d;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;

    node = d->tail;
    while (node != NULL) {
        next = node->Next;
        if (d->compare(node->Data, item, &ci) == 0) {
            previous = node->Previous;
            if (previous != NULL)
                previous->Next = next;
            else
                d->tail = next;
            if (next != NULL)
                next->Previous = previous;
            else
                d->head = previous;
            --d->count;
            ++d->timestamp;
            DestroyNode(d, node);
            removed = 1;
            if (!all)
                break;
        }
        node = next;
    }
    return removed;
}

static int Erase(Deque *d, const void *item)
{
    return EraseInternal(d, item, 0);
}

static int EraseAll(Deque *d, const void *item)
{
    return EraseInternal(d, item, 1);
}

static size_t GetCount(Deque *d)
{
    if (d == NULL) {
        (void)iError.NullPtrError("iDeque.Size");
        return 0;
    }
    return d->count;
}

static Deque *CreateWithAllocator(size_t elementsize,
                                  ContainerAllocator *allocator)
{
    ContainerAllocator *saved;
    Deque *result;

    if (allocator == NULL || allocator->malloc == NULL ||
        allocator->free == NULL || elementsize == 0 ||
        elementsize > SIZE_MAX - sizeof(DlistElement)) {
        (void)DequeError(NULL, "iDeque.Create", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    /* Init's public contract uses CurrentAllocator.  Temporarily selecting a
     * caller allocator keeps CreateWithAllocator private and avoids making a
     * second ABI entry solely for deque. */
    saved = CurrentAllocator;
    CurrentAllocator = allocator;
    result = Create(elementsize);
    CurrentAllocator = saved;
    return result;
}

static Deque *Copy(Deque *d)
{
    Deque *copy;
    DequeNode node;
    int result;

    if (d == NULL) {
        (void)iError.NullPtrError("iDeque.Copy");
        return NULL;
    }
    copy = CreateWithAllocator(d->ElementSize, d->Allocator);
    if (copy == NULL)
        return NULL;
    node = d->tail;
    while (node != NULL) {
        result = Add(copy, node->Data);
        if (result < 0) {
            (void)Finalize(copy);
            return NULL;
        }
        node = node->Next;
    }
    copy->Flags = d->Flags;
    copy->compare = d->compare;
    copy->RaiseError = d->RaiseError;
    /* Copy duplicates element bytes, not ownership of resources referenced by
     * those bytes.  Propagating the destructor would destroy shallow-copied
     * resources twice when both deques are finalized. */
    copy->DestructorFn = NULL;
    copy->timestamp = d->timestamp;
    if (d->Flags & CONTAINER_HAS_OBSERVER)
        (void)iObserver.Notify(d, CCL_COPY, copy, NULL);
    return copy;
}

static int Reverse(Deque *d)
{
    DequeNode node, next;
    DequeNode old_head;

    if (d == NULL)
        return iError.NullPtrError("iDeque.Reverse");
    if (d->count == 0)
        return 0;
    node = d->tail;
    while (node != NULL) {
        next = node->Next;
        node->Next = node->Previous;
        node->Previous = next;
        node = next;
    }
    old_head = d->head;
    d->head = d->tail;
    d->tail = old_head;
    ++d->timestamp;
    return 1;
}

static size_t Contains(Deque *d, void *item)
{
    DequeNode node;
    CompareInfo ci;
    size_t position = 1;

    if (d == NULL) {
        (void)iError.NullPtrError("iDeque.Contains");
        return 0;
    }
    if (item == NULL) {
        (void)DequeError(d, "iDeque.Contains", CONTAINER_ERROR_BADARG);
        return 0;
    }
    ci.ContainerLeft = d;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    for (node = d->tail; node != NULL; node = node->Next, ++position)
        if (d->compare(node->Data, item, &ci) == 0)
            return position;
    return 0;
}

static int Equal(Deque *d1, Deque *d2)
{
    DequeNode left, right;
    CompareInfo ci;

    if (d1 == NULL || d2 == NULL) {
        (void)iError.NullPtrError("iDeque.Equal");
        return 0;
    }
    if (d1->ElementSize != d2->ElementSize || d1->count != d2->count)
        return 0;
    ci.ContainerLeft = d1;
    ci.ContainerRight = d2;
    ci.ExtraArgs = NULL;
    left = d1->tail;
    right = d2->tail;
    while (left != NULL && right != NULL) {
        if (d1->compare(left->Data, right->Data, &ci) != 0)
            return 0;
        left = left->Next;
        right = right->Next;
    }
    return left == NULL && right == NULL;
}

static void Apply(Deque *d, int (*Applyfn)(void *, void *), void *arg)
{
    DequeNode node;

    if (d == NULL) {
        (void)iError.NullPtrError("iDeque.Apply");
        return;
    }
    if (Applyfn == NULL) {
        (void)DequeError(d, "iDeque.Apply", CONTAINER_ERROR_BADARG);
        return;
    }
    for (node = d->tail; node != NULL; node = node->Next)
        (void)Applyfn(node->Data, arg);
}

static unsigned GetFlags(Deque *d)
{
    if (d == NULL) {
        (void)iError.NullPtrError("iDeque.GetFlags");
        return 0;
    }
    return d->Flags;
}

static unsigned SetFlags(Deque *d, unsigned newflags)
{
    unsigned oldflags;
    if (d == NULL) {
        (void)iError.NullPtrError("iDeque.SetFlags");
        return 0;
    }
    oldflags = d->Flags;
    d->Flags = newflags;
    ++d->timestamp;
    return oldflags;
}

static int Save(const Deque *d, FILE *stream, SaveFunction saveFn, void *arg)
{
    DequeNode node;
    size_t i;
    uint32_t version = DEQUE_PERSIST_VERSION;
    uint32_t flags;
    uint64_t element_size;
    uint64_t count;

    if (d == NULL)
        return iError.NullPtrError("iDeque.Save");
    if (stream == NULL)
        return DequeError((Deque *)d, "iDeque.Save", CONTAINER_ERROR_BADARG);
    flags = (uint32_t)(d->Flags & CONTAINER_READONLY);
    element_size = (uint64_t)d->ElementSize;
    count = (uint64_t)d->count;
    if (fwrite(&DequeGuid, 1, sizeof(DequeGuid), stream) != sizeof(DequeGuid) ||
        fwrite(&version, 1, sizeof(version), stream) != sizeof(version) ||
        fwrite(&flags, 1, sizeof(flags), stream) != sizeof(flags) ||
        fwrite(&element_size, 1, sizeof(element_size), stream) != sizeof(element_size) ||
        fwrite(&count, 1, sizeof(count), stream) != sizeof(count))
        return EOF;

    node = d->tail;
    for (i = 0; i < d->count; ++i) {
        if (node == NULL)
            return EOF;
        if (saveFn != NULL) {
            if (saveFn(node->Data, arg, stream) <= 0)
                return EOF;
        } else if (fwrite(node->Data, 1, d->ElementSize, stream) !=
                   d->ElementSize) {
            return EOF;
        }
        node = node->Next;
    }
    return 1;
}

static Deque *Load(FILE *stream, ReadFunction loadFn, void *arg)
{
    guid guid_value;
    uint32_t version, flags;
    uint64_t serialized_size, serialized_count;
    size_t element_size, count, node_size, i;
    Deque *d;
    void *buffer;
    int result;

    if (stream == NULL) {
        (void)iError.NullPtrError("iDeque.Load");
        return NULL;
    }
    if (fread(&guid_value, 1, sizeof(guid_value), stream) != sizeof(guid_value)) {
        (void)iError.RaiseError("iDeque.Load", CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (memcmp(&guid_value, &DequeGuid, sizeof(guid_value)) != 0) {
        (void)iError.RaiseError("iDeque.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    if (fread(&version, 1, sizeof(version), stream) != sizeof(version) ||
        fread(&flags, 1, sizeof(flags), stream) != sizeof(flags) ||
        fread(&serialized_size, 1, sizeof(serialized_size), stream) != sizeof(serialized_size) ||
        fread(&serialized_count, 1, sizeof(serialized_count), stream) != sizeof(serialized_count)) {
        (void)iError.RaiseError("iDeque.Load", CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (version != DEQUE_PERSIST_VERSION || serialized_size == 0 ||
        serialized_size > (uint64_t)SIZE_MAX ||
        serialized_count > (uint64_t)SIZE_MAX) {
        (void)iError.RaiseError("iDeque.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    element_size = (size_t)serialized_size;
    count = (size_t)serialized_count;
    if (element_size > SIZE_MAX - sizeof(DlistElement)) {
        (void)iError.RaiseError("iDeque.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    node_size = sizeof(DlistElement) + element_size;
    if (count > SIZE_MAX / node_size) {
        (void)iError.RaiseError("iDeque.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }

    d = Create(element_size);
    if (d == NULL)
        return NULL;
    buffer = d->Allocator->malloc(element_size);
    if (buffer == NULL) {
        (void)DequeError(d, "iDeque.Load", CONTAINER_ERROR_NOMEMORY);
        (void)Finalize(d);
        return NULL;
    }
    result = 1;
    for (i = 0; i < count; ++i) {
        if (loadFn != NULL)
            result = loadFn(buffer, arg, stream) > 0 ? 1 : CONTAINER_ERROR_FILE_READ;
        else
            result = fread(buffer, 1, element_size, stream) == element_size ?
                     1 : CONTAINER_ERROR_FILE_READ;
        if (result < 0) {
            (void)DequeError(d, "iDeque.Load", result);
            break;
        }
        result = Add(d, buffer);
        if (result < 0)
            break;
    }
    d->Allocator->free(buffer);
    if (result < 0) {
        (void)Finalize(d);
        return NULL;
    }
    d->Flags = flags & CONTAINER_READONLY;
    return d;
}

static ErrorFunction SetErrorFunction(Deque *d, ErrorFunction fn)
{
    ErrorFunction old;
    if (d == NULL)
        return iError.RaiseError;
    old = d->RaiseError;
    d->RaiseError = fn != NULL ? fn : iError.EmptyErrorFunction;
    return old;
}

static size_t Sizeof(Deque *d)
{
    size_t node_size;
    size_t result = sizeof(Deque);
    if (d == NULL)
        return result;
    if (NodeSize(d, &node_size) < 0 ||
        d->count > (SIZE_MAX - result) / node_size)
        return SIZE_MAX;
    return result + d->count * node_size;
}

struct DequeIterator {
    Iterator it;
    Deque *D;
    size_t index;
    DequeNode Current;
    unsigned timestamp;
    unsigned owns_memory;
    char ElementBuffer[1];
};

static int IteratorChanged(struct DequeIterator *iterator, const char *name)
{
    if (iterator == NULL || iterator->D == NULL)
        return 1;
    if (iterator->timestamp != iterator->D->timestamp) {
        (void)DequeError(iterator->D, name, CONTAINER_ERROR_OBJECT_CHANGED);
        return 1;
    }
    return 0;
}

static void *GetNext(Iterator *it)
{
    struct DequeIterator *iterator = (struct DequeIterator *)it;
    Deque *d;

    if (iterator == NULL || (d = iterator->D) == NULL)
        return NULL;
    if (IteratorChanged(iterator, "iDeque.GetNext"))
        return NULL;
    if (iterator->Current == NULL || iterator->index + 1 >= d->count)
        return NULL;
    iterator->Current = iterator->Current->Next;
    ++iterator->index;
    return iterator->Current != NULL ? iterator->Current->Data : NULL;
}

static void *GetPrevious(Iterator *it)
{
    struct DequeIterator *iterator = (struct DequeIterator *)it;

    if (iterator == NULL || iterator->D == NULL)
        return NULL;
    if (IteratorChanged(iterator, "iDeque.GetPrevious"))
        return NULL;
    if (iterator->Current == NULL || iterator->index == 0)
        return NULL;
    iterator->Current = iterator->Current->Previous;
    --iterator->index;
    return iterator->Current != NULL ? iterator->Current->Data : NULL;
}

static void *GetCurrent(Iterator *it)
{
    struct DequeIterator *iterator = (struct DequeIterator *)it;
    if (iterator == NULL || iterator->D == NULL)
        return NULL;
    if (IteratorChanged(iterator, "iDeque.GetCurrent"))
        return NULL;
    return iterator->Current != NULL ? iterator->Current->Data : NULL;
}

static void *GetFirst(Iterator *it)
{
    struct DequeIterator *iterator = (struct DequeIterator *)it;
    Deque *d;

    if (iterator == NULL || (d = iterator->D) == NULL)
        return NULL;
    if (IteratorChanged(iterator, "iDeque.GetFirst"))
        return NULL;
    iterator->index = 0;
    iterator->Current = d->tail;
    return iterator->Current != NULL ? iterator->Current->Data : NULL;
}

static Iterator *NewIterator(Deque *d)
{
    struct DequeIterator *iterator;
    if (d == NULL) {
        (void)iError.NullPtrError("iDeque.NewIterator");
        return NULL;
    }
    iterator = d->Allocator->malloc(sizeof(*iterator));
    if (iterator == NULL) {
        (void)DequeError(d, "iDeque.NewIterator", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    memset(iterator, 0, sizeof(*iterator));
    iterator->it.GetNext = GetNext;
    iterator->it.GetPrevious = GetPrevious;
    iterator->it.GetFirst = GetFirst;
    iterator->it.GetCurrent = GetCurrent;
    iterator->D = d;
    iterator->timestamp = d->timestamp;
    iterator->owns_memory = 1;
    return &iterator->it;
}

static int InitIterator(Deque *d, void *buf)
{
    struct DequeIterator *iterator;
    if (d == NULL)
        return iError.NullPtrError("iDeque.InitIterator");
    if (buf == NULL)
        return DequeError(d, "iDeque.InitIterator", CONTAINER_ERROR_BADARG);
    iterator = (struct DequeIterator *)buf;
    memset(iterator, 0, sizeof(*iterator));
    iterator->it.GetNext = GetNext;
    iterator->it.GetPrevious = GetPrevious;
    iterator->it.GetFirst = GetFirst;
    iterator->it.GetCurrent = GetCurrent;
    iterator->D = d;
    iterator->timestamp = d->timestamp;
    iterator->owns_memory = 0;
    return 1;
}

static int DeleteIterator(Iterator *it)
{
    struct DequeIterator *iterator;
    if (it == NULL)
        return iError.NullPtrError("iDeque.DeleteIterator");
    iterator = (struct DequeIterator *)it;
    if (iterator->D == NULL)
        return DequeError(NULL, "iDeque.DeleteIterator", CONTAINER_ERROR_BADARG);
    if (iterator->owns_memory)
        iterator->D->Allocator->free(iterator);
    return 1;
}

static DestructorFunction SetDestructor(Deque *d, DestructorFunction fn)
{
    DestructorFunction old;
    if (d == NULL)
        return NULL;
    old = d->DestructorFn;
    d->DestructorFn = fn;
    return old;
}

static size_t SizeofIterator(Deque *d)
{
    (void)d;
    return sizeof(struct DequeIterator);
}

DequeInterface iDeque = {
    GetCount,
    GetFlags,
    SetFlags,
    Clear,
    Contains,
    Erase,
    EraseAll,
    Finalize,
    Apply,
    Equal,
    Copy,
    SetErrorFunction,
    Sizeof,
    NewIterator,
    InitIterator,
    DeleteIterator,
    SizeofIterator,
    Save,
    Load,
    Add,
    AddLeft,
    Reverse,
    PopBack,
    PeekBack,
    PeekFront,
    PopFront,
    Create,
    Init,
    SetDestructor,
};
