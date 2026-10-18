#include "containers.h"
/* -------------------------------------------------------------------
 *                           QUEUES
 * -------------------------------------------------------------------*/
typedef struct _Queue {
    QueueInterface *VTable;
    List *Items;
} _Queue;

static size_t QSize(Queue *Q)
{
	if (Q == NULL) {
		iError.NullPtrError("iQueue.Size");
		return 0;
	}
	return iList.Size(Q->Items);
}

static size_t Sizeof(Queue *q)
{
    size_t list_size;
    if (q == NULL) return sizeof(Queue);
    list_size = iList.Sizeof(q->Items);
    if (list_size > SIZE_MAX - sizeof(*q))
        return SIZE_MAX;
    return sizeof(*q) + list_size;
}

static int Finalize(Queue *Q)
{
	const ContainerAllocator *allocator;
	int r;

	if (Q == NULL)
		return iError.NullPtrError("iQueue.Finalize");
	allocator = iList.GetAllocator(Q->Items);
	r = iList.Finalize(Q->Items);
	if (r < 0)
		return r;
	if (allocator != NULL && allocator->free != NULL)
		allocator->free(Q);
    return 1;
}

static int QClear(Queue *Q)
{
	if (Q == NULL)
		return iError.NullPtrError("iQueue.Clear");
    return iList.Clear(Q->Items);
}


static int Dequeue(Queue *Q,void *result)
{
	if (Q == NULL)
		return iError.NullPtrError("iQueue.Dequeue");
    return iList.PopFront(Q->Items,result);
}

static int Enqueue(Queue *Q,void *newval)
{
	if (Q == NULL)
		return iError.NullPtrError("iQueue.Enqueue");
    return iList.Add(Q->Items,newval);
}


static Queue *CreateWithAllocator(size_t ElementSize,ContainerAllocator *allocator)
{
    Queue *result;

    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (allocator == NULL || allocator->malloc == NULL || allocator->free == NULL) {
		iError.RaiseError("iQueue.CreateWithAllocator", CONTAINER_ERROR_BADARG);
		return NULL;
	}

    result = allocator->malloc(sizeof(Queue));

    if (result == NULL)
        return NULL;
    result->Items = iList.CreateWithAllocator(ElementSize,allocator);
    if (result->Items == NULL) {
        allocator->free(result);
        return NULL;
    }
    result->VTable = &iQueue;
    return result;
}

static Queue *Create(size_t ElementSize)
{
	return CreateWithAllocator(ElementSize,CurrentAllocator);
}
static int Front(Queue *Q,void *result)
{
	size_t idx;
	if (Q == NULL || result == NULL) {
		return iError.NullPtrError("iQueue.Front");
	}
	idx = iList.Size(Q->Items);
	if (idx == 0)
		return 0;
	return iList.CopyElement(Q->Items,0,result);
}

static int Back(Queue *Q,void *result)
{
	size_t idx;
	if (Q == NULL || result == NULL) {
		return iError.NullPtrError("iQueue.Back");
	}
	idx = iList.Size(Q->Items);
	if (idx == 0)
		return 0;
	return iList.CopyElement(Q->Items,idx-1,result);
}

static List *GetData(Queue *q)
{
	if (q == NULL) {
		iError.RaiseError("iQueue.GetData",CONTAINER_ERROR_BADARG);
		return NULL;
	}
	return q->Items;
}


QueueInterface iQueue = {
    Create,
	CreateWithAllocator,
    QSize,
    Sizeof,
    Enqueue,
    Dequeue,
    QClear,
    Finalize,
	Front,
	Back,
	GetData,
};
