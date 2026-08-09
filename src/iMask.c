#include "containers.h"
#include "ccl_internal.h"


static Mask *CreateFromMask(size_t n,const char *data)
{
    Mask *result;

    if (n > SIZE_MAX - sizeof(Mask)) {
        iError.RaiseError("iMask.CreateFromMask",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    result = CurrentAllocator->malloc(n+sizeof(Mask));
    if (result == NULL) {
        iError.RaiseError("iMask.CreateFromMask",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if (data) memcpy(result->data,data,n);
    else memset(result->data,0,n);
    result->Allocator = CurrentAllocator;
    result->length = n;
    return result;
}

static Mask *Copy(const Mask *src)
{
    Mask *result;

    if (src == NULL) return NULL;
    if (src->length > SIZE_MAX - sizeof(Mask)) {
        iError.RaiseError("iMask.Copy",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    result = src->Allocator->malloc(src->length + sizeof(Mask));
    if (result == NULL) {
        iError.RaiseError("iMask.Copy",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    memcpy(result->data,src->data,src->length);
    result->Allocator = src->Allocator;
    result->length = src->length;
    return result;
}

static Mask *Create(size_t n)
{
    return CreateFromMask(n,NULL);
}

static int SetElement(Mask *m,size_t idx,int val)
{
    if (m == NULL) {
        return iError.NullPtrError("iMask.Set");
    }
    if (idx >= m->length) {
        iError.RaiseError("iMask.Set",CONTAINER_ERROR_INDEX,m,idx);
        return CONTAINER_ERROR_INDEX;
    }
    m->data[idx]=(char)val;
    return 1;
}
int GetElement(const Mask *m,size_t position)
{
	if (m == NULL) {
		iError.RaiseError("iMask.GetElement",CONTAINER_ERROR_BADARG);
		return 0;
	}
	if (position >= m->length) {
		iError.RaiseError("iMask.GetElement",CONTAINER_ERROR_INDEX);
		return 0;
	}
    return m->data[position] ;
}
static int Clear(Mask *m)
{
    if (m == NULL) {
        return iError.NullPtrError("iMask.Clear");
    }
    memset(m->data,0,m->length);
    return 1;
}

static int Finalize(Mask *m)
{
    if (m == NULL) {
        return iError.NullPtrError("iMask.Finalize");
    }
    m->Allocator->free(m);
    return 1;
}

static size_t Size(const Mask *m)
{
    return (m == NULL) ? 0 : m->length;
}

static int Verify(const Mask *src1, const Mask *src2,const char *name)
{
    if (src1 == NULL || src2 == NULL) {
        return iError.NullPtrError(name);
    }
    if (src1->length != src2->length) {
        iError.RaiseError(name,CONTAINER_ERROR_INCOMPATIBLE);
        return CONTAINER_ERROR_INCOMPATIBLE;
    }
    return 0;
}

static int And(Mask *src1,const Mask *src2)
{
    size_t i;
    int r = Verify(src1,src2,"iMask.And");

    if (r) return r;
    for (i=0; i<src1->length;i++) {
        src1->data[i] &= src2->data[i];
    }
    return 1;
}

static int Not(Mask *src1)
{
    size_t i;

    if (src1 == NULL ) {
        return iError.NullPtrError("iMask.Not");
    }
    for (i=0; i<src1->length;i++) {
        src1->data[i] = src1->data[i] ? 0 : 1;
    }
    return 1;
}


static int Or(Mask *src1,const Mask *src2)
{
    size_t i;
    int r = Verify(src1,src2,"iMask.Or");

    if (r) return r;
    for (i=0; i<src1->length;i++) {
            src1->data[i] |= src2->data[i];
    }
    return 1;
}

static size_t Sizeof(const Mask *m)
{
    if (m == NULL) return sizeof(Mask);
    return sizeof(Mask) + m->length;
}

static size_t PopulationCount(const Mask *m)
{
    size_t i,result=0;

    if (m == NULL) {
        iError.NullPtrError("iMask.PopulationCount");
        return 0;
    }
    for (i=0; i<m->length;i++) {
        if (m->data[i]) result++;
    }
    return result;
}

MaskInterface iMask = {
    And,
    Or,
    Not,
    CreateFromMask,
    Create,
    Copy,
    Size,
    Sizeof,
    SetElement,
    GetElement,
    Clear,
    Finalize,
    PopulationCount,
};
