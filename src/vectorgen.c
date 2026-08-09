#include "containers.h"
#include "ccl_internal.h"
#include "vectorgen.h"

#include <limits.h>
#include <stddef.h>

/* Number of elements by default. */
#ifndef DEFAULT_START_SIZE
#define DEFAULT_START_SIZE 20
#endif

/* The high flag bit is private to the typed placement adapter.  It is masked
 * by the typed flag accessors and never reaches the generic implementation's
 * public persistence format. */
#define VECTOR_TYPED_PLACEMENT_FLAG (1u << (sizeof(unsigned) * CHAR_BIT - 1))

_Static_assert(sizeof(VECTOR_TYPE) == sizeof(Vector),
               "typed vector must have the generic vector layout");
_Static_assert(offsetof(VECTOR_TYPE,VTable) == offsetof(Vector,VTable),
               "typed vector VTable offset");
_Static_assert(offsetof(VECTOR_TYPE,count) == offsetof(Vector,count),
               "typed vector count offset");
_Static_assert(offsetof(VECTOR_TYPE,Flags) == offsetof(Vector,Flags),
               "typed vector Flags offset");
_Static_assert(offsetof(VECTOR_TYPE,ElementSize) == offsetof(Vector,ElementSize),
               "typed vector ElementSize offset");
_Static_assert(offsetof(VECTOR_TYPE,contents) == offsetof(Vector,contents),
               "typed vector contents offset");
_Static_assert(offsetof(VECTOR_TYPE,capacity) == offsetof(Vector,capacity),
               "typed vector capacity offset");
_Static_assert(offsetof(VECTOR_TYPE,timestamp) == offsetof(Vector,timestamp),
               "typed vector timestamp offset");
_Static_assert(offsetof(VECTOR_TYPE,CompareFn) == offsetof(Vector,CompareFn),
               "typed vector CompareFn offset");
_Static_assert(offsetof(VECTOR_TYPE,RaiseError) == offsetof(Vector,RaiseError),
               "typed vector RaiseError offset");
_Static_assert(offsetof(VECTOR_TYPE,Allocator) == offsetof(Vector,Allocator),
               "typed vector Allocator offset");
_Static_assert(offsetof(VECTOR_TYPE,DestructorFn) == offsetof(Vector,DestructorFn),
               "typed vector DestructorFn offset");

_Static_assert(sizeof(struct ITERATOR(DATA_TYPE)) == sizeof(struct VectorIterator),
               "typed iterator must have the generic iterator layout");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),it) == offsetof(struct VectorIterator,it),
               "typed iterator it offset");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),Magic) == offsetof(struct VectorIterator,Magic),
               "typed iterator Magic offset");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),L) == offsetof(struct VectorIterator,AL),
               "typed iterator vector offset");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),index) == offsetof(struct VectorIterator,index),
               "typed iterator index offset");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),timestamp) == offsetof(struct VectorIterator,timestamp),
               "typed iterator timestamp offset");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),Flags) == offsetof(struct VectorIterator,Flags),
               "typed iterator Flags offset");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),Current) == offsetof(struct VectorIterator,Current),
               "typed iterator Current offset");
_Static_assert(offsetof(struct ITERATOR(DATA_TYPE),ElementBuffer) == offsetof(struct VectorIterator,ElementBuffer),
               "typed iterator element buffer offset");

static VECTOR_TYPE *SetVTable(VECTOR_TYPE *result);

static int NullPtrError(const char *fnName)
{
    char buf[512];

    snprintf(buf,sizeof(buf),"iVector.%s",fnName);
    return iError.NullPtrError(buf);
}

static Vector *as_vector(VECTOR_TYPE *v)
{
    return (Vector *)(void *)v;
}

static const Vector *as_const_vector(const VECTOR_TYPE *v)
{
    return (const Vector *)(const void *)v;
}

static VECTOR_TYPE *as_typed(Vector *v)
{
    return (VECTOR_TYPE *)(void *)v;
}

