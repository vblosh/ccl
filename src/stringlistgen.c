/* -------------------------------------------------------------------
        StringList                
        StringList routines sample implementation
        -----------------------------------------
This routines handle the StringList container class. This is a very general implementation
and efficiency considerations aren't yet primordial. StringLists can have elements
of any size. This implement single linked StringLists.
   The design goals here are just correctness and showing how the implementation of
the proposed interface COULD be done.
---------------------------------------------------------------------- */

#ifndef DEFAULT_START_SIZE
#define DEFAULT_START_SIZE 20
#endif
#include <stdint.h>
#ifndef INT_MAX
#define INT_MAX (((unsigned)-1) >> 1)
#endif
#define STRINGLIST_STREAM_VERSION 1u
#define STRINGLIST_STREAM_ENCODING 1u
#define STRINGLIST_STREAM_MAGIC 0x31534c53u /* "SLS1" in little-endian */
static int IndexOf_nd(const LIST_TYPE(DATA_TYPE) *AL,const CHARTYPE *SearchedElement,void *ExtraArgs,size_t *result);
static int RemoveAt_nd(LIST_TYPE(DATA_TYPE) *AL,size_t idx);
static LIST_TYPE(DATA_TYPE) *CreateWithAllocator(const ContainerAllocator *allocator);
static LIST_TYPE(DATA_TYPE) *Create(void);
static int Finalize(LIST_TYPE(DATA_TYPE) *l);
#define CONTAINER_LIST_SMALL    2
#define CHUNK_SIZE    1000

static int ErrorReadOnly(LIST_TYPE(DATA_TYPE) *L,char *fnName)
{
    char buf[512];
    
    snprintf(buf,sizeof(buf),"iStringList.%s",fnName);
    L->RaiseError(buf,CONTAINER_ERROR_READONLY);
    return CONTAINER_ERROR_READONLY;
}

static int NullPtrError(char *fnName)
{
    char buf[512];
    
    snprintf(buf,sizeof(buf),"iStringList.%s",fnName);
    return iError.NullPtrError(buf);
}

static int SizeAdd(size_t a,size_t b,size_t *out)
{
    if (b > SIZE_MAX-a)
        return 0;
    *out = a+b;
    return 1;
}

static int SizeMul(size_t a,size_t b,size_t *out)
{
    if (a != 0 && b > SIZE_MAX/a)
        return 0;
    *out = a*b;
    return 1;
}

static int StringBytes(size_t chars,size_t *bytes)
{
    return SizeMul(chars,sizeof(CHARTYPE),bytes);
}

static void ReleaseLink(LIST_TYPE(DATA_TYPE) *l,LIST_ELEMENT(DATA_TYPE) *link)
{
    if (link == NULL)
        return;
    if (l->DestructorFn)
        l->DestructorFn(link->Data);
    if (l->Heap)
        iHeap.FreeObject(l->Heap,link);
    else
        l->Allocator->free(link);
}

