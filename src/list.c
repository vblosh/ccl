/*
 * List routines sample implementation 
 * ----------------------------------- 
 * This routines handle the List container class. This is a very general
 * implementation and efficiency considerations aren't yet primordial. Lists
 * can have elements of any size. This implement single linked Lists. The
 * design goals here are just correctness and showing how the implementation
 * of the proposed interface COULD be done.
 * Functions suffixed with XXX_nd are no-debug functions, i.e. they do not check
 * at all their arguments and suppose the checking is already done.
 * ----------------------------------------------------------------------
 */

/* Number of elements by default */
#ifndef DEFAULT_START_SIZE
#define DEFAULT_START_SIZE 20
#endif
#include "containers.h"
#include "ccl_internal.h"
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#ifndef INT_MAX
#define INT_MAX (((unsigned)-1) >> 1)
#endif
static int IndexOf_nd(const List * AL, const void *SearchedElement, 
                      void *ExtraArgs, size_t * result);
static int RemoveAt(List * AL, size_t idx);
static int RemoveRange(List * AL, size_t start, size_t end);
#define CHUNK_SIZE    1000
static const guid ListGuid = {0x672abd64, 0xe231, 0x486b,
    {0xbc, 0x72, 0x9b, 0x3a, 0x88, 0x20, 0x10, 0x35}
};
#define LIST_PERSIST_VERSION UINT32_C(1)

/* Keep persistence independent of the in-memory List ABI (which contains
 * pointers, callbacks and allocator addresses).  Fields are written one at a
 * time below, so struct padding and host pointer size do not enter the file. */

/* Placement-initialized List headers are caller-owned while Create headers
 * are allocator-owned.  List predates an ownership field in its public
 * layout (which is shared by generated lists), so keep a small side registry
 * for headers created here.  This also makes Finalize safe for placement
 * headers without changing the ABI. */
typedef struct ListHeaderOwner ListHeaderOwner;
struct ListHeaderOwner {
    List *list;
    ListHeaderOwner *next;
};
static ListHeaderOwner *ListOwners;

static int RegisterListHeader(List *list)
{
    ListHeaderOwner *owner = (ListHeaderOwner *)malloc(sizeof(*owner));
    if (owner == NULL)
        return 0;
    owner->list = list;
    owner->next = ListOwners;
    ListOwners = owner;
    return 1;
}

static int UnregisterListHeader(List *list)
{
    ListHeaderOwner **p = &ListOwners;
    while (*p) {
        if ((*p)->list == list) {
            ListHeaderOwner *owner = *p;
            *p = owner->next;
            free(owner);
            return 1;
        }
        p = &(*p)->next;
    }
    return 0;
}

static int IsRegisteredListHeader(const List *list)
{
    const ListHeaderOwner *owner = ListOwners;
    while (owner) {
        if (owner->list == list)
            return 1;
        owner = owner->next;
    }
    return 0;
}

static int ListNodeSize(const List *list, size_t *size)
{
    if (list == NULL || size == NULL)
        return CONTAINER_ERROR_BADARG;
    if (list->ElementSize > SIZE_MAX - offsetof(ListElement, Data))
        return CONTAINER_ERROR_NOMEMORY;
    *size = offsetof(ListElement, Data) + list->ElementSize;
    return 1;
}

static void FreeListNode(List *list, ListElement *node)
{
    if (list->Heap)
        (void)iHeap.FreeObject(list->Heap, node);
    else
        list->Allocator->free(node);
}

static void DestroyListNode(List *list, ListElement *node)
{
    if (list->DestructorFn)
        (void)list->DestructorFn(node->Data);
    FreeListNode(list, node);
}

static void ClearListNodes(List *list)
{
    ListElement *node = list->First;
    while (node) {
        ListElement *next = node->Next;
        DestroyListNode(list, node);
        node = next;
    }
    if (list->Heap) {
        iHeap.Finalize(list->Heap);
        list->Heap = NULL;
    }
    list->First = NULL;
    list->Last = NULL;
    list->count = 0;
}

static int ListContainsNode(const List *list, const ListElement *needle)
{
    const ListElement *node;
    size_t i;
    if (list == NULL || needle == NULL)
        return 0;
    node = list->First;
    for (i = 0; node && i < list->count; ++i, node = node->Next)
        if (node == needle)
            return 1;
    return 0;
}

static int ErrorReadOnly(const List * L,const char *fnName)
{
    char            buf[512];

    snprintf(buf, sizeof(buf), "iList.%s", fnName);
    L->RaiseError(buf, CONTAINER_ERROR_READONLY);
    return CONTAINER_ERROR_READONLY;
}

static int NullPtrError(const char *fnName)
{
    char            buf[512];

    snprintf(buf, sizeof(buf), "iList.%s", fnName);
    return iError.NullPtrError(buf);
}

/*------------------------------------------------------------------------
 Procedure:     NewLink ID:1
 Purpose:       Allocation of a new list element. 
            Note that we allocate the size of a list element
            plus the size of the data in a single
            block. This block should be passed to the FREE
            function.

 Input:         The list where the new element should be added and a
            pointer to the data that will be added (can be
            NULL).
 Output:        A pointer to the new list element (can be NULL)
 Errors:        If there is no memory returns NULL
------------------------------------------------------------------------*/
static ListElement * NewLink(List * li, const void *data, const char *fname)
{
    ListElement    *result;
    size_t          nodeSize;

    if (ListNodeSize(li, &nodeSize) < 0) {
        li->RaiseError(fname, CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if (li->Heap == NULL)
        result = (ListElement *)li->Allocator->malloc(nodeSize);
    else
        result = (ListElement *)iHeap.NewObject(li->Heap);
    if (result == NULL) {
        li->RaiseError(fname, CONTAINER_ERROR_NOMEMORY);
    } else {
        result->Next = NULL;
        memcpy(&result->Data, data, li->ElementSize);
    }
    return result;
}
static int DefaultListCompareFunction(const void *left, const void *right, CompareInfo * ExtraArgs)
{
    size_t          siz = ((List *) ExtraArgs->ContainerLeft)->ElementSize;
    return memcmp(left, right, siz);
}

/*------------------------------------------------------------------------
 Procedure:     Contains ID:1
 Purpose:       Determines if the given data is in the container
 Input:         The list and the data to be searched
 Output:        Returns 1 (true) if the data is in there, false
                otherwise
 Errors:        The same as the function IndexOf
------------------------------------------------------------------------*/
static int Contains(const List * l, const void *data)
{
    size_t          idx;
    if (l == NULL || data == NULL) {
        if (l)
            l->RaiseError("iList.Contains", CONTAINER_ERROR_BADARG);
        else
            iError.NullPtrError("iList.Contains");
        return CONTAINER_ERROR_BADARG;
    }
    return (IndexOf_nd(l, data, NULL, &idx) < 0) ? 0 : 1;
}

/*------------------------------------------------------------------------
 Procedure:     Clear ID:1
 Purpose:       Reclaims all memory used by a list. The list header
                itself is not reclaimed but zeroed. Note that the
                list must be writable.
 Input:         The list to be cleared
 Output:        Returns the number of elemnts that were in the list
                if OK, CONTAINER_ERROR_READONLY otherwise.
 Errors:        None
------------------------------------------------------------------------*/
static int Clear_nd(List * l)
{
    unsigned oldFlags = l->Flags;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_CLEAR, NULL, NULL);
    ClearListNodes(l);
    /* Configuration (observer/read-only state, callbacks, allocator) is not
     * element storage and must survive a Clear.  Advance, rather than reset,
     * the generation so existing iterators cannot become valid again. */
    l->Flags = oldFlags;
    l->timestamp++;
    return 1;
}

static int Clear(List * l)
{
    if (l == NULL) {
        return NullPtrError("Clear");
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l,"Clear");
    }
    return Clear_nd(l);
}