static unsigned public_flags(unsigned flags)
{
    return flags & ~VECTOR_TYPED_PLACEMENT_FLAG;
}

static int is_placement(const VECTOR_TYPE *v)
{
    return v != NULL && (v->Flags & VECTOR_TYPED_PLACEMENT_FLAG) != 0;
}

static size_t Size(const VECTOR_TYPE *v)
{
    return iVector.Size(as_const_vector(v));
}

static unsigned GetFlags(const VECTOR_TYPE *v)
{
    if (v == NULL)
        return iVector.GetFlags(NULL);
    return public_flags(v->Flags);
}

static unsigned SetFlags(VECTOR_TYPE *v,unsigned flags)
{
    unsigned old;

    if (v == NULL)
        return iVector.SetFlags(NULL,flags);
    old = public_flags(v->Flags);
    v->Flags = (v->Flags & VECTOR_TYPED_PLACEMENT_FLAG) |
               (flags & ~VECTOR_TYPED_PLACEMENT_FLAG);
    return old;
}

static int Clear(VECTOR_TYPE *v)
{
    return iVector.Clear(as_vector(v));
}

static int Contains(const VECTOR_TYPE *v,const DATA_TYPE value,void *extra)
{
    return iVector.Contains(as_const_vector(v),&value,extra);
}

static int Erase(VECTOR_TYPE *v,const DATA_TYPE value)
{
    return iVector.Erase(as_vector(v),&value);
}

static int EraseAll(VECTOR_TYPE *v,const DATA_TYPE value)
{
    return iVector.EraseAll(as_vector(v),&value);
}

/* Generic Finalize assumes a non-generic VTable was heap allocated.  The
 * generated interface is static, so make the generic ownership decision
 * unambiguously generic before delegating.  Placement Init has no owned
 * header; its private marker lets us release only its contents. */
static int Finalize(VECTOR_TYPE *v)
{
    unsigned flags;
    const ContainerAllocator *allocator;
    int result;

    if (v == NULL)
        return CONTAINER_ERROR_BADARG;
    if (!is_placement(v)) {
        v->VTable = (INTERFACE(DATA_TYPE) *)(void *)&iVector;
        return iVector.Finalize(as_vector(v));
    }
    allocator = v->Allocator;
    flags = v->Flags;
    v->VTable = (INTERFACE(DATA_TYPE) *)(void *)&iVector;
    v->Flags &= ~(VECTOR_TYPED_PLACEMENT_FLAG | CONTAINER_READONLY);
    result = iVector.Clear(as_vector(v));
    v->Flags = flags;
    if (result < 0)
        return result;
    if (flags & CONTAINER_HAS_OBSERVER)
        iObserver.Notify(as_vector(v),CCL_FINALIZE,NULL,NULL);
    allocator->free(v->contents);
    v->contents = NULL;
    return result;
}

static int Apply(VECTOR_TYPE *v,int (*fn)(DATA_TYPE *,void *),void *arg)
{
    size_t i;
    unsigned char *p;
    DATA_TYPE *temporary = NULL;

    if (v == NULL || fn == NULL)
        return NullPtrError("Apply");
    if (v->Flags & CONTAINER_READONLY) {
        temporary = v->Allocator->malloc(sizeof(DATA_TYPE));
        if (temporary == NULL) {
            v->RaiseError("iVector.Apply",CONTAINER_ERROR_NOMEMORY);
            return CONTAINER_ERROR_NOMEMORY;
        }
    }
    p = (unsigned char *)v->contents;
    for (i=0; i<v->count; ++i) {
        DATA_TYPE *element;
        if (temporary != NULL) {
            memcpy(temporary,p,sizeof(DATA_TYPE));
            element = temporary;
        } else {
            element = (DATA_TYPE *)(void *)p;
        }
        (void)fn(element,arg);
        p += sizeof(DATA_TYPE);
    }
    if (temporary != NULL)
        v->Allocator->free(temporary);
    if (v->count != 0)
        ++v->timestamp;
    return 1;
}

