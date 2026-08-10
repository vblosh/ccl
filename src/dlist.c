/* Number of elements by default */
#ifndef DEFAULT_START_SIZE
#define DEFAULT_START_SIZE 20
#endif
#include "containers.h"
#include "ccl_internal.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

static const guid DlistGuid = {0xac2525ff, 0x2e2a, 0x4540,
{0xae,0x70,0xc4,0x7a,0x2,0xf7,0xa,0xed}
};
#define DLIST_PERSIST_VERSION UINT32_C(1)

#define LIST_HASPOINTER	2
#define CHUNK_SIZE	1000
static int Add_nd(Dlist *l,const void *elem);
static int AddRange(Dlist *l,size_t n,const void *data);
static int Finalize(Dlist *l);
static size_t SizeofIterator(const Dlist *l);

/* Dlist predates an ownership bit in its public header.  Keep ownership of
 * the header (Create versus Init caller storage) out of the ABI, just as the
 * newer List implementation does. */
typedef struct DlistHeaderOwner DlistHeaderOwner;
struct DlistHeaderOwner {
    Dlist *list;
    DlistHeaderOwner *next;
};
static DlistHeaderOwner *DlistOwners;

static int RegisterDlistHeader(Dlist *list)
{
    DlistHeaderOwner *owner = malloc(sizeof(*owner));
    if (owner == NULL)
        return 0;
    owner->list = list;
    owner->next = DlistOwners;
    DlistOwners = owner;
    return 1;
}

static int UnregisterDlistHeader(Dlist *list)
{
    DlistHeaderOwner **p = &DlistOwners;
    while (*p != NULL) {
        if ((*p)->list == list) {
            DlistHeaderOwner *owner = *p;
            *p = owner->next;
            free(owner);
            return 1;
        }
        p = &(*p)->next;
    }
    return 0;
}

static int DlistHeaderIsOwned(const Dlist *list)
{
    const DlistHeaderOwner *owner;
    for (owner = DlistOwners; owner != NULL; owner = owner->next)
        if (owner->list == list)
            return 1;
    return 0;
}

typedef struct DlistIteratorOwner DlistIteratorOwner;
struct DlistIteratorOwner {
    struct DListIterator *iterator;
    DlistIteratorOwner *next;
};
static DlistIteratorOwner *DlistIteratorOwners;

static int RegisterDlistIterator(struct DListIterator *iterator)
{
    DlistIteratorOwner *owner = malloc(sizeof(*owner));
    if (owner == NULL)
        return 0;
    owner->iterator = iterator;
    owner->next = DlistIteratorOwners;
    DlistIteratorOwners = owner;
    return 1;
}

static int UnregisterDlistIterator(struct DListIterator *iterator)
{
    DlistIteratorOwner **p = &DlistIteratorOwners;
    while (*p != NULL) {
        if ((*p)->iterator == iterator) {
            DlistIteratorOwner *owner = *p;
            *p = owner->next;
            free(owner);
            return 1;
        }
        p = &(*p)->next;
    }
    return 0;
}

static int DlistNodeSize(const Dlist *l, size_t *size)
{
    if (l == NULL || size == NULL)
        return CONTAINER_ERROR_BADARG;
    if (l->ElementSize > SIZE_MAX - sizeof(DlistElement))
        return CONTAINER_ERROR_NOMEMORY;
    *size = sizeof(DlistElement) + l->ElementSize;
    return 1;
}

static void FreeDlistNode(Dlist *l, DlistElement *node)
{
    if (node == NULL)
        return;
    if (l->Heap != NULL)
        (void)iHeap.FreeObject(l->Heap, node);
    else
        l->Allocator->free(node);
}

static void DestroyDlistNode(Dlist *l, DlistElement *node)
{
    if (node == NULL)
        return;
    if (l->DestructorFn != NULL)
        (void)l->DestructorFn(node->Data);
    FreeDlistNode(l, node);
}

static int DlistContainsNode(const Dlist *l, const DlistElement *needle)
{
    const DlistElement *node;
    size_t i;
    if (l == NULL || needle == NULL)
        return 0;
    node = l->First;
    for (i = 0; node != NULL && i < l->count; ++i, node = node->Next)
        if (node == needle)
            return 1;
    return 0;
}