/*------------------------------------------------------------------------
 Procedure:     Add ID:1
 Purpose:       Adds an element to the list
 Input:         The list and the element to be added
 Output:        Returns the number of elements in the list or a
                negative error code if an error occurs.
 Errors:        The element to be added can't be NULL, and the list
                must be writable.
------------------------------------------------------------------------*/
static int Add_nd(List * l, const void *elem)
{
    ListElement    *newl;

    newl = NewLink(l, elem, "iList.Add");
    if (newl == 0)
        return CONTAINER_ERROR_NOMEMORY;
    if (l->count == 0) {
        l->First = newl;
    } else {
        l->Last->Next = newl;
    }
    l->Last = newl;
    l->timestamp++;
    ++l->count;
    return 1;
}

static int Add(List * l, const void *elem)
{
    int             r;
    if (l == NULL || elem == NULL)
        return NullPtrError("Add");
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l, "Add");
    r = Add_nd(l, elem);
    if (r > 0 && (l->Flags & CONTAINER_HAS_OBSERVER))
        iObserver.Notify(l, CCL_ADD, elem, NULL);
    return r;
}


/*------------------------------------------------------------------------
 Procedure:     SetReadOnly ID:1
 Purpose:       Sets/Unsets the read only flag.
 Input:         The list and the new value
 Output:        The old value of the flag
 Errors:        None
------------------------------------------------------------------------*/
static unsigned SetFlags(List * l, unsigned newval)
{
    unsigned        result=0;

    if (l == NULL) {
        NullPtrError("SetFlags");
    }
    else {
        result = l->Flags;
        l->Flags = newval;
    }
    return result;
}

/*------------------------------------------------------------------------
 Procedure:     GetFlags ID:1
 Purpose:       Queries the read only flag
 Input:         The list
 Output:        The state of the flag
 Errors:        None
------------------------------------------------------------------------*/
static unsigned GetFlags(const List * l)
{
    if (l == NULL) {
        NullPtrError("GetFlags");
        return 0;
    }
    return l->Flags;
}

/*------------------------------------------------------------------------
 Procedure:     Size ID:1
 Purpose:       Returns the number of elements in the list
 Input:         The list
 Output:        The number of elements
 Errors:        None
------------------------------------------------------------------------*/
static size_t Size(const List * l)
{
    if (l == NULL) {
        (void)NullPtrError("Size");
        return 0;
    }
    return l->count;
}

/*------------------------------------------------------------------------
 Procedure:     SetCompareFunction ID:1
 Purpose:       Defines the function to be used in comparison of
                list elements
 Input:         The list and the new comparison function. If the new
                comparison function is NULL no changes are done.
 Output:        Returns the old function
 Errors:        None
------------------------------------------------------------------------*/
static CompareFunction SetCompareFunction(List * l, CompareFunction fn)
{
    CompareFunction oldfn;

    if (l == NULL) {
        NullPtrError("SetCompareFunction");
        return NULL;
    }
    oldfn = l->Compare;
    if (fn != NULL) {    /* Treat NULL as an enquiry to get the
                          * compare function */
        if (l->Flags & CONTAINER_READONLY) {
            ErrorReadOnly(l,"SetCompareFunction");
        } else
            l->Compare = fn;
    }
    return oldfn;
}

