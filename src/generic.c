#include "containers.h"
#include "ccl_internal.h"

#include <string.h>

/*
 * The first part of ListInterface, DlistInterface, VectorInterface, and the
 * string-collection interfaces has the generic order.  Their function
 * pointer types are not literally identical, however (Apply returns int in
 * those interfaces and Vector.Contains has an extra CompareInfo argument).
 * Keep the adapter honest by calling the known concrete interfaces through
 * their declared types.  Unknown tables are assumed to implement the public
 * GenericContainerInterface exactly; this is what makes small protocol spies
 * and user-defined containers useful without a registration API.
 */
typedef enum GenericKind {
    GENERIC_UNKNOWN,
    GENERIC_LIST,
    GENERIC_DLIST,
    GENERIC_VECTOR,
    GENERIC_STRING,
    GENERIC_WSTRING
} GenericKind;

static GenericKind KindOf(const GenericContainer *gen)
{
    const void *table;

    if (gen == NULL || gen->vTable == NULL)
        return GENERIC_UNKNOWN;
    table = (const void *)gen->vTable;
    if (table == (const void *)&iList)
        return GENERIC_LIST;
    if (table == (const void *)&iDlist)
        return GENERIC_DLIST;
    if (table == (const void *)&iVector)
        return GENERIC_VECTOR;
    if (table == (const void *)&istrCollection)
        return GENERIC_STRING;
    if (table == (const void *)&iWstrCollection)
        return GENERIC_WSTRING;
    return GENERIC_UNKNOWN;
}

static int BadArg(const char *name)
{
    iError.RaiseError(name, CONTAINER_ERROR_BADARG);
    return CONTAINER_ERROR_BADARG;
}

static void BadArgVoid(const char *name)
{
    iError.RaiseError(name, CONTAINER_ERROR_BADARG);
}

static size_t Size(const GenericContainer *gen)
{
    if (gen == NULL || gen->vTable == NULL) {
        BadArg("iGeneric.Size");
        return 0;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.Size((const List *)gen);
    case GENERIC_DLIST:  return iDlist.Size((const Dlist *)gen);
    case GENERIC_VECTOR: return iVector.Size((const Vector *)gen);
    case GENERIC_STRING: return istrCollection.Size((const strCollection *)gen);
    case GENERIC_WSTRING:return iWstrCollection.Size((const WstrCollection *)gen);
    default:             return gen->vTable->Size(gen);
    }
}

static unsigned GetFlags(const GenericContainer *gen)
{
    if (gen == NULL || gen->vTable == NULL) {
        BadArg("iGeneric.GetFlags");
        return 0;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.GetFlags((const List *)gen);
    case GENERIC_DLIST:  return iDlist.GetFlags((const Dlist *)gen);
    case GENERIC_VECTOR: return iVector.GetFlags((const Vector *)gen);
    case GENERIC_STRING: return istrCollection.GetFlags((const strCollection *)gen);
    case GENERIC_WSTRING:return iWstrCollection.GetFlags((const WstrCollection *)gen);
    default:             return gen->vTable->GetFlags(gen);
    }
}

static unsigned SetFlags(GenericContainer *gen, unsigned newFlags)
{
    if (gen == NULL || gen->vTable == NULL) {
        BadArg("iGeneric.SetFlags");
        return 0;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.SetFlags((List *)gen, newFlags);
    case GENERIC_DLIST:  return iDlist.SetFlags((Dlist *)gen, newFlags);
    case GENERIC_VECTOR: return iVector.SetFlags((Vector *)gen, newFlags);
    case GENERIC_STRING: return istrCollection.SetFlags((strCollection *)gen, newFlags);
    case GENERIC_WSTRING:return iWstrCollection.SetFlags((WstrCollection *)gen, newFlags);
    default:             return gen->vTable->SetFlags(gen, newFlags);
    }
}

static int Clear(GenericContainer *gen)
{
    if (gen == NULL || gen->vTable == NULL) return BadArg("iGeneric.Clear");
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.Clear((List *)gen);
    case GENERIC_DLIST:  return iDlist.Clear((Dlist *)gen);
    case GENERIC_VECTOR: return iVector.Clear((Vector *)gen);
    case GENERIC_STRING: return istrCollection.Clear((strCollection *)gen);
    case GENERIC_WSTRING:return iWstrCollection.Clear((WstrCollection *)gen);
    default:             return gen->vTable->Clear(gen);
    }
}