static int Equal(const VECTOR_TYPE *left,const VECTOR_TYPE *right)
{
    unsigned lf,rf;
    int result;

    if (left == NULL || right == NULL)
        return iVector.Equal(as_const_vector(left),as_const_vector(right));
    lf = left->Flags;
    rf = right->Flags;
    ((VECTOR_TYPE *)left)->Flags = public_flags(lf);
    ((VECTOR_TYPE *)right)->Flags = public_flags(rf);
    result = iVector.Equal(as_const_vector(left),as_const_vector(right));
    ((VECTOR_TYPE *)left)->Flags = lf;
    ((VECTOR_TYPE *)right)->Flags = rf;
    return result;
}

static VECTOR_TYPE *Copy(const VECTOR_TYPE *v)
{
    return SetVTable(as_typed(iVector.Copy(as_const_vector(v))));
}

static ErrorFunction SetErrorFunction(VECTOR_TYPE *v,ErrorFunction fn)
{
    return iVector.SetErrorFunction(as_vector(v),fn);
}

static size_t Sizeof(const VECTOR_TYPE *v)
{
    return iVector.Sizeof(as_const_vector(v));
}

static Iterator *NewIterator(VECTOR_TYPE *v)
{
    return iVector.NewIterator(as_vector(v));
}

static int InitIterator(VECTOR_TYPE *v,void *storage)
{
    return iVector.InitIterator(as_vector(v),storage);
}

static int DeleteIterator(Iterator *it)
{
    return iVector.DeleteIterator(it);
}

static size_t SizeofIterator(const VECTOR_TYPE *v)
{
    if (v == NULL)
        return sizeof(struct ITERATOR(DATA_TYPE)) + sizeof(DATA_TYPE);
    return iVector.SizeofIterator(as_const_vector(v));
}

static int Save(const VECTOR_TYPE *v,FILE *stream,SaveFunction saveFn,void *arg)
{
    unsigned flags;
    int result;

    if (v == NULL)
        return iVector.Save(NULL,stream,saveFn,arg);
    flags = v->Flags;
    ((VECTOR_TYPE *)v)->Flags = public_flags(flags);
    result = iVector.Save(as_const_vector(v),stream,saveFn,arg);
    ((VECTOR_TYPE *)v)->Flags = flags;
    return result;
}

static VECTOR_TYPE *Load(FILE *stream,ReadFunction loadFn,void *arg)
{
    Vector *generic = iVector.Load(stream,loadFn,arg);

    if (generic == NULL)
        return NULL;
    /* The generic on-disk format carries width, but not a language type.
     * Reject all widths other than this specialization before installing its
     * scalar vtable.  Equal-width records are necessarily ABI-compatible at
     * the byte level because the format has no type identity. */
    if (generic->ElementSize != sizeof(DATA_TYPE)) {
        iVector.Finalize(generic);
        return NULL;
    }
    return SetVTable(as_typed(generic));
}

static size_t GetElementSize(const VECTOR_TYPE *v)
{
    if (v == NULL || v->ElementSize != sizeof(DATA_TYPE))
        return 0;
    return sizeof(DATA_TYPE);
}

static int Add(VECTOR_TYPE *v,const DATA_TYPE value)
{
    return iVector.Add(as_vector(v),&value);
}

static DATA_TYPE *GetElement(const VECTOR_TYPE *v,size_t index)
{
    return (DATA_TYPE *)iVector.GetElement(as_const_vector(v),index);
}

static int PushBack(VECTOR_TYPE *v,const DATA_TYPE value)
{
    return iVector.PushBack(as_vector(v),&value);
}

static int PopBack(VECTOR_TYPE *v,DATA_TYPE *result)
{
    return iVector.PopBack(as_vector(v),result);
}

