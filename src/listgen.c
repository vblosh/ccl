/*
 * Value types generic list routines sample implementation 
 * ----------------------------------- ------------------
 * This routines handle the List container class. This is a very general
 * implementation and efficiency considerations aren't yet primordial. Lists
 * can have elements of any size. This implement single linked Lists. The
 * design goals here are just correctness and showing how the implementation
 * of the proposed interface COULD be done.
 * ----------------------------------------------------------------------
 */

#define LISTGEN_IMPLEMENTATION
#include "listgen.h"
#undef LISTGEN_IMPLEMENTATION
#include "ccl_internal.h"

/* Forward declarations */
static LIST_TYPE *SetVTable(LIST_TYPE *result);
static LIST_TYPE * Create(void);
static LIST_TYPE *CreateWithAllocator(const ContainerAllocator * allocator);
static int Finalize(LIST_TYPE *l);
static int EraseRange(LIST_TYPE *l, size_t start, size_t end);
static int InsertIn(LIST_TYPE *l, size_t idx, LIST_TYPE *newData);
static LIST_TYPE *GetRange(const LIST_TYPE *l, size_t start, size_t end);
static LIST_TYPE *SplitAfter(LIST_TYPE *l, LIST_ELEMENT *pt);
#define CONTAINER_LIST_SMALL    2
#define CHUNK_SIZE    1000

static int DefaultListCompareFunction(const void * left, const void * right, CompareInfo * ExtraArgs)
{
    (void)ExtraArgs;
    return memcmp(left, right, sizeof(DATA_TYPE));
}

/*------------------------------------------------------------------------
 Procedure:     Contains ID:1
 Purpose:       Determines if the given data is in the container
 Input:         The list and the data to be searched
 Output:        Returns 1 (true) if the data is in there, false
                otherwise
 Errors:        The same as the function IndexOf
------------------------------------------------------------------------*/
static int Contains(const LIST_TYPE * l, const DATA_TYPE data)
{
    size_t idx;
    return (iList.IndexOf((List *)l, &data, NULL, &idx) < 0) ? 0 : 1;
}

static int Add(LIST_TYPE * l, const DATA_TYPE elem)
{
    return iList.Add((List *)l,&elem);
}

static int EraseMatching(LIST_TYPE *l, DATA_TYPE elem, int all)
{
    LIST_ELEMENT *current;
    LIST_ELEMENT *previous = NULL;
    int result = CONTAINER_ERROR_NOTFOUND;
    CompareInfo ci;

    if (l == NULL)
        return iError.NullPtrError("Erase");
    if (l->Flags & CONTAINER_READONLY)
        return (l->RaiseError("iList.Erase", CONTAINER_ERROR_READONLY),
                CONTAINER_ERROR_READONLY);
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    current = l->First;
    while (current != NULL) {
        LIST_ELEMENT *next = current->Next;
        if (l->Compare(&current->Data, &elem, &ci) == 0) {
            if (previous == NULL)
                l->First = next;
            else
                previous->Next = next;
            if (l->Last == current)
                l->Last = previous;
            if (l->DestructorFn)
                l->DestructorFn(&current->Data);
            if (l->Heap)
                iHeap.FreeObject(l->Heap, current);
            else
                l->Allocator->free(current);
            --l->count;
            ++l->timestamp;
            result = 1;
            if (!all)
                break;
        } else {
            previous = current;
        }
        current = next;
    }
    if (l->count == 0)
        l->First = l->Last = NULL;
    return result;
}

static int CopyElement(const LIST_TYPE * l, size_t position, DATA_TYPE *outBuffer)
{
    return iList.CopyElement((List *)l,position,outBuffer);
}

static int ReplaceAt(LIST_TYPE * l, size_t position, const DATA_TYPE data)
{
    return iList.ReplaceAt((List *)l,position,&data);;
}

static int PushFront(LIST_TYPE * l, const DATA_TYPE pdata)
{
    return iList.PushFront((List *)l,&pdata);
}