static int Contains(const GenericContainer *gen, const void *value)
{
    if (gen == NULL || gen->vTable == NULL) return BadArg("iGeneric.Contains");
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.Contains((const List *)gen, value);
    case GENERIC_DLIST:  return iDlist.Contains((const Dlist *)gen, value);
    /* Vector's CompareInfo slot is optional at the public generic boundary;
       passing NULL is deterministic and avoids an uninitialised read. */
    case GENERIC_VECTOR: return iVector.Contains((const Vector *)gen, value, NULL);
    case GENERIC_STRING: return istrCollection.Contains((const strCollection *)gen, (const char *)value);
    case GENERIC_WSTRING:return iWstrCollection.Contains((const WstrCollection *)gen, (const wchar_t *)value);
    default:             return gen->vTable->Contains(gen, value);
    }
}

static int Erase(GenericContainer *gen, const void *elem)
{
    if (gen == NULL || gen->vTable == NULL) return BadArg("iGeneric.Erase");
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.Erase((List *)gen, elem);
    case GENERIC_DLIST:  return iDlist.Erase((Dlist *)gen, elem);
    case GENERIC_VECTOR: return iVector.Erase((Vector *)gen, elem);
    case GENERIC_STRING: return istrCollection.Erase((strCollection *)gen, (const char *)elem);
    case GENERIC_WSTRING:return iWstrCollection.Erase((WstrCollection *)gen, (const wchar_t *)elem);
    default:             return gen->vTable->Erase(gen, elem);
    }
}

static int EraseAll(GenericContainer *gen, const void *elem)
{
    if (gen == NULL || gen->vTable == NULL) return BadArg("iGeneric.EraseAll");
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.EraseAll((List *)gen, elem);
    case GENERIC_DLIST:  return iDlist.EraseAll((Dlist *)gen, elem);
    case GENERIC_VECTOR: return iVector.EraseAll((Vector *)gen, elem);
    case GENERIC_STRING: return istrCollection.EraseAll((strCollection *)gen, (const char *)elem);
    case GENERIC_WSTRING:return iWstrCollection.EraseAll((WstrCollection *)gen, (const wchar_t *)elem);
    default:             return gen->vTable->EraseAll(gen, elem);
    }
}

static int Finalize(GenericContainer *gen)
{
    if (gen == NULL || gen->vTable == NULL) return BadArg("iGeneric.Finalize");
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.Finalize((List *)gen);
    case GENERIC_DLIST:  return iDlist.Finalize((Dlist *)gen);
    case GENERIC_VECTOR: return iVector.Finalize((Vector *)gen);
    case GENERIC_STRING: return istrCollection.Finalize((strCollection *)gen);
    case GENERIC_WSTRING:return iWstrCollection.Finalize((WstrCollection *)gen);
    default:             return gen->vTable->Finalize(gen);
    }
}

static void Apply(GenericContainer *gen, int (*applyFn)(void *, void *), void *arg)
{
    if (gen == NULL || gen->vTable == NULL) {
        BadArgVoid("iGeneric.Apply");
        return;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:
        (void)iList.Apply((List *)gen, applyFn, arg); return;
    case GENERIC_DLIST:
        (void)iDlist.Apply((Dlist *)gen, applyFn, arg); return;
    case GENERIC_VECTOR:
        (void)iVector.Apply((Vector *)gen, applyFn, arg); return;
    case GENERIC_STRING:
        (void)istrCollection.Apply((strCollection *)gen,
                                    (int (*)(char *, void *))applyFn, arg); return;
    case GENERIC_WSTRING:
        (void)iWstrCollection.Apply((WstrCollection *)gen,
                                     (int (*)(wchar_t *, void *))applyFn, arg); return;
    default:
        gen->vTable->Apply(gen, applyFn, arg); return;
    }
}

static int Equal(const GenericContainer *left, const GenericContainer *right)
{
    if (left == NULL || left->vTable == NULL || right == NULL) return BadArg("iGeneric.Equal");
    switch (KindOf(left)) {
    case GENERIC_LIST:   return iList.Equal((const List *)left, (const List *)right);
    case GENERIC_DLIST:  return iDlist.Equal((const Dlist *)left, (const Dlist *)right);
    case GENERIC_VECTOR: return iVector.Equal((const Vector *)left, (const Vector *)right);
    case GENERIC_STRING: return istrCollection.Equal((const strCollection *)left, (const strCollection *)right);
    case GENERIC_WSTRING:return iWstrCollection.Equal((const WstrCollection *)left, (const WstrCollection *)right);
    default:             return left->vTable->Equal(left, right);
    }
}