static int InsertAt(VECTOR_TYPE *v,size_t index,const DATA_TYPE value)
{
    return iVector.InsertAt(as_vector(v),index,(void *)&value);
}

static int EraseAt(VECTOR_TYPE *v,size_t index)
{
    return iVector.EraseAt(as_vector(v),index);
}

static int ReplaceAt(VECTOR_TYPE *v,size_t index,const DATA_TYPE value)
{
    return iVector.ReplaceAt(as_vector(v),index,(void *)&value);
}

static int IndexOf(const VECTOR_TYPE *v,const DATA_TYPE value,void *extra,size_t *result)
{
    return iVector.IndexOf(as_const_vector(v),&value,extra,result);
}

static int Insert(VECTOR_TYPE *v,const DATA_TYPE value)
{
    return iVector.Insert(as_vector(v),(void *)&value);
}

static int InsertIn(VECTOR_TYPE *v,size_t index,VECTOR_TYPE *newData)
{
    return iVector.InsertIn(as_vector(v),index,as_vector(newData));
}

static VECTOR_TYPE *IndexIn(VECTOR_TYPE *v,VECTOR_TYPE *indices)
{
    return SetVTable(as_typed(iVector.IndexIn(as_vector(v),as_vector(indices))));
}

static size_t GetCapacity(const VECTOR_TYPE *v)
{
    return iVector.GetCapacity(as_const_vector(v));
}

static int SetCapacity(VECTOR_TYPE *v,size_t capacity)
{
    return iVector.SetCapacity(as_vector(v),capacity);
}

static CompareFunction SetCompareFunction(VECTOR_TYPE *v,CompareFunction fn)
{
    return iVector.SetCompareFunction(as_vector(v),fn);
}

static int Sort(VECTOR_TYPE *v)
{
    return iVector.Sort(as_vector(v));
}

static VECTOR_TYPE *CreateWithAllocator(size_t startsize,const ContainerAllocator *allocator)
{
    return SetVTable(as_typed(iVector.CreateWithAllocator(sizeof(DATA_TYPE),
                                                           startsize,allocator)));
}

static VECTOR_TYPE *Create(size_t startsize)
{
    return CreateWithAllocator(startsize,CurrentAllocator);
}

static VECTOR_TYPE *Init(VECTOR_TYPE *result,size_t startsize)
{
    Vector *v;

    v = iVector.Init(as_vector(result),sizeof(DATA_TYPE),startsize);
    if (v == NULL)
        return NULL;
    v->Flags |= VECTOR_TYPED_PLACEMENT_FLAG;
    return SetVTable(as_typed(v));
}

static int AddRange(VECTOR_TYPE *v,size_t count,const DATA_TYPE *values)
{
    return iVector.AddRange(as_vector(v),count,values);
}

static VECTOR_TYPE *GetRange(const VECTOR_TYPE *v,size_t start,size_t end)
{
    Vector *generic;

    if (v == NULL)
        return NULL;
    generic = iVector.GetRange(as_const_vector(v),start,end);
    return SetVTable(as_typed(generic));
}

static int CopyElement(const VECTOR_TYPE *v,size_t index,DATA_TYPE *out)
{
    return iVector.CopyElement(as_const_vector(v),index,out);
}

static void **CopyTo(const VECTOR_TYPE *v)
{
    return iVector.CopyTo(as_const_vector(v));
}

static int Reverse(VECTOR_TYPE *v)
{
    return iVector.Reverse(as_vector(v));
}

static int Append(VECTOR_TYPE *left,VECTOR_TYPE *right)
{
    return iVector.Append(as_vector(left),as_vector(right));
}

static int Mismatch(VECTOR_TYPE *left,VECTOR_TYPE *right,size_t *mismatch)
{
    return iVector.Mismatch(as_vector(left),as_vector(right),mismatch);
}

static const ContainerAllocator *GetAllocator(const VECTOR_TYPE *v)
{
    return iVector.GetAllocator(as_const_vector(v));
}