static int PopFront(LIST_TYPE * l, DATA_TYPE *result)
{
    return iList.PopFront((List *)l,result);
}


static int InsertAt(LIST_TYPE * l, size_t pos, const DATA_TYPE pdata)
{
    return iList.InsertAt((List *)l,pos,&pdata);
}

static int Erase(LIST_TYPE * l, const DATA_TYPE elem)
{
    return EraseMatching(l, elem, 0);
}

static int EraseAll(LIST_TYPE * l, const DATA_TYPE elem)
{
    return EraseMatching(l, elem, 1);
}

static int IndexOf(const LIST_TYPE * l, const DATA_TYPE ElementToFind, void *ExtraArgs, size_t * result)
{
    return iList.IndexOf((List *)l, &ElementToFind, ExtraArgs, result);
}

static size_t Sizeof(const LIST_TYPE * l)
{
    if (l == NULL) {
        return sizeof(List);
    }
    return sizeof(LIST_TYPE) + l->ElementSize * l->count + l->count * offsetof(LIST_ELEMENT,Data);
}

static size_t SizeofIterator(const LIST_TYPE * l)
{
    (void)l;
    return sizeof(struct ITERATOR(DATA_TYPE));
}

static LIST_TYPE *Load(FILE * stream, ReadFunction loadFn, void *arg)
{
    LIST_TYPE *result = (LIST_TYPE *)iList.Load(stream,loadFn,arg);
    if (result == NULL)
        return NULL;
    if (result->ElementSize != sizeof(DATA_TYPE)) {
        /* Load returns an ordinary generic list.  Keep its generic vtable
         * while disposing of a file whose scalar width is not ours. */
        iList.Finalize((List *)result);
        return NULL;
    }
    result->Compare = DefaultListCompareFunction;
    return SetVTable(result);
}

/*
 * ---------------------------------------------------------------------------
 *
 *                           Iterators
 *
 * ---------------------------------------------------------------------------
 */