/*------------------------------------------------------------------------
 Procedure:     Copy ID:1
 Purpose:       Copies a list. The result is fully allocated (list
                elements and data).
 Input:         The list to be copied
 Output:        A pointer to the new list
 Errors:        None. Returns NULL if therfe is no memory left.
------------------------------------------------------------------------*/
static List    *Copy(const List * l)
{
    List           *result;
    ListElement    *elem, *newElem;

    if (l == NULL) {
        NullPtrError("Copy");
        return NULL;
    }
    result = iList.CreateWithAllocator(l->ElementSize, l->Allocator);
    if (result == NULL) {
        l->RaiseError("iList.Copy", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    /* Byte copies are not clones of pointer-owned values; do not propagate a
     * destructor or heap policy.  Observer registrations belong to the
     * source object, not the result. */
    result->Flags = l->Flags & ~(CONTAINER_HAS_OBSERVER | CONTAINER_READONLY);
    result->VTable = l->VTable;    /* Copy possibly subclassed methods */
    result->Compare = l->Compare;    /* Copy compare function */
    result->RaiseError = l->RaiseError;
    elem = l->First;
    while (elem) {
        newElem = NewLink(result, elem->Data, "iList.Copy");
        if (newElem == NULL) {
            l->RaiseError("iList.Copy", CONTAINER_ERROR_NOMEMORY);
            ClearListNodes(result);
            (void)UnregisterListHeader(result);
            result->Allocator->free(result);
            return NULL;
        }
        if (elem == l->First) {
            result->First = newElem;
            result->count++;
        } else {
            result->Last->Next = newElem;
            result->count++;
        }
        result->Last = newElem;
        elem = elem->Next;
    }
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_COPY, result, NULL);

    return result;
}
/*------------------------------------------------------------------------
 Procedure:     Finalize ID:1
 Purpose:       Reclaims all memory for a list. The data, list
                elements and the list header are reclaimed.
 Input:         The list to be destroyed. It should NOT be read-
                only.
 Output:        Returns the old count or a negative value if an
                error occurs (list not writable)
 Errors:        Needs a writable list
------------------------------------------------------------------------*/
static int Finalize(List * l)
{
    int             t = 0;
    unsigned        Flags = 0;

    if (l)
        Flags = l->Flags;
    else
        return CONTAINER_ERROR_BADARG;
    /* Finalization must always release owned storage, even for a read-only
     * view.  Read-only blocks mutation APIs; it must not turn ownership into
     * a leak. */
    t = (l->Flags & CONTAINER_READONLY) ? Clear_nd(l) : Clear(l);
    if (t < 0)
        return t;
    if (Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_FINALIZE, NULL, NULL);
    /* Generated interfaces are static objects.  Only headers allocated by
     * Create are released; placement headers are caller-owned. */
    if (UnregisterListHeader(l))
        l->Allocator->free(l);
    return 1;
}

/*------------------------------------------------------------------------
 Procedure:     GetElement ID:1
 Purpose:       Returns the data associated with a given position
 Input:         The list and the position
 Output:        A pointer to the data
 Errors:        NULL if error in the positgion index
------------------------------------------------------------------------*/
static void    *GetElement(const List * l, size_t position)
{
    ListElement    *rvp;

    if (l == NULL) {
        NullPtrError("GetElement");
        return NULL;
    }
    if (position >= l->count) {
        l->RaiseError("GetElement", CONTAINER_ERROR_INDEX,l,position);
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        ErrorReadOnly(l,"GetElement");
        return NULL;
    }
    rvp = l->First;
    while (position) {
        rvp = rvp->Next;
        position--;
    }
    return rvp->Data;
}

static void    *Back(const List * l)
{
    if (l == NULL) {
        NullPtrError("Back");
        return NULL;
    }
    if (0 == l->count) {
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        ErrorReadOnly(l,"Back");
        return NULL;
    }
    return l->Last->Data;
}

static void    *Front(const List * l)
{
    if (l == NULL) {
        NullPtrError("Front");
        return NULL;
    }
    if (0 == l->count) {
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        ErrorReadOnly(l,"Front");
        return NULL;
    }
    return l->First->Data;
}

/*------------------------------------------------------------------------
 Procedure:     CopyElement ID:1
 Purpose:       Returns the data associated with a given position
 Input:         The list and the position
 Output:        A pointer to the data
 Errors:        NULL if error in the positgion index
 ------------------------------------------------------------------------*/
static int CopyElement(const List * l, size_t position, void *outBuffer)
{
    ListElement    *rvp;

    if (l == NULL || outBuffer == NULL) {
        if (l)
            l->RaiseError("iList.CopyElement", CONTAINER_ERROR_BADARG);
        else
            NullPtrError("CopyElement");
        return CONTAINER_ERROR_BADARG;
    }
    if (position >= l->count) {
        l->RaiseError("iList.CopyElement", CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    rvp = l->First;
    while (position) {
        rvp = rvp->Next;
        position--;
    }
    memcpy(outBuffer, rvp->Data, l->ElementSize);
    return 1;
}

static int ReplaceAt(List * l, size_t position, const void *data)
{
    ListElement    *rvp;
    size_t          originalPosition = position;

    /* Error checking */
    if (l == NULL || data == NULL) {
        if (l)
            l->RaiseError("iList.ReplaceAt", CONTAINER_ERROR_BADARG);
        else
            NullPtrError("ReplaceAt");
        return CONTAINER_ERROR_BADARG;
    }
    if (position >= l->count) {
        l->RaiseError("iList.ReplaceAt", CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l,"ReplaceAt");
    }
    /* Position at the right data item */
    if (position == l->count - 1)
        rvp = l->Last;
    else {
        rvp = l->First;
        while (position && rvp) {
            rvp = rvp->Next;
            position--;
        }
    }
    if (rvp == NULL) {
        iError.RaiseError("iList.ReplaceAt",CONTAINER_INTERNAL_ERROR);
        return CONTAINER_INTERNAL_ERROR;
    }
    if (data == rvp->Data)
        return 1;
    if (l->DestructorFn)
        (void)l->DestructorFn(rvp->Data);

    /* Replace the data there */
    memcpy(&rvp->Data, data, l->ElementSize);
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_REPLACEAT, rvp, (void *)originalPosition);
    return 1;
}


/*------------------------------------------------------------------------
 Procedure:     GetRange ID:1
 Purpose:       Gets a consecutive sub sequence of the list within
                two indexes.
 Input:         The list, the subsequence start and end
 Output:        A new freshly allocated list. If the source list is
                empty or the range is empty, the result list is
                empty too.
 Errors:        None
------------------------------------------------------------------------*/
static List    *GetRange(const List * l, size_t start, size_t end)
{
    size_t          counter;
    List           *result;
    ListElement    *rvp;;

    if (l == NULL) {
        NullPtrError("GetRange");
        return NULL;
    }
    result = iList.CreateWithAllocator(l->ElementSize, l->Allocator);
    if (result == NULL)
        return NULL;
    result->VTable = l->VTable;
    result->Compare = l->Compare;
    result->RaiseError = l->RaiseError;
    if (l->count == 0)
        return result;
    if (end >= l->count)
        end = l->count;
    if (start >= end || start > l->count) {
        (void)Finalize(result);
        return NULL;
    }
    if (start == l->count - 1)
        rvp = l->Last;
    else {
        rvp = l->First;
        counter = 0;
        while (counter < start) {
            rvp = rvp->Next;
            counter++;
        }
    }
    while (start < end && rvp != NULL) {
        int             r = Add_nd(result, &rvp->Data);
        if (r < 0) {
            ClearListNodes(result);
            (void)UnregisterListHeader(result);
            result->Allocator->free(result);
            result = NULL;
            break;
        }
        rvp = rvp->Next;
        start++;
    }
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
static int Equal(const List * l1, const List * l2)
{
    ListElement    *link1, *link2;
    CompareFunction fn;
    CompareInfo     ci;

    if (l1 == l2)
        return 1;
    if (l1 == NULL || l2 == NULL)
        return 0;
    if (l1->count != l2->count)
        return 0;
    if (l1->ElementSize != l2->ElementSize)
        return 0;
    if (l1->Compare != l2->Compare)
        return 0;
    if (l1->count == 0)
        return 1;
    fn = l1->Compare;
    link1 = l1->First;
    link2 = l2->First;
    ci.ContainerLeft = l1;
    ci.ContainerRight = l2;
    ci.ExtraArgs = NULL;
    while (link1 && link2) {
        if (fn(link1->Data, link2->Data, &ci))
            return 0;
        link1 = link1->Next;
        link2 = link2->Next;
    }
    if (link1 || link2)
        return 0;
    return 1;
}


/*------------------------------------------------------------------------
 Procedure:     PushFront ID:1
 Purpose:       Inserts an element at the start of the list
 Input:         The list and the data to insert
 Output:        The count of the list after insertion, or CONTAINER_ERROR_READONLY
                if the list is not writable or CONTAINER_ERROR_NOMEMORY if there is
                no more memory left.
 Errors:        None
------------------------------------------------------------------------*/
static int PushFront(List * l, const void *pdata)
{
    ListElement    *rvp;

    if (l == NULL || pdata == NULL) {
        if (l)
            l->RaiseError("iList.PushFront", CONTAINER_ERROR_BADARG);
        else
            NullPtrError("PushFront");
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l, "PushFront");
    }
    rvp = NewLink(l, pdata, "Insert");
    if (rvp == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    rvp->Next = l->First;
    l->First = rvp;
    if (l->Last == NULL)
        l->Last = rvp;
    l->count++;
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_PUSH, pdata, NULL);

    return 1;
}


/*------------------------------------------------------------------------
 Procedure:     Pop ID:1
 Purpose:       Takes out the first element of a list.
 Input:         The list
 Output:        The first element or NULL if there is none or the
                list is read only
 Errors:        None
------------------------------------------------------------------------*/
static int PopFront(List * l, void *result)
{
    ListElement    *le;

    if (l == NULL) {
        iError.RaiseError("iList.PopFront", CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l, "PopFront");
    }
    if (l->count == 0)
        return 0;
    le = l->First;
    if (l->count == 1) {
        l->First = l->Last = NULL;
    } else
        l->First = l->First->Next;
    l->count--;
    if (result)
        memcpy(result, &le->Data, l->ElementSize);
    /* Supplying result transfers ownership of the element to the caller. */
    if (l->DestructorFn && result == NULL)
        (void)l->DestructorFn(le->Data);
    FreeListNode(l, le);
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_POP, result, NULL);

    return 1;
}


static int InsertIn(List * l, size_t idx, List * newData)
{
    ListElement *insertedFirst = NULL, *insertedLast = NULL;
    ListElement *source;
    size_t newCount, originalCount;

    if (l == NULL || newData == NULL) {
        if (l)
            l->RaiseError("iList.InsertIn", CONTAINER_ERROR_BADARG);
        else
            NullPtrError("InsertIn");
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l, "InsertIn");
    }
    if (l == newData) {
        l->RaiseError("iList.InsertIn", CONTAINER_ERROR_INCOMPATIBLE, l, newData);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (idx > l->count) {
        l->RaiseError("iList.InsertIn", CONTAINER_ERROR_INDEX,l,idx);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->ElementSize != newData->ElementSize) {
        l->RaiseError("iList.InsertIn", CONTAINER_ERROR_INCOMPATIBLE,l,newData);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (l->DestructorFn || newData->DestructorFn) {
        l->RaiseError("iList.InsertIn", CONTAINER_ERROR_INCOMPATIBLE, l, newData);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (newData->count == 0)
        return 1;
    if (newData->count > SIZE_MAX - l->count) {
        l->RaiseError("iList.InsertIn", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    newCount = l->count + newData->count;
    originalCount = l->count;

    /* Copy values into nodes owned by the destination.  This deliberately
     * avoids adopting nodes from a different allocator/heap (and keeps the
     * source list valid after the operation). */
    for (source = newData->First; source; source = source->Next) {
        ListElement *node = NewLink(l, source->Data, "iList.InsertIn");
        if (node == NULL) {
            while (insertedFirst) {
                ListElement *next = insertedFirst->Next;
                FreeListNode(l, insertedFirst);
                insertedFirst = next;
            }
            l->RaiseError("iList.InsertIn", CONTAINER_ERROR_NOMEMORY);
            return CONTAINER_ERROR_NOMEMORY;
        }
        if (insertedLast)
            insertedLast->Next = node;
        else
            insertedFirst = node;
        insertedLast = node;
    }
    if (idx == 0) {
        insertedLast->Next = l->First;
        l->First = insertedFirst;
        if (originalCount == 0)
            l->Last = insertedLast;
    } else if (idx == originalCount) {
        if (l->Last)
            l->Last->Next = insertedFirst;
        else
            l->First = insertedFirst;
        l->Last = insertedLast;
    } else {
        ListElement *before = l->First;
        size_t i;
        for (i = 1; i < idx; ++i)
            before = before->Next;
        insertedLast->Next = before->Next;
        before->Next = insertedFirst;
    }
    l->timestamp++;
    l->count = newCount;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_INSERT_IN, newData, NULL);

    return 1;
}

static int InsertAt(List * l, size_t pos, const void *pdata)
{
    ListElement    *elem;
    size_t          originalPos = pos;
    if (l == NULL || pdata == NULL) {
        if (l)
            l->RaiseError("iList.InsertAt", CONTAINER_ERROR_BADARG);
        else
            NullPtrError("InsertAt");
        return CONTAINER_ERROR_BADARG;
    }
    if (pos > l->count) {
        l->RaiseError("iList.InsertAt", CONTAINER_ERROR_INDEX,l,pos);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l, "InsertAt");
    }
    if (pos == l->count) {
        return Add_nd(l, pdata);
    }
    elem = NewLink(l, pdata, "iList. InsertAt");
    if (elem == NULL) {
        l->RaiseError("iList.InsertAt", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    if (pos == 0) {
        elem->Next = l->First;
        l->First = elem;
    } else {
        ListElement    *rvp = l->First;
        while (--pos > 0) {
            rvp = rvp->Next;
        }
        elem->Next = rvp->Next;
        rvp->Next = elem;
    }
    l->count++;
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_INSERT_AT, pdata, (void *) originalPos);

    return 1;
}

static int EraseInternal(List * l, const void *elem, int all)
{
    size_t          position;
    ListElement    *rvp, *previous;
    int             r;
    int             result = CONTAINER_ERROR_NOTFOUND;
    CompareFunction fn;
    CompareInfo     ci;

    if (l == NULL) {
        return NullPtrError("Erase");
    }
    if (elem == NULL) {
        l->RaiseError("iList.Erase", CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l, "Erase");
    if (l->count == 0) {
        return CONTAINER_ERROR_NOTFOUND;
    }
    position = 0;
    rvp = l->First;
    previous = NULL;
    fn = l->Compare;
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    while (rvp) {
        ListElement *next = rvp->Next;
        r = fn(&rvp->Data, elem, &ci);
        if (r == 0) {
            if (l->Flags & CONTAINER_HAS_OBSERVER)
                iObserver.Notify(l, CCL_ERASE_AT, rvp, (void *) position);

            if (previous == NULL) {
                l->First = next;
            } else if (rvp == l->Last) {
                previous->Next = NULL;
                l->Last = previous;
            } else {
                previous->Next = next;
            }

            DestroyListNode(l, rvp);
            l->count--;
            l->timestamp++;
            if (all == 0)
                return 1;
            result = 1;
        } else {
            previous = rvp;
            ++position;
        }
        rvp = next;
        if (l->count == 0)
            l->First = l->Last = NULL;
        position++;
    }
    return result;
}

static int Erase(List * l, const void *elem)
{
    return EraseInternal(l, elem, 0);
}

static int EraseAll(List * l, const void *elem)
{
    return EraseInternal(l, elem, 1);
}

static int EraseRange(List * l, size_t start, size_t end)
{
    return RemoveRange(l, start, end);
}

static int RemoveAt(List * l, size_t position)
{
    ListElement    *rvp, *last, *removed;
    size_t          originalPosition = position;

	/* Error handling: l should not be NULL, and position should be within the bounds
	   of the list. Besides that, the list should not be read only of course. */
    if (l == NULL) {
        return NullPtrError("RemoveAt");
    }
    if (position >= l->count) {
        l->RaiseError("iListRemoveAt", CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l, "RemoveAt");
    }
    rvp = l->First;
	/* Handle first special cases: removing the first or the last element */
    if (position == 0) {
        removed = l->First;
        if (l->count == 1) {
            l->First = l->Last = NULL;
        } else {
            l->First = l->First->Next;
        }
    } else if (position == l->count - 1) {
        while (rvp->Next != l->Last)
            rvp = rvp->Next;
        removed = rvp->Next;
        rvp->Next = NULL;
        l->Last = rvp;
    } else { /* Normal case */
        last = rvp;
        while (position > 0) {
            last = rvp;
            rvp = rvp->Next;
            position--;
        }
        removed = rvp;
        last->Next = rvp->Next;
    }
    if (l->Flags & CONTAINER_HAS_OBSERVER) /* Notify observer if needed */
        iObserver.Notify(l, CCL_ERASE_AT, removed, (void *) originalPosition);
    DestroyListNode(l, removed);
    l->timestamp++; /* List has been modified */
    --l->count; /* One element less */
    return 1;
}


static int Append(List * l1, List * l2)
{
    ListElement *source;
    ListElement *oldLast;
    ListElement *oldFirst;
    size_t oldCount;
    unsigned oldTimestamp;

    if (l1 == NULL || l2 == NULL) {
        if (l1)
            l1->RaiseError("iList.Append", CONTAINER_ERROR_BADARG);
        else
            iError.RaiseError("iList.Append", CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if ((l1->Flags & CONTAINER_READONLY) || (l2->Flags & CONTAINER_READONLY)) {
        return ErrorReadOnly(l1,"Append");
    }
    if (l1 == l2) {
        l1->RaiseError("iList.Append", CONTAINER_ERROR_INCOMPATIBLE, l1, l2);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (l2->ElementSize != l1->ElementSize) {
        l1->RaiseError("iList.Append", CONTAINER_ERROR_INCOMPATIBLE,l1,l2);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (l1->DestructorFn || l2->DestructorFn) {
        l1->RaiseError("iList.Append", CONTAINER_ERROR_INCOMPATIBLE, l1, l2);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (l2->count > SIZE_MAX - l1->count) {
        l1->RaiseError("iList.Append", CONTAINER_ERROR_NOMEMORY, l1, l2);
        return CONTAINER_ERROR_NOMEMORY;
    }

    /* Append by value, so nodes remain owned by the allocator/heap that
     * created them and l2 remains a usable source list. */
    oldFirst = l1->First;
    oldLast = l1->Last;
    oldCount = l1->count;
    oldTimestamp = l1->timestamp;
    for (source = l2->First; source; source = source->Next) {
        if (Add_nd(l1, source->Data) < 0) {
            ListElement *node = oldLast ? oldLast->Next : l1->First;
            while (node) {
                ListElement *next = node->Next;
                FreeListNode(l1, node);
                node = next;
            }
            l1->First = oldFirst;
            l1->Last = oldLast;
            l1->count = oldCount;
            l1->timestamp = oldTimestamp;
            if (oldLast)
                oldLast->Next = NULL;
            return CONTAINER_ERROR_NOMEMORY;
        }
    }
    if (l2->count)
        l1->timestamp = oldTimestamp + 1;
    if (l1->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l1, CCL_APPEND, l2, NULL);
    return 1;
}

static int Reverse(List * l)
{
    ListElement    *Previous, *current, *old;

    if (l == NULL) {
        iError.RaiseError("iList.Reverse", CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iList.Reverse", CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
	/* If the list has one or zero elements there is nothing to do */
    if (l->count < 2)
        return 1;
    old = l->First;
    l->Last = l->First;
    Previous = NULL;
    while (old) {
        current = old;
        old = old->Next;
        current->Next = Previous;
        Previous = current;
    }
    l->First = Previous;
    if (l->Last)
        l->Last->Next = NULL;
    l->timestamp++; /* the list has been modified */
    return 1;
}


static int AddRange(List * AL, size_t n, const void *data)
{
    const unsigned char *p;
    ListElement    *oldLast, *oldFirst;
    size_t oldCount, original_n;
    unsigned oldTimestamp;

    if (AL == NULL)
        return NullPtrError("AddRange");

    if (AL->Flags & CONTAINER_READONLY) {
        AL->RaiseError("iList.AddRange", CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    if (data == NULL) {
        AL->RaiseError("iList.AddRange", CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    original_n = n;
    oldFirst = AL->First;
    p = (const unsigned char *)data;
    oldLast = AL->Last;
    oldCount = AL->count;
    oldTimestamp = AL->timestamp;
    while (n > 0) {
        int             r = Add_nd(AL, p);
        if (r < 0) {
            ListElement *removed = oldLast ? oldLast->Next : oldFirst;
            while (removed) {
                ListElement *tmp = removed->Next;
                DestroyListNode(AL, removed);
                removed = tmp;
            }
            AL->First = oldFirst;
            AL->Last = oldLast;
            AL->count = oldCount;
            AL->timestamp = oldTimestamp;
            if (oldLast)
                oldLast->Next = NULL;
            return r;
        }
        p += AL->ElementSize;
        n--;
    }
    if (original_n != 0)
        ++AL->timestamp;
    if (AL->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(AL, CCL_ADDRANGE, data, (void *) original_n);

    return 1;
}


/*
 * Searches a List for a given data item Returns a positive integer if found,
 * negative if the end is reached
 */
static int IndexOf_nd(const List * l, const void *ElementToFind, void *ExtraArgs, size_t * result)
{
    ListElement    *rvp;
    int             r;
    size_t          i = 0;
    CompareFunction fn;
    CompareInfo     ci;

    rvp = l->First;
    fn = l->Compare;
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = ExtraArgs;
    while (rvp) {
        r = fn(&rvp->Data, ElementToFind, &ci);
        if (r == 0) {
            if (result)
                *result = i;
            return 1;
        }
        rvp = rvp->Next;
        i++;
    }
    return CONTAINER_ERROR_NOTFOUND;
}

static int IndexOf(const List * l, const void *ElementToFind, void *ExtraArgs, size_t * result)
{
    if (l == NULL || ElementToFind == NULL) {
        if (l)
            l->RaiseError("iList.IndexOf", CONTAINER_ERROR_BADARG);
        else
            NullPtrError("IndexOf");
        return CONTAINER_ERROR_BADARG;
    }
    return IndexOf_nd(l, ElementToFind, ExtraArgs, result);
}

static int lcompar(const void *elem1, const void *elem2, CompareInfo * ExtraArgs)
{
    ListElement    *Elem1 = *(ListElement **) elem1;
    ListElement    *Elem2 = *(ListElement **) elem2;
    List           *l = (List *) ExtraArgs->ContainerLeft;
    CompareFunction fn = l->Compare;
    return fn(Elem1->Data, Elem2->Data, ExtraArgs);
}

static int Sort(List * l)
{
    ListElement   **tab;
    size_t          i;
    ListElement    *rvp;
    CompareInfo     ci;

    if (l == NULL)
        return NullPtrError("Sort");

    if (l->count < 2)
        return 1;
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iList.Sort", CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    if (l->count > SIZE_MAX / sizeof(ListElement *)) {
        l->RaiseError("iList.Sort", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    tab = (ListElement **)l->Allocator->malloc(l->count * sizeof(ListElement *));
    if (tab == NULL) {
        l->RaiseError("iList.Sort", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    rvp = l->First;
    for (i = 0; i < l->count; i++) {
        tab[i] = rvp;
        rvp = rvp->Next;
    }
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    qsortEx(tab, l->count, sizeof(ListElement *), lcompar, &ci);
    for (i = 0; i < l->count - 1; i++) {
        tab[i]->Next = tab[i + 1];
    }
    tab[l->count - 1]->Next = NULL;
    l->Last = tab[l->count - 1];
    l->First = tab[0];
    l->Allocator->free(tab);
    ++l->timestamp;
    return 1;

}
static int      Apply(List * L, int (Applyfn) (void *, void *), void *arg) {
    ListElement    *le;
    void           *pElem = NULL;

    if (L == NULL || Applyfn == NULL) {
        if (L)
            L->RaiseError("iList.Apply", CONTAINER_ERROR_BADARG);
        else
            iError.RaiseError("iList.Apply", CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    le = L->First;
    if (L->Flags & CONTAINER_READONLY) {
        pElem = L->Allocator->malloc(L->ElementSize);
        if (pElem == NULL) {
            L->RaiseError("iList.Apply", CONTAINER_ERROR_NOMEMORY);
            return CONTAINER_ERROR_NOMEMORY;
        }
    }
    while (le) {
        if (pElem) {
            memcpy(pElem, le->Data, L->ElementSize);
            (void)Applyfn(pElem, arg);
        } else
            (void)Applyfn(le->Data, arg);
        le = le->Next;
    }
    if (pElem)
        L->Allocator->free(pElem);
    if (!(L->Flags & CONTAINER_READONLY) && L->count)
        ++L->timestamp;
    return 1;
}

static ErrorFunction SetErrorFunction(List * l, ErrorFunction fn)
{
    ErrorFunction   old;
    if (l == NULL) {
        return iError.RaiseError;
    }
    old = l->RaiseError;
    if (fn)
        l->RaiseError = fn;
    return old;
}

static size_t Sizeof(const List * l)
{
    size_t nodeSize;
    if (l == NULL) {
        return sizeof(List);
    }
    if (ListNodeSize(l, &nodeSize) < 0 ||
        l->count > (SIZE_MAX - sizeof(List)) / nodeSize)
        return 0;
    return sizeof(List) + nodeSize * l->count;
}

static size_t SizeofIterator(const List * l)
{
    if (l == NULL)
        return sizeof(struct ListIterator);
    if (l->ElementSize > SIZE_MAX - offsetof(struct ListIterator, ElementBuffer))
        return 0;
    return offsetof(struct ListIterator, ElementBuffer) + l->ElementSize;
}

static int UseHeap(List * L, const ContainerAllocator * m)
{
    size_t nodeSize;
    if (L == NULL) {
        return NullPtrError("UseHeap");
    }
    if (L->Heap || L->count) {
        L->RaiseError("UseHeap", CONTAINER_ERROR_NOT_EMPTY);
        return CONTAINER_ERROR_NOT_EMPTY;
    }
    if (m == NULL)
        m = L->Allocator;
    if (m == NULL)
        return CONTAINER_ERROR_BADARG;
    if (ListNodeSize(L, &nodeSize) < 0) {
        L->RaiseError("iList.UseHeap", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    /* heap.c advances through blocks by sizeof(void *) + ElementSize.  Give
     * it a pointer-aligned stride so each returned ListElement is aligned. */
    L->Heap = iHeap.Create(roundupTo(nodeSize, sizeof(void *)), m);
    if (L->Heap == NULL) {
        L->RaiseError("iList.UseHeap", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    return 1;
}

static ContainerHeap *GetHeap(const List *l)
{
    if (l == NULL) {
        NullPtrError("GetHeap");
        return NULL;
    }
    return l->Heap;
}

/*
 * ---------------------------------------------------------------------------
 *
 *                           Iterators
 *
 * ---------------------------------------------------------------------------
 */
static void    * Seek(Iterator * it, size_t idx)
{
    struct ListIterator *li = (struct ListIterator *) it;
    ListElement    *rvp;

    if (it == NULL) {
        NullPtrError("Seek");
        return NULL;
    }

	if (li->Magic != LIST_MAGIC_NUMBER) {
		iError.RaiseError("List.Seek",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
    if (li->timestamp != li->L->timestamp) {
        li->L->RaiseError("iList.Seek", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (li->L->count == 0)
        return NULL;
    if (idx >= li->L->count) {
        li->L->RaiseError("iList.Seek", CONTAINER_ERROR_INDEX, li->L, idx);
        return NULL;
    }
    rvp = li->L->First;
    if (idx == 0) {
        li->index = 0;
        li->Current = li->L->First;
    } else if (idx >= li->L->count - 1) {
        li->index = li->L->count - 1;
        li->Current = li->L->Last;
    } else {
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

/*------------------------------------------------------------------------
 Procedure:     GetNext ID:1
 Purpose:       Moves the cursor to the next element and returns a
                pointer to the data it contains.
 Input:         The iterator
 Output:        The data pointer or NULL if there isn't any next
                element
 Errors:        CONTAINER_ERROR_OBJECT_CHANGED
------------------------------------------------------------------------*/
static void    *GetNext(Iterator * it)
{
    struct ListIterator *li = (struct ListIterator *) it;
    List           *L;
    void           *result;


    if (li == NULL) {
        NullPtrError("GetNext");
        return NULL;
    }
	if (li->Magic != LIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetNext",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
    L = li->L;
    if (li->timestamp != L->timestamp) {
        L->RaiseError("GetNext", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (L->count == 0 || li->index >= (L->count - 1) || li->Current == NULL)
        return NULL;
    if (li->Current == NULL) return NULL;
    li->Current = li->Current->Next;
    if (li->Current == NULL) return NULL;
    li->index++;
    if (L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, li->Current->Data, L->ElementSize);
        return li->ElementBuffer;
    }
    result = li->Current->Data;
    return result;
}

static size_t GetPosition(Iterator *it)
{
    struct ListIterator *li = (struct ListIterator *) it;
	if (li == NULL) {
		NullPtrError("GetPosition");
		return (size_t)-1;
	}
	if (li->Magic != LIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetPosition",CONTAINER_ERROR_WRONG_ITERATOR);
		return (size_t)-1;
	}
	if (li->L && li->timestamp != li->L->timestamp) {
		li->L->RaiseError("GetPosition", CONTAINER_ERROR_OBJECT_CHANGED);
		return (size_t)-1;
	}
    return li->index;
}
static void    *GetPrevious(Iterator * it)
{
    struct ListIterator *li = (struct ListIterator *) it;
    List           *L;
    ListElement    *rvp;
    size_t          i;

    if (li == NULL) {
        NullPtrError("GetPrevious");
        return NULL;
    }
	if (li->Magic != LIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetPrevious",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
    L = li->L;
    if (li->timestamp != L->timestamp) {
        L->RaiseError("GetPrevious", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (L->count == 0)
        return NULL;
    if (li->index >= L->count || li->index == 0)
        return NULL;
    rvp = L->First;
    i = 0;
    li->index--;
    if (li->index > 0) {
        while (rvp && i < li->index) {
            rvp = rvp->Next;
            i++;
        }
    }
    li->Current = rvp;
    if (rvp == NULL) return NULL;
    if (L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, rvp->Data, L->ElementSize);
        return li->ElementBuffer;
    }
    return rvp->Data;
}

static void    *GetCurrent(Iterator * it)
{
    struct ListIterator *li = (struct ListIterator *) it;

    if (li == NULL) {
        NullPtrError("GetCurrent");
        return NULL;
    }

	if (li->Magic != LIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetCurrent",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
	if (li->timestamp != li->L->timestamp) {
        li->L->RaiseError("GetCurrent", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (li->L->count == 0)
        return NULL;
    if (li->index == (size_t) - 1) {
        li->L->RaiseError("GetCurrent", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (li->Current == NULL)
        return NULL;
    if (li->L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, li->Current->Data, li->L->ElementSize);
        return li->ElementBuffer;
    }
    return li->Current->Data;
}

static void *GetLast(Iterator *it)
{
    struct ListIterator *li = (struct ListIterator *)it;
    List *list;
    if (li == NULL) {
        NullPtrError("GetLast");
        return NULL;
    }
    if (li->Magic != LIST_MAGIC_NUMBER) {
        iError.RaiseError("List.GetLast", CONTAINER_ERROR_WRONG_ITERATOR);
        return NULL;
    }
    list = li->L;
    if (li->timestamp != list->timestamp) {
        list->RaiseError("GetLast", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (list->count == 0)
        return NULL;
    li->index = list->count - 1;
    li->Current = list->Last;
    if (list->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, li->Current->Data, list->ElementSize);
        return li->ElementBuffer;
    }
    return li->Current->Data;
}
static int ReplaceWithIterator(Iterator * it, void *data, int direction)
{
    struct ListIterator *li = (struct ListIterator *) it;
    int result;
    size_t pos;

    if (it == NULL) {
        return NullPtrError("Replace");
    }

	if (li->Magic != LIST_MAGIC_NUMBER) {
		iError.RaiseError("List.ReplaceWith",CONTAINER_ERROR_WRONG_ITERATOR);
		return CONTAINER_ERROR_WRONG_ITERATOR;
	}
    if (li->L->count == 0)
        return 0;
    if (li->L->Flags & CONTAINER_READONLY) {
        li->L->RaiseError("Replace", CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    if (li->timestamp != li->L->timestamp) {
        li->L->RaiseError("Replace", CONTAINER_ERROR_OBJECT_CHANGED);
        return CONTAINER_ERROR_OBJECT_CHANGED;
    }
    pos = li->index;
    if (data == NULL)
        result = RemoveAt(li->L, pos);
    else {
        result = ReplaceAt(li->L, pos, data);
    }
    if (result >= 0) {
        li->timestamp = li->L->timestamp;
        if (li->L->count == 0) {
            li->index = (size_t)-1;
            li->Current = NULL;
        } else if (data != NULL) {
            /* Replacement keeps the cursor on the same logical position. */
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

static void    *GetFirst(Iterator * it)
{
    struct ListIterator *li = (struct ListIterator *) it;
    List           *L;


    if (li == NULL) {
        NullPtrError("GetFirst");
        return NULL;
    }

	if (li->Magic != LIST_MAGIC_NUMBER) {
		iError.RaiseError("List.GetFirst",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
    }
    L = li->L;
    if (li->timestamp != L->timestamp) {
        L->RaiseError("iList.GetFirst", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (L->count == 0)
        return NULL;
    li->index = 0;
    li->Current = L->First;
    if (L->Flags & CONTAINER_READONLY) {
        memcpy(li->ElementBuffer, L->First->Data, L->ElementSize);
        return li->ElementBuffer;
    }
    return L->First->Data;
}

static int InitIterator(List * L, void *r)
{
    struct ListIterator *result = (struct ListIterator *) r;

    if (L == NULL) {
        return (int) SizeofIterator(NULL);
    }
    if (result == NULL)
        return NullPtrError("InitIterator");
    result->it.GetNext = GetNext;
    result->it.GetPrevious = GetPrevious;
    result->it.GetFirst = GetFirst;
    result->it.GetCurrent = GetCurrent;
    result->it.GetLast = GetLast;
    result->it.Seek = Seek;
    result->it.GetPosition = GetPosition;
    result->it.Replace = ReplaceWithIterator;
    result->L = L;
    result->timestamp = L->timestamp;
    result->index = (size_t) - 1;
    result->Current = NULL;
	result->Magic = LIST_MAGIC_NUMBER;
    result->Previous = NULL; /* NULL marks caller-provided placement storage. */
    return 1;
}

static Iterator *NewIterator(List * L)
{
    struct ListIterator *result;
    size_t iteratorSize;

    if (L == NULL) {
        NullPtrError("NewIterator");
        return NULL;
    }
    iteratorSize = SizeofIterator(L);
    if (iteratorSize == 0)
        return NULL;
    result = (struct ListIterator *)L->Allocator->malloc(iteratorSize);
    if (result == NULL) {
        L->RaiseError("iList.NewIterator", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    InitIterator(L,result);
    result->Previous = (ListElement *)(uintptr_t)1; /* allocator-owned */
    return &result->it;
}
static int DeleteIterator(Iterator * it)
{
    struct ListIterator *li;
    List           *L;

    if (it == NULL) {
        return NullPtrError("DeleteIterator");
    }
    li = (struct ListIterator *) it;
    L = li->L;
    if (li->Magic != LIST_MAGIC_NUMBER)
        return CONTAINER_ERROR_WRONG_ITERATOR;
    if (li->Previous == (ListElement *)(uintptr_t)1) {
        li->Magic = 0;
        L->Allocator->free(it);
    } else {
        li->Magic = 0;
    }
    return 1;
}

static int DefaultSaveFunction(const void *element, void *arg, FILE * Outfile)
{
    const unsigned char *str = (const unsigned char *)element;
    size_t          len = *(size_t *) arg;

    return len == fwrite(str, 1, len, Outfile);
}

static int Save(const List * L, FILE * stream, SaveFunction saveFn, void *arg)
{
    size_t          i;
    size_t          elemsiz;
    ListElement    *rvp;

    if (L == NULL)
        return NullPtrError("Save");

    if (stream == NULL) {
        L->RaiseError("iList.Save", CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (saveFn == NULL) {
        saveFn = DefaultSaveFunction;
        elemsiz = L->ElementSize;
        arg = &elemsiz;
    }
    if (fwrite(&ListGuid, sizeof(guid), 1, stream) != 1)
        return EOF;
    {
        uint32_t version = LIST_PERSIST_VERSION;
        uint32_t flags = (uint32_t)L->Flags;
        uint64_t elementSize = (uint64_t)L->ElementSize;
        uint64_t count = (uint64_t)L->count;
        if (fwrite(&version, sizeof(version), 1, stream) != 1 ||
            fwrite(&flags, sizeof(flags), 1, stream) != 1 ||
            fwrite(&elementSize, sizeof(elementSize), 1, stream) != 1 ||
            fwrite(&count, sizeof(count), 1, stream) != 1)
            return EOF;
    }
    rvp = L->First;
    for (i = 0; i < L->count; i++) {
        char           *p = rvp->Data;

        if (saveFn(p, arg, stream) <= 0)
            return EOF;
        rvp = rvp->Next;
    }
    return 1;
}

static int DefaultLoadFunction(void *element, void *arg, FILE * Infile)
{
    size_t          len = *(size_t *) arg;

    return len == fread(element, 1, len, Infile);
}

static List    *Load(FILE * stream, ReadFunction loadFn, void *arg)
{
    size_t          i, elemSize;
    size_t          count;
    uint32_t        version, flags;
    uint64_t        serializedSize, serializedCount;
    List           *result;
    char           *buf;
    int             r;
    guid            Guid;

    if (stream == NULL) {
        NullPtrError("Load");
        return NULL;
    }
    if (loadFn == NULL) {
        loadFn = DefaultLoadFunction;
        arg = &elemSize;
    }
    if (fread(&Guid, sizeof(guid), 1, stream) != 1) {
        iError.RaiseError("iList.Load", CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (memcmp(&Guid, &ListGuid, sizeof(guid))) {
        iError.RaiseError("iList.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    if (fread(&version, sizeof(version), 1, stream) != 1 ||
        fread(&flags, sizeof(flags), 1, stream) != 1 ||
        fread(&serializedSize, sizeof(serializedSize), 1, stream) != 1 ||
        fread(&serializedCount, sizeof(serializedCount), 1, stream) != 1) {
        iError.RaiseError("iList.Load", CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (version != LIST_PERSIST_VERSION || serializedSize == 0 ||
        serializedSize > (uint64_t)INT_MAX || serializedSize > SIZE_MAX ||
        serializedCount > SIZE_MAX) {
        iError.RaiseError("iList.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    elemSize = (size_t)serializedSize;
    count = (size_t)serializedCount;
    if (elemSize > SIZE_MAX - offsetof(ListElement, Data) ||
        count > SIZE_MAX / (offsetof(ListElement, Data) + elemSize)) {
        iError.RaiseError("iList.Load", CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    result = iList.Create(elemSize);
    if (result == NULL) {
        iError.RaiseError("iList.Load", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    buf = (char *)result->Allocator->calloc(1, elemSize);
    if (buf == NULL) {
        iError.RaiseError("iList.Load", CONTAINER_ERROR_NOMEMORY);
        (void)Finalize(result);
        return NULL;
    }
    /* Keep policy flags off until all records have been read; in particular,
     * a serialized READONLY bit must not prevent cleanup of a partial load. */
    r = 1;
    for (i = 0; i < count; i++) {
        if (loadFn(buf, arg, stream) <= 0) {
            r = CONTAINER_ERROR_FILE_READ;
            break;
        }
        if ((r = Add_nd(result, buf)) < 0) {
            break;
        }
    }
    result->Allocator->free(buf);
    if (r < 0) {
        iError.RaiseError("iList.Load", r);
        (void)Finalize(result);
        result = NULL;
    } else {
        result->Flags = flags & CONTAINER_READONLY;
    }
    return result;
}

static size_t 
GetElementSize(const List * l)
{
    if (l) {
        return l->ElementSize;
    }
    NullPtrError("GetElementSize");
    return 0;
}

/*------------------------------------------------------------------------
 Procedure:     Create ID:1
 Purpose:       Allocates a new list object header, initializes the
                VTable field and the element size
 Input:         The size of the elements of the list.
 Output:        A pointer to the newly created list or NULL if
                there is no memory.
 Errors:        If element size is smaller than zero an error
                routine is called. If there is no memory result is
                NULL.
 ------------------------------------------------------------------------*/
static List    *CreateWithAllocator(size_t elementsize, const ContainerAllocator * allocator)
{
    List           *result;

    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (allocator == NULL || allocator->calloc == NULL || allocator->malloc == NULL ||
        allocator->free == NULL || elementsize == 0 || elementsize > (size_t)INT_MAX ||
        elementsize > SIZE_MAX - offsetof(ListElement, Data)) {
        (void)NullPtrError("Create");
        return NULL;
    }
    result = (List *)allocator->calloc(1, sizeof(List));
    if (result == NULL) {
        iError.RaiseError("iList.Create", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    result->ElementSize = elementsize;
    result->VTable = &iList;
    result->Compare = DefaultListCompareFunction;
    result->RaiseError = iError.RaiseError;
    result->Allocator = (ContainerAllocator *) allocator;
    if (!RegisterListHeader(result)) {
        allocator->free(result);
        iError.RaiseError("iList.Create", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    return result;
}

static List    * Create(size_t elementsize)
{
    return CreateWithAllocator(elementsize, CurrentAllocator);
}

static List    *InitializeWith(size_t elementSize, size_t n, const void *Data)
{
    List           *result = Create(elementSize);
    size_t          i;
    const char     *pData = (const char *)Data;
    if (result == NULL)
        return result;
    if (n && Data == NULL) {
        result->RaiseError("iList.InitializeWith", CONTAINER_ERROR_BADARG);
        Finalize(result);
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (Add_nd(result, pData) < 0) {
            Finalize(result);
            return NULL;
        }
        pData += elementSize;
    }
    return result;
}

static List    *InitWithAllocator(List * result, size_t elementsize,
          const ContainerAllocator * allocator)
{
    if (result == NULL) {
        (void)NullPtrError("Init");
        return NULL;
    }
    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (allocator == NULL || allocator->malloc == NULL || allocator->free == NULL ||
        elementsize == 0 || elementsize > (size_t)INT_MAX ||
        elementsize > SIZE_MAX - offsetof(ListElement, Data)) {
        (void)NullPtrError("Init");
        return NULL;
    }
    if (IsRegisteredListHeader(result) && result->Allocator)
        ClearListNodes(result);
    (void)UnregisterListHeader(result);
    memset(result, 0, sizeof(List));
    result->ElementSize = elementsize;
    result->VTable = &iList;
    result->Compare = DefaultListCompareFunction;
    result->RaiseError = iError.RaiseError;
    result->Allocator = (ContainerAllocator *) allocator;
    return result;
}

static List    *
Init(List * result, size_t elementsize)
{
    return InitWithAllocator(result, elementsize, CurrentAllocator);
}

static const ContainerAllocator *GetAllocator(const List * l)
{
    if (l == NULL)
        return NULL;
    return l->Allocator;
}

static DestructorFunction 
SetDestructor(List * cb, DestructorFunction fn)
{
    DestructorFunction oldfn;
    if (cb == NULL)
        return NULL;
    oldfn = cb->DestructorFn;
    if (fn)
        cb->DestructorFn = fn;
    return oldfn;
}

static int RemoveRange(List * l, size_t start, size_t end)
{
    ListElement *node, *previous = NULL;
    size_t position = 0;
    size_t removeCount;

    if (l == NULL)
        return NullPtrError("RemoveRange");
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l, "RemoveRange");
    if (start >= l->count)
        return 0;
    if (end > l->count)
        end = l->count;
    if (end < start) {
        size_t tmp = start;
        start = end;
        end = tmp;
        if (start >= l->count)
            return 0;
        if (end > l->count)
            end = l->count;
    }
    if (start >= end)
        return 0;
    removeCount = end - start;
    node = l->First;
    while (node && position < start) {
        previous = node;
        node = node->Next;
        ++position;
    }
    while (node && position < end) {
        ListElement *next = node->Next;
        if (l->Flags & CONTAINER_HAS_OBSERVER)
            iObserver.Notify(l, CCL_ERASE_AT, node, (void *)position);
        DestroyListNode(l, node);
        node = next;
        ++position;
    }
    if (previous)
        previous->Next = node;
    else
        l->First = node;
    if (node == NULL)
        l->Last = previous;
    l->count -= removeCount;
    if (l->count == 0)
        l->First = l->Last = NULL;
    ++l->timestamp;
    return 1;
}

static int RotateLeft(List * l, size_t n)
{
    ListElement    *rvp, *oldStart, *last = NULL;
    if (l == NULL)
        return NullPtrError("RotateLeft");
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l, "RotateLeft");
    if (l->count < 2 || n == 0)
        return 0;
    n %= l->count;
    if (n == 0)
        return 0;
    rvp = l->First;
    oldStart = rvp;
    while (n > 0) {
        last = rvp;
        rvp = rvp->Next;
        n--;
    }
    l->First = rvp;
    last->Next = NULL;
    l->Last->Next = oldStart;
    l->Last = last;
    ++l->timestamp;
    return 1;
}


static int RotateRight(List * l, size_t n)
{
    ListElement    *rvp, *oldStart, *last = NULL;
    if (l == NULL)
        return NullPtrError("RotateRight");
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l, "RotateRight");
    if (l->count < 2 || n == 0)
        return 0;
    n %= l->count;
    if (n == 0)
        return 0;
    rvp = l->First;
    oldStart = rvp;
    n = l->count - n;
    while (n > 0) {
        last = rvp;
        rvp = rvp->Next;
        n--;
    }
    l->First = rvp;
    if (last == NULL) {
        iError.RaiseError("RotateRight", CONTAINER_INTERNAL_ERROR);
        return CONTAINER_INTERNAL_ERROR;
    }
    last->Next = NULL;
    l->Last->Next = oldStart;
    l->Last = last;
    ++l->timestamp;
    return 1;
}

static int Select(List *src,const Mask *m)
{
    size_t i, kept = 0;
    ListElement *node, *previous = NULL;

    if (src == NULL || m == NULL) {
        return NullPtrError("Select");
    }
    if (src->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(src,"Select");
    if (m->length != src->count) {
        iError.RaiseError("Select",CONTAINER_ERROR_BADMASK,src,m);
        return CONTAINER_ERROR_BADMASK;
    }
    if (src->count == 0)
        return 0;
    node = src->First;
    for (i = 0; node && i < m->length; ++i) {
        ListElement *next = node->Next;
        if (m->data[i]) {
            if (previous)
                previous->Next = node;
            else
                src->First = node;
            previous = node;
            src->Last = node;
            ++kept;
        } else {
            DestroyListNode(src, node);
        }
        node = next;
    }
    if (previous)
        previous->Next = NULL;
    else
        src->First = src->Last = NULL;
    src->count = kept;
    ++src->timestamp;
    return 1;
}

static List *SelectCopy(const List *src,const Mask *m)
{
    List *result;
    ListElement *rvp;
    size_t i;
    int r;

    if (src == NULL || m == NULL) {
        NullPtrError("SelectCopy");
        return NULL;
    }
    if (m->length != src->count) {
        iError.RaiseError("SelectCopy",CONTAINER_ERROR_BADMASK,src,m);
        return NULL;
    }
    result = CreateWithAllocator(src->ElementSize, src->Allocator);
    if (result == NULL) return NULL;
    result->Compare = src->Compare;
    result->RaiseError = src->RaiseError;
    rvp = src->First;
    for (i=0; i<m->length;i++) {
        if (m->data[i]) {
            r = Add_nd(result,rvp->Data);
            if (r < 0) {
                Finalize(result);
                return NULL;
            }
        }
        rvp = rvp->Next;
    }
    return result;
}

static ListElement *FirstElement(List *l)
{
    if (l == NULL) {
        NullPtrError("FirstElement");
        return NULL;
    }
    if (l->Flags&CONTAINER_READONLY) {
        ErrorReadOnly(l,"FirstElement");
        return NULL;
    }
    return l->First;
}

static ListElement *LastElement(List *l)
{
    if (l == NULL) {
        NullPtrError("LastElement");
        return NULL;
    }
    if (l->Flags&CONTAINER_READONLY) {
        ErrorReadOnly(l,"LastElement");
        return NULL;
    }
    return l->Last;
}

static ListElement *NextElement(ListElement *le)
{
    if (le == NULL) return NULL;
    return le->Next;
}

static void *GetElementData(ListElement *le)
{
    if (le == NULL) return NULL;
    return le->Data;
}

static int SetElementData(List *l,ListElement *le,void *data)
{
    if (l == NULL || le == NULL || data == NULL) {
        return iError.NullPtrError("iList.SetElementData");
    }
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l, "SetElementData");
    if (!ListContainsNode(l, le)) {
        l->RaiseError("iList.SetElementData", CONTAINER_ERROR_WRONGELEMENT, l, le);
        return CONTAINER_ERROR_WRONGELEMENT;
    }
    if (data == le->Data)
        return 1;
    if (l->DestructorFn)
        (void)l->DestructorFn(le->Data);
    memcpy(le->Data,data,l->ElementSize);
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l, CCL_REPLACEAT, le, NULL);
    return 1;
}

static void *Advance(ListElement **ple)
{
    ListElement *le;
    void *result;

    if (ple == NULL)
        return NULL;
    le = *ple;
    if (le == NULL) return NULL;
    result = le->Data;
    le = le->Next;
    *ple = le;
    return result;
}

static ListElement *Skip(ListElement *le, size_t n)
{
    if (le == NULL) return NULL;
    while (le != NULL && n > 0) {
        le = le->Next;
        n--;
    }
    return le;
}

static List *SplitAfter(List *l, ListElement *pt)
{
    ListElement *pNext;
    List *result;    
    size_t count=0;

    if (pt == NULL || l == NULL) {
        iError.NullPtrError("iList.SplitAfter");
        return NULL;
    }
    if (l->Flags&CONTAINER_READONLY) {
        ErrorReadOnly(l,"SplitAfter");
        return NULL;
    }
    if (!ListContainsNode(l, pt)) {
        l->RaiseError("iList.SplitAfter", CONTAINER_ERROR_WRONGELEMENT, l, pt);
        return NULL;
    }
    /* A heap backs the entire allocation arena.  Moving only a suffix would
     * leave the prefix and suffix sharing an ownership object, so reject the
     * ambiguous transfer instead of finalizing through the wrong allocator. */
    if (l->Heap) {
        l->RaiseError("iList.SplitAfter", CONTAINER_ERROR_INCOMPATIBLE, l, pt);
        return NULL;
    }
    pNext = pt->Next;
    if (pNext == NULL) return NULL;
    result = CreateWithAllocator(l->ElementSize, l->Allocator);
    if (result == NULL) return NULL;
    result->Compare = l->Compare;
    result->RaiseError = l->RaiseError;
    result->DestructorFn = l->DestructorFn;
    result->First = pNext;
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

ListInterface   iList = {
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
    /* end of generic part */
    Add,
    GetElement,
    PushFront,
    PopFront,
    InsertAt,
    RemoveAt,
    ReplaceAt,
    IndexOf,
    /* End of sequential container part */
    InsertIn,
    CopyElement,
    EraseRange,
    Sort,
    Reverse,
    GetRange,
    Append,
    SetCompareFunction,
    DefaultListCompareFunction,
    UseHeap,
    GetHeap,
    AddRange,
    Create,
    CreateWithAllocator,
    Init,
    InitWithAllocator,
    GetAllocator,
    SetDestructor,
    InitializeWith,
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
    GetElementData,
    SetElementData,
    Advance,
    Skip,
    SplitAfter,
};