static GenericContainer *Copy(const GenericContainer *src)
{
    if (src == NULL || src->vTable == NULL) {
        BadArg("iGeneric.Copy");
        return NULL;
    }
    switch (KindOf(src)) {
    case GENERIC_LIST:   return (GenericContainer *)iList.Copy((const List *)src);
    case GENERIC_DLIST:  return (GenericContainer *)iDlist.Copy((const Dlist *)src);
    case GENERIC_VECTOR: return (GenericContainer *)iVector.Copy((const Vector *)src);
    case GENERIC_STRING: return (GenericContainer *)istrCollection.Copy((const strCollection *)src);
    case GENERIC_WSTRING:return (GenericContainer *)iWstrCollection.Copy((const WstrCollection *)src);
    default:             return src->vTable->Copy(src);
    }
}

static ErrorFunction SetErrorFunction(GenericContainer *gen, ErrorFunction fn)
{
    if (gen == NULL || gen->vTable == NULL) {
        BadArg("iGeneric.SetErrorFunction");
        return iError.RaiseError;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.SetErrorFunction((List *)gen, fn);
    case GENERIC_DLIST:  return iDlist.SetErrorFunction((Dlist *)gen, fn);
    case GENERIC_VECTOR: return iVector.SetErrorFunction((Vector *)gen, fn);
    case GENERIC_STRING: return istrCollection.SetErrorFunction((strCollection *)gen, fn);
    case GENERIC_WSTRING:return iWstrCollection.SetErrorFunction((WstrCollection *)gen, fn);
    default:             return gen->vTable->SetErrorFunction(gen, fn);
    }
}

static size_t Sizeof(const GenericContainer *gen)
{
    if (gen == NULL || gen->vTable == NULL) {
        BadArg("iGeneric.Sizeof");
        return 0;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.Sizeof((const List *)gen);
    case GENERIC_DLIST:  return iDlist.Sizeof((const Dlist *)gen);
    case GENERIC_VECTOR: return iVector.Sizeof((const Vector *)gen);
    case GENERIC_STRING: return istrCollection.Sizeof((const strCollection *)gen);
    case GENERIC_WSTRING:return iWstrCollection.Sizeof((const WstrCollection *)gen);
    default:             return gen->vTable->Sizeof(gen);
    }
}

/* A registry is needed only for iterators that do not carry a standard magic
 * marker (custom protocol objects and string collections).  Known list,
 * dlist, and vector iterators are recovered by their marker in Delete below.
 */
typedef struct IteratorOwner IteratorOwner;
struct IteratorOwner {
    Iterator *iterator;
    GenericContainer *container;
    IteratorOwner *next;
};
static IteratorOwner *IteratorOwners;

static void RememberIterator(GenericContainer *container, Iterator *iterator)
{
    IteratorOwner *entry;
    if (container == NULL || iterator == NULL)
        return;
    entry = (IteratorOwner *)malloc(sizeof(*entry));
    if (entry == NULL)
        return;
    entry->iterator = iterator;
    entry->container = container;
    entry->next = IteratorOwners;
    IteratorOwners = entry;
}

static GenericContainer *ForgetIterator(Iterator *iterator)
{
    IteratorOwner **cursor = &IteratorOwners;
    while (*cursor != NULL) {
        if ((*cursor)->iterator == iterator) {
            IteratorOwner *entry = *cursor;
            GenericContainer *container = entry->container;
            *cursor = entry->next;
            free(entry);
            return container;
        }
        cursor = &(*cursor)->next;
    }
    return NULL;
}

static Iterator *NewIterator(GenericContainer *gen)
{
    Iterator *iterator;
    if (gen == NULL || gen->vTable == NULL) {
        BadArg("iGeneric.NewIterator");
        return NULL;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:   iterator = iList.NewIterator((List *)gen); break;
    case GENERIC_DLIST:  iterator = iDlist.NewIterator((Dlist *)gen); break;
    case GENERIC_VECTOR: iterator = iVector.NewIterator((Vector *)gen); break;
    case GENERIC_STRING: iterator = istrCollection.NewIterator((strCollection *)gen); break;
    case GENERIC_WSTRING:iterator = iWstrCollection.NewIterator((WstrCollection *)gen); break;
    default:             iterator = gen->vTable->NewIterator(gen); break;
    }
    if (iterator != NULL && (KindOf(gen) == GENERIC_UNKNOWN ||
                             KindOf(gen) == GENERIC_STRING ||
                             KindOf(gen) == GENERIC_WSTRING))
        RememberIterator(gen, iterator);
    return iterator;
}

