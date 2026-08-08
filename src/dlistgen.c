/*
 * Value types generic list routines sample implementation 
 * ----------------------------------- ------------------
 * Thisroutines handle the Dlist container class. This is a very general
 * implementation and efficiency considerations aren't yet primordial. Dlists
 * can have elements of any size. This implement single linked Dlists. The
 * design goals here are just correctness and showing how the implementation
 * of the proposed interface COULD be done.
 * ----------------------------------------------------------------------
 */

#include "dlistgen.h"
#include "ccl_internal.h"

/* Forward declarations */
static LIST_TYPE *SetVTable(LIST_TYPE *result);
static LIST_TYPE * Create(size_t elementsize);
static LIST_TYPE *CreateWithAllocator(size_t elementsize, const ContainerAllocator * allocator);
#define CONTAINER_LIST_SMALL    2
#define CHUNK_SIZE    1000

/* Numeric ordering is useful for typed sorting, while the bytewise tie-break
 * preserves the generic dlist's representation-sensitive equality (notably
 * +0/-0 and distinct NaN encodings). */
static int TypedDefaultCompare(const void *left, const void *right,
                               CompareInfo *info)
{
    DATA_TYPE a = *(const DATA_TYPE *)left;
    DATA_TYPE b = *(const DATA_TYPE *)right;
    (void)info;
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return memcmp(left, right, sizeof(DATA_TYPE));
}

static void SetTypedDefaultCompare(LIST_TYPE *result)
{
    if (result != NULL)
        result->Compare = TypedDefaultCompare;
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
    int result = iDlist.IndexOf((Dlist *)l, &data, NULL, &idx);
    if (result == CONTAINER_ERROR_NOTFOUND)
        return 0;
    if (result < 0)
        return result;
    return 1;
}

static int Add(LIST_TYPE * l, const DATA_TYPE elem)
{
    return iDlist.Add((Dlist *)l,&elem);
}

static int CopyElement(const LIST_TYPE * l, size_t position, DATA_TYPE *outBuffer)
{
    return iDlist.CopyElement((Dlist *)l,position,outBuffer);
}

static int ReplaceAt(LIST_TYPE * l, size_t position, const DATA_TYPE data)
{
    return iDlist.ReplaceAt((Dlist *)l,position,&data);;
}

static int PushFront(LIST_TYPE * l, const DATA_TYPE pdata)
{
    return iDlist.PushFront((Dlist *)l,&pdata);
}


static int PushBack(LIST_TYPE * l, const DATA_TYPE pdata)
{
    return iDlist.PushBack((Dlist *)l,&pdata);
}

static int PopFront(LIST_TYPE * l, DATA_TYPE *result)
{
    return iDlist.PopFront((Dlist *)l,result);
}

static int PopBack(LIST_TYPE * l, DATA_TYPE *result)
{
    return iDlist.PopBack((Dlist *)l,result);
}

static int InsertAt(LIST_TYPE * l, size_t pos, const DATA_TYPE pdata)
{
    return iDlist.InsertAt((Dlist *)l,pos,&pdata);
}

static int Erase(LIST_TYPE * l, const DATA_TYPE elem)
{
    return iDlist.Erase((Dlist *)l, &elem);
}

static int EraseAll(LIST_TYPE * l, const DATA_TYPE elem)
{
    return iDlist.EraseAll((Dlist *)l, &elem);
}

static int IndexOf(const LIST_TYPE * l, const DATA_TYPE ElementToFind, void *ExtraArgs, size_t * result)
{
    return iDlist.IndexOf((Dlist *)l, &ElementToFind, ExtraArgs, result);
}

static size_t Sizeof(const LIST_TYPE * l)
{
    if (l == NULL) {
        return sizeof(Dlist);
    }
    return sizeof(LIST_TYPE) + l->ElementSize * l->count + l->count * offsetof(LIST_ELEMENT,Data);
}

static size_t SizeofIterator(const LIST_TYPE * l)
{
    return iDlist.SizeofIterator((const Dlist *)l);
}

