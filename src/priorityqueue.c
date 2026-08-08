/*
 * Fibonacci min-heap priority queue.
 *
 * The backing ContainerHeap is used only as an allocator for nodes.  The
 * queue's logical membership is represented by the root/child rings below;
 * this is important because a heap iterator describes allocation history,
 * not the set of nodes currently in the queue.
 */
#include "containers.h"
#include "ccl_internal.h"

static PQueue *Create(size_t ElementSize);
static PQueue *CreateWithAllocator(size_t ElementSize,
                                   ContainerAllocator *allocator);
static int Add(PQueue *, intptr_t, const void *);
static intptr_t Front(const PQueue *, void *result);
static int Finalize(PQueue *);
static PQueue *Union(PQueue *, PQueue *);

struct _PQueue {
    PQueueInterface *VTable;
    size_t count;
    unsigned Flags;
    size_t ElementSize;
    int Log2N;
    struct _PQueueElement **lognTable;
    struct _PQueueElement *Minimum;
    struct _PQueueElement *Root;
    ContainerHeap *Heap;
    unsigned timestamp;
    ContainerAllocator *Allocator;
};

struct _PQueueElement {
    int degree;
    int Mark;
    PQueueElement *Parent;
    PQueueElement *Child;
    PQueueElement *Left;
    PQueueElement *Right;
    intptr_t Key;
    char Data[1];
};

/* Return a portable ceiling(log2(value)); zero is treated as zero. */
static int ceillog2(size_t value)
{
    size_t power;
    int result;

    if (value <= 1)
        return 0;
    power = 1;
    result = 0;
    while (power < value) {
        if (power > SIZE_MAX / 2)
            return (int)(sizeof(size_t) * CHAR_BIT);
        power <<= 1;
        ++result;
    }
    return result;
}

static int element_storage_size(size_t element_size, size_t *storage)
{
    if (element_size > SIZE_MAX - sizeof(PQueueElement))
        return 0;
    *storage = sizeof(PQueueElement) + element_size;
    return 1;
}

static int compare(PQueueElement *a, PQueueElement *b)
{
    if (a->Key < b->Key)
        return -1;
    if (a->Key == b->Key)
        return 0;
    return 1;
}

static void release_storage(PQueue *h)
{
    if (h->lognTable != NULL) {
        h->Allocator->free(h->lognTable);
        h->lognTable = NULL;
    }
    h->Log2N = -1;
    if (h->Heap != NULL)
        iHeap.Finalize(h->Heap);
    h->Heap = NULL;
}

static PQueue *CreateWithAllocator(size_t ElementSize,
                                   ContainerAllocator *allocator)
{
    PQueue *result;
    size_t storage;

    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (allocator == NULL || allocator->calloc == NULL ||
        allocator->malloc == NULL || allocator->free == NULL ||
        allocator->realloc == NULL ||
        !element_storage_size(ElementSize, &storage)) {
        iError.RaiseError("iPQueue.CreateWithAllocator",
                          CONTAINER_ERROR_BADARG);
        return NULL;
    }

    result = allocator->calloc(1, sizeof(*result));
    if (result == NULL)
        return NULL;

    result->VTable = &iPQueue;
    result->ElementSize = ElementSize;
    result->Log2N = -1;
    result->Allocator = allocator;
    result->Heap = iHeap.Create(storage, allocator);
    if (result->Heap == NULL) {
        allocator->free(result);
        return NULL;
    }
    return result;
}

static PQueue *Create(size_t ElementSize)
{
    return CreateWithAllocator(ElementSize, CurrentAllocator);
}

static PQueueElement *NewElement(PQueue *h)
{
    PQueueElement *element;

    element = iHeap.NewObject(h->Heap);
    if (element == NULL)
        return NULL;
    memset(element, 0, sizeof(*element));
    element->Left = element;
    element->Right = element;
    return element;
}

static void insertafter(PQueueElement *a, PQueueElement *b)
{
    if (a == a->Right) {
        a->Right = b;
        a->Left = b;
        b->Right = a;
        b->Left = a;
    } else {
        b->Right = a->Right;
        a->Right->Left = b;
        a->Right = b;
        b->Left = a;
    }
}

static void insertrootlist(PQueue *h, PQueueElement *x)
{
    x->Parent = NULL;
    x->Mark = 0;
    if (h->Root == NULL) {
        h->Root = x;
        x->Left = x;
        x->Right = x;
    } else {
        insertafter(h->Root, x);
    }
}