static int InitIterator(GenericContainer *gen, void *buffer)
{
    int result;
    if (gen == NULL || gen->vTable == NULL || buffer == NULL)
        return BadArg("iGeneric.InitIterator");
    switch (KindOf(gen)) {
    case GENERIC_LIST:   result = iList.InitIterator((List *)gen, buffer); break;
    case GENERIC_DLIST:  result = iDlist.InitIterator((Dlist *)gen, buffer); break;
    case GENERIC_VECTOR: result = iVector.InitIterator((Vector *)gen, buffer); break;
    case GENERIC_STRING: result = istrCollection.InitIterator((strCollection *)gen, buffer); break;
    case GENERIC_WSTRING:result = iWstrCollection.InitIterator((WstrCollection *)gen, buffer); break;
    default:             result = gen->vTable->InitIterator(gen, buffer); break;
    }
    if (result > 0 && (KindOf(gen) == GENERIC_UNKNOWN ||
                       KindOf(gen) == GENERIC_STRING ||
                       KindOf(gen) == GENERIC_WSTRING))
        RememberIterator(gen, (Iterator *)buffer);
    return result;
}

static int DeleteKnownIterator(Iterator *iterator, long long magic)
{
    if (magic == LIST_MAGIC_NUMBER)
        return iList.DeleteIterator(iterator);
    if (magic == DLIST_MAGIC_NUMBER)
        return iDlist.DeleteIterator(iterator);
    if (magic == VECTOR_MAGIC_NUMBER)
        return iVector.DeleteIterator(iterator);
    if (magic == STRINGLIST_MAGIC_NUMBER) {
        /* StringList's interface is generated and not part of the concrete
           generic set.  Its owner is still directly after Magic; the
           generated table supplies the DeleteIterator operation. */
        GenericContainer *owner;
        memcpy(&owner, (const unsigned char *)iterator + sizeof(Iterator) + sizeof(long long), sizeof(owner));
        if (owner != NULL && owner->vTable != NULL)
            return owner->vTable->DeleteIterator(iterator);
    }
    return CONTAINER_ERROR_WRONG_ITERATOR;
}

static int DeleteIterator(Iterator *iterator)
{
    GenericContainer *owner;
    long long magic = 0;

    if (iterator == NULL)
        return BadArg("iGeneric.DeleteIterator");
    owner = ForgetIterator(iterator);
    if (owner != NULL && owner->vTable != NULL) {
        switch (KindOf(owner)) {
        case GENERIC_LIST:    return iList.DeleteIterator(iterator);
        case GENERIC_DLIST:   return iDlist.DeleteIterator(iterator);
        case GENERIC_VECTOR:  return iVector.DeleteIterator(iterator);
        case GENERIC_STRING:  return istrCollection.DeleteIterator(iterator);
        case GENERIC_WSTRING: return iWstrCollection.DeleteIterator(iterator);
        default:              return owner->vTable->DeleteIterator(iterator);
        }
    }
    memcpy(&magic, (const unsigned char *)iterator + sizeof(Iterator), sizeof(magic));
    return DeleteKnownIterator(iterator, magic);
}

static size_t SizeofIterator(const GenericContainer *gen)
{
    if (gen == NULL || gen->vTable == NULL) {
        BadArg("iGeneric.SizeofIterator");
        return 0;
    }
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.SizeofIterator((const List *)gen);
    case GENERIC_DLIST:  return iDlist.SizeofIterator((const Dlist *)gen);
    case GENERIC_VECTOR: return iVector.SizeofIterator((const Vector *)gen);
    case GENERIC_STRING: return istrCollection.SizeofIterator((const strCollection *)gen);
    case GENERIC_WSTRING:return iWstrCollection.SizeofIterator((const WstrCollection *)gen);
    default:             return gen->vTable->SizeofIterator(gen);
    }
}

static int Save(const GenericContainer *gen, FILE *stream, SaveFunction saveFn, void *arg)
{
    if (gen == NULL || gen->vTable == NULL) return BadArg("iGeneric.Save");
    switch (KindOf(gen)) {
    case GENERIC_LIST:   return iList.Save((const List *)gen, stream, saveFn, arg);
    case GENERIC_DLIST:  return iDlist.Save((const Dlist *)gen, stream, saveFn, arg);
    case GENERIC_VECTOR: return iVector.Save((const Vector *)gen, stream, saveFn, arg);
    case GENERIC_STRING: return istrCollection.Save((const strCollection *)gen, stream, saveFn, arg);
    case GENERIC_WSTRING:return iWstrCollection.Save((const WstrCollection *)gen, stream, saveFn, arg);
    default:             return gen->vTable->Save(gen, stream, saveFn, arg);
    }
}

GenericContainerInterface iGeneric = {
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
};