static DestructorFunction SetDestructor(VECTOR_TYPE *v,DestructorFunction fn)
{
    return iVector.SetDestructor(as_vector(v),fn);
}

static int SearchWithKey(VECTOR_TYPE *v,size_t startByte,size_t sizeKey,
                         size_t startIndex,const DATA_TYPE item,size_t *result)
{
    return iVector.SearchWithKey(as_vector(v),startByte,sizeKey,startIndex,
                                 (void *)&item,result);
}

static int Select(VECTOR_TYPE *v,const Mask *mask)
{
    return iVector.Select(as_vector(v),mask);
}

static VECTOR_TYPE *SelectCopy(VECTOR_TYPE *v,Mask *mask)
{
    return SetVTable(as_typed(iVector.SelectCopy(as_vector(v),mask)));
}

static int Resize(VECTOR_TYPE *v,size_t newSize)
{
    return iVector.Resize(as_vector(v),newSize);
}

static VECTOR_TYPE *InitializeWith(size_t count,const DATA_TYPE *values)
{
    VECTOR_TYPE *result;

    if (count != 0 && values == NULL) {
        iError.RaiseError("iVector.InitializeWith",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    result = Create(count);
    if (result == NULL)
        return NULL;
    if (count != 0) {
        memcpy(result->contents,values,count * sizeof(DATA_TYPE));
        result->count = count;
    }
    return result;
}

static DATA_TYPE *GetData(const VECTOR_TYPE *v)
{
    return (DATA_TYPE *)iVector.GetData(as_const_vector(v));
}

static DATA_TYPE *Back(const VECTOR_TYPE *v)
{
    return (DATA_TYPE *)iVector.Back(as_const_vector(v));
}

static DATA_TYPE *Front(const VECTOR_TYPE *v)
{
    return (DATA_TYPE *)iVector.Front(as_const_vector(v));
}

static int RemoveRange(VECTOR_TYPE *v,size_t start,size_t end)
{
    return iVector.RemoveRange(as_vector(v),start,end);
}

static int RotateLeft(VECTOR_TYPE *v,size_t count)
{
    return iVector.RotateLeft(as_vector(v),count);
}

static int RotateRight(VECTOR_TYPE *v,size_t count)
{
    return iVector.RotateRight(as_vector(v),count);
}

static Mask *CompareEqual(const VECTOR_TYPE *left,const VECTOR_TYPE *right,Mask *mask)
{
    return iVector.CompareEqual(as_const_vector(left),as_const_vector(right),mask);
}

static Mask *CompareEqualScalar(const VECTOR_TYPE *left,const DATA_TYPE right,Mask *mask)
{
    return iVector.CompareEqualScalar(as_const_vector(left),&right,mask);
}

static int Reserve(VECTOR_TYPE *v,size_t capacity)
{
    return iVector.Reserve(as_vector(v),capacity);
}

static VECTOR_TYPE *SetVTable(VECTOR_TYPE *result)
{
    if (result != NULL)
        result->VTable = &INTERFACE_NAME(DATA_TYPE);
    return result;
}

INTERFACE(DATA_TYPE) INTERFACE_NAME(DATA_TYPE) = {
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
    Add,
    GetElement,
    PushBack,
    PopBack,
    InsertAt,
    EraseAt,
    ReplaceAt,
    IndexOf,
    Insert,
    InsertIn,
    IndexIn,
    GetCapacity,
    SetCapacity,
    SetCompareFunction,
    Sort,
    Create,
    CreateWithAllocator,
    Init,
    AddRange,
    GetRange,
    CopyElement,
    CopyTo,
    Reverse,
    Append,
    Mismatch,
    GetAllocator,
    SetDestructor,
    SearchWithKey,
    Select,
    SelectCopy,
    Resize,
    InitializeWith,
    GetData,
    Back,
    Front,
    RemoveRange,
    RotateLeft,
    RotateRight,
    CompareEqual,
    CompareEqualScalar,
    Reserve
};