static CHARTYPE *CopyToIteratorBuffer(struct ITERATOR(DATA_TYPE) *it,
                                      const CHARTYPE *data)
{
    LIST_TYPE(DATA_TYPE) *l = it->L;
    size_t chars,bytes;
    CHARTYPE *tmp;

    chars = STRLEN(data);
    if (!SizeAdd(chars,1,&chars) || !StringBytes(chars,&bytes)) {
        l->RaiseError("iStringList.Iterator",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    tmp = l->Allocator->realloc(it->ElementBuffer,bytes);
    if (tmp == NULL) {
        l->RaiseError("iStringList.Iterator",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    it->ElementBuffer = tmp;
    memcpy(tmp,data,bytes);
    return tmp;
}

static LIST_ELEMENT(DATA_TYPE) *NewLink(LIST_TYPE(DATA_TYPE) *li,const CHARTYPE *data,const char *fname)
{
    LIST_ELEMENT(DATA_TYPE) *result;
    size_t len,chars,bytes,total;

    if (data == NULL) {
        li->RaiseError(fname,CONTAINER_ERROR_BADARG);
        return NULL;
    }
    chars = STRLEN(data);
    if (!SizeAdd(chars,1,&len) || !StringBytes(len,&bytes) ||
        !SizeAdd(offsetof(LIST_ELEMENT(DATA_TYPE),Data),bytes,&total)) {
        li->RaiseError(fname,CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }

    result = li->Allocator->malloc(total);
    if (result == NULL) {
        li->RaiseError(fname,CONTAINER_ERROR_NOMEMORY);
    }
    else {
        result->Next = NULL;
        STRCPY(result->Data,data);
    }
    return result;
}

static int DefaultStringListCompareFunction(const void *left,const void *right,CompareInfo *ExtraArgs)
{
   return STRCMP(left,right);
}

/*------------------------------------------------------------------------
 Procedure:     Contains ID:1
 Purpose:       Determines if the given data is in the container
 Input:         The list and the data to be searched
 Output:        Returns 1 (true) if the data is in there, false
                otherwise
 Errors:        The same as the function IndexOf
------------------------------------------------------------------------*/
static int Contains(const LIST_TYPE(DATA_TYPE) *l,const CHARTYPE *data)
{
    size_t idx;
    if (l == NULL || data == NULL) {
        if (l)
            l->RaiseError("iStringList.Contains",CONTAINER_ERROR_BADARG);
        else
            iError.NullPtrError("iStringList.Contains");
        return CONTAINER_ERROR_BADARG;
    }
    return (IndexOf_nd(l,data,NULL,&idx) < 0) ? 0 : 1;
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
static int Clear_nd(LIST_TYPE(DATA_TYPE) *l)
{
    LIST_ELEMENT(DATA_TYPE) *rvp,*tmp;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_CLEAR,NULL,NULL);
#ifdef NO_GC
    rvp = l->First;
    while (rvp) {
        tmp = rvp->Next;
        ReleaseLink(l,rvp);
        rvp = tmp;
    }
    if (l->Heap)
        iHeap.Finalize(l->Heap);
#endif
    /* Clear the fields that need to be cleared but not all fields */
    l->count = 0;
    l->Heap = NULL;
    l->First = l->Last = NULL;
    l->Flags = 0;
    l->timestamp++;
    return 1;
}

static int Clear(LIST_TYPE(DATA_TYPE) *l)
{
    if (l == NULL) {
        return NullPtrError("Clear");
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iStringList.Clear",CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    return Clear_nd(l);
}


/*------------------------------------------------------------------------
 Procedure:     Add ID:1
 Purpose:       Adds an element to the list
 Input:         The list and the elemnt to be added
 Output:        Returns the number of elements in the list or a
                negative error code if an error occurs.
 Errors:        The element to be added can't be NULL, and the list
                must be writable.
------------------------------------------------------------------------*/
static int Add_nd(LIST_TYPE(DATA_TYPE) *l,const CHARTYPE *elem)
{
    LIST_ELEMENT(DATA_TYPE) *newl;

    newl = NewLink(l,elem,"iStringList.Add");
    if (newl == 0)
        return CONTAINER_ERROR_NOMEMORY;
    if (l->count ==  0) {
        l->First = newl;
    }
    else {
        l->Last->Next = newl;
    }
    l->Last = newl;
    l->timestamp++;
    ++l->count;
    return 1;
}

static int Add(LIST_TYPE(DATA_TYPE) *l,CHARTYPE *elem)
{
    int r;
    if (l == NULL || elem == NULL) return NullPtrError("Add");
    if (l->Flags &CONTAINER_READONLY) return ErrorReadOnly(l,"Add");
    r = Add_nd(l,elem);
    if (r > 0 && (l->Flags & CONTAINER_HAS_OBSERVER))
        iObserver.Notify(l,CCL_ADD,elem,NULL);
    return r;
}


/*------------------------------------------------------------------------
 Procedure:     SetReadOnly ID:1
 Purpose:       Sets/Unsets the read only flag.
 Input:         The list and the new value
 Output:        The old value of the flag
 Errors:        None
------------------------------------------------------------------------*/
static unsigned SetFlags(LIST_TYPE(DATA_TYPE) *l,unsigned newval)
{
    unsigned result;

    if (l == NULL) {
        NullPtrError("SetFlags");
        return 0;
    }
    result = l->Flags;
    l->Flags = newval;
    return result;
}

/*------------------------------------------------------------------------
 Procedure:     IsReadOnly ID:1
 Purpose:       Queries the read only flag
 Input:         The list
 Output:        The state of the flag
 Errors:        None
------------------------------------------------------------------------*/
static unsigned GetFlags(const LIST_TYPE(DATA_TYPE) *l)
{
    if (l == NULL) {
        NullPtrError("GetFlags");
        return 0;
    }
    return l->Flags;
}

/*------------------------------------------------------------------------
 Procedure:     Size ID:1
 Purpose:       Returns the number of elements in the StringList
 Input:         The StringList
 Output:        The number of elements
 Errors:        None
------------------------------------------------------------------------*/
static size_t Size(const LIST_TYPE(DATA_TYPE) *l)
{
    if (l == NULL) {
        return (size_t)NullPtrError("Size");
    }
    return l->count;
}

static LIST_TYPE(DATA_TYPE) *SetAllocator(LIST_TYPE(DATA_TYPE) *l,ContainerAllocator  *allocator)
{
    if (l == NULL) {
        NullPtrError("SetAllocator");
        return NULL;
    }
    if (l->count || l->Heap)
        return NULL;
    if (allocator != NULL) {
        LIST_TYPE(DATA_TYPE) *newStringList;
        if (l->Flags&CONTAINER_READONLY) {
            l->RaiseError("iStringList.SetAllocator",CONTAINER_READONLY);
            return NULL;
        }
        newStringList = allocator->malloc(sizeof(LIST_TYPE(DATA_TYPE)));
        if (newStringList == NULL) {
            l->RaiseError("iStringList.SetAllocator",CONTAINER_ERROR_NOMEMORY);
            return NULL;
        }
        memcpy(newStringList,l,sizeof(LIST_TYPE(DATA_TYPE)));
        newStringList->Allocator = allocator;
        newStringList->ownsStorage = 1;
        if (l->ownsStorage)
            l->Allocator->free(l);
        return newStringList;
    }
    return NULL;
}

/*------------------------------------------------------------------------
 Procedure:     SetCompareFunction ID:1
 Purpose:       Defines the function to be used in comparison of
                StringList elements
 Input:         The StringList and the new comparison function. If the new
                comparison function is NULL no changes are done.
 Output:        Returns the old function
 Errors:        None
------------------------------------------------------------------------*/
static CompareFunction SetCompareFunction(LIST_TYPE(DATA_TYPE) *l,CompareFunction fn)
{
    CompareFunction oldfn;


    if (l == NULL) {
        NullPtrError("iStringList.SetCompareFunction");
        return NULL;
    }
    oldfn = l->Compare;
    if (fn != NULL) { /* Treat NULL as an enquiry to get the compare function */
        if (l->Flags&CONTAINER_READONLY) {
            l->RaiseError("iStringList.SetCompareFunction",CONTAINER_READONLY);
        }
        else l->Compare = fn;
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
static LIST_TYPE(DATA_TYPE) *Copy(const LIST_TYPE(DATA_TYPE) *l)
{
    LIST_TYPE(DATA_TYPE) *result;
    LIST_ELEMENT(DATA_TYPE) *elem,*newElem;

    if (l == NULL) {
        NullPtrError("Copy");
        return NULL;
    }
    result = CreateWithAllocator(l->Allocator);
    if (result == NULL) {
        l->RaiseError("iStringList.Copy",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    result->Flags = l->Flags;   /* Same flags */
    result->VTable = l->VTable; /* Copy possibly subclassed methods */
    result->Compare = l->Compare; /* Copy compare function */
    result->RaiseError = l->RaiseError;
    result->ElementSize = l->ElementSize;
    result->DestructorFn = l->DestructorFn;
    elem = l->First;
    while (elem) {
        newElem = NewLink(result,elem->Data,"iStringList.Copy");
        if (newElem == NULL) {
            l->RaiseError("iStringList.Copy",CONTAINER_ERROR_NOMEMORY);
            result->Flags &= ~CONTAINER_READONLY;
            Finalize(result);
            return NULL;
        }
        if (elem == l->First) {
        	result->First = newElem;
        	result->count++;
        }
        else {
            result->Last->Next = newElem;
            result->count++;
        }
        result->Last = newElem;
        elem = elem->Next;
    }
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_COPY,result,NULL);

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
static int Finalize(LIST_TYPE(DATA_TYPE) *l)
{
    int t=0;
    unsigned Flags=0;

    if (l) Flags = l->Flags;
    else return CONTAINER_ERROR_BADARG;
    t = Clear(l);
    if (t < 0)
        return t;
    if (Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_FINALIZE,NULL,NULL);
    if (l->VTable != &iSTRINGLIST(DATA_TYPE) && l->ownsStorage)
        l->Allocator->free(l->VTable);
    if (l->ownsStorage)
        l->Allocator->free(l);
    return 1;
}

/*------------------------------------------------------------------------
 Procedure:     GetElement ID:1
 Purpose:       Returns the data associated with a given position
 Input:         The StringList and the position
 Output:        A pointer to the data
 Errors:        NULL if error in the positgion index
------------------------------------------------------------------------*/
static CHARTYPE *GetElement(LIST_TYPE(DATA_TYPE) *l,size_t position)
{
    LIST_ELEMENT(DATA_TYPE) *rvp;

    if (l == NULL) {
        NullPtrError("GetElement");
        return NULL;
    }
    if (position >= l->count) {
        l->RaiseError("GetElement",CONTAINER_ERROR_INDEX,l,position);
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iStringList.GetElement",CONTAINER_ERROR_READONLY);
        return NULL;
    }
    rvp = l->First;
    while (position) {
        rvp = rvp->Next;
        position--;
    }
    return rvp->Data;
}

static CHARTYPE *Back(const LIST_TYPE(DATA_TYPE) *l)
{
    if (l == NULL) {
        NullPtrError("Back");
        return NULL;
    }
    if (0 == l->count) {
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iStringList.Back",CONTAINER_ERROR_READONLY);
        return NULL;
    }
    return l->Last->Data;
}
static CHARTYPE *Front(const LIST_TYPE(DATA_TYPE) *l)
{
    if (l == NULL) {
        NullPtrError("Front");
        return NULL;
    }
    if (0 == l->count) {
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iStringList.Front",CONTAINER_ERROR_READONLY);
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
static int CopyElement(LIST_TYPE(DATA_TYPE) *l,size_t position,CHARTYPE *outBuffer)
{
    LIST_ELEMENT(DATA_TYPE) *rvp;

    if (l == NULL || outBuffer == NULL) {
        if (l)
            l->RaiseError("iStringList.CopyElement",CONTAINER_ERROR_BADARG);
        else
            NullPtrError("CopyElement");
        return CONTAINER_ERROR_BADARG;
    }
    if (position >= l->count) {
        l->RaiseError("iStringList.CopyElement",CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    rvp = l->First;
    while (position) {
        rvp = rvp->Next;
        position--;
    }
    STRCPY(outBuffer,rvp->Data);
    return 1;
}

static int ReplaceAt(LIST_TYPE(DATA_TYPE) *l,size_t position,CHARTYPE *data)
{
    LIST_ELEMENT(DATA_TYPE) *rvp,*previous = NULL,*newnode;

    /* Error checking */
    if (l == NULL || data == NULL) {
        if (l)
            l->RaiseError("iStringList.ReplaceAt",CONTAINER_ERROR_BADARG);
        else
            NullPtrError("ReplaceAt");
        return CONTAINER_ERROR_BADARG;
    }
    if (position >= l->count ) {
        l->RaiseError("iStringList.ReplaceAt",CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iStringList.ReplaceAt",CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    rvp = l->First;
    while (position-- > 0) {
        previous = rvp;
        rvp = rvp->Next;
    }
    /* Allocate a complete replacement before touching the old node.  Nodes
     * have no capacity field, so an in-place copy cannot be made safe. */
    newnode = NewLink(l,data,"iStringList.ReplaceAt");
    if (newnode == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    newnode->Next = rvp->Next;
    if (previous)
        previous->Next = newnode;
    else
        l->First = newnode;
    if (l->Last == rvp)
        l->Last = newnode;
    ReleaseLink(l,rvp);
    l->timestamp++;
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
static LIST_TYPE(DATA_TYPE) *GetRange(LIST_TYPE(DATA_TYPE) *l,size_t start,size_t end)
{
    size_t counter;
    LIST_TYPE(DATA_TYPE) *result;
    LIST_ELEMENT(DATA_TYPE) *rvp;;

    if (l == NULL) {
        NullPtrError("GetRange");
        return NULL;
    }
    result = CreateWithAllocator(l->Allocator);
    if (result == NULL) return NULL;
    result->VTable = l->VTable;
    result->Flags = l->Flags;
    result->Compare = l->Compare;
    result->RaiseError = l->RaiseError;
    result->ElementSize = l->ElementSize;
    result->DestructorFn = l->DestructorFn;
    if (l->count == 0)
        return result;
    if (end >= l->count)
        end = l->count;
    if (start >= end || start > l->count)
        return result;
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
    while (start < end && rvp != NULL) {
        int r = Add_nd(result,rvp->Data);
        if (r < 0) {
            result->Flags &= ~CONTAINER_READONLY;
            Finalize(result);
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
static int Equal(LIST_TYPE(DATA_TYPE) *l1,LIST_TYPE(DATA_TYPE) *l2)
{
    LIST_ELEMENT(DATA_TYPE) *link1,*link2;
    CompareFunction fn;
    CompareInfo ci;

    if (l1 == l2)
        return 1;
    if (l1 == NULL || l2 == NULL)
        return 0;
    if (l1->count != l2->count)
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
 Procedure:     PushFront ID:1
 Purpose:       Inserts an element at the start of the list
 Input:         The list and the data to insert
 Output:        The count of the list after insertion, or CONTAINER_ERROR_READONLY
                if the list is not writable or CONTAINER_ERROR_NOMEMORY if there is
                no more memory left.
 Errors:        None
------------------------------------------------------------------------*/
static int PushFront(LIST_TYPE(DATA_TYPE) *l,CHARTYPE *pdata)
{
    LIST_ELEMENT(DATA_TYPE) *rvp;

    if (l == NULL || pdata == NULL) {
        if (l)
            l->RaiseError("iStringList.PushFront",CONTAINER_ERROR_BADARG);
        else
            NullPtrError("PushFront");
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l,"PushFront");
    }
    rvp = NewLink(l,pdata,"Insert");
    if (rvp == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    rvp->Next = l->First;
    l->First = rvp;
    if (l->Last == NULL)
        l->Last = rvp;
    l->count++;
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_PUSH,pdata,NULL);

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
static int PopFront(LIST_TYPE(DATA_TYPE) *l,CHARTYPE *result)
{
    LIST_ELEMENT(DATA_TYPE) *le;

    if (l == NULL) {
        iError.RaiseError("iStringList.PopFront",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l,"PopFront");
    }
    if (l->count == 0)
        return 0;
    le = l->First;
    if (l->count == 1) {
        l->First = l->Last = NULL;
    }
    else l->First = l->First->Next;
    l->count--;
    if (result)
        STRCPY(result,le->Data);
    ReleaseLink(l,le);
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_POP,result,NULL);
    return 1;
}


static int InsertIn(LIST_TYPE(DATA_TYPE) *l, size_t idx,LIST_TYPE(DATA_TYPE) *newData)
{
    size_t newCount;
    LIST_ELEMENT(DATA_TYPE) *le,*nle;

    if (l == NULL || newData == NULL) {
        if (l)
            l->RaiseError("iStringList.InsertIn",CONTAINER_ERROR_BADARG);
        else
            NullPtrError("InsertIn");
        return CONTAINER_ERROR_BADARG;
    }
    if (l == newData)
        return CONTAINER_ERROR_INCOMPATIBLE;
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l,"InsertIn");
    }
    if (idx > l->count) {
        l->RaiseError("iStringList.InsertIn",CONTAINER_ERROR_INDEX,l,idx);
        return CONTAINER_ERROR_INDEX;
    }
    if (newData->count == 0)
        return 1;
    newData = Copy(newData);
    if (newData == NULL) {
        l->RaiseError("iStringList.InsertIn",CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    if (newData->count > SIZE_MAX-l->count) {
        Finalize(newData);
        l->RaiseError("iStringList.InsertIn",CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    newCount = l->count + newData->count;
    if (l->count == 0) {
        l->First = newData->First;
        l->Last = newData->Last;
    }
    else {
        if (idx == 0) {
            newData->Last->Next = l->First;
            l->First = newData->First;
        } else if (idx == l->count) {
            l->Last->Next = newData->First;
            l->Last = newData->Last;
        } else {
            le = l->First;
            while (--idx > 0)
                le = le->Next;
            nle = le->Next;
            le->Next = newData->First;
            newData->Last->Next = nle;
        }
    }
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_INSERT_IN,newData,NULL);
    newData->Allocator->free(newData);
    l->timestamp++;
    l->count = newCount;

    return 1;
}

static int InsertAt(LIST_TYPE(DATA_TYPE) *l,size_t pos,CHARTYPE *pdata)
{
    LIST_ELEMENT(DATA_TYPE) *elem;
    if (l == NULL || pdata == NULL) {
        if (l)
            l->RaiseError("iStringList.InsertAt",CONTAINER_ERROR_BADARG);
        else
            NullPtrError("InsertAt");
        return CONTAINER_ERROR_BADARG;
    }
    if (pos > l->count) {
        l->RaiseError("iStringList.InsertAt",CONTAINER_ERROR_INDEX,l,pos);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l,"InsertAt");
    }
    if (pos == l->count) {
        return Add_nd(l,pdata);
    }

    elem = NewLink(l,pdata,"iStringList.InsertAt");
    if (elem == NULL) {
        l->RaiseError("iStringList.InsertAt",CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    if (pos == 0) {
        elem->Next = l->First;
        l->First = elem;
    }
    else {
        LIST_ELEMENT(DATA_TYPE) *rvp = l->First;
        while (--pos > 0) {
            rvp = rvp->Next;
        }
        elem->Next = rvp->Next;
        rvp->Next = elem;
    }
    l->count++;
    l->timestamp++;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_INSERT_AT,pdata,(void *)pos);

    return 1;
}

static int Erase(LIST_TYPE(DATA_TYPE) *l, CHARTYPE *elem)
{
    size_t position;
    LIST_ELEMENT(DATA_TYPE) *rvp,*previous;
    int r;
    CompareFunction fn;
    CompareInfo ci;

    if (l == NULL) {
        return NullPtrError("Erase");
    }
    if (elem == NULL) {
        l->RaiseError("iList.Erase",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l,"Erase");
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
        r = fn(&rvp->Data,elem,&ci);
        if (r == 0) {
            if (l->Flags & CONTAINER_HAS_OBSERVER)
                iObserver.Notify(l,CCL_ERASE_AT,rvp,(void *)position);

            if (position == 0) {
                if (l->count == 1) {
                    l->First = l->Last = NULL;
                }
                else {
                    l->First = l->First->Next;
                }
            }
            else if (position == l->count - 1) {
                if (previous) previous->Next = NULL;
                l->Last = previous;
            }
            else {
                if (previous) previous->Next = rvp->Next;
            }

            ReleaseLink(l,rvp);
            l->count--;
            l->timestamp++;
            return 1;
        }
        previous = rvp;
        rvp = rvp->Next;
        position++;
    }
    return CONTAINER_ERROR_NOTFOUND;
}

static int EraseRange(LIST_TYPE(DATA_TYPE) *l,size_t start,size_t end)
{
    LIST_ELEMENT(DATA_TYPE) *previous = NULL,*current,*tmp;
    size_t i,n;
    if (l == NULL) {
        return NullPtrError("EraseRange");
    }
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l,"EraseRange");
    if (end > l->count)
        end = l->count;
    if (start >= end || start >= l->count)
        return 0;
    current = l->First;
    for (i=0; i<start; ++i) {
        previous = current;
        current = current->Next;
    }
    n = end-start;
    while (n-- > 0 && current) {
        tmp = current->Next;
        ReleaseLink(l,current);
        current = tmp;
        --l->count;
    }
    if (previous)
        previous->Next = current;
    else
        l->First = current;
    if (current == NULL)
        l->Last = previous;
    if (l->count == 0)
        l->First = l->Last = NULL;
    l->timestamp++;
    return 1;
}

static int RemoveAt_nd(LIST_TYPE(DATA_TYPE) *l,size_t position)
{
    LIST_ELEMENT(DATA_TYPE) *rvp,*last = NULL,*removed;

    rvp = l->First;
    while (position > 0) {
        last = rvp;
        rvp = rvp->Next;
        --position;
    }
    removed = rvp;
    if (last)
        last->Next = removed->Next;
    else
        l->First = removed->Next;
    if (l->Last == removed)
        l->Last = last;
    if (l->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l,CCL_ERASE_AT,removed,(void *)position);
    ReleaseLink(l,removed);
    l->timestamp++;
    --l->count;
    return 1;
}

static int RemoveAt(LIST_TYPE(DATA_TYPE) *l,size_t position)
{
    if (l == NULL) {
        return NullPtrError("RemoveAt");
    }
    if (position >= l->count) {
        l->RaiseError("iStringListRemoveAt",CONTAINER_ERROR_INDEX,l,position);
        return CONTAINER_ERROR_INDEX;
    }
    if (l->Flags & CONTAINER_READONLY) {
        return ErrorReadOnly(l,"RemoveAt");
    }
    return RemoveAt_nd(l,position);
}    
static int Append(LIST_TYPE(DATA_TYPE) *l1,LIST_TYPE(DATA_TYPE) *l2)
{

    if (l1 == NULL || l2 == NULL) {
        if (l1)
            l1->RaiseError("iStringList.Append",CONTAINER_ERROR_BADARG);
        else
            iError.RaiseError("iStringList.Append",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l1 == l2)
        return CONTAINER_ERROR_INCOMPATIBLE;
    if ((l1->Flags & CONTAINER_READONLY) || (l2->Flags & CONTAINER_READONLY)) {
        l1->RaiseError("iStringList.Append",CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    if (l1->Allocator != l2->Allocator || l1->Heap != l2->Heap) {
        l1->RaiseError("iStringList.Append",CONTAINER_ERROR_INCOMPATIBLE);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    if (l1->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l1,CCL_APPEND,l2,NULL);

    if (l2->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(l2,CCL_FINALIZE,NULL,NULL);

    if (l1->count == 0) {
        l1->First = l2->First;
        l1->Last = l2->Last;
    }
    else if (l2->count > 0) {
        if (l2->First)
            l1->Last->Next = l2->First;
        if (l2->Last)
            l1->Last = l2->Last;
    }
    l1->count += l2->count;
    l1->timestamp++;
    if (l2->ownsStorage)
        l2->Allocator->free(l2);
    else {
        /* Placement-created headers remain caller-owned after their nodes
         * have been transferred. */
        l2->First = l2->Last = NULL;
        l2->count = 0;
        l2->timestamp++;
    }
    return 1;
}

static int Reverse(LIST_TYPE(DATA_TYPE) *l)
{
    LIST_ELEMENT(DATA_TYPE) *New,*current,*old;

    if (l == NULL) {
        iError.RaiseError("Reverse",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iStringList.Reverse",CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    if (l->count < 2)
        return 1;
    old = l->First;
    l->Last = l->First;
    New = NULL;
    while (old) {
        current = old;
        old = old->Next;
        current->Next = New;
        New = current;
    }
    l->First = New;
    if (l->Last)
        l->Last->Next = NULL;
    l->timestamp++;
    return 1;
}


static int AddRange(LIST_TYPE(DATA_TYPE) * AL,size_t n, CHARTYPE **data)
{
    CHARTYPE **p;
    size_t oldCount;

    if (AL == NULL) return NullPtrError("AddRange");
        
    if (AL->Flags & CONTAINER_READONLY) {
        AL->RaiseError("iStringList.AddRange",CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    if (data == NULL) {
        AL->RaiseError("iStringList.AddRange",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    p = data;
    oldCount = AL->count;
    while (n > 0) {
        if (*p == NULL) {
            (void)EraseRange(AL,oldCount,AL->count);
            AL->RaiseError("iStringList.AddRange",CONTAINER_ERROR_BADARG);
            return CONTAINER_ERROR_BADARG;
        }
        int r = Add_nd(AL,*p);
        if (r < 0) {
            (void)EraseRange(AL,oldCount,AL->count);
            return r;
        }
        p++;
        n--;
    }
    AL->timestamp++;
    if (AL->Flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(AL,CCL_ADDRANGE,data,(void *)n);

    return 1;
}


/* Searches a StringList for a given data item
   Returns a positive integer if found, negative if the end is reached
*/
static int IndexOf_nd(const LIST_TYPE(DATA_TYPE) *l,const CHARTYPE *ElementToFind,void *ExtraArgs,size_t *result)
{
    LIST_ELEMENT(DATA_TYPE) *rvp;
    int r;
    size_t i=0;
    CompareFunction fn;
    CompareInfo ci;

    rvp = l->First;
    fn = l->Compare;
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = ExtraArgs;
    while (rvp) {
        r = fn(&rvp->Data,ElementToFind,&ci);
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

static int IndexOf(LIST_TYPE(DATA_TYPE) *l,CHARTYPE *ElementToFind,void *ExtraArgs,size_t *result)
{
    if (l == NULL || ElementToFind == NULL) {
        if (l)
            l->RaiseError("iStringList.IndexOf",CONTAINER_ERROR_BADARG);
        else
            NullPtrError("IndexOf");
        return CONTAINER_ERROR_BADARG;
    }
    return IndexOf_nd(l,ElementToFind,ExtraArgs,result);
}    

static int lcompar (const void *elem1, const void *elem2,CompareInfo *ExtraArgs)
{
    LIST_ELEMENT(DATA_TYPE) *Elem1 = *(LIST_ELEMENT(DATA_TYPE) **)elem1;
    LIST_ELEMENT(DATA_TYPE) *Elem2 = *(LIST_ELEMENT(DATA_TYPE) **)elem2;
    LIST_TYPE(DATA_TYPE) *l = (LIST_TYPE(DATA_TYPE) *)ExtraArgs->ContainerLeft;
    CompareFunction fn = l->Compare;
    return fn(Elem1->Data,Elem2->Data,ExtraArgs);
}

static int Sort(LIST_TYPE(DATA_TYPE) *l)
{
    LIST_ELEMENT(DATA_TYPE) **tab;
    size_t i;
    LIST_ELEMENT(DATA_TYPE) *rvp;
    CompareInfo ci;
    
    if (l == NULL) return NullPtrError("Sort");
    
    if (l->count < 2)
        return 1;
    if (l->Flags&CONTAINER_READONLY) {
        l->RaiseError("iStringList.Sort",CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    {
        size_t bytes;
        if (!SizeMul(l->count,sizeof(LIST_ELEMENT(DATA_TYPE) *),&bytes)) {
            l->RaiseError("iStringList.Sort",CONTAINER_ERROR_NOMEMORY);
            return CONTAINER_ERROR_NOMEMORY;
        }
        tab = l->Allocator->malloc(bytes);
    }
    if (tab == NULL) {
        l->RaiseError("iStringList.Sort",CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    rvp = l->First;
    for (i=0; i<l->count;i++) {
        tab[i] = rvp;
        rvp = rvp->Next;
    }
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    qsortEx(tab,l->count,sizeof(LIST_ELEMENT(DATA_TYPE) *),lcompar,&ci);
    for (i=0; i<l->count-1;i++) {
        tab[i]->Next = tab[i+1];
    }
    tab[l->count-1]->Next = NULL;
    l->Last = tab[l->count-1];
    l->First = tab[0];
    l->Allocator->free(tab);
    l->timestamp++;
    return 1;

}
static int Apply(LIST_TYPE(DATA_TYPE) *L,int (Applyfn)(CHARTYPE *,void *),void *arg)
{
    LIST_ELEMENT(DATA_TYPE) *le;
    size_t slen,bufsiz=256,bytes;
    CHARTYPE *pElem=NULL;

    if (L == NULL || Applyfn == NULL) {
        if (L)
            L->RaiseError("iStringList.Apply",CONTAINER_ERROR_BADARG);
        else
            iError.RaiseError("iStringList.Apply",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    le = L->First;
    if (L->Flags&CONTAINER_READONLY) {
        if (!StringBytes(bufsiz+1,&bytes))
            return CONTAINER_ERROR_NOMEMORY;
        pElem = L->Allocator->malloc(bytes);
        if (pElem == NULL) {
            L->RaiseError("iStringList.Apply",CONTAINER_ERROR_NOMEMORY);
            return CONTAINER_ERROR_NOMEMORY;
        }
    }
    while (le) {
        if (pElem) {
        	slen = STRLEN(le->Data);
        	if (slen >= bufsiz) {
        		L->Allocator->free(pElem);
                if (!SizeAdd(slen,20,&bufsiz) || !StringBytes(bufsiz,&bytes)) {
                    L->RaiseError("iStringList.Apply",CONTAINER_ERROR_NOMEMORY);
                    return CONTAINER_ERROR_NOMEMORY;
                }
                pElem = L->Allocator->malloc(bytes);
        		if (pElem == NULL) {
        			L->RaiseError("iStringList.Apply",CONTAINER_ERROR_NOMEMORY);
        			return CONTAINER_ERROR_NOMEMORY;
        		}
        	}
            STRCPY(pElem,le->Data);
            Applyfn(pElem,arg);
        }
        else Applyfn(le->Data,arg);
        le = le->Next;
    }
    if (pElem)
        L->Allocator->free(pElem);
    return 1;
}

static ErrorFunction SetErrorFunction(LIST_TYPE(DATA_TYPE) *l,ErrorFunction fn)
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

static size_t Sizeof(LIST_TYPE(DATA_TYPE) *l)
{
    size_t sum=0,i,chars,bytes,nodebytes;
    LIST_ELEMENT(DATA_TYPE) *rvp;
    if (l == NULL) {
        return sizeof(LIST_TYPE(DATA_TYPE));
    }

    rvp=l->First;
    for (i=0; i<l->count;i++) {
        chars = STRLEN(rvp->Data);
        if (!SizeAdd(chars,1,&chars) || !StringBytes(chars,&bytes) ||
            !SizeAdd(offsetof(LIST_ELEMENT(DATA_TYPE),Data),bytes,&nodebytes) ||
            !SizeAdd(sum,nodebytes,&sum))
            return 0;
        rvp = rvp->Next;
    }
    if (!SizeAdd(sizeof(LIST_TYPE(DATA_TYPE)),sum,&sum))
        return 0;
    return sum;
}

static int UseHeap(LIST_TYPE(DATA_TYPE) *L, ContainerAllocator *m)
{
    if (L == NULL)
        return NullPtrError("UseHeap");
    (void)m;
    L->RaiseError("iStringList.UseHeap",CONTAINER_ERROR_NOTIMPLEMENTED);
    return CONTAINER_ERROR_NOTIMPLEMENTED;
}

/* ------------------------------------------------------------------------------ */
/*                                Iterators                                       */
/* ------------------------------------------------------------------------------ */
static void *Seek(Iterator *it,size_t idx)
{
    struct ITERATOR(DATA_TYPE) *li = (struct ITERATOR(DATA_TYPE) *)it;
    LIST_ELEMENT(DATA_TYPE) *rvp;

    if (it == NULL) {
        NullPtrError("Seek");
        return NULL;
    }
	if (li->Magic != STRINGLIST_MAGIC_NUMBER) {
		iError.RaiseError("StringList.Seek",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
    if (li->timestamp != li->L->timestamp) {
        li->L->RaiseError("StringList.Seek",CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (li->L->count == 0)
        return NULL;
    rvp = li->L->First;
    if (idx == 0) {
        li->index = 0;
        li->Current = li->L->First;
    }
    else if (idx >= li->L->count-1) {
        li->index = li->L->count-1;
        li->Current = li->L->Last;
    }
    else {
        li->index = idx;
        while (idx > 0) {
            rvp = rvp->Next;
            idx--;
        }
        li->Current = rvp;
    }
    return li->Current;
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
static void *GetNext(Iterator *it)
{
    struct ITERATOR(DATA_TYPE) *li = (struct ITERATOR(DATA_TYPE) *)it;
    LIST_TYPE(DATA_TYPE) *L;
    void *result;


    if (li == NULL) {
        NullPtrError("GetNext");
        return NULL;
    }
	if (li->Magic != STRINGLIST_MAGIC_NUMBER) {
		iError.RaiseError("StringList.GetNext",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
    }
    L = li->L;
    if (li->timestamp != L->timestamp) {
        L->RaiseError("GetNext",CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    if (li->L->count == 0)
        return NULL;
    if (li->index >= (L->count-1) || li->Current == NULL)
        return NULL;
    li->Current = li->Current->Next;
    li->index++;
    if (L->Flags & CONTAINER_READONLY) {
        return CopyToIteratorBuffer(li,li->Current->Data);
    }
    result = li->Current->Data;
    return result;
}
static size_t GetPosition(Iterator *it)
{
	struct ITERATOR(DATA_TYPE) *li = (struct ITERATOR(DATA_TYPE) *) it;
	if (li == NULL)
	    return (size_t)NullPtrError("GetPosition");
	if (li->Magic != STRINGLIST_MAGIC_NUMBER) {
		iError.RaiseError("StringList.GetPosition",CONTAINER_ERROR_WRONG_ITERATOR);
		return (size_t)-1;
	}
	return li->index;
}


static void *GetPrevious(Iterator *it)
{
	struct ITERATOR(DATA_TYPE) *li = (struct ITERATOR(DATA_TYPE) *)it;
	LIST_TYPE(DATA_TYPE) *L;
	LIST_ELEMENT(DATA_TYPE) *rvp;
	size_t i;

	if (li == NULL) {
	    NullPtrError("GetPrevious");
	    return NULL;
	}
	if (li->Magic != STRINGLIST_MAGIC_NUMBER) {
		iError.RaiseError("StringList.GetPrevious",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
	L = li->L;
	if (li->timestamp != L->timestamp) {
	    L->RaiseError("GetPrevious",CONTAINER_ERROR_OBJECT_CHANGED);
	    return NULL;
	}
	if (li->L->count == 0)
	    return NULL;
	if (li->index >= L->count || li->index == 0)
	    return NULL;
	rvp = L->First;
	i=0;
	li->index--;
	if (li->index > 0) {
	    while (rvp && i < li->index) {
	        rvp = rvp->Next;
	        i++;
	    }
	}
	li->Current = rvp;
	if (rvp == NULL) return NULL;
	if (rvp && (L->Flags & CONTAINER_READONLY)) {
	    return CopyToIteratorBuffer(li,li->Current->Data);
	}
	return rvp->Data;
}

static void *GetCurrent(Iterator *it)
{
	struct ITERATOR(DATA_TYPE) *li = (struct ITERATOR(DATA_TYPE) *)it;

	if (li == NULL) {
	    NullPtrError("GetCurrent");
	    return NULL;
	}
	if (li->Magic != STRINGLIST_MAGIC_NUMBER) {
		iError.RaiseError("StringList.GetCurrent",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
	if (li->index == (size_t)-1) {
	    li->L->RaiseError("GetCurrent",CONTAINER_ERROR_BADARG);
	    return NULL;
	}
	if (li->timestamp != li->L->timestamp) {
	    li->L->RaiseError("GetCurrent",CONTAINER_ERROR_OBJECT_CHANGED);
    return NULL;
	}
	if (li->L->count == 0)
	    return NULL;
	if (li->Current == NULL)
	    return NULL;
	if (li->L->Flags & CONTAINER_READONLY) {
	    return CopyToIteratorBuffer(li,li->Current->Data);
	}
	return li->Current->Data;
}
static int ReplaceWithIterator(Iterator *it, void *data,int direction) 
{
	struct ITERATOR(DATA_TYPE) *li = (struct ITERATOR(DATA_TYPE) *)it;
	int result;
	size_t pos;
	
	if (it == NULL) {
	    return NullPtrError("Replace");
	}
	if (li->Magic != STRINGLIST_MAGIC_NUMBER) {
		iError.RaiseError("StringList.ReplaceWith",CONTAINER_ERROR_WRONG_ITERATOR);
		return CONTAINER_ERROR_WRONG_ITERATOR;
	}
	if (li->L->count == 0)
	    return 0;
	if (li->Current == NULL || li->index >= li->L->count)
	    return CONTAINER_ERROR_BADARG;
	if (li->L->Flags & CONTAINER_READONLY) {
	    li->L->RaiseError("Replace",CONTAINER_ERROR_READONLY);
	    return CONTAINER_ERROR_READONLY;
	}    
	if (li->timestamp != li->L->timestamp) {
	    li->L->RaiseError("Replace",CONTAINER_ERROR_OBJECT_CHANGED);
	    return CONTAINER_ERROR_OBJECT_CHANGED;
	}
	pos = li->index;
	if (direction)
	    GetNext(it);
	else
	    GetPrevious(it);
	if (data == NULL)
	    result = RemoveAt_nd(li->L,pos);
	else {
	    result = ReplaceAt(li->L,pos,data);
	}
	if (result >= 0) {
	    li->timestamp = li->L->timestamp;
	}
	return result;
}

static void *GetFirst(Iterator *it)
{
	struct ITERATOR(DATA_TYPE) *li = (struct ITERATOR(DATA_TYPE) *)it;
	LIST_TYPE(DATA_TYPE) *L;


	if (li == NULL) {
	    NullPtrError("GetFirst");
	    return NULL;
	}
	if (li->Magic != STRINGLIST_MAGIC_NUMBER) {
		iError.RaiseError("StringList.GetFirst",CONTAINER_ERROR_WRONG_ITERATOR);
		return NULL;
	}
	L = li->L;
	if (li->timestamp != L->timestamp) {
	    L->RaiseError("iStringList.GetFirst",CONTAINER_ERROR_OBJECT_CHANGED);
	    return NULL;
	}
	if (L->count == 0)
	    return NULL;
	li->index = 0;
	li->Current = L->First;
	if (L->Flags & CONTAINER_READONLY) {
        return CopyToIteratorBuffer(li,L->First->Data);
	}
	return L->First->Data;
}

static Iterator *NewIterator(LIST_TYPE(DATA_TYPE) *L)
{
	struct ITERATOR(DATA_TYPE) *result;
	
	if (L == NULL) {
	    NullPtrError("NewIterator");
	    return NULL;
	}
	result = L->Allocator->malloc(sizeof(struct ITERATOR(DATA_TYPE)));
	if (result == NULL) {
	    L->RaiseError("iStringList.NewIterator",CONTAINER_ERROR_NOMEMORY);
	    return NULL;
	}
    memset(result,0,sizeof(*result));
    result->it.GetNext = GetNext;
    result->it.GetPrevious = GetPrevious;
    result->it.GetFirst = GetFirst;
    result->it.GetCurrent = GetCurrent;
    result->it.GetPosition = GetPosition;
    result->it.Seek = Seek;
    result->it.Replace = ReplaceWithIterator;
	result->L = L;
	result->Magic = STRINGLIST_MAGIC_NUMBER;
	result->timestamp = L->timestamp;
	result->index = (size_t)-1;
    result->Current = NULL;
    result->ElementBuffer = NULL;
    result->ownsStorage = 1;
	return &result->it;
}
static int InitIterator(LIST_TYPE(DATA_TYPE) *L,void *r)
{
	struct ITERATOR(DATA_TYPE) *result=(struct ITERATOR(DATA_TYPE) *)r;
	
	if (L == NULL) {
	    return sizeof(struct ITERATOR(DATA_TYPE));
	}
	if (result == NULL)
	    return NullPtrError("InitIterator");
	memset(result,0,sizeof(*result));
	result->it.GetNext = GetNext;
	result->it.GetPrevious = GetPrevious;
	result->it.GetFirst = GetFirst;
	result->it.GetCurrent = GetCurrent;
	result->it.GetPosition = GetPosition;
	result->it.Seek = Seek;
	result->it.Replace = ReplaceWithIterator;
	result->L = L;
	result->Magic = STRINGLIST_MAGIC_NUMBER;
	result->timestamp = L->timestamp;
	result->index = (size_t)-1;
	result->Current = NULL;
	result->ElementBuffer = NULL;
	result->ownsStorage = 0;
	return 1;
}
static int DeleteIterator(Iterator *it)
{
	struct ITERATOR(DATA_TYPE) *li;
	LIST_TYPE(DATA_TYPE) *L;

	if (it == NULL) {
	    return NullPtrError("DeleteIterator");
	}
	li = (struct ITERATOR(DATA_TYPE) *)it;
	if (li->Magic != STRINGLIST_MAGIC_NUMBER)
	    return iError.RaiseError("StringList.DeleteIterator",CONTAINER_ERROR_WRONG_ITERATOR),CONTAINER_ERROR_WRONG_ITERATOR;
	L = li->L;
	if (li->ElementBuffer)
	    L->Allocator->free(li->ElementBuffer);
	if (li->ownsStorage)
	    L->Allocator->free(it);
	return 1;
}

static int DefaultSaveFunction(const void *element,void *arg, FILE *Outfile)
{
    const CHARTYPE *str = element;
    size_t chars,bytes;
    (void)arg;
    chars = STRLEN(str);
    if (!SizeAdd(chars,1,&chars) || !StringBytes(chars-1,&bytes))
        return -1;
    return bytes == fwrite(str,1,bytes,Outfile);
}

static int Save(LIST_TYPE(DATA_TYPE) *L,FILE *stream, SaveFunction saveFn,void *arg)
{
    size_t i,chars,bytes;
    uint32_t magic = STRINGLIST_STREAM_MAGIC;
    uint32_t version = STRINGLIST_STREAM_VERSION;
    uint32_t encoding = STRINGLIST_STREAM_ENCODING;
    uint32_t unit = (uint32_t)sizeof(CHARTYPE);
    uint32_t flags;
    uint64_t count;
    LIST_ELEMENT(DATA_TYPE) *rvp;

	if (L == NULL) return NullPtrError("Save");

	if (stream == NULL) {
	    L->RaiseError("iStringList.Save",CONTAINER_ERROR_BADARG);
	    return CONTAINER_ERROR_BADARG;
	}
	if (saveFn == NULL) {
	    saveFn = DefaultSaveFunction;
	}

    if (L->count > UINT64_MAX)
        return CONTAINER_ERROR_FILE_WRITE;
    flags = (uint32_t)(L->Flags & (CONTAINER_READONLY|CONTAINER_HAS_OBSERVER));
    count = (uint64_t)L->count;
    if (fwrite(&StringListGuid,sizeof(guid),1,stream) != 1 ||
        fwrite(&magic,sizeof(magic),1,stream) != 1 ||
        fwrite(&version,sizeof(version),1,stream) != 1 ||
        fwrite(&encoding,sizeof(encoding),1,stream) != 1 ||
        fwrite(&unit,sizeof(unit),1,stream) != 1 ||
        fwrite(&flags,sizeof(flags),1,stream) != 1 ||
        fwrite(&count,sizeof(count),1,stream) != 1)
        return EOF;
    rvp = L->First;
    for (i=0; i< L->count; i++) {
        chars = STRLEN(rvp->Data);
        if (!StringBytes(chars,&bytes) || bytes > UINT64_MAX)
            return CONTAINER_ERROR_FILE_WRITE;
        {
            uint64_t payload = (uint64_t)bytes;
            if (fwrite(&payload,sizeof(payload),1,stream) != 1 ||
                saveFn(rvp->Data,arg,stream) <= 0)
                return EOF;
        }
        rvp = rvp->Next;
    }
    return 1;
}

static int ReadExact(FILE *stream,void *data,size_t size)
{
    return size == 0 || fread(data,1,size,stream) == size;
}

static LIST_TYPE(DATA_TYPE) *Load(FILE *stream, ReadFunction loadFn,void *arg)
{
    size_t i,chars,bytes;
    LIST_TYPE(DATA_TYPE) *result = NULL;
    CHARTYPE *buf = NULL;
    uint32_t magic,version,encoding,unit,flags;
    uint64_t count,payload;
    guid Guid;
    int r = CONTAINER_ERROR_FILE_READ;

    if (stream == NULL) {
        NullPtrError("Load");
        return NULL;
    }
    if (!ReadExact(stream,&Guid,sizeof(Guid))) {
        iError.RaiseError("iStringList.Load",CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (memcmp(&Guid,&StringListGuid,sizeof(guid)) != 0 ||
        !ReadExact(stream,&magic,sizeof(magic)) ||
        !ReadExact(stream,&version,sizeof(version)) ||
        !ReadExact(stream,&encoding,sizeof(encoding)) ||
        !ReadExact(stream,&unit,sizeof(unit)) ||
        !ReadExact(stream,&flags,sizeof(flags)) ||
        !ReadExact(stream,&count,sizeof(count)) ||
        magic != STRINGLIST_STREAM_MAGIC ||
        version != STRINGLIST_STREAM_VERSION ||
        encoding != STRINGLIST_STREAM_ENCODING ||
        unit != sizeof(CHARTYPE) ||
        count > SIZE_MAX) {
        iError.RaiseError("iStringList.Load",CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    result = Create();
    if (result == NULL)
        return NULL;
    for (i=0; i<(size_t)count; ++i) {
        if (!ReadExact(stream,&payload,sizeof(payload)) || payload > SIZE_MAX) {
            r = CONTAINER_ERROR_FILE_READ;
            goto err;
        }
        bytes = (size_t)payload;
        if (bytes % sizeof(CHARTYPE) != 0) {
            r = CONTAINER_ERROR_WRONGFILE;
            goto err;
        }
        chars = bytes/sizeof(CHARTYPE);
        if (!SizeAdd(chars,1,&chars) || !StringBytes(chars,&bytes)) {
            r = CONTAINER_ERROR_NOMEMORY;
            goto err;
        }
        {
            CHARTYPE *tmp = result->Allocator->realloc(buf,bytes);
            if (tmp == NULL) {
                r = CONTAINER_ERROR_NOMEMORY;
                goto err;
            }
            buf = tmp;
        }
        if (buf == NULL) {
            r = CONTAINER_ERROR_NOMEMORY;
            goto err;
        }
        if (loadFn) {
            if (loadFn(buf,arg,stream) <= 0) {
                r = CONTAINER_ERROR_FILE_READ;
                goto err;
            }
        } else if (!ReadExact(stream,buf,bytes-sizeof(CHARTYPE))) {
            r = CONTAINER_ERROR_FILE_READ;
            goto err;
        }
        buf[chars-1] = (CHARTYPE)0;
        if (Add_nd(result,buf) < 0) {
            r = CONTAINER_ERROR_NOMEMORY;
            goto err;
        }
    }
    result->Flags = flags & (CONTAINER_READONLY|CONTAINER_HAS_OBSERVER);
    if (buf)
        result->Allocator->free(buf);
    return result;
err:
    if (buf)
        result->Allocator->free(buf);
    if (result) {
        result->Flags &= ~CONTAINER_READONLY;
        Finalize(result);
    }
    iError.RaiseError("iStringList.Load",r);
    return NULL;
}

static size_t GetElementSize(LIST_TYPE(DATA_TYPE) *l)
{
	if (l) {
	    return l->ElementSize;
	}
	NullPtrError("GetElementSize");
	return 0;
}

/*------------------------------------------------------------------------
 Procedure:     Create ID:1
 Purpose:       Allocates a new StringList object header, initializes the
	            VTable field and the element size
 Input:         The size of the elements of the StringList.
 Output:        A pointer to the newly created StringList or NULL if
	            there is no memory.
 Errors:        If element size is smaller than zero an error
	            routine is called. If there is no memory result is
	            NULL.
 ------------------------------------------------------------------------*/
static LIST_TYPE(DATA_TYPE) *CreateWithAllocator(const ContainerAllocator *allocator)
{
    LIST_TYPE(DATA_TYPE) *result;

    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (allocator == NULL || allocator->malloc == NULL)
        return NULL;
    result = allocator->malloc(sizeof(LIST_TYPE(DATA_TYPE)));
	if (result == NULL) {
	    iError.RaiseError("iStringList.Create",CONTAINER_ERROR_NOMEMORY);
	    return NULL;
	}
	memset(result,0,sizeof(LIST_TYPE(DATA_TYPE)));
	result->VTable = &iSTRINGLIST(DATA_TYPE);
	result->Compare = DefaultStringListCompareFunction;
    result->RaiseError = iError.RaiseError;
    result->Allocator = allocator;
    result->ElementSize = sizeof(CHARTYPE);
    result->ownsStorage = 1;
    return result;
}

static LIST_TYPE(DATA_TYPE) *Create(void)
{
	return CreateWithAllocator(CurrentAllocator);
}

static LIST_TYPE(DATA_TYPE) *InitializeWith(size_t n,CHARTYPE **Data)
{
    LIST_TYPE(DATA_TYPE) *result = Create();
    size_t i;
    CHARTYPE **pData = Data;
    if (result == NULL)
        return result;
    if (n != 0 && Data == NULL) {
        Finalize(result);
        return NULL;
    }
    for (i=0; i<n; i++) {
        if (Add_nd(result,*pData) < 0) {
            Finalize(result);
            return NULL;
        }
	    pData++;
	}
	return result;
}

static LIST_TYPE(DATA_TYPE) *InitWithAllocator(LIST_TYPE(DATA_TYPE) *result,ContainerAllocator *allocator)
{
    if (result == NULL)
        return NULL;
    if (allocator == NULL)
        allocator = CurrentAllocator;
    memset(result,0,sizeof(LIST_TYPE(DATA_TYPE)));
	result->VTable = &iSTRINGLIST(DATA_TYPE);
	result->Compare = DefaultStringListCompareFunction;
    result->RaiseError = iError.RaiseError;
    result->Allocator = allocator;
    result->ElementSize = sizeof(CHARTYPE);
    result->ownsStorage = 0;
	return result;
}

static LIST_TYPE(DATA_TYPE) *Init(LIST_TYPE(DATA_TYPE) *result)
{
	return InitWithAllocator(result,CurrentAllocator);
}

static const ContainerAllocator *GetAllocator(LIST_TYPE(DATA_TYPE) *l)
{
	if (l == NULL)
	    return NULL;
	return l->Allocator;
}

static DestructorFunction SetDestructor(LIST_TYPE(DATA_TYPE) *cb,DestructorFunction fn)
{
	DestructorFunction oldfn;
	if (cb == NULL)
	    return NULL;
	oldfn = cb->DestructorFn;
	if (fn)
	    cb->DestructorFn = fn;
	return oldfn;
}
static size_t SizeofIterator(LIST_TYPE(DATA_TYPE) *l)
{
    (void)l;
    return sizeof(struct ITERATOR(DATA_TYPE));
}

static int Select(LIST_TYPE(DATA_TYPE) *src,const Mask *m)
{
    size_t i,kept=0;
    LIST_ELEMENT(DATA_TYPE) *dst = NULL,*current,*next;

	if (src == NULL || m == NULL) {
	    return NullPtrError("Select");
	}
	if (src->Flags & CONTAINER_READONLY)
	    return ErrorReadOnly(src,"Select");
	if (m->length != src->count) {
	    iError.RaiseError("Select",CONTAINER_ERROR_BADMASK,src,m);
	    return CONTAINER_ERROR_BADMASK;
	}
	if (src->count == 0) return 0;
    current = src->First;
    for (i=0; i<m->length && current; ++i) {
        next = current->Next;
        if (m->data[i]) {
            if (dst)
                dst->Next = current;
            else
                src->First = current;
            dst = current;
            ++kept;
        } else {
            ReleaseLink(src,current);
        }
        current = next;
    }
    if (dst)
        dst->Next = NULL;
    src->Last = dst;
    src->count = kept;
    if (!dst)
        src->First = NULL;
    src->timestamp++;
    return 1;
}


static LIST_TYPE(DATA_TYPE) *SelectCopy(const LIST_TYPE(DATA_TYPE) *src,const Mask *m)
{
	LIST_TYPE(DATA_TYPE) *result;
	LIST_ELEMENT(DATA_TYPE) *rvp;
	size_t i;
	int r;

	if (src == NULL || m == NULL) {
	    NullPtrError("SelectCopy");
	    return NULL;
	}
	if (m->length != src->count) {
	    iError.RaiseError("SelectCopy",CONTAINER_ERROR_INCOMPATIBLE,src,m);
	    return NULL;
	}
    result = CreateWithAllocator(src->Allocator);
    if (result == NULL) return NULL;
    result->Flags = src->Flags;
    result->Compare = src->Compare;
    result->RaiseError = src->RaiseError;
    result->ElementSize = src->ElementSize;
    result->DestructorFn = src->DestructorFn;
	rvp = src->First;
	for (i=0; i<m->length;i++) {
	    if (m->data[i]) {
	        r = Add_nd(result,rvp->Data);
	        if (r < 0) {
            result->Flags &= ~CONTAINER_READONLY;
            Finalize(result);
	            return NULL;
	        }
	    }
	    rvp = rvp->Next;
	}
	return result;
}


static LIST_ELEMENT(DATA_TYPE) *FirstElement(LIST_TYPE(DATA_TYPE) *l)
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

static LIST_ELEMENT(DATA_TYPE) *LastElement(LIST_TYPE(DATA_TYPE) *l)
{
	if (l == NULL) {
	    NullPtrError("FirstElement");
	    return NULL;
	}
	if (l->Flags&CONTAINER_READONLY) {
	    ErrorReadOnly(l,"FirstElement");
	    return NULL;
	}
	return l->Last;
}

static LIST_ELEMENT(DATA_TYPE) *NextElement(LIST_ELEMENT(DATA_TYPE) *le)
{
	if (le == NULL) return NULL;
	return le->Next;
}

static void *ElementData(LIST_ELEMENT(DATA_TYPE) *le)
{
	if (le == NULL) return NULL;
	return le->Data;
}

static int SetElementData(LIST_TYPE(DATA_TYPE) *l,LIST_ELEMENT(DATA_TYPE) **pple,const CHARTYPE *data)
{
    LIST_ELEMENT(DATA_TYPE) *newle,*le,*rvp,*previous = NULL;
    if (l == NULL || pple == NULL || data == NULL) {
        return iError.NullPtrError("iList.SetElementData");
	}
	if (l->Flags&CONTAINER_READONLY) {
	    return ErrorReadOnly(l,"SetElementData");
	}
    le = *pple;
    if (le == NULL)
        return CONTAINER_ERROR_WRONGELEMENT;
    rvp = l->First;
    while (rvp && rvp != le) {
        previous = rvp;
        rvp = rvp->Next;
    }
    if (rvp == NULL)
        return CONTAINER_ERROR_WRONGELEMENT;
    newle = NewLink(l,data,"iStringList.SetElementData");
    if (newle == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    newle->Next = le->Next;
    if (previous)
        previous->Next = newle;
    else
        l->First = newle;
    if (l->Last == le)
        l->Last = newle;
    ReleaseLink(l,le);
    l->timestamp++;
	*pple = newle;
	return 1;
}

static void *Advance(LIST_ELEMENT(DATA_TYPE) **ple)
{
	LIST_ELEMENT(DATA_TYPE) *le;
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

static LIST_ELEMENT(DATA_TYPE) *Skip(LIST_ELEMENT(DATA_TYPE) *le, size_t n)
{
	if (le == NULL) return NULL;
	while (le != NULL && n > 0) {
	    le = le->Next;
	    n--;
	}
	return le;
}

static LIST_TYPE(DATA_TYPE) *SplitAfter(LIST_TYPE(DATA_TYPE) *l, LIST_ELEMENT(DATA_TYPE) *pt)
{
	LIST_ELEMENT(DATA_TYPE) *pNext;
	LIST_TYPE(DATA_TYPE) *result;    
	size_t count=0;

    if (pt == NULL || l == NULL) {
	    iError.NullPtrError("iList.SplitAfter");
        return NULL;
    }
    if (l->Flags & CONTAINER_READONLY)
        return ErrorReadOnly(l,"SplitAfter"),NULL;
    {
        LIST_ELEMENT(DATA_TYPE) *check = l->First;
        while (check && check != pt)
            check = check->Next;
        if (check == NULL)
            return l->RaiseError("iStringList.SplitAfter",CONTAINER_ERROR_WRONGELEMENT),NULL;
    }
	pNext = pt->Next;
	if (pNext == NULL) return NULL;
	result = CreateWithAllocator(l->Allocator);
	if (result == NULL) return NULL;
	result->First = pNext;
	while (pNext) {
	    count++;
	    if (pNext->Next == NULL) result->Last = pNext;
	    pNext = pNext->Next;
	}
    result->count = count;
    result->Compare = l->Compare;
    result->RaiseError = l->RaiseError;
    result->ElementSize = l->ElementSize;
    result->DestructorFn = l->DestructorFn;
	pt->Next = NULL;
	l->Last = pt;
	l->count -= count;
	l->timestamp++;
	return result;
}
INTERFACE(DATA_TYPE) iSTRINGLIST(DATA_TYPE) = {
	Size,
	GetFlags,
	SetFlags,
	Clear,
	Contains,
	Erase,
	Finalize,
	Apply,
	Equal,
	Copy,
	SetErrorFunction,
	Sizeof,
	NewIterator,
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
	DefaultStringListCompareFunction,
	UseHeap,
	AddRange,
	Create,
	CreateWithAllocator,
	Init,
	InitWithAllocator,
	SetAllocator,
	InitIterator,
	GetAllocator,
	SetDestructor,
	InitializeWith,
	Back,
	Front,
	Select,
	SelectCopy,
	FirstElement,
	LastElement,
	NextElement,
	ElementData,
	SetElementData,
	Advance,
	Skip,
	SplitAfter,
};