static LIST_TYPE *Load(FILE * stream, ReadFunction loadFn, void *arg)
{
    LIST_TYPE *result = (LIST_TYPE *)iDlist.Load(stream,loadFn,arg);
    if (result != NULL && result->ElementSize != sizeof(DATA_TYPE)) {
        iError.RaiseError("typedDlist.Load", CONTAINER_ERROR_INCOMPATIBLE);
        iDlist.Finalize((Dlist *)result);
        return NULL;
    }
    SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static Iterator *NewIterator(LIST_TYPE * L)
{
    return iDlist.NewIterator((Dlist *)L);
}
static int InitIterator(LIST_TYPE * L, void *r)
{
    return iDlist.InitIterator((Dlist *)L,r);
}
static size_t GetElementSize(const LIST_TYPE * l)
{
    (void)l;
    return sizeof(DATA_TYPE);
}

static int Finalize(LIST_TYPE *l)
{
    return iDlist.Finalize((Dlist *)l);
}

static int Sort(LIST_TYPE * l)
{
    return iDlist.Sort((Dlist *)l);
}
static LIST_TYPE *SetVTable(LIST_TYPE *result)
{
    if (result == NULL)
        return NULL;
    result->VTable = &INTERFACE_NAME(DATA_TYPE);
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
static LIST_TYPE *CreateWithAllocator(size_t elementsize, const ContainerAllocator * allocator)
{
    if (elementsize != sizeof(DATA_TYPE) || allocator == NULL) {
        iError.RaiseError("typedDlist.CreateWithAllocator", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    LIST_TYPE *result =  (LIST_TYPE *)iDlist.CreateWithAllocator(sizeof(DATA_TYPE), allocator);
    SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static LIST_TYPE * Create(size_t elementsize)
{
    if (elementsize != sizeof(DATA_TYPE)) {
        iError.RaiseError("typedDlist.Create", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    LIST_TYPE *result =  (LIST_TYPE *)iDlist.CreateWithAllocator(sizeof(DATA_TYPE), CurrentAllocator);
    SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static LIST_TYPE *InitializeWith(size_t elementSize, size_t n, const DATA_TYPE *Data)
{
    if (elementSize != sizeof(DATA_TYPE) || (n != 0 && Data == NULL)) {
        iError.RaiseError("typedDlist.InitializeWith", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    LIST_TYPE *result = (LIST_TYPE *)iDlist.InitializeWith(sizeof(DATA_TYPE),n,Data);
    SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static LIST_TYPE *InitWithAllocator(LIST_TYPE * result, size_t elementsize,
          const ContainerAllocator * allocator)
{
    if (result == NULL || elementsize != sizeof(DATA_TYPE) || allocator == NULL) {
        iError.RaiseError("typedDlist.InitWithAllocator", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (iDlist.InitWithAllocator((Dlist *)result,sizeof(DATA_TYPE),allocator) == NULL)
        return NULL;
    SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static LIST_TYPE * Init(LIST_TYPE * result, size_t elementsize)
{
    return InitWithAllocator(result, elementsize, CurrentAllocator);
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

static int SetElementData(LIST_TYPE *l,LIST_ELEMENT *le,DATA_TYPE data)
{
    return iDlist.SetElementData((Dlist *)l,(DlistElement *)le,&data);
}

struct TypedApplyContext {
    int (*fn)(DATA_TYPE *, void *);
    void *arg;
};

static int ApplyThunk(void *element, void *context)
{
    struct TypedApplyContext *ctx = (struct TypedApplyContext *)context;
    return ctx->fn((DATA_TYPE *)element, ctx->arg);
}

static int ApplyTyped(LIST_TYPE *l, int (*fn)(DATA_TYPE *, void *), void *arg)
{
    struct TypedApplyContext context;
    if (fn == NULL)
        return iDlist.Apply((Dlist *)l, NULL, arg);
    context.fn = fn;
    context.arg = arg;
    return iDlist.Apply((Dlist *)l, ApplyThunk, &context);
}

static LIST_TYPE *GetRangeTyped(const LIST_TYPE *l, size_t start, size_t end)
{
    LIST_TYPE *result = (LIST_TYPE *)iDlist.GetRange((Dlist *)l, start, end);
    if (result != NULL && l != NULL)
        result->Compare = l->Compare;
    else
        SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static LIST_TYPE *SplitAfterTyped(LIST_TYPE *l, LIST_ELEMENT *pos)
{
    LIST_TYPE *result = (LIST_TYPE *)iDlist.SplitAfter((Dlist *)l,
                                                       (DlistElement *)pos);
    if (result != NULL && l != NULL)
        result->Compare = l->Compare;
    else
        SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static LIST_TYPE *SpliceTyped(LIST_TYPE *l, void *pos, LIST_TYPE *toInsert,
                              int direction)
{
    LIST_TYPE *result;
    if (l == NULL || toInsert == NULL) {
        iError.NullPtrError("typedDlist.Splice");
        return NULL;
    }
    if (l == toInsert) {
        l->RaiseError("typedDlist.Splice", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (toInsert->count != 0 &&
        (l->Allocator != toInsert->Allocator || l->Heap != toInsert->Heap)) {
        l->RaiseError("typedDlist.Splice", CONTAINER_ERROR_INCOMPATIBLE);
        return NULL;
    }
    result = (LIST_TYPE *)iDlist.Splice((Dlist *)l, pos,
                                        (Dlist *)toInsert, direction);
    if (result != NULL && toInsert->count != 0) {
        toInsert->First = NULL;
        toInsert->Last = NULL;
        toInsert->count = 0;
        toInsert->timestamp++;
    }
    return SetVTable(result);
}

static LIST_ELEMENT *FirstElementTyped(LIST_TYPE *l)
{
    return (LIST_ELEMENT *)iDlist.FirstElement((Dlist *)l);
}

static LIST_ELEMENT *LastElementTyped(LIST_TYPE *l)
{
    return (LIST_ELEMENT *)iDlist.LastElement((Dlist *)l);
}

static LIST_ELEMENT *PreviousElement(LIST_ELEMENT *le)
{
    return le == NULL ? NULL : le->Previous;
}

static DATA_TYPE *GetElementTyped(const LIST_TYPE *l, size_t idx)
{
    return (DATA_TYPE *)iDlist.GetElement((const Dlist *)l, idx);
}

static DATA_TYPE *GetElementDataTyped(LIST_ELEMENT *le)
{
    return le == NULL ? NULL : &le->Data;
}

static DATA_TYPE *AdvanceTyped(LIST_ELEMENT **ple)
{
    return (DATA_TYPE *)iDlist.Advance((DlistElement **)ple);
}

static LIST_ELEMENT *SkipTyped(LIST_ELEMENT *le, size_t n)
{
    return (LIST_ELEMENT *)iDlist.Skip((DlistElement *)le, n);
}

static void *MoveBackTyped(LIST_ELEMENT **ple)
{
    return iDlist.MoveBack((DlistElement **)ple);
}

static DATA_TYPE *BackTyped(const LIST_TYPE *l)
{
    return (DATA_TYPE *)iDlist.Back((const Dlist *)l);
}

static DATA_TYPE *FrontTyped(const LIST_TYPE *l)
{
    return (DATA_TYPE *)iDlist.Front((const Dlist *)l);
}

static LIST_TYPE *CopyTyped(const LIST_TYPE *l)
{
    return SetVTable((LIST_TYPE *)iDlist.Copy((const Dlist *)l));
}

static LIST_TYPE *SelectCopyTyped(const LIST_TYPE *l, const Mask *m)
{
    LIST_TYPE *result = (LIST_TYPE *)iDlist.SelectCopy((const Dlist *)l, m);
    if (result != NULL && l != NULL)
        result->Compare = l->Compare;
    else
        SetTypedDefaultCompare(result);
    return SetVTable(result);
}

static int SaveTyped(const LIST_TYPE *l, FILE *stream, SaveFunction fn,
                     void *arg)
{
    return iDlist.Save((const Dlist *)l, stream, fn, arg);
}

static unsigned GetFlagsTyped(const LIST_TYPE *l)
{
    return iDlist.GetFlags((const Dlist *)l);
}

static int ClearTyped(LIST_TYPE *l)
{
    return iDlist.Clear((Dlist *)l);
}

static int EqualTyped(const LIST_TYPE *left, const LIST_TYPE *right)
{
    return iDlist.Equal((const Dlist *)left, (const Dlist *)right);
}

static ErrorFunction SetErrorFunctionTyped(LIST_TYPE *l, ErrorFunction fn)
{
    return iDlist.SetErrorFunction((Dlist *)l, fn);
}

static CompareFunction SetCompareFunctionTyped(LIST_TYPE *l,
                                                CompareFunction fn)
{
    return iDlist.SetCompareFunction((Dlist *)l, fn);
}

static DestructorFunction SetDestructorTyped(LIST_TYPE *l,
                                              DestructorFunction fn)
{
    return iDlist.SetDestructor((Dlist *)l, fn);
}

static int AddRangeTyped(LIST_TYPE *l, size_t n, const DATA_TYPE *data)
{
    return iDlist.AddRange((Dlist *)l, n, data);
}

static int InsertInTyped(LIST_TYPE *l, size_t idx, LIST_TYPE *data)
{
    return iDlist.InsertIn((Dlist *)l, idx, (Dlist *)data);
}

static int AppendTyped(LIST_TYPE *l, LIST_TYPE *data)
{
    if (l == data || l == NULL || data == NULL)
        return CONTAINER_ERROR_BADARG;
    if (data->count != 0 &&
        (l->Allocator != data->Allocator || l->Heap != data->Heap)) {
        l->RaiseError("typedDlist.Append", CONTAINER_ERROR_INCOMPATIBLE);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    return iDlist.Append((Dlist *)l, (Dlist *)data);
}

static int UseHeapTyped(LIST_TYPE *l, const ContainerAllocator *allocator)
{
    return iDlist.UseHeap((Dlist *)l, allocator);
}

static int SelectTyped(LIST_TYPE *l, const Mask *m)
{
    return iDlist.Select((Dlist *)l, m);
}

static size_t SizeTyped(const LIST_TYPE *l)
{
    return iDlist.Size((const Dlist *)l);
}

static unsigned SetFlagsTyped(LIST_TYPE *l, unsigned flags)
{
    return iDlist.SetFlags((Dlist *)l, flags);
}

static int EraseAtTyped(LIST_TYPE *l, size_t position)
{
    return iDlist.EraseAt((Dlist *)l, position);
}

static int ReverseTyped(LIST_TYPE *l)
{
    return iDlist.Reverse((Dlist *)l);
}

static int RemoveRangeTyped(LIST_TYPE *l, size_t start, size_t end)
{
    return iDlist.RemoveRange((Dlist *)l, start, end);
}

static int RotateLeftTyped(LIST_TYPE *l, size_t n)
{
    return iDlist.RotateLeft((Dlist *)l, n);
}

static int RotateRightTyped(LIST_TYPE *l, size_t n)
{
    return iDlist.RotateRight((Dlist *)l, n);
}

static int DeleteIteratorTyped(Iterator *it)
{
    return iDlist.DeleteIterator(it);
}

INTERFACE(DATA_TYPE)   INTERFACE_NAME(DATA_TYPE) = {
    SizeTyped,
    GetFlagsTyped,
    SetFlagsTyped,
    ClearTyped,
    Contains,
    Erase,
    EraseAll,
    Finalize,
    ApplyTyped,
    EqualTyped,
    CopyTyped,
    SetErrorFunctionTyped,
    Sizeof,
    NewIterator,
    InitIterator,
    DeleteIteratorTyped,
    SizeofIterator,
    SaveTyped,
    Load,
    GetElementSize,
    /* end of generic part */
    Add,
    GetElementTyped,
    PushFront,
    PopFront,
    InsertAt,
    EraseAtTyped,
    ReplaceAt,
    IndexOf,
    /* End of sequential container part */
    PushBack,
    PopBack,
    SpliceTyped,
    Sort,
    ReverseTyped,
    GetRangeTyped,
    AppendTyped,
    SetCompareFunctionTyped,
    UseHeapTyped,
    AddRangeTyped,
    Create,
    CreateWithAllocator,
    Init,
    InitWithAllocator,
    CopyElement,
    InsertInTyped,
    SetDestructorTyped,
    InitializeWith,
    GetAllocator,
    BackTyped,
    FrontTyped,
    RemoveRangeTyped,
    RotateLeftTyped,
    RotateRightTyped,
    SelectTyped,
    SelectCopyTyped,
    FirstElementTyped,
    LastElementTyped,
    NextElement,
    PreviousElement,
    GetElementDataTyped,
    SetElementData,
    AdvanceTyped,
    SkipTyped,
    MoveBackTyped,
    SplitAfterTyped,

};