static void insertel(PQueue *h, PQueueElement *x)
{
    insertrootlist(h, x);
    if (h->Minimum == NULL || x->Key < h->Minimum->Key)
        h->Minimum = x;
    ++h->count;
    ++h->timestamp;
}

static int Add(PQueue *h, intptr_t key, const void *data)
{
    PQueueElement *x;

    if (h == NULL)
        return iError.NullPtrError("iPQueue.Add");
    x = NewElement(h);
    if (x == NULL) {
        iError.RaiseError("iPQueue.Add", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }

    if (h->ElementSize != 0) {
        if (data != NULL)
            memcpy(x->Data, data, h->ElementSize);
        else
            memset(x->Data, 0, h->ElementSize);
    }
    if (key < (intptr_t)CCL_PRIORITY_MIN)
        key = (intptr_t)CCL_PRIORITY_MIN;
    else if (key > (intptr_t)CCL_PRIORITY_MAX)
        key = (intptr_t)CCL_PRIORITY_MAX;
    x->Key = key;
    insertel(h, x);
    return 1;
}

static intptr_t Front(const PQueue *h, void *result)
{
    if (h == NULL)
        return iError.NullPtrError("iPQueue.Front");
    if (h->Minimum == NULL)
        return INT_MIN;
    if (result == NULL && h->ElementSize != 0)
        return iError.NullPtrError("iPQueue.Front");
    if (h->ElementSize != 0)
        memcpy(result, h->Minimum->Data, h->ElementSize);
    return h->Minimum->Key;
}

/* Remove x from whichever circular sibling ring contains it. */
static PQueueElement *removeNode(PQueueElement *x)
{
    PQueueElement *ret;

    ret = (x->Left == x) ? NULL : x->Left;
    if (x->Parent != NULL && x->Parent->Child == x)
        x->Parent->Child = ret;
    x->Right->Left = x->Left;
    x->Left->Right = x->Right;
    x->Parent = NULL;
    x->Left = x;
    x->Right = x;
    return ret;
}

static void removerootlist(PQueue *h, PQueueElement *x)
{
    h->Root = removeNode(x);
}

static void insertbefore(PQueueElement *a, PQueueElement *b)
{
    insertafter(a->Left, b);
}

static void heaplink(PQueueElement *y, PQueueElement *x)
{
    if (x->Child == NULL)
        x->Child = y;
    else
        insertbefore(x->Child, y);
    y->Parent = x;
    ++x->degree;
    y->Mark = 0;
}

static int checkcons(PQueue *h)
{
    int new_log2;
    int old_log2;
    size_t entries;
    PQueueElement **table;

    new_log2 = ceillog2(h->count) + 1;
    if (new_log2 < 8)
        new_log2 = 8;
    if (h->Log2N >= new_log2 && h->lognTable != NULL)
        return 1;
    if ((size_t)new_log2 > (SIZE_MAX / sizeof(*h->lognTable)) - 1)
        return CONTAINER_ERROR_NOMEMORY;
    entries = (size_t)new_log2 + 1;
    old_log2 = h->Log2N;
    table = (PQueueElement **)h->Allocator->realloc(
        h->lognTable, entries * sizeof(*h->lognTable));
    if (table == NULL) {
        iError.RaiseError("iPQueue.ExtractMin", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    h->lognTable = table;
    h->Log2N = new_log2;
    (void)old_log2;
    return 1;
}

static int consolidate(PQueue *h)
{
    PQueueElement **table;
    PQueueElement *w;
    PQueueElement *x;
    PQueueElement *y;
    int degree;
    int i;
    int entries;

    if (checkcons(h) < 0)
        return CONTAINER_ERROR_NOMEMORY;
    table = h->lognTable;
    entries = h->Log2N + 1;
    for (i = 0; i < entries; ++i)
        table[i] = NULL;

    while ((w = h->Root) != NULL) {
        x = w;
        removerootlist(h, w);
        degree = x->degree;
        if (degree < 0 || degree >= entries)
            return CONTAINER_INTERNAL_ERROR;
        while (table[degree] != NULL) {
            y = table[degree];
            if (compare(x, y) > 0) {
                PQueueElement *tmp = x;
                x = y;
                y = tmp;
            }
            table[degree] = NULL;
            heaplink(y, x);
            ++degree;
            if (degree >= entries)
                return CONTAINER_INTERNAL_ERROR;
        }
        table[degree] = x;
    }

    h->Root = NULL;
    h->Minimum = NULL;
    for (i = 0; i < entries; ++i) {
        if (table[i] != NULL) {
            insertrootlist(h, table[i]);
            if (h->Minimum == NULL || compare(table[i], h->Minimum) < 0)
                h->Minimum = table[i];
        }
    }
    return 1;
}

/*
 * Remove the minimum without returning its storage to iHeap.  Pop copies the
 * value and then recycles the node, so no pointer is used after FreeObject.
 */
static PQueueElement *ExtractMin(PQueue *h)
{
    PQueueElement *ret;
    PQueueElement *child;
    PQueueElement *next;
    PQueueElement *head;

    if (h == NULL || h->Minimum == NULL || h->count == 0)
        return NULL;
    if (h->count > 1 && checkcons(h) < 0)
        return NULL;

    ret = h->Minimum;
    head = ret->Child;
    ret->Child = NULL;
    ret->degree = 0;
    if (head != NULL) {
        child = head;
        do {
            next = child->Right;
            child->Left = child;
            child->Right = child;
            child->Parent = NULL;
            child->Mark = 0;
            insertrootlist(h, child);
            child = next;
        } while (child != head);
    }

    removerootlist(h, ret);
    --h->count;
    ret->Left = ret;
    ret->Right = ret;
    ret->Parent = NULL;
    if (h->count == 0) {
        h->Root = NULL;
        h->Minimum = NULL;
    } else {
        h->Minimum = h->Root;
        if (consolidate(h) < 0) {
            /* checkcons was performed before mutation; this is defensive. */
            h->Minimum = h->Root;
        }
    }
    ++h->timestamp;
    return ret;
}

static size_t Size(const PQueue *h)
{
    if (h == NULL) {
        iError.NullPtrError("iPQueue.Size");
        return 0;
    }
    return h->count;
}

static size_t Sizeof(const PQueue *h)
{
    size_t result = sizeof(PQueue);
    size_t extra;

    if (h == NULL)
        return result;
    if (h->Heap != NULL) {
        extra = iHeap.Sizeof(h->Heap);
        if (SIZE_MAX - result < extra)
            return SIZE_MAX;
        result += extra;
    }
    if (h->lognTable != NULL && h->Log2N >= 0) {
        extra = ((size_t)h->Log2N + 1) * sizeof(*h->lognTable);
        if (SIZE_MAX - result < extra)
            return SIZE_MAX;
        result += extra;
    }
    return result;
}

static int Clear(PQueue *h)
{
    if (h == NULL)
        return iError.NullPtrError("iPQueue.Clear");
    if (h->lognTable != NULL) {
        h->Allocator->free(h->lognTable);
        h->lognTable = NULL;
    }
    h->Log2N = -1;
    iHeap.Clear(h->Heap);
    h->Root = NULL;
    h->Minimum = NULL;
    h->count = 0;
    ++h->timestamp;
    return 1;
}

static intptr_t Pop(PQueue *h, void *result)
{
    PQueueElement *x;
    intptr_t key;

    if (h == NULL)
        return iError.NullPtrError("iPQueue.Pop");
    if (h->Minimum == NULL || h->count == 0)
        return INT_MAX;
    if (result == NULL && h->ElementSize != 0)
        return iError.NullPtrError("iPQueue.Pop");

    x = ExtractMin(h);
    if (x == NULL)
        return INT_MAX;
    key = x->Key;
    if (result != NULL && h->ElementSize != 0)
        memcpy(result, x->Data, h->ElementSize);
    iHeap.FreeObject(h->Heap, x);
    return key;
}

/* Visit every node in one sibling ring and all of its child rings. */
static int copy_ring(PQueue *dst, PQueueElement *head)
{
    PQueueElement *x;

    if (head == NULL)
        return 1;
    x = head;
    do {
        if (Add(dst, x->Key, x->Data) < 0)
            return 0;
        if (!copy_ring(dst, x->Child))
            return 0;
        x = x->Right;
    } while (x != head);
    return 1;
}

static int copy_contents(PQueue *dst, const PQueue *src)
{
    return copy_ring(dst, src->Root);
}

static PQueue *Copy(const PQueue *src)
{
    PQueue *result;

    if (src == NULL)
        return NULL;
    result = CreateWithAllocator(src->ElementSize, src->Allocator);
    if (result == NULL)
        return NULL;
    if (!copy_contents(result, src)) {
        Finalize(result);
        return NULL;
    }
    return result;
}

static int collect_ring(PQueueElement *head, PQueueElement **nodes,
                        size_t *index)
{
    PQueueElement *x;

    if (head == NULL)
        return 1;
    x = head;
    do {
        nodes[(*index)++] = x;
        if (!collect_ring(x->Child, nodes, index))
            return 0;
        x = x->Right;
    } while (x != head);
    return 1;
}

static int Equal(const PQueue *left, const PQueue *right)
{
    PQueueElement **left_nodes;
    PQueueElement **right_nodes;
    unsigned char *used;
    size_t i;
    size_t j;
    int found;

    if (left == NULL && right == NULL)
        return 1;
    if (left == NULL || right == NULL)
        return 0;
    if (left->ElementSize != right->ElementSize ||
        left->count != right->count)
        return 0;
    if (left->count == 0)
        return 1;
    if (left->count > SIZE_MAX / sizeof(*left_nodes) ||
        right->count > SIZE_MAX / sizeof(*right_nodes))
        return 0;

    left_nodes = (PQueueElement **)left->Allocator->malloc(
        left->count * sizeof(*left_nodes));
    right_nodes = (PQueueElement **)right->Allocator->malloc(
        right->count * sizeof(*right_nodes));
    used = (unsigned char *)right->Allocator->calloc(right->count, 1);
    if (left_nodes == NULL || right_nodes == NULL || used == NULL) {
        if (left_nodes != NULL)
            left->Allocator->free(left_nodes);
        if (right_nodes != NULL)
            right->Allocator->free(right_nodes);
        if (used != NULL)
            right->Allocator->free(used);
        return 0;
    }

    i = 0;
    j = 0;
    if (!collect_ring(left->Root, left_nodes, &i) ||
        !collect_ring(right->Root, right_nodes, &j)) {
        left->Allocator->free(left_nodes);
        right->Allocator->free(right_nodes);
        right->Allocator->free(used);
        return 0;
    }
    found = 1;
    for (i = 0; i < left->count && found; ++i) {
        size_t k;
        found = 0;
        for (k = 0; k < right->count; ++k) {
            if (!used[k] && left_nodes[i]->Key == right_nodes[k]->Key &&
                (left->ElementSize == 0 ||
                 memcmp(left_nodes[i]->Data, right_nodes[k]->Data,
                        left->ElementSize) == 0)) {
                used[k] = 1;
                found = 1;
                break;
            }
        }
    }
    left->Allocator->free(left_nodes);
    right->Allocator->free(right_nodes);
    right->Allocator->free(used);
    return found;
}

static PQueue *Union(PQueue *left, PQueue *right)
{
    PQueue *merged;
    PQueue state;
    ContainerAllocator *allocator;

    if (left == NULL || right == NULL) {
        iError.RaiseError("iPQueue.Union", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (left == right)
        return left;
    if (left->ElementSize != right->ElementSize) {
        iError.RaiseError("iPQueue.Union", CONTAINER_ERROR_INCOMPATIBLE);
        return NULL;
    }
    if (left->Root == NULL) {
        Finalize(left);
        return right;
    }
    if (right->Root == NULL) {
        Finalize(right);
        return left;
    }

    /* Build first, so an allocator failure leaves both input queues intact. */
    merged = CreateWithAllocator(left->ElementSize, left->Allocator);
    if (merged == NULL || !copy_contents(merged, left) ||
        !copy_contents(merged, right)) {
        if (merged != NULL)
            Finalize(merged);
        return NULL;
    }

    /* Transfer the completed storage into left, retaining its public address. */
    allocator = left->Allocator;
    state = *merged;
    release_storage(left);
    *left = state;
    left->VTable = &iPQueue;
    left->Allocator = allocator;
    allocator->free(merged);
    Finalize(right);
    return left;
}

static int Finalize(PQueue *h)
{
    ContainerAllocator *allocator;

    if (h == NULL)
        return iError.NullPtrError("iPQueue.Finalize");
    allocator = h->Allocator;
    release_storage(h);
    allocator->free(h);
    return 1;
}

PQueueInterface iPQueue = {
    Add,
    Size,
    Create,
    CreateWithAllocator,
    Equal,
    Sizeof,
    Add,
    Clear,
    Finalize,
    Pop,
    Front,
    Copy,
    Union,
};