static int DlistTransferCompatible(const Dlist *dst, const Dlist *src)
{
    if (dst == NULL || src == NULL)
        return 0;
    return dst->ElementSize == src->ElementSize &&
           dst->Allocator == src->Allocator &&
           dst->Heap == src->Heap &&
           dst->DestructorFn == src->DestructorFn &&
           dst->Compare == src->Compare &&
           dst->RaiseError == src->RaiseError &&
           dst->VTable == src->VTable;
}
/*------------------------------------------------------------------------
 Procedure:     new_link ID:1
 Purpose:       Allocation of a new Dlist element. If the element
                size is zero, we have an heterogenous
                Dlist, and we allocate just a pointer to the data
                that is maintained by the user.
                Note that we allocate the size of a Dlist element
                plus the size of the data in a single
                block. This block should be passed to the FREE
                function.

 Input:         The Dlist where the new element should be added and a
                pointer to the data that will be added (can be
                NULL).
 Output:        A pointer to the new Dlist element (can be NULL)
 Errors:        If there is no memory returns NULL
------------------------------------------------------------------------*/
static DlistElement *new_dlink(Dlist *l,const void *data,const char *fname)
{
    DlistElement *result;
    size_t node_size;

    if (l == NULL || l->Allocator == NULL || data == NULL)
        return NULL;
    if (DlistNodeSize(l, &node_size) < 0) {
        l->RaiseError(fname, CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if (l->Heap == NULL) {
	    result = l->Allocator->malloc(node_size);
    }
    else result = iHeap.NewObject(l->Heap);
    if (result == NULL) {
        l->RaiseError(fname,CONTAINER_ERROR_NOMEMORY);
    }
    else {
        result->Next = NULL;
        result->Previous = NULL;
        memcpy(&result->Data,data,l->ElementSize);
    }
    return result;
}

static int DefaultDlistCompareFunction(const void *left,const void *right,CompareInfo *ExtraArgs)
{
        size_t siz=((Dlist *)ExtraArgs->ContainerLeft)->ElementSize;
        return memcmp(left,right,siz);
}

static Dlist *InitWithAllocator(Dlist *result,size_t elementsize,const  ContainerAllocator *allocator)
{
    if (result == NULL || elementsize == 0 ||
        elementsize > SIZE_MAX - sizeof(DlistElement)) {
        iError.NullPtrError("iDlist.Create");
        return NULL;
    }
    if (allocator == NULL || allocator->malloc == NULL ||
        allocator->free == NULL || allocator->calloc == NULL ||
        allocator->realloc == NULL) {
        iError.NullPtrError("iDlist.Create");
        return NULL;
    }
    memset(result,0,sizeof(Dlist));
    result->ElementSize = elementsize;
    result->VTable = &iDlist;
    result->Compare = DefaultDlistCompareFunction;
    result->RaiseError = iError.RaiseError;
    result->Allocator = allocator;
    return result;
}

static Dlist *Init(Dlist *result,size_t elementsize)
{
    return InitWithAllocator(result,elementsize,CurrentAllocator);
}

/*------------------------------------------------------------------------
 Procedure:     Create ID:1
 Purpose:       Allocates a new Dlist object header, initializes the
                VTable field and the element size
 Input:         The size of the elements of the Dlist. Can be zero,
                meaning that the Dlist is made of objects managed by
                the user
 Output:        A pointer to the newly created Dlist or NULL if
                there is no memory.
 Errors:        If element size is smaller than zero an error
                routine is called. If there is no memory result is
                NULL.
------------------------------------------------------------------------*/
static Dlist *CreateWithAllocator(size_t elementsize,const ContainerAllocator *allocator)
{
    Dlist *result,*r;

    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (allocator == NULL || elementsize == 0 ||
        elementsize > SIZE_MAX - sizeof(DlistElement) ||
        allocator->malloc == NULL || allocator->free == NULL ||
        allocator->calloc == NULL || allocator->realloc == NULL) {
	    iError.NullPtrError("iDlist.Create");
	    return NULL;
    }
    result = allocator->malloc(sizeof(Dlist));
    if (result == NULL) {
        iError.RaiseError("iDlist.Create",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    r = InitWithAllocator(result,elementsize,allocator);
    if (r == NULL) {
        allocator->free(result);
        return NULL;
    }
    if (!RegisterDlistHeader(result)) {
        allocator->free(result);
        iError.RaiseError("iDlist.Create", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    return r;
}

static Dlist *Create(size_t elementsize)
{
    return CreateWithAllocator(elementsize,CurrentAllocator);
}

static Dlist *InitializeWith(size_t elementSize,size_t n,const void *Data)
{
        Dlist *result = Create(elementSize);
        if (result == NULL)
                return result;
        if (n != 0 && Data == NULL) {
                result->RaiseError("iDlist.InitializeWith", CONTAINER_ERROR_BADARG);
            (void)Finalize(result);
                return NULL;
        }
        if (AddRange(result, n, Data) < 0) {
                (void)Finalize(result);
                return NULL;
        }
        return result;
}


static int UseHeap(Dlist *L,const ContainerAllocator *m)
{
    size_t node_size;
    size_t alignment = _Alignof(max_align_t);
    if (L == NULL)
        return iError.NullPtrError("iDlist.UseHeap");
    if (L->Heap || L->count) {
        L->RaiseError("iDlist.UseHeap",CONTAINER_ERROR_NOT_EMPTY);
        return 0;
    }
    if (m == NULL)
     m = L->Allocator;
    if (m == NULL || DlistNodeSize(L, &node_size) < 0) {
        L->RaiseError("iDlist.UseHeap", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    if (alignment > 1) {
        size_t remainder = node_size % alignment;
        if (remainder != 0) {
            size_t padding = alignment - remainder;
            if (node_size > SIZE_MAX - padding) {
                L->RaiseError("iDlist.UseHeap", CONTAINER_ERROR_NOMEMORY);
                return CONTAINER_ERROR_NOMEMORY;
            }
            node_size += padding;
        }
    }
    L->Heap = iHeap.Create(node_size, m);
    if (L->Heap == NULL) {
        L->RaiseError("iDlist.UseHeap",CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    return 1;

}

/* This function was proposed by Ben Pfaff in c.l.c if I remember correctly */
static Dlist *Splice ( Dlist *list, void *ppos, Dlist *toInsert, int dir )
{
    DlistElement *pos = ppos;
    DlistElement *before;
    DlistElement *after;
    size_t new_count;
    if (( list == NULL ) || toInsert == NULL) {
        iError.NullPtrError("iDlist.Splice");
        return NULL;
    }
    if (list == toInsert) {
        list->RaiseError("iDlist.Splice", CONTAINER_ERROR_BADARG, list, pos);
        return NULL;
    }
    if (toInsert->count == 0)
	    return list;
    if (list->count == 0 && pos != NULL) {
        list->RaiseError("iDlist.Splice", CONTAINER_ERROR_BADARG, list, pos);
        return NULL;
    }
    if (list->count != 0 && (pos == NULL || !DlistContainsNode(list, pos))) {
        list->RaiseError("iDlist.Splice", CONTAINER_ERROR_BADARG, list, pos);
        return NULL;
    }
    if ((dir != 0 && dir != 1) || (list->Flags & CONTAINER_READONLY) ||
        toInsert->Flags & CONTAINER_READONLY) {
        list->RaiseError("iDlist.Splice",
                         (list->Flags & CONTAINER_READONLY) ||
                         (toInsert->Flags & CONTAINER_READONLY) ?
                         CONTAINER_ERROR_READONLY : CONTAINER_ERROR_BADARG,
                         list);
        return NULL;
    }
    /* A heap is an arena owner, not a node allocator that can be split
     * between headers.  Rejecting heap transfers keeps every node owned by
     * exactly one live header. */
    if (toInsert->Heap != NULL || list->Heap != NULL ||
        !DlistTransferCompatible(list, toInsert)) {
        list->RaiseError("iDlist.Splice", CONTAINER_ERROR_INCOMPATIBLE,
                         list, toInsert);
        return NULL;
    }
    if (toInsert->count > SIZE_MAX - list->count) {
        list->RaiseError("iDlist.Splice", CONTAINER_ERROR_NOMEMORY, list);
        return NULL;
    }
    if (list->count == 0) {
        list->First = toInsert->First;
        list->Last = toInsert->Last;
        list->count = toInsert->count;
        toInsert->First->Previous = NULL;
        toInsert->Last->Next = NULL;
        toInsert->First = toInsert->Last = NULL;
        toInsert->count = 0;
        toInsert->timestamp++;
        list->timestamp++;
        if (list->Flags & CONTAINER_HAS_OBSERVER)
            iObserver.Notify(list, CCL_APPEND, toInsert, NULL);
        return list;
    }
    before = dir == 0 ? pos->Previous : pos;
    after = dir == 0 ? pos : pos->Next;
    toInsert->First->Previous = before;
    toInsert->Last->Next = after;
    if (before != NULL)
        before->Next = toInsert->First;
    else
        list->First = toInsert->First;
    if (after != NULL)
        after->Previous = toInsert->Last;
    else
        list->Last = toInsert->Last;
    new_count = list->count + toInsert->count;
    list->count = new_count;
    toInsert->First = toInsert->Last = NULL;
    toInsert->count = 0;
    toInsert->timestamp++;
    list->timestamp++;
    if (list->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(list, CCL_APPEND, toInsert, NULL);
    return list;
}



/*------------------------------------------------------------------------
 Procedure:     Clear ID:1
 Purpose:       Reclaims all memory used by a Dlist. The Dlist header
                itself is not reclaimed but zeroed. Note that the
                Dlist must be writable.
 Input:         The Dlist to be cleared
 Output:        Returns the number of elemnts that were in the Dlist
                if OK, CONTAINER_ERROR_READONLY otherwise.
 Errors:        None
------------------------------------------------------------------------*/
static int Clear(Dlist *l)
{
    unsigned old_flags;
    unsigned old_timestamp;
    DlistElement *rvp,*next;

    if (l == NULL) {
        return iError.NullPtrError("iDlist.Size");
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.Add",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    old_flags = l->Flags;
    old_timestamp = l->timestamp;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_CLEAR,NULL,NULL);
    rvp = l->First;
    while (rvp != NULL) {
        next = rvp->Next;
        if (l->DestructorFn != NULL)
            (void)l->DestructorFn(rvp->Data);
        if (l->Heap == NULL)
            l->Allocator->free(rvp);
        rvp = next;
    }
    /* Popped nodes are released immediately by PopFront/PopBack.  Drain a
     * legacy FreeList as well so old headers cannot leak on Clear. */
    rvp = l->FreeList;
    while (rvp != NULL) {
        next = rvp->Next;
        if (l->Heap == NULL)
            l->Allocator->free(rvp);
        rvp = next;
    }
    if (l->Heap != NULL)
        iHeap.Finalize(l->Heap);
    l->count = 0;
    l->Heap = NULL;
    l->First = l->Last = NULL;
    l->FreeList = NULL;
    l->Flags = old_flags;
    l->timestamp = old_timestamp + 1;
    return 1;
}

/*------------------------------------------------------------------------
 Procedure:     Add ID:1
 Purpose:       Adds an element to the Dlist
 Input:         The Dlist and the elemnt to be added
 Output:        Returns the number of elements in the Dlist or a
                negative error code if an error occurs.
 Errors:        The element to be added can't be NULL, and the Dlist
                must be writable.
------------------------------------------------------------------------*/
static int Add_nd(Dlist *l,const void *elem)
{
        if (l == NULL || elem == NULL)
                return CONTAINER_ERROR_BADARG;
        if (l->count == SIZE_MAX)
                return CONTAINER_ERROR_NOMEMORY;
        DlistElement *newl = new_dlink(l,elem,"iList.Add");
        if (newl == 0)
                return CONTAINER_ERROR_NOMEMORY;
        if (l->count ==  0) {
                l->First = newl;
        }
        else {
                l->Last->Next = newl;
                newl->Previous = l->Last;
        }
        l->Last = newl;
        l->timestamp++;
        ++l->count;
    return 1;
}

static int Add(Dlist *l,const void *elem)
{
    int r;
    if (elem == NULL || l == NULL) {
        if (l)
                l->RaiseError("iDlist.Add",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.Add");
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags &CONTAINER_READONLY) {
        l->RaiseError("iDlist.Add",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    r = Add_nd(l,elem);
    if (r < 0)
        return r;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_ADD,elem,NULL);

    return 1;
}

static int AddRange(Dlist * AL,size_t n, const void *data)
{
        const unsigned char *p;
        size_t original_n = n;
        size_t original_count;
        unsigned original_timestamp;
        DlistElement *old_last;

        if (AL == NULL) {
                return iError.NullPtrError("iList.AddRange");
        }
        if (AL->Flags & CONTAINER_READONLY) {
                AL->RaiseError("iList.AddRange",CONTAINER_ERROR_READONLY,AL);
                return CONTAINER_ERROR_READONLY;
        }
        if (data == NULL && n != 0) {
                AL->RaiseError("iList.AddRange",CONTAINER_ERROR_BADARG);
                return CONTAINER_ERROR_BADARG;
        }
        if (n > SIZE_MAX - AL->count ||
            (AL->ElementSize != 0 && n > SIZE_MAX / AL->ElementSize)) {
                AL->RaiseError("iList.AddRange", CONTAINER_ERROR_NOMEMORY, AL);
                return CONTAINER_ERROR_NOMEMORY;
        }
        original_count = AL->count;
        old_last = AL->Last;
        original_timestamp = AL->timestamp;
        p = data;
        while (n > 0) {
                int r = Add_nd(AL,p);
                if (r < 0) {
                        /* AddRange is atomic with respect to allocation
                         * failure: dispose only the newly appended suffix. */
                        DlistElement *node = old_last == NULL ? AL->First : old_last->Next;
                        if (old_last != NULL)
                                old_last->Next = NULL;
                        else
                                AL->First = NULL;
                        while (node != NULL) {
                                DlistElement *next = node->Next;
                                DestroyDlistNode(AL, node);
                                node = next;
                        }
                        AL->Last = old_last;
                        if (old_last != NULL)
                                old_last->Next = NULL;
                        AL->count = original_count;
                        AL->timestamp = original_timestamp;
                        return r;
                }
                p += AL->ElementSize;
                --n;
        }
        if (original_n != 0)
                AL->timestamp = original_timestamp + 1;
    if (original_n != 0 && (AL->Flags & CONTAINER_HAS_OBSERVER)) {
        iObserver.Notify(AL,CCL_ADDRANGE,data,(void *)original_n);
    }

	return 1;
}

/*------------------------------------------------------------------------
 Procedure:     SetReadOnly ID:1
 Purpose:       Sets/Unsets the read only flag.
 Input:         The Dlist and the new value
 Output:        The old value of the flag
 Errors:        None
------------------------------------------------------------------------*/
static unsigned SetFlags(Dlist *l,unsigned newval)
{
    unsigned result;

    if (l == NULL) {
        iError.NullPtrError("iDlist.Size");
        return 0;
    }
    result = l->Flags;
    l->Flags = newval;
	l->timestamp++;
    return result;
}

/*------------------------------------------------------------------------
 Procedure:     IsReadOnly ID:1
 Purpose:       Queries the read only flag
 Input:         The Dlist
 Output:        The state of the flag
 Errors:        None
------------------------------------------------------------------------*/
static unsigned GetFlags(const Dlist *l)
{
    if (l == NULL) {
        iError.NullPtrError("iDlist.Size");
        return 0;
    }
    return l->Flags;
}

/*------------------------------------------------------------------------
 Procedure:     Size ID:1
 Purpose:       Returns the number of elements in the Dlist
 Input:         The Dlist
 Output:        The number of elements
 Errors:        None
------------------------------------------------------------------------*/
static size_t Size(const Dlist *l)
{
    if (l == NULL) {
        iError.NullPtrError("iDlist.Size");
        return 0;
    }
    return l->count;
}

/*------------------------------------------------------------------------
 Procedure:     SetCompareFunction ID:1
 Purpose:       Defines the function to be used in comparison of
                Dlist elements
 Input:         The Dlist and the new comparison function. If the new
                comparison function is NULL no changes are done.
 Output:        Returns the old function
 Errors:        None
------------------------------------------------------------------------*/
static CompareFunction SetCompareFunction(Dlist *l,CompareFunction fn)
{
    CompareFunction oldfn;


    if (l == NULL) {
        iError.NullPtrError("iDlist.Size");
        return NULL;
    }
    oldfn = l->Compare;
    if (fn != NULL && (l->Flags & CONTAINER_READONLY)) {
        l->RaiseError("iDlist.SetCompareFunction", CONTAINER_ERROR_READONLY, l);
        return oldfn;
    }
    if (fn != NULL) { /* Treat NULL as an enquiry to get the compare function */
        l->Compare = fn;
        l->timestamp++;
    }
    return oldfn;
}

/*------------------------------------------------------------------------
 Procedure:     Copy ID:1
 Purpose:       Copies a Dlist. The result is fully allocated (Dlist
                elements and data).
 Input:         The Dlist to be copied
 Output:        A pointer to the new Dlist
 Errors:        None. Returns NULL if therfe is no memory left.
------------------------------------------------------------------------*/
static Dlist *Copy(const Dlist *l)
{
    Dlist *result;
    DlistElement *rvp,*newRvp,*last;

    if (l == NULL) {
        iError.NullPtrError("iDlist.Size");
        return 0;
    }
    result = CreateWithAllocator(l->ElementSize, l->Allocator);
    if (result == NULL)
        return NULL;
    result->Flags = l->Flags;
    result->RaiseError = l->RaiseError;
    result->Compare = l->Compare;
    result->VTable = l->VTable;
    last = rvp = l->First;
    while (rvp) {
        newRvp = new_dlink(result,rvp->Data,"iDlist.Copy");
        if (newRvp == NULL) {
                l->RaiseError("iDlist.Copy",CONTAINER_ERROR_NOMEMORY);
                result->Flags &= ~CONTAINER_READONLY;
                (void)Finalize(result);
                   return NULL;
        }
        if (result->First == NULL)
                last = result->First = newRvp;
        else {
                last->Next = newRvp;
                newRvp->Previous = last;
                last = newRvp;
        }
        result->Last = newRvp;
        result->count++;
        rvp = rvp->Next;
    }
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_COPY,result,NULL);

    return result;
}

/*------------------------------------------------------------------------
 Procedure:     Finalize ID:1
 Purpose:       Reclaims all memory for a Dlist. The data, Dlist
                elements and the Dlist header are reclaimed.
 Input:         The Dlist to be destroyed. It should NOT be read-
                only.
 Output:        Returns the old count or a negative value if an
                error occurs (Dlist not writable)
 Errors:        Needs a writable Dlist
------------------------------------------------------------------------*/
static int Finalize(Dlist *l)
{
    int t;
    unsigned Flags;
    const ContainerAllocator *allocator;
    int owned;

    if (l == NULL) return CONTAINER_ERROR_BADARG;
    allocator = l->Allocator;
    owned = DlistHeaderIsOwned(l);
    Flags = l->Flags;
    t = Clear(l);
    if (t < 0)
        return t;
    if (Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_FINALIZE,NULL,NULL);
    if (owned) {
        (void)UnregisterDlistHeader(l);
        allocator->free(l);
    }
    return 1;
}

/*------------------------------------------------------------------------
 Procedure:     GetElement ID:1
 Purpose:       Returns the data associated with a given position
 Input:         The Dlist and the position
 Output:        A pointer to the data
 Errors:        NULL if error in the positgion index
------------------------------------------------------------------------*/
static void * GetElement(const Dlist *l,size_t position)
{
    DlistElement *rvp;


    if (l == NULL) {
        iError.NullPtrError("iDlist.GetElement");
        return 0;
    }
    if (position >= l->count) {
        l->RaiseError("GetElement",CONTAINER_ERROR_INDEX,l,position);
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.GetElement",CONTAINER_ERROR_READONLY,l);
        return NULL;
    }
    rvp = l->First;
    while (position) {
        rvp = rvp->Next;
        position--;
    }
    return rvp->Data;
}

static int ReplaceAt(Dlist *l,size_t position,const void *data)
{
    DlistElement *rvp;

    if (l == NULL || data == NULL) {
        if (l)
                l->RaiseError("iDlist.ReplaceAt",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.ReplaceAt");
        return CONTAINER_ERROR_BADARG;
    }
    /* Error checking */
    if (position >= l->count) {
        l->RaiseError("iDlist.ReplaceAt",CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags &CONTAINER_READONLY) {
        l->RaiseError("iDlist.ReplaceAt",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    /* Position at the right data item */
    if (position == l->count-1)
        rvp = l->Last;
    else  {
        rvp = l->First;
        while (position) {
                rvp = rvp->Next;
                position--;
        }
    }
    if (l->DestructorFn)
        l->DestructorFn(&rvp->Data);
    /* Replace the data there */
    memcpy(&rvp->Data , data,l->ElementSize);
    l->timestamp++;
    return 1;
}

/*------------------------------------------------------------------------
 Procedure:     GetRange ID:1
 Purpose:       Gets a consecutive sub sequence of the Dlist within
                two indexes.
 Input:         The Dlist, the subsequence start and end
 Output:        A new freshly allocated Dlist. If the source Dlist is
                empty or the range is empty, the result Dlist is
                empty too.
 Errors:        None
------------------------------------------------------------------------*/
static Dlist *GetRange(Dlist *l,size_t start,size_t end)
{
    size_t counter;
    Dlist *result;
    DlistElement *rvp;

    if (l == NULL) {
        iError.NullPtrError("iDlist.GetRange");
        return 0;
    }
    result = CreateWithAllocator(l->ElementSize, l->Allocator);
    if (result == NULL)
        return NULL;
    result->VTable = l->VTable;
    result->Compare = l->Compare;
    result->RaiseError = l->RaiseError;
    if (l->count == 0) {
        result->Flags = l->Flags;
        return result;
    }
    if (end >= l->count)
        end = l->count-1;
    if (start > end) {
        result->Flags = l->Flags;
        return result;
    }
    if (start == l->count-1)
        rvp = l->Last;
    else {
        rvp = l->First;
        counter = 0;
        while (counter < start) {
                rvp = rvp->Next;
                counter++;
        }
    }
    while (start <= end && rvp != NULL) {
        int r = Add_nd(result,&rvp->Data);
        if (r < 0) {
                Finalize(result);
                return NULL;
        }
        rvp = rvp->Next;
        start++;
    }
    result->Flags = l->Flags;
    return result;
}

/*------------------------------------------------------------------------
 Procedure:     Equal ID:1
 Purpose:       Compares the data of two lists. They must be of the
                same length, the elements must have the same size
                and the comparison functions must be the same. If
                all this is true, the comparisons are done.
 Input:         The two lists to be compared
 Output:        Returns 1 if the two lists are equal, zero otherwise
 Errors:        None
------------------------------------------------------------------------*/
static int Equal(const Dlist *l1,const Dlist *l2)
{
    DlistElement *link1,*link2;
    CompareInfo ci;
    CompareFunction fn;

    if (l1 == l2)
        return 1;
    if (l1 == NULL || l2 == NULL)
        return 0;
    if (l1->count != l2->count)
        return 0;
    if (l1->ElementSize != l2->ElementSize)
        return 0;
    if (l1->count == 0)
        return 1;
    if (l1->Compare != l2->Compare)
        return 0;
    fn = l1->Compare;
    link1 = l1->First;
    link2 = l2->First;
    ci.ContainerLeft = l1;
    ci.ContainerRight = l2;
    ci.ExtraArgs = NULL;
    while (link1 && link2) {
        if (fn(link1->Data,link2->Data,&ci))
                return 0;
        link1 = link1->Next;
        link2 = link2->Next;
    }
    if (link1 || link2)
        return 0;
    return 1;
}


/*------------------------------------------------------------------------
 Procedure:     Insert ID:1
 Purpose:       Inserts an element at the start of the Dlist
 Input:         The Dlist and the data to insert
 Output:        The count of the Dlist after insertion, or CONTAINER_ERROR_READONLY
                if the Dlist is not writable or CONTAINER_ERROR_NOMEMORY if there is
    no more memory left.
 Errors:        None
------------------------------------------------------------------------*/
static int PushFront(Dlist *l,const void *pdata)
{
    DlistElement *rvp;

    if (l == NULL || pdata == NULL) {
        if (l)
                l->RaiseError("iDlist.PushFront",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.PushFront");
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.PushFront",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    rvp = new_dlink(l,pdata,"Insert");
    if (rvp == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    rvp->Next = l->First;
    rvp->Previous = NULL;
    if (l->First) {
        l->First->Previous = rvp;
    }
    l->First = rvp;
    if (l->Last == NULL)
        l->Last = rvp;
    l->count++;
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_PUSH,pdata,NULL);

    return 1;
}

static int PushBack(Dlist *l,const void *pdata)
{
    DlistElement *rvp;

    if (l == NULL || pdata == NULL) {
        if (l != NULL)
            l->RaiseError("iDlist.PushBack", CONTAINER_ERROR_BADARG);
        else
            iError.NullPtrError("iDlist.PushBack");
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.PushBack",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    rvp = new_dlink(l,pdata,"iDlist.Insert");
    if (rvp == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    if (l->count == 0)
        l->Last = l->First = rvp;
    else {
        l->Last->Next = rvp;
        rvp->Previous = l->Last;
        l->Last = rvp;
    }
    l->count++;
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_PUSH,pdata,NULL);

    return 1;
}


/*------------------------------------------------------------------------
 Procedure:     Pop ID:1
 Purpose:       Takes out the first element of a Dlist.
 Input:         The Dlist
 Output:        The first element or NULL if there is none or the
                Dlist is read only
 Errors:        None
------------------------------------------------------------------------*/
static int PopFront(Dlist *l,void *result)
{
    DlistElement *le;

    if (l == NULL) return iError.NullPtrError("iDlist.PopFront");

    if (l->count == 0)
        return 0;
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.PopFront",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    le = l->First;
    if (l->count == 1) {
        l->First = l->Last = NULL;
    }
    else {
        l->First = l->First->Next;
        l->First->Previous = NULL;
    }
    l->count--;
    if (result)
        memcpy(result,&le->Data,l->ElementSize);
    if (result == NULL && l->DestructorFn != NULL)
        (void)l->DestructorFn(le->Data);
    FreeDlistNode(l, le);
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_POP,result,NULL);

    return 1;
}

static int PopBack(Dlist *l,void *result)
{
    DlistElement *le;

    if (l == NULL) return iError.NullPtrError("iDlist.PopBack");

    if (l->count == 0)
        return 0;
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.PopBack",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    le = l->Last;
    if (l->count == 1) {
        l->First = l->Last = NULL;
    }
    else {
        l->Last = le->Previous;
        l->Last->Next = NULL;
    }
    l->count--;
    if (result)
        memcpy(result,&le->Data,l->ElementSize);
    if (result == NULL && l->DestructorFn != NULL)
        (void)l->DestructorFn(le->Data);
    FreeDlistNode(l, le);
    l->timestamp++;
    return 1;
}

static int InsertAt(Dlist *l,size_t pos,const void *pdata)
{
    DlistElement *elem;

    if (l == NULL || pdata == NULL) {
        if (l)
                l->RaiseError("iDlist.InsertAt",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.InsertAt");
        return CONTAINER_ERROR_BADARG;
    }
    if (pos > l->count) {
        l->RaiseError("iDlist.InsertAt",CONTAINER_ERROR_INDEX,l,pos);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.PushBack",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    if (pos == l->count) {
        return l->VTable->Add(l,pdata);
    }
    elem = new_dlink(l,pdata,"iDlist.InsertAt");
    if (elem == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    if (pos == 0 || l->First == NULL) {
        elem->Next = l->First;
        if (l->First != NULL)
                l->First->Previous = elem;
        l->First = elem;
    }
    else {
        DlistElement *rvp = l->First;
        while (pos > 0) {
                rvp = rvp->Next;
                pos--;
        }
        elem->Next = rvp;
        if (rvp->Previous)
                rvp->Previous->Next = elem;
        elem->Previous = rvp->Previous;
        rvp->Previous = elem;
    }
    l->timestamp++;
    ++l->count;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_INSERT_AT,pdata,(void *)pos);

    return 1;
}

static int InsertIn(Dlist *l, size_t idx,Dlist *newData)
{
    size_t newCount, i;
    DlistElement *before, *after, *first = NULL, *last = NULL;
    DlistElement *source;

    if (l == NULL || newData == NULL) {
        if (l)
                l->RaiseError("iDlist.InsertIn",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.InsertIn");
        return CONTAINER_ERROR_BADARG;
    }
    if (l == newData) {
        l->RaiseError("iDlist.InsertIn", CONTAINER_ERROR_BADARG, l);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.InsertIn",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    if (idx > l->count) {
        l->RaiseError("iDlist.InsertIn",CONTAINER_ERROR_INDEX,l,idx);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->ElementSize != newData->ElementSize) {
        l->RaiseError("iDlist.InsertIn",CONTAINER_ERROR_INCOMPATIBLE);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (newData->count == 0)
     return 1;
    if (newData->count > SIZE_MAX - l->count) {
        l->RaiseError("iDlist.InsertIn", CONTAINER_ERROR_NOMEMORY, l);
        return CONTAINER_ERROR_NOMEMORY;
    }
    /* Copy values into nodes owned by the destination.  This keeps source
     * allocator/heap provenance out of the destination and leaves newData
     * untouched, including when the source uses another allocator. */
    source = newData->First;
    for (i = 0; i < newData->count && source != NULL; ++i,
         source = source->Next) {
        DlistElement *node = new_dlink(l, source->Data, "iDlist.InsertIn");
        if (node == NULL) {
            while (first != NULL) {
                DlistElement *next = first->Next;
                FreeDlistNode(l, first);
                first = next;
            }
            return CONTAINER_ERROR_NOMEMORY;
        }
        if (first == NULL)
            first = node;
        else {
            last->Next = node;
            node->Previous = last;
        }
        last = node;
    }
    if (i != newData->count) {
        while (first != NULL) {
            DlistElement *next = first->Next;
            FreeDlistNode(l, first);
            first = next;
        }
        return CONTAINER_INTERNAL_ERROR;
    }
    if (idx == 0) {
        before = NULL;
        after = l->First;
    } else if (idx == l->count) {
        before = l->Last;
        after = NULL;
    } else {
        after = l->First;
        for (i = 0; i < idx; ++i)
            after = after->Next;
        before = after->Previous;
    }
    if (before != NULL)
        before->Next = first;
    else
        l->First = first;
    first->Previous = before;
    if (after != NULL)
        after->Previous = last;
    else
        l->Last = last;
    last->Next = after;
    newCount = l->count + newData->count;
    l->timestamp++;
    l->count = newCount;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_INSERT_IN,newData,NULL);

    return 1;
}

static int EraseAt(Dlist *l,size_t position)
{
    DlistElement *rvp=NULL,*before,*after;
    size_t original_position = position;

    if (l == NULL) return iError.NullPtrError("iDlist.InsertIn");

    if (position >= l->count) {
        l->RaiseError("iDlist.EraseAt",CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.PushBack",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    rvp = l->First;
    if (position < l->count / 2) {
        while (position-- > 0)
            rvp = rvp->Next;
    } else {
        size_t from_end = l->count - position - 1;
        rvp = l->Last;
        while (from_end-- > 0)
            rvp = rvp->Previous;
    }
    before = rvp->Previous;
    after = rvp->Next;
    if (before != NULL)
        before->Next = after;
    else
        l->First = after;
    if (after != NULL)
        after->Previous = before;
    else
        l->Last = before;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_ERASE_AT,rvp->Data,(void *)original_position);
    if (l->DestructorFn != NULL)
        (void)l->DestructorFn(rvp->Data);
    FreeDlistNode(l, rvp);
    l->timestamp++;
    --l->count;

    return 1;
}


static int Reverse(Dlist *l)
{
    DlistElement *tmp,*rvp;

    if (l == NULL)  {
		return iError.NullPtrError("iDlist.Reverse");
	}
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.Reverse",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    if (l->count < 2)
        return 1;
    rvp = l->Last;
    l->Last = l->First;
    l->First = rvp;
    while (rvp) {
        tmp = rvp->Previous;
        rvp->Previous = rvp->Next;
        rvp->Next = tmp;
        rvp = tmp;
    }
    l->Last->Next = NULL;
    if (l->First)
        l->First->Previous = NULL;
    l->timestamp++;
    return 1;
}



/* Searches a Dlist for a given data item
   Returns a positive integer if found, negative if the end is reached
*/
static int IndexOf(const Dlist *l,const void *ElementToFind, void * args,size_t *result)
{
    DlistElement *rvp;
    int r,i=0;
    CompareFunction fn;
    CompareInfo ci;
    size_t ignored_result;

    if (l == NULL || ElementToFind == NULL) {
        if (l)
                l->RaiseError("iDlist.IndexOf",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.IndexOf");
        return CONTAINER_ERROR_BADARG;
    }
    rvp = l->First;
    fn = l->Compare;
    if (result == NULL)
        result = &ignored_result;
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = args;
    while (rvp) {
        r = fn(&rvp->Data,ElementToFind,&ci);
        if (r == 0) {
                *result = i;
                return 1;
        }
        rvp = rvp->Next;
        i++;
    }
    return CONTAINER_ERROR_NOTFOUND;
}

static int EraseInternal(Dlist *l,const void *elem,int all)
{
    int r,result = CONTAINER_ERROR_NOTFOUND;
    CompareFunction fn;
    CompareInfo ci;
    DlistElement *rvp;
    size_t position;

    if (l == NULL || elem == NULL) {
        if (l)
                l->RaiseError("iDlist.Erase",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.Erase");
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.Erase", CONTAINER_ERROR_READONLY, l);
        return CONTAINER_ERROR_READONLY;
    }
    position = 0;
    rvp = l->First;
    fn = l->Compare;
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    while (rvp) {
        r = fn(&rvp->Data,elem,&ci);
        if (r == 0) {
            DlistElement *next = rvp->Next;
            DlistElement *previous = rvp->Previous;
            if (l->Flags & CONTAINER_HAS_OBSERVER)
                iObserver.Notify(l,CCL_ERASE_AT,rvp->Data,(void *)position);
            if (previous != NULL)
                previous->Next = next;
            else
                l->First = next;
            if (next != NULL)
                next->Previous = previous;
            else
                l->Last = previous;
            if (l->DestructorFn != NULL)
                (void)l->DestructorFn(rvp->Data);
            FreeDlistNode(l, rvp);
            l->count--;
            l->timestamp++;
            if (all == 0) return 1;
            result = 1;
            rvp = next;
            position++;
            continue;
        }
        rvp = rvp->Next;
        position++;
    }
    return result;
}

static int Erase(Dlist *l,const void *elem)
{
    return EraseInternal(l,elem,0);
}
static int EraseAll(Dlist *l,const void *elem)
{
    return EraseInternal(l,elem,1);
}


/*------------------------------------------------------------------------
 Procedure:     Contains ID:1
 Purpose:       Determines if the given data is in the container
 Input:         The Dlist and the data to be searched
 Output:        Returns 1 (true) if the data is in there, false
 otherwise
 Errors:        The same as the function IndexOf
 ------------------------------------------------------------------------*/
static int Contains(const Dlist *l,const void *data)
{
    size_t r;
    int i;
    if (l == NULL || data == NULL) {
        if (l)
                l->RaiseError("iDlist.Contains",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.Contains");
        return CONTAINER_ERROR_BADARG;
    }
    i = IndexOf(l,data,NULL,&r);
    return i == CONTAINER_ERROR_NOTFOUND ? 0 : i < 0 ? i : 1;
}

static int CopyElement(const Dlist *l,size_t position,void *outBuffer)
{
    DlistElement *rvp;

    if (l == NULL || outBuffer == NULL) {
        if (l)
                l->RaiseError("iDlist.CopyElement",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.CopyElement");
        return CONTAINER_ERROR_BADARG;
    }
    if (position >= l->count) {
        l->RaiseError("iDlist.CopyElement",CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    if (position < l->count/2) {
        rvp = l->First;
        while (position) {
                rvp = rvp->Next;
                position--;
        }
    }
    else {
        rvp = l->Last;
        position = l->count - position;
        position--;
        while (position > 0) {
                rvp = rvp->Previous;
                position--;
        }
    }
    memcpy(outBuffer,rvp->Data,l->ElementSize);
    return 1;
}


static int dlcompar (const void *elem1, const void *elem2,CompareInfo *ExtraArgs)
{
    DlistElement *Elem1 = *(DlistElement **)elem1;
    DlistElement *Elem2 = *(DlistElement **)elem2;
    Dlist *l = (Dlist *)ExtraArgs->ContainerLeft;
    CompareFunction fn = l->Compare;
    return fn(Elem1->Data,Elem2->Data,ExtraArgs);
}

static int Sort(Dlist *l)
{
    DlistElement **tab;
    size_t i;
    size_t bytes;
    DlistElement *rvp;
    CompareInfo ci;

    if (l == NULL) return iError.NullPtrError("iDlist.Sort");

    if (l->Flags&CONTAINER_READONLY) {
        l->RaiseError("iDlist.Sort",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    if (l->count < 2)
        return 1;
    if (l->count > SIZE_MAX / sizeof(DlistElement *)) {
        l->RaiseError("iDlist.Sort", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    bytes = l->count * sizeof(DlistElement *);
    tab = l->Allocator->malloc(bytes);
    if (tab == NULL) {
        l->RaiseError("iDlist.Sort",CONTAINER_ERROR_NOMEMORY);
        return 0;
    }
    rvp = l->First;
    for (i=0; i<l->count;i++) {
        tab[i] = rvp;
        rvp = rvp->Next;
    }
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    qsortEx(tab,l->count,sizeof(DlistElement *),dlcompar,&ci);
    for (i=1; i<l->count-1;i++) {
        tab[i]->Next = tab[i+1];
        tab[i]->Previous = tab[i-1];
    }
    tab[0]->Next = tab[1];
    tab[0]->Previous = NULL;
    tab[l->count-1]->Next = NULL;
    tab[l->count-1]->Previous = tab[l->count-2];
    l->Last = tab[l->count-1];
    l->First = tab[0];
    l->timestamp++;
    l->Allocator->free(tab);
    return 1;
}
static int Apply(Dlist *L,int (Applyfn)(void *,void *),void *arg)
{
    DlistElement *le;
    void *buffer = NULL;
    int result = 1;

    if (L == NULL)  return iError.NullPtrError("iDist.Apply");
    if (Applyfn == NULL) {
        L->RaiseError("iDlist.Apply",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (L->Flags & CONTAINER_READONLY) {
        buffer = L->Allocator->malloc(L->ElementSize);
        if (buffer == NULL) {
            L->RaiseError("iDlist.Apply", CONTAINER_ERROR_NOMEMORY);
            return CONTAINER_ERROR_NOMEMORY;
        }
    }
    le = L->First;
    while (le) {
	    void *data = buffer != NULL ? buffer : le->Data;
        if (buffer != NULL)
            memcpy(buffer, le->Data, L->ElementSize);
        if (!Applyfn(data,arg)) {
            result = 0;
            break;
        }
        le = le->Next;
    }
    if (buffer != NULL)
        L->Allocator->free(buffer);
    if (buffer == NULL && L->count != 0)
        L->timestamp++;
    return result;
}

static ErrorFunction SetErrorFunction(Dlist *l,ErrorFunction fn)
{
    ErrorFunction old;

    if (l == NULL) {
        return iError.RaiseError;
    }
    old = l->RaiseError;
    if (fn)
        l->RaiseError = fn;
    return old;
}

/* ------------------------------------------------------------------------------ */
/*                                Iterators                                       */
/* ------------------------------------------------------------------------------ */

static void *GetNext(Iterator *it)
{
    struct DListIterator *li = (struct DListIterator *)it;

    if (li == NULL) {
	    iError.RaiseError("iDlist.GetNext", CONTAINER_ERROR_BADARG);
	    return NULL;
	}
	if (li->Magic != DLIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetNext",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
	if (li->timestamp != li->L->timestamp) {
	    li->L->RaiseError("iDlist.GetNext",CONTAINER_ERROR_OBJECT_CHANGED);
	    return NULL;
    }
    if (li->L->count == 0 || li->Current == NULL ||
        li->index == (size_t)-1 || li->index + 1 >= li->L->count)
        return NULL;
    li->Current = li->Current->Next;
    ++li->index;
    if (li->L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, li->Current->Data, li->L->ElementSize);
        return li->ElementBuffer;
    }
    return li->Current->Data;
}

static void *GetPrevious(Iterator *it)
{
    struct DListIterator *li = (struct DListIterator *)it;

	if (li == NULL) {
	    iError.RaiseError("iDlist.GetPrevious", CONTAINER_ERROR_BADARG);
	    return NULL;
	}
	if (li->Magic != DLIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetPrevious",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
	if (li->timestamp != li->L->timestamp) {
	    li->L->RaiseError("iDlist.GetPrevious",CONTAINER_ERROR_OBJECT_CHANGED);
	    return NULL;
    }
    if (li->L->count == 0 || li->Current == NULL ||
        li->index == (size_t)-1 || li->index == 0)
        return NULL;
    li->Current = li->Current->Previous;
    --li->index;
    if (li->L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, li->Current->Data, li->L->ElementSize);
        return li->ElementBuffer;
    }
    return li->Current->Data;
}

static void *GetFirst(Iterator *it)
{
    struct DListIterator *li = (struct DListIterator *)it;

    if (li == NULL) {
	    iError.RaiseError("iDlist.GetFirst", CONTAINER_ERROR_BADARG);
	    return NULL;
	}
	if (li->Magic != DLIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetNext",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
    if (li->timestamp != li->L->timestamp) {
	    li->L->RaiseError("iDlist.GetFirst",CONTAINER_ERROR_OBJECT_CHANGED);
	    return NULL;
    }
	if (li->L->count == 0)
	    return NULL;
    li->index = 0;
    li->Current = li->L->First;
    if (li->L->Flags & CONTAINER_READONLY) {
	    memcpy(li->ElementBuffer,li->Current->Data,li->L->ElementSize);
	    return li->ElementBuffer;
    }
    return li->Current->Data;
}

static int ReplaceWithIterator(Iterator *it, void *data,int direction)
{
    struct DListIterator *li = (struct DListIterator *)it;
	int result;
	size_t pos;

	if (it == NULL || li == NULL) {
		iError.RaiseError("Replace",CONTAINER_ERROR_BADARG);
		return 0;
	}
	if (li->Magic != DLIST_MAGIC_NUMBER) {
		iError.RaiseError("List.ReplaceWith",CONTAINER_ERROR_WRONG_ITERATOR);
		return CONTAINER_ERROR_WRONG_ITERATOR;
	}
	if (li->L->Flags & CONTAINER_READONLY) {
		li->L->RaiseError("Replace",CONTAINER_ERROR_READONLY,li->L);
		return CONTAINER_ERROR_READONLY;
	}
	if (li->L->count == 0 || li->Current == NULL ||
            li->index == (size_t)-1 || li->index >= li->L->count)
		return 0;
    if (li->timestamp != li->L->timestamp) {
        li->L->RaiseError("Replace",CONTAINER_ERROR_OBJECT_CHANGED);
        return CONTAINER_ERROR_OBJECT_CHANGED;
    }
	pos = li->index;
	if (data == NULL)
		result = EraseAt(li->L,pos);
	else {
		result = ReplaceAt(li->L,pos,data);
	}
	if (result >= 0) {
		li->timestamp = li->L->timestamp;
		if (li->L->count == 0) {
		    li->index = (size_t)-1;
		    li->Current = NULL;
		} else if (data != NULL) {
		    li->Current = li->L->First;
		    for (size_t i = 0; i < pos; ++i)
		        li->Current = li->Current->Next;
		} else if (direction && pos < li->L->count) {
		    li->index = pos;
		    li->Current = li->L->First;
		    for (size_t i = 0; i < pos; ++i)
		        li->Current = li->Current->Next;
		} else {
		    li->index = pos == 0 ? 0 : pos - 1;
		    li->Current = li->L->First;
		    for (size_t i = 0; i < li->index; ++i)
		        li->Current = li->Current->Next;
		}
	}
	return result;
}


static void *GetCurrent(Iterator *it)
{
    struct DListIterator *li = (struct DListIterator *)it;

    if (li == NULL) {
        iError.NullPtrError("iDlist.GetCurrent");
        return NULL;
    }
	if (li->Magic != DLIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetCurrent",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
    if (li->timestamp != li->L->timestamp) {
        li->L->RaiseError("iDlist.GetCurrent", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (li->L->count == 0 || li->Current == NULL)
        return NULL;
    if (li->index == (size_t)-1) {
        li->L->RaiseError("GetCurrent",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (li->L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, li->Current->Data, li->L->ElementSize);
	    return li->ElementBuffer;
    }
    return li->Current->Data;
}

static void *Seek(Iterator *it,size_t idx)
{
    struct DListIterator *li = (struct DListIterator *)it;
    DlistElement *rvp;

	if (it == NULL) {
		iError.NullPtrError("iDlist.Seek");
		return NULL;
	}
	if (li->Magic != DLIST_MAGIC_NUMBER) {
		iError.RaiseError("List.Seek",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
	if (li->timestamp != li->L->timestamp) {
	    li->L->RaiseError("iDlist.Seek", CONTAINER_ERROR_OBJECT_CHANGED);
	    return NULL;
	}
	if (li->L->count == 0)
		return NULL;
	if (idx >= li->L->count) {
	    li->L->RaiseError("iDlist.Seek", CONTAINER_ERROR_INDEX, li->L, idx);
	    return NULL;
	}
	rvp = li->L->First;
	if (idx == 0) {
		li->index = 0;
		li->Current = li->L->First;
	}
	else {
		li->index = idx;
		while (idx > 0) {
		    rvp = rvp->Next;
		    idx--;
		}
		li->Current = rvp;
	}
	if (li->L->Flags & CONTAINER_READONLY) {
	    memcpy(li->ElementBuffer, li->Current->Data, li->L->ElementSize);
	    return li->ElementBuffer;
	}
	return li->Current->Data;
}

static void *GetLast(Iterator *it)
{
    struct DListIterator *li = (struct DListIterator *)it;
    if (li == NULL || li->Magic != DLIST_MAGIC_NUMBER) {
        iError.RaiseError("iDlist.GetLast", li == NULL ?
                          CONTAINER_ERROR_BADARG : CONTAINER_ERROR_WRONG_ITERATOR);
        return NULL;
    }
    if (li->timestamp != li->L->timestamp) {
        li->L->RaiseError("iDlist.GetLast", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (li->L->count == 0)
        return NULL;
    li->index = li->L->count - 1;
    li->Current = li->L->Last;
    if (li->L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, li->Current->Data, li->L->ElementSize);
        return li->ElementBuffer;
    }
    return li->Current->Data;
}

static size_t GetPosition(Iterator *it)
{
    struct DListIterator *li = (struct DListIterator *)it;
    if (li == NULL) {
        iError.RaiseError("iDlist.GetPosition", CONTAINER_ERROR_BADARG);
        return (size_t)-1;
    }
    if (li->Magic != DLIST_MAGIC_NUMBER) {
        iError.RaiseError("iDlist.GetPosition", CONTAINER_ERROR_WRONG_ITERATOR);
        return (size_t)-1;
    }
    return li->index;
}

static void doinit(struct DListIterator *result,Dlist *L)
{
    result->it.GetNext = GetNext;
    result->it.GetPrevious = GetPrevious;
    result->it.GetFirst = GetFirst;
    result->it.GetCurrent = GetCurrent;
	result->it.GetLast = GetLast;
    result->it.Seek = Seek;
	result->it.GetPosition = GetPosition;
	result->it.Replace = ReplaceWithIterator;
    result->L = L;
	result->Magic = DLIST_MAGIC_NUMBER;
    result->timestamp = L->timestamp;
    result->index = (size_t)-1;
    result->Current = NULL;
}


static Iterator *NewIterator(Dlist *L)
{
    struct DListIterator *result;

    if (L == NULL) {
        iError.NullPtrError("iDlist.NewIterator");
        return NULL;
    }
    result = L->Allocator->malloc(SizeofIterator(L));
    if (result == NULL) {
        L->RaiseError("iDlist.NewIterator",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    doinit(result,L);
    if (!RegisterDlistIterator(result)) {
        L->Allocator->free(result);
        L->RaiseError("iDlist.NewIterator", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    return &result->it;
}

static int InitIterator(Dlist *L,void *buf)
{
    struct DListIterator *result;

    if (L == NULL || buf == NULL) {
        iError.NullPtrError("iDlist.NewIterator");
	    return CONTAINER_ERROR_BADARG;
    }
    result = buf;
    doinit(result,L);
    return 1;
}


static int Append(Dlist *l1, Dlist *l2)
{
    const ContainerAllocator *source_allocator;
    int source_owned;

    if (l1 == NULL || l2 == NULL)  return iError.NullPtrError("iDlist.Append");
    if (l1 == l2)
        return l1->RaiseError("iDlist.Append", CONTAINER_ERROR_BADARG, l1),
               CONTAINER_ERROR_BADARG;

    if ((l1->Flags & CONTAINER_READONLY) || (l2->Flags & CONTAINER_READONLY)) {
        l1->RaiseError("iDlist.Append",CONTAINER_ERROR_READONLY,l1);
        return CONTAINER_ERROR_READONLY;
    }
    if (l2->ElementSize != l1->ElementSize) {
        l1->RaiseError("iDlist.Append",CONTAINER_ERROR_INCOMPATIBLE);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (l2->count != 0 &&
        (l1->Heap != NULL || l2->Heap != NULL ||
         !DlistTransferCompatible(l1, l2))) {
        l1->RaiseError("iDlist.Append", CONTAINER_ERROR_INCOMPATIBLE, l1, l2);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (l2->count > SIZE_MAX - l1->count) {
        l1->RaiseError("iDlist.Append", CONTAINER_ERROR_NOMEMORY, l1);
        return CONTAINER_ERROR_NOMEMORY;
    }
    source_allocator = l2->Allocator;
    source_owned = DlistHeaderIsOwned(l2);
    if (l1->count == 0) {
        l1->First = l2->First;
        l1->Last = l2->Last;
    }
    else if (l2->count > 0) {
        l1->Last->Next = l2->First;
        l2->First->Previous = l1->Last;
        l1->Last = l2->Last;
    }
    l1->count += l2->count;
    l1->timestamp++;
    if (l1->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l1,CCL_APPEND,l2,NULL);
    if (l2->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l2,CCL_FINALIZE,NULL,NULL);

    l2->First = l2->Last = NULL;
    l2->count = 0;
    l2->FreeList = NULL;
    if (l2->Heap != NULL) {
        iHeap.Finalize(l2->Heap);
        l2->Heap = NULL;
    }
    l2->timestamp++;
    if (l2->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l2, CCL_FINALIZE, NULL, NULL);
    if (source_owned) {
        (void)UnregisterDlistHeader(l2);
        source_allocator->free(l2);
    }
    return 1;
}


static size_t GetElementSize(const Dlist *d)
{
    if (d == NULL) {
        iError.NullPtrError("iDlist.GetElementSize");
        return 0;
    }

    return d->ElementSize;
}
static size_t Sizeof(const Dlist *dl)
{
    size_t node_size;
    if (dl == NULL) {
        return sizeof(Dlist);
    }
    if (DlistNodeSize(dl, &node_size) < 0 ||
        dl->count > (SIZE_MAX - sizeof(Dlist)) / node_size)
        return 0;
    return sizeof(Dlist) + dl->count * node_size;
}

static int DeleteIterator(Iterator *it)
{
    struct DListIterator *li = (struct DListIterator *)it;
    Dlist *L;

    if (it == NULL) return iError.NullPtrError("iDlist.DeleteIterator");

    if (li->Magic != DLIST_MAGIC_NUMBER)
        return CONTAINER_ERROR_WRONG_ITERATOR;
    L = li->L;
    if (UnregisterDlistIterator(li))
        L->Allocator->free(it);
    return 1;
}
static int DefaultSaveFunction(const void *element,void *arg, FILE *Outfile)
{
    const unsigned char *str = element;
    size_t len = *(size_t *)arg;

    return len == fwrite(str,1,len,Outfile);
}

static int Save(const Dlist *L,FILE *stream, SaveFunction saveFn,void *arg)
{
    size_t i;
    DlistElement *rvp;
    size_t elmsiz;
    uint32_t version = DLIST_PERSIST_VERSION;
    uint32_t flags;
    uint64_t serialized_size;
    uint64_t serialized_count;

    if (stream == NULL || L == NULL ) {
        if (L)
                L->RaiseError("iDlist.Save",CONTAINER_ERROR_BADARG);
        else
                iError.NullPtrError("iDlist.Save");
        return CONTAINER_ERROR_BADARG;
    }
    if (saveFn == NULL) {
        saveFn = DefaultSaveFunction;
        /* Copy element size to preserve const semantics */
        elmsiz = L->ElementSize;
        arg = &elmsiz;
    }
    flags = (uint32_t)(L->Flags & CONTAINER_READONLY);
    serialized_size = (uint64_t)L->ElementSize;
    serialized_count = (uint64_t)L->count;
    if (fwrite(&DlistGuid,sizeof(DlistGuid),1,stream) != 1 ||
        fwrite(&version,sizeof(version),1,stream) != 1 ||
        fwrite(&flags,sizeof(flags),1,stream) != 1 ||
        fwrite(&serialized_size,sizeof(serialized_size),1,stream) != 1 ||
        fwrite(&serialized_count,sizeof(serialized_count),1,stream) != 1)
        return EOF;
    rvp = L->First;
    for (i=0; i< L->count; i++) {
	    const char *p;

	    if (rvp == NULL)
	    return EOF;
	    p = rvp->Data;
	    if (saveFn(p,arg,stream) <= 0)
	    return EOF;
        rvp = rvp->Next;
    }
    return 1;
}

static int DefaultLoadFunction(void *element,void *arg, FILE *Infile)
{
    size_t len = *(size_t *)arg;

    return len == fread(element,1,len,Infile);
}

static Dlist *Load(FILE *stream, ReadFunction loadFn,void *arg)
{
    size_t i;
    Dlist *result;
    char *buf;
    guid Guid;
    uint32_t version, flags;
    uint64_t serialized_size, serialized_count;
    size_t element_size, count, node_size;
    int r;

    if (stream == NULL) {
        iError.NullPtrError("iDlist.Load");
        return NULL;
    }
    if (fread(&Guid,sizeof(Guid),1,stream) != 1) {
        iError.RaiseError("iDlist.Load",CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (memcmp(&Guid,&DlistGuid,sizeof(guid)) != 0) {
        iError.RaiseError("iDlist.Load",CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    if (fread(&version,sizeof(version),1,stream) != 1 ||
        fread(&flags,sizeof(flags),1,stream) != 1 ||
        fread(&serialized_size,sizeof(serialized_size),1,stream) != 1 ||
        fread(&serialized_count,sizeof(serialized_count),1,stream) != 1) {
        iError.RaiseError("iDlist.Load", CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (version != DLIST_PERSIST_VERSION || serialized_size == 0 ||
        serialized_size > SIZE_MAX || serialized_count > SIZE_MAX) {
        iError.RaiseError("iDlist.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    element_size = (size_t)serialized_size;
    count = (size_t)serialized_count;
    if (element_size > SIZE_MAX - sizeof(DlistElement) ||
        DlistNodeSize(&(Dlist){ .ElementSize = element_size }, &node_size) < 0 ||
        count > SIZE_MAX / node_size) {
        iError.RaiseError("iDlist.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    if (loadFn == NULL) {
        loadFn = DefaultLoadFunction;
        arg = &element_size;
    }
    buf = malloc(element_size);
    if (buf == NULL) {
        iError.RaiseError("iDlist.Load",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    result = iDlist.Create(element_size);
    if (result == NULL) {
        free(buf);
        return NULL;
    }
	for (i=0; i < count; i++) {
	    r = loadFn(buf,arg,stream);
        if (r <= 0) {
	    iError.RaiseError("iDlist.Load",CONTAINER_ERROR_FILE_READ);
	    result->Flags &= ~CONTAINER_READONLY;
	    (void)Finalize(result);
	    result = NULL;
	    break;
        }
	    r = Add_nd(result, buf);
	    if (r < 0) {
	    result->Flags &= ~CONTAINER_READONLY;
	    (void)Finalize(result);
	    result=NULL;
	    break;
        }
	}
    free(buf);
    if (result != NULL)
        result->Flags = flags & CONTAINER_READONLY;
    return result;
}
static DestructorFunction SetDestructor(Dlist *cb,DestructorFunction fn)
{
    DestructorFunction oldfn;
    if (cb == NULL)
        return NULL;
    oldfn = cb->DestructorFn;
    if (fn)
        cb->DestructorFn = fn;
    return oldfn;
}
static const ContainerAllocator *GetAllocator(const Dlist *l)
{
    if (l == NULL)
        return NULL;
    return l->Allocator;
}

static void *Back(const Dlist *l)
{
    if (l == NULL) {
        iError.NullPtrError("Back");
        return NULL;
    }
    if (0 == l->count) {
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iList.Back",CONTAINER_ERROR_READONLY,l);
        return NULL;
    }
    return l->Last->Data;
}
static void *Front(const Dlist *l)
{
    if (l == NULL) {
        iError.NullPtrError("Front");
        return NULL;
    }
    if (0 == l->count) {
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iList.Front",CONTAINER_ERROR_READONLY,l);
        return NULL;
    }
    return l->First->Data;
}

static size_t SizeofIterator(const Dlist *l)
{
    size_t base = offsetof(struct DListIterator, ElementBuffer);
    if (l == NULL)
        return base + 1;
    if (l->ElementSize > SIZE_MAX - base)
        return 0;
    return base + l->ElementSize;
}

static int RemoveRange(Dlist *l,size_t start, size_t end)
{
    DlistElement *node,*before,*after;
    size_t position, remove_count;

    if (l == NULL) {
        iError.RaiseError("RemoveRange",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags&CONTAINER_READONLY) {
        l->RaiseError("RemoveRange",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    if (end < start) {
        size_t tmp = end;
        end = start;
        start = tmp;
    }
    if (start >= l->count)
        return 0;
    if (end > l->count)
        end = l->count;
    if (start >= end)
        return 0;
    remove_count = end - start;
    node = l->First;
    for (position = 0; node != NULL && position < start; ++position)
        node = node->Next;
    before = node == NULL ? NULL : node->Previous;
    while (node != NULL && position < end) {
        DlistElement *next = node->Next;
        if (l->Flags & CONTAINER_HAS_OBSERVER)
            iObserver.Notify(l, CCL_ERASE_AT, node->Data, (void *)position);
        if (l->DestructorFn != NULL)
            (void)l->DestructorFn(node->Data);
        FreeDlistNode(l, node);
        node = next;
        ++position;
    }
    after = node;
    if (before != NULL)
        before->Next = after;
    else
        l->First = after;
    if (after != NULL)
        after->Previous = before;
    else
        l->Last = before;
    l->count -= remove_count;
    if (l->count == 0)
        l->First = l->Last = NULL;
    l->timestamp++;
    return 1;
}


static int Select(Dlist *src,const Mask *m)
{
    size_t i,kept = 0;
    DlistElement *node,*previous = NULL;

    if (src == NULL || m == NULL) {
        return iError.NullPtrError("iDlist.Select");
    }
    if (src->Flags & CONTAINER_READONLY) {
        iError.RaiseError("Select",CONTAINER_ERROR_READONLY,src);
        return CONTAINER_ERROR_READONLY;
    }
    if (m->length != src->count) {
        iError.RaiseError("Select",CONTAINER_ERROR_BADMASK,src,m);
        return CONTAINER_ERROR_BADMASK;
    }
    if (src->count == 0) return 0;
    node = src->First;
    for (i = 0; node != NULL && i < m->length; ++i) {
        DlistElement *next = node->Next;
        if (m->data[i]) {
            node->Previous = previous;
            if (previous != NULL)
                previous->Next = node;
            else
                src->First = node;
            previous = node;
            src->Last = node;
            ++kept;
        } else {
            if (src->DestructorFn != NULL)
                (void)src->DestructorFn(node->Data);
            FreeDlistNode(src, node);
        }
        node = next;
    }
    if (previous != NULL)
        previous->Next = NULL;
    else
        src->First = src->Last = NULL;
    src->count = kept;
    src->timestamp++;
    return 1;
}

static Dlist *SelectCopy(const Dlist *src,const Mask *m)
{
    Dlist *result;
    DlistElement *rvp;
    size_t i;
    int r;

    if (src == NULL || m == NULL) {
        iError.NullPtrError("iDlist.SelectCopy");
        return NULL;
    }
    if (m->length != src->count) {
        iError.RaiseError("iDlist.SelectCopy",CONTAINER_ERROR_BADMASK,src,m);
        return NULL;
    }
    result = CreateWithAllocator(src->ElementSize, src->Allocator);
    if (result == NULL) return NULL;
    result->Compare = src->Compare;
    result->RaiseError = src->RaiseError;
    result->VTable = src->VTable;
    rvp = src->First;
    for (i=0; i<m->length;i++) {
        if (m->data[i]) {
            r = Add_nd(result,rvp->Data);
            if (r < 0) {
                result->Flags &= ~CONTAINER_READONLY;
                (void)Finalize(result);
                return NULL;
            }
        }
        rvp = rvp->Next;
    }
    result->Flags = src->Flags;
    return result;
}


static DlistElement *FirstElement(Dlist *l)
{
	if (l == NULL) {
		iError.NullPtrError("FirstElement");
		return NULL;
	}
	if (l->Flags&CONTAINER_READONLY) {
		iError.RaiseError("FirstElement",CONTAINER_ERROR_READONLY,l);
		return NULL;
	}
	return l->First;
}

static DlistElement *LastElement(Dlist *l)
{
	if (l == NULL) {
		iError.NullPtrError("FirstElement");
		return NULL;
	}
	if (l->Flags&CONTAINER_READONLY) {
		iError.RaiseError("FirstElement",CONTAINER_ERROR_READONLY,l);
		return NULL;
	}
	return l->Last;
}

static DlistElement *NextElement(DlistElement *le)
{
	if (le == NULL) return NULL;
	return le->Next;
}

static DlistElement *PreviousElement(DlistElement *le)
{
	if (le == NULL) return NULL;
	return le->Previous;
}
static void *ElementData(DlistElement *le)
{
	if (le == NULL) return NULL;
	return le->Data;
}

static void *AdvanceInternal(DlistElement **ple,int goback)
{
	DlistElement *le;
	void *result;

	if (ple == NULL)
		return NULL;
	le = *ple;
	if (le == NULL) return NULL;
	result = le->Data;
	if (goback) le = le->Previous;
	else le = le->Next;
	*ple = le;
	return result;
}

static void *MoveBack(DlistElement **ple)
{
	return AdvanceInternal(ple,1);
}
static void *Advance(DlistElement **ple)
{
	return AdvanceInternal(ple,0);
}

static DlistElement *Skip(DlistElement *le, size_t n)
{
    if (le == NULL) return NULL;
    while (le != NULL && n > 0) {
        le = le->Next;
        n--;
    }
    return le;
}


static int RotateLeft(Dlist * l, size_t n)
{
    DlistElement    *rvp, *oldStart;
    if (l == NULL)
        return iError.NullPtrError("iDlist.RotateLeft");
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.RotateLeft",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    if (l->count < 2 || n == 0)
        return 0;
    n %= l->count;
    if (n == 0)
        return 0;
    rvp = l->First;
    oldStart = rvp;
    while (n > 0) {
        rvp = rvp->Next;
        n--;
    }
    l->First = rvp;
    l->Last->Next = oldStart;
    oldStart->Previous = l->Last;
    l->Last = rvp->Previous;
    l->Last->Next = NULL;
    rvp->Previous = NULL;
    l->timestamp++;
    return 1;
}

static int RotateRight(Dlist * l, size_t n)
{
    DlistElement    *rvp, *oldStart;
    if (l == NULL)
        return iError.NullPtrError("iDlist.RotateRight");
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.RotateLeft",CONTAINER_ERROR_READONLY,l);
        return CONTAINER_ERROR_READONLY;
    }
    if (l->count < 2 || n == 0)
        return 0;
    n %= l->count;
    if (n == 0)
        return 0;
    rvp = l->First;
    oldStart = rvp;
    n = l->count - n;
    while (n > 0) {
        rvp = rvp->Next;
        n--;
    }
    l->First = rvp;
    l->Last->Next = oldStart;
    oldStart->Previous = l->Last;
    l->Last = rvp->Previous;
    l->Last->Next = NULL;
    rvp->Previous = NULL;
    l->timestamp++;
    return 1;
}

static Dlist *SplitAfter(Dlist *l, DlistElement *pt)
{
    DlistElement *pNext;
    Dlist *result;
    size_t count=0;

    if (pt == NULL || l == NULL) {
        iError.NullPtrError("iList.SplitAfter");
        return NULL;
    }
    if (l->Flags&CONTAINER_READONLY) {
        l->RaiseError("iDlist.SplitAfter",CONTAINER_ERROR_READONLY,l);
        return NULL;
    }
    if (!DlistContainsNode(l, pt)) {
        l->RaiseError("iDlist.SplitAfter", CONTAINER_ERROR_WRONGELEMENT, l, pt);
        return NULL;
    }
    if (l->Heap != NULL) {
        l->RaiseError("iDlist.SplitAfter", CONTAINER_ERROR_INCOMPATIBLE, l, pt);
        return NULL;
    }
    pNext = pt->Next;
    if (pNext == NULL) return NULL;
    result = CreateWithAllocator(l->ElementSize, l->Allocator);
    if (result == NULL) return NULL;
    result->Compare = l->Compare;
    result->RaiseError = l->RaiseError;
    result->VTable = l->VTable;
    result->First = pNext;
    result->First->Previous = NULL;
    while (pNext) {
        count++;
        if (pNext->Next == NULL) result->Last = pNext;
        pNext = pNext->Next;
    }
    result->count = count;
    pt->Next = NULL;
    l->Last = pt;
    l->count -= count;
    l->timestamp++;
    return result;
}

static int SetElementData(Dlist *l, DlistElement *le,void *data)
{
    if (l == NULL || le == NULL || data == NULL) {
        return iError.NullPtrError("iList.SetElementData");
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iDlist.SetElementData", CONTAINER_ERROR_READONLY, l);
        return CONTAINER_ERROR_READONLY;
    }
    if (!DlistContainsNode(l, le)) {
        l->RaiseError("iDlist.SetElementData", CONTAINER_ERROR_WRONGELEMENT,
                      l, le);
        return CONTAINER_ERROR_WRONGELEMENT;
    }
    if (data == le->Data)
        return 1;
    if (l->DestructorFn != NULL)
        (void)l->DestructorFn(le->Data);
    memcpy(le->Data,data,l->ElementSize);
    l->timestamp++;
    return 1;
}

DlistInterface iDlist = {
    Size,
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
    GetElementSize,
/* Sequential container */
    Add,
    GetElement,
    PushFront,
    PopFront,
    InsertAt,
    EraseAt,
    ReplaceAt,
    IndexOf,
/* Dlist specific part */
    PushBack,
    PopBack,
    Splice,
    Sort,
    Reverse,
    GetRange,
    Append,
    SetCompareFunction,
    UseHeap,
    AddRange,
    Create,
    CreateWithAllocator,
    Init,
    InitWithAllocator,
    CopyElement,
    InsertIn,
    SetDestructor,
    InitializeWith,
    GetAllocator,
    Back,
    Front,
    RemoveRange,
    RotateLeft,
    RotateRight,
    Select,
    SelectCopy,
    FirstElement,
    LastElement,
    NextElement,
    PreviousElement,
    ElementData,
    SetElementData,
    Advance,
    Skip,
    MoveBack,
    SplitAfter,
};