static Iterator *NewIterator(LIST_TYPE * L)
{
    struct ITERATOR(DATA_TYPE) *result;
    if (L == NULL)
        return NULL;
    result = (struct ITERATOR(DATA_TYPE) *)L->Allocator->malloc(sizeof(*result));
    if (result == NULL) {
        L->RaiseError("iList.NewIterator", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if (iList.InitIterator((List *)L, result) <= 0) {
        L->Allocator->free(result);
        return NULL;
    }
    /* Generic InitIterator also serves caller-provided placement storage;
     * mark this generator-owned allocation so iList.DeleteIterator can
     * release it without ever freeing placement buffers. */
    result->Previous = (LIST_ELEMENT *)(uintptr_t)1;
    return &result->it;
}
static int InitIterator(LIST_TYPE * L, void *r)
{
    if (L == NULL)
        return (int)sizeof(struct ITERATOR(DATA_TYPE));
    if (r == NULL)
        return iError.NullPtrError("InitIterator");
    return iList.InitIterator((List *)L,r);
}
static size_t GetElementSize(const LIST_TYPE * l)
{
    (void)l;
    return sizeof(DATA_TYPE);
}

static LIST_TYPE  *Copy(const LIST_TYPE * l)
{
    LIST_TYPE *result = (LIST_TYPE *)iList.Copy((List *)l);
    return SetVTable(result);
}


static LIST_TYPE *SelectCopy(const LIST_TYPE *l,const Mask *m)
{
    LIST_TYPE *result = (LIST_TYPE *)iList.SelectCopy((const List *)l,m);
    if (result != NULL && l != NULL)
        result->Compare = l->Compare;
    return SetVTable(result);
}

/*---------------------------------------------------------------------------*/
/* qsort() - perform a quicksort on an array                                 */
/*---------------------------------------------------------------------------*/
#define CUTOFF 8

static void shortsort(const LIST_TYPE *l, LIST_ELEMENT **lo, LIST_ELEMENT **hi);
#define swap(a,b) { LIST_ELEMENT *tmp = *a; *a = *b; *b = tmp; }

static int CompareElements(const LIST_TYPE *l, const LIST_ELEMENT *left,
                           const LIST_ELEMENT *right)
{
    CompareInfo ci;
    ci.ContainerLeft = l;
    ci.ContainerRight = NULL;
    ci.ExtraArgs = NULL;
    if (l->Compare == DefaultListCompareFunction) {
        const DATA_TYPE a = left->Data;
        const DATA_TYPE b = right->Data;
        return a > b ? 1 : a < b ? -1 : 0;
    }
    return l->Compare(&left->Data, &right->Data, &ci);
}

static void QSORT(const LIST_TYPE *l, LIST_ELEMENT **base, size_t num)
{
  LIST_ELEMENT **lo, **hi, **mid;
  LIST_ELEMENT **loguy, **higuy;
  size_t size;
  LIST_ELEMENT **lostk[30], **histk[30];
  int stkptr;

  if (num < 2) return;
  stkptr = 0;

  lo = base;
  hi = base + (num - 1);

recurse:
  size = (hi - lo) + 1;

  if (size <= CUTOFF) {
    shortsort(l, lo, hi);
  }
  else {
    mid = lo + (size / 2);
    swap(mid, lo);

    loguy = lo;
    higuy = hi + 1;

    for (;;) {
      /* Changed <= to < and >= to > according to the advise of "pete" of comp.lang.c. */
      do { loguy++; } while (loguy <= hi &&
                              CompareElements(l, *loguy, *lo) < 0);
      do { higuy--; } while (higuy > lo &&
                              CompareElements(l, *higuy, *lo) > 0);
      if (higuy < loguy) break;
      swap(loguy, higuy);
    }

    swap(lo, higuy);

    if (higuy - 1 - lo >= hi - loguy) {
      if (lo + 1 < higuy) {
        lostk[stkptr] = lo;
        histk[stkptr] = (higuy - 1);
        ++stkptr;
      }

      if (loguy < hi) {
        lo = loguy;
        goto recurse;
      }
    }
    else {
      if (loguy < hi) {
        lostk[stkptr] = loguy;
        histk[stkptr] = hi;
        ++stkptr;
      }

      if (lo + 1 < higuy) {
        hi = higuy - 1;
        goto recurse;
      }
    }
  }

  --stkptr;
  if (stkptr >= 0) {
    lo = lostk[stkptr];
    hi = histk[stkptr];
    goto recurse;
  }
}

static void shortsort(const LIST_TYPE *l, LIST_ELEMENT **lo, LIST_ELEMENT **hi)
{
  LIST_ELEMENT **p, **max;

  while (hi > lo) 
  {
    max = lo;
    for (p = (lo+1); p <= hi; p++)
        if (CompareElements(l, *p, *max) > 0) max = p;
    swap(max, hi);
    hi--;
  }
}

static int Sort(LIST_TYPE * l)
{
    LIST_ELEMENT   **tab;
    size_t          i;
    LIST_ELEMENT    *rvp;

    if (l == NULL)
        return iError.NullPtrError("Sort");

    if (l->count < 2)
        return 1;
    if (l->Flags & CONTAINER_READONLY) {
        l->RaiseError("iList.Sort", CONTAINER_ERROR_READONLY);
        return CONTAINER_ERROR_READONLY;
    }
    tab = l->Allocator->malloc(l->count * sizeof(LIST_ELEMENT *));
    if (tab == NULL) {
        l->RaiseError("iList.Sort", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    rvp = l->First;
    for (i = 0; i < l->count; i++) {
        tab[i] = rvp;
        rvp = rvp->Next;
    }
    QSORT(l, tab, l->count );
    for (i = 0; i < l->count - 1; i++) {
        tab[i]->Next = tab[i + 1];
    }
    tab[l->count - 1]->Next = NULL;
    l->Last = tab[l->count - 1];
    l->First = tab[0];
    l->timestamp++;
    l->Allocator->free(tab);
    return 1;

}

/* The generic finalizer assumes that subclass vtables are heap-owned.  The
 * generated scalar interfaces are static globals, so temporarily restore the
 * generic vtable while it releases the list storage. */
static int Finalize(LIST_TYPE *l)
{
    int result;
    if (l == NULL)
        return iList.Finalize(NULL);
    l->VTable = (INTERFACE(DATA_TYPE) *)&iList;
    result = iList.Finalize((List *)l);
    return result;
}

/* EraseRange predates RemoveRange and is part of the typed ABI.  Implement it
 * in terms of EraseAt so heap-backed lists, destructors, endpoints, and
 * timestamps all follow the same ownership path as other scalar erases.
 * The range is half-open, [start, end), like RemoveRange. */
static int EraseRange(LIST_TYPE *l, size_t start, size_t end)
{
    size_t i;
    if (l == NULL)
        return iError.NullPtrError("EraseRange");
    if (l->Flags & CONTAINER_READONLY)
        return (iError.RaiseError("iList.EraseRange", CONTAINER_ERROR_READONLY),
                CONTAINER_ERROR_READONLY);
    if (start >= l->count)
        return 0;
    if (end > l->count)
        end = l->count;
    if (end < start) {
        i = start;
        start = end;
        end = i;
    }
    if (start == end)
        return 0;
    for (i = start; i < end; ++i) {
        int result = iList.EraseAt((List *)l, start);
        if (result < 0)
            return result;
    }
    return 1;
}

static int InsertIn(LIST_TYPE *l, size_t idx, LIST_TYPE *newData)
{
    if (l != NULL && newData != NULL && l->Allocator != newData->Allocator) {
        l->RaiseError("iList.InsertIn", CONTAINER_ERROR_INCOMPATIBLE);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    return iList.InsertIn((List *)l, idx, (List *)newData);
}

static LIST_TYPE *GetRange(const LIST_TYPE *l, size_t start, size_t end)
{
    LIST_TYPE *result;
    LIST_ELEMENT *element;
    size_t count;
    size_t position;
    if (l == NULL)
        return NULL;
    if (l->count != 0 && (start >= end || start >= l->count))
        return NULL;
    result = CreateWithAllocator(l->Allocator);
    if (result == NULL)
        return NULL;
    result->Compare = l->Compare;
    if (l->count == 0)
        return result;
    if (end > l->count)
        end = l->count;
    count = end - start;
    position = start;
    element = l->First;
    while (position > 0 && element != NULL) {
        element = element->Next;
        --position;
    }
    while (count > 0 && element != NULL) {
        if (iList.Add((List *)result, &element->Data) < 0) {
            Finalize(result);
            return NULL;
        }
        element = element->Next;
        --count;
    }
    return result;
}

static LIST_TYPE *SplitAfter(LIST_TYPE *l, LIST_ELEMENT *pt)
{
    LIST_TYPE *result = (LIST_TYPE *)iList.SplitAfter((List *)l,
                                                      (ListElement *)pt);
    if (result != NULL) {
        result->Compare = l->Compare;
        SetVTable(result);
    }
    return result;
}

static LIST_TYPE *SetVTable(LIST_TYPE *result)
{
    static int Initialized;
    INTERFACE(DATA_TYPE) *intface = &INTERFACE_NAME(DATA_TYPE);
    
    if (result == NULL)
        return NULL;
    result->VTable = intface;
    if (Initialized) return result;
    Initialized = 1;
    intface->FirstElement = (LIST_ELEMENT *(*)(LIST_TYPE *))iList.FirstElement;
    intface->LastElement = (LIST_ELEMENT *(*)(LIST_TYPE *))iList.LastElement;
    intface->GetElement = (DATA_TYPE *(*)(const LIST_TYPE *,size_t))iList.GetElement;
    intface->Clear = (int (*)(LIST_TYPE *))iList.Clear;
    intface->EraseAt = (int (*)(LIST_TYPE *,size_t))iList.EraseAt;
    intface->EraseRange = EraseRange;
    intface->RemoveRange = (int (*)(LIST_TYPE *,size_t,size_t))iList.RemoveRange;
    intface->Select = (int (*)(LIST_TYPE *,const Mask *))iList.Select;
    intface->SetFlags = (unsigned (*)(LIST_TYPE *,unsigned))iList.SetFlags;
    intface->GetFlags = (unsigned (*)(const LIST_TYPE *))iList.GetFlags;
    intface->SetDestructor = (DestructorFunction (*)(LIST_TYPE *, DestructorFunction))iList.SetDestructor;
    intface->Apply = (int (*)(LIST_TYPE *, int (Applyfn) (DATA_TYPE *, void * ), void *))iList.Apply;
    intface->Reverse = (int (*)(LIST_TYPE *))iList.Reverse;
    intface->SetCompareFunction = (CompareFunction (*)(LIST_TYPE *, CompareFunction ))iList.SetCompareFunction;
    intface->GetRange = GetRange;
    intface->Skip = (LIST_ELEMENT *(*)(LIST_ELEMENT *, size_t))iList.Skip;
    intface->Append = (int (*)(LIST_TYPE *, LIST_TYPE *))iList.Append;
    intface->Equal = (int (*)(const LIST_TYPE *, const LIST_TYPE *))iList.Equal;
    intface->InsertIn = InsertIn;
    intface->AddRange = (int (*)(LIST_TYPE *, size_t, const DATA_TYPE *))iList.AddRange;
    intface->SetErrorFunction = (ErrorFunction (*)(LIST_TYPE *, ErrorFunction))iList.SetErrorFunction;
    intface->SetFlags = (unsigned (*)(LIST_TYPE * l, unsigned newval))iList.SetFlags;
    intface->UseHeap = (int (*)(LIST_TYPE *, const ContainerAllocator *))iList.UseHeap;
    intface->GetHeap = (ContainerHeap *(*)(const LIST_TYPE *))iList.GetHeap;
    intface->RotateLeft = (int (*)(LIST_TYPE *, size_t))iList.RotateLeft;
    intface->RotateRight = (int (*)(LIST_TYPE *, size_t))iList.RotateRight;
    intface->Save = (int (*)(const LIST_TYPE *, FILE *, SaveFunction, void *))iList.Save;
    intface->Size = (size_t (*)(const LIST_TYPE *))iList.Size;
    intface->DeleteIterator = (int (*)(Iterator *))iList.DeleteIterator;
    intface->SplitAfter = SplitAfter;
    intface->Back = (DATA_TYPE *(*)(const LIST_TYPE *))iList.Back;
    intface->Front = (DATA_TYPE *(*)(const LIST_TYPE *))iList.Front;
    intface->Finalize = Finalize;
    return result;
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
static LIST_TYPE *CreateWithAllocator(const ContainerAllocator * allocator)
{
    if (allocator == NULL)
        allocator = CurrentAllocator;
    LIST_TYPE *result =  (LIST_TYPE *)iList.CreateWithAllocator(sizeof(DATA_TYPE), allocator);
    if (result != NULL)
        result->Compare = DefaultListCompareFunction;
    return SetVTable(result);
}

static LIST_TYPE * Create(void)
{
    LIST_TYPE *result =  (LIST_TYPE *)iList.CreateWithAllocator(sizeof(DATA_TYPE), CurrentAllocator);
    if (result != NULL)
        result->Compare = DefaultListCompareFunction;
    return SetVTable(result);
}

static LIST_TYPE *InitializeWith(size_t n, const void *Data)
{
    LIST_TYPE *result = (LIST_TYPE *)iList.InitializeWith(sizeof(DATA_TYPE),n,Data);
    if (result != NULL)
        result->Compare = DefaultListCompareFunction;
    return SetVTable(result);
}

static LIST_TYPE *InitWithAllocator(LIST_TYPE * result, const ContainerAllocator * allocator)
{
    if (result == NULL)
        return NULL;
    if (allocator == NULL)
        allocator = CurrentAllocator;
    result = (LIST_TYPE *)iList.InitWithAllocator((List *)result,sizeof(DATA_TYPE),allocator);
    if (result != NULL)
        result->Compare = DefaultListCompareFunction;
    return SetVTable(result);
}

static LIST_TYPE * Init(LIST_TYPE * result)
{
    return InitWithAllocator(result, CurrentAllocator);
}

static const ContainerAllocator *GetAllocator(const LIST_TYPE * l)
{
    if (l == NULL)
        return NULL;
    return l->Allocator;
}

static LIST_ELEMENT *NextElement(LIST_ELEMENT *le)
{
    if (le == NULL) return NULL;
    return le->Next;
}

static DATA_TYPE *ElementData(LIST_ELEMENT *le)
{
    if (le == NULL) return NULL;
    return &le->Data;
}

static int SetElementData(LIST_TYPE *l,LIST_ELEMENT *le,DATA_TYPE data)
{
    return iList.SetElementData((List *)l,(ListElement *)le,&data);
}

static DATA_TYPE *Advance(LIST_ELEMENT **ple)
{
    LIST_ELEMENT *le;
    DATA_TYPE *result;

    if (ple == NULL) {
        iError.NullPtrError("Advance");
        return NULL;
    }
    le = *ple;
    if (le == NULL) return NULL;
    result = &le->Data;
    le = le->Next;
    *ple = le;
    return result;
}

INTERFACE(DATA_TYPE)   INTERFACE_NAME(DATA_TYPE) = {
    NULL,         /* Size, */
    NULL,         /* GetFlags, */
    NULL,         /* SetFlags, */
    NULL,         /* Clear,    */
    Contains,
    Erase,
    EraseAll,
    NULL,         /* Finalize, */
    NULL,         /* Apply,    */
    NULL,         /* Equal,    */
    Copy,
    NULL,         /* SetErrorFunction,*/
    Sizeof,
    NewIterator,
    InitIterator,
    NULL,         /* DeleteIterator, */
    SizeofIterator,
    NULL,          /* Save, */
    Load,
    GetElementSize,
    /* end of generic part */
    Add,
    NULL,         /* GetElement, */
    PushFront,
    PopFront,
    InsertAt,
    NULL,         /* EraseAt */
    ReplaceAt,
    IndexOf,
    /* End of sequential container part */
    NULL,        /* InsertIn, */
    CopyElement,
    EraseRange,
    Sort,
    NULL,        /* Reverse, */
    NULL,        /* GetRange, */
    NULL,        /* Append, */
    NULL,        /* SetCompareFunction, */
    DefaultListCompareFunction,
    NULL,        /* UseHeap, */
    NULL,        /* GetHeap, */
    NULL,        /* AddRange, */
    Create,
    CreateWithAllocator,
    Init,
    InitWithAllocator,
    GetAllocator,
    NULL,          /* SetDestructor */
    InitializeWith,
    NULL,          /* Back, */
    NULL,          /* Front, */
    NULL,          /* RemoveRange, */
    NULL,          /* RotateLeft, */
    NULL,          /* RotateRight, */
    NULL,          /* Select, */
    SelectCopy,
    NULL,          /* FirstElement, */
    NULL,          /* LastElement, */
    NextElement,
    ElementData,
    SetElementData,
    Advance,
    NULL,          /* Skip, */
    NULL,          /* SplitAfter, */
};

/* Do not leak template implementation macros into consumers or into the next
 * scalar instantiation. */
#undef CUTOFF
#undef swap
#undef CONTAINER_LIST_SMALL
#undef CHUNK_SIZE
#undef CONCAT
#undef CONCAT3_
#undef CONCAT3
#undef EVAL
#undef INTERFACE
#undef ITERATOR
#undef ERROR_RETURN
#undef LIST_TYPE
#undef LIST_TYPE_
#undef INTERFACE_NAME
#undef LIST_STRUCT_INTERNAL_NAME
#undef INTERFACE_STRUCT_INTERNAL_NAME
#undef LIST_ELEMENT
#undef LIST_ELEMENT_
#undef COMPARE_EXPRESSION
