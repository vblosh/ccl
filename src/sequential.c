#include "containers.h"
#include "ccl_internal.h"

/* The concrete sequential interfaces put Load and GetElementSize between the
 * generic Save entry and Add.  The two reserved fields in
 * SequentialContainerInterface mirror that layout; generic operations are
 * delegated to iGeneric so NULL handling and iterator ownership stay in one
 * implementation. */
struct SequentialContainer {
	SequentialContainerInterface *vTable;
	size_t Size;
	unsigned Flags;
	size_t ElementSize;
};

static int BadArg(const char *name)
{
	iError.RaiseError(name, CONTAINER_ERROR_BADARG);
	return CONTAINER_ERROR_BADARG;
}

static int Unsupported(const char *name)
{
	iError.RaiseError(name, CONTAINER_ERROR_INCOMPATIBLE);
	return CONTAINER_ERROR_INCOMPATIBLE;
}

static int IsStringTable(const SequentialContainer *sc)
{
	const void *table;
	if (sc == NULL || sc->vTable == NULL)
		return 0;
	table = (const void *)sc->vTable;
	return table == (const void *)&istrCollection ||
	       table == (const void *)&iWstrCollection;
}

static size_t Size(const SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.Size");
		return 0;
	}
	return iGeneric.Size((const GenericContainer *)sc);
}

static unsigned GetFlags(const SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.GetFlags");
		return 0;
	}
	return iGeneric.GetFlags((const GenericContainer *)sc);
}

static unsigned SetFlags(SequentialContainer *sc, unsigned flags)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.SetFlags");
		return 0;
	}
	return iGeneric.SetFlags((GenericContainer *)sc, flags);
}

static int Clear(SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) return BadArg("iSequentialContainer.Clear");
	return iGeneric.Clear((GenericContainer *)sc);
}

static int Contains(const SequentialContainer *sc, const void *value)
{
	if (sc == NULL || sc->vTable == NULL) return BadArg("iSequentialContainer.Contains");
	return iGeneric.Contains((const GenericContainer *)sc, value);
}

static int Erase(SequentialContainer *sc, const void *value)
{
	if (sc == NULL || sc->vTable == NULL) return BadArg("iSequentialContainer.Erase");
	return iGeneric.Erase((GenericContainer *)sc, value);
}

static int EraseAll(SequentialContainer *sc, const void *value)
{
	if (sc == NULL || sc->vTable == NULL) return BadArg("iSequentialContainer.EraseAll");
	return iGeneric.EraseAll((GenericContainer *)sc, value);
}

static int Finalize(SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) return BadArg("iSequentialContainer.Finalize");
	return iGeneric.Finalize((GenericContainer *)sc);
}

static void Apply(SequentialContainer *sc, int (*fn)(void *, void *), void *arg)
{
	if (sc == NULL || sc->vTable == NULL) {
		iError.RaiseError("iSequentialContainer.Apply", CONTAINER_ERROR_BADARG);
		return;
	}
	iGeneric.Apply((GenericContainer *)sc, fn, arg);
}

static int Equal(const SequentialContainer *left, const SequentialContainer *right)
{
	if (left == NULL || left->vTable == NULL || right == NULL)
		return BadArg("iSequentialContainer.Equal");
	return iGeneric.Equal((const GenericContainer *)left,
	                      (const GenericContainer *)right);
}

static SequentialContainer *Copy(const SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.Copy");
		return NULL;
	}
	return (SequentialContainer *)iGeneric.Copy((const GenericContainer *)sc);
}

static ErrorFunction SetErrorFunction(SequentialContainer *sc, ErrorFunction fn)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.SetErrorFunction");
		return iError.RaiseError;
	}
	return iGeneric.SetErrorFunction((GenericContainer *)sc, fn);
}

static size_t Sizeof(const SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.Sizeof");
		return 0;
	}
	return iGeneric.Sizeof((const GenericContainer *)sc);
}

static Iterator *NewIterator(SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.NewIterator");
		return NULL;
	}
	return iGeneric.NewIterator((GenericContainer *)sc);
}

static int InitIterator(SequentialContainer *sc, void *buf)
{
	if (sc == NULL || sc->vTable == NULL || buf == NULL)
		return BadArg("iSequentialContainer.InitIterator");
	return iGeneric.InitIterator((GenericContainer *)sc, buf);
}

static int DeleteIterator(Iterator *iterator)
{
	return iGeneric.DeleteIterator(iterator);
}

static size_t SizeofIterator(const SequentialContainer *sc)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.SizeofIterator");
		return 0;
	}
	return iGeneric.SizeofIterator((const GenericContainer *)sc);
}

static int Save(const SequentialContainer *sc, FILE *stream,
			SaveFunction saveFn, void *arg)
{
	if (sc == NULL || sc->vTable == NULL) return BadArg("iSequentialContainer.Save");
	return iGeneric.Save((const GenericContainer *)sc, stream, saveFn, arg);
}

/*-------------------------------------------------------------------------*/
/* Sequential operations                                                   */

static int Add(SequentialContainer *sc, const void *element)
{
	if (sc == NULL || sc->vTable == NULL || element == NULL)
		return BadArg("iSequentialContainer.Add");
	if (IsStringTable(sc)) {
		/* A string collection has variable-sized values; this adapter's Pop
		 * contract is fixed-size and therefore cannot represent it safely. */
		return Unsupported("iSequentialContainer.Add");
	}
	return sc->vTable->Add(sc, element);
}

static void *GetElement(const SequentialContainer *sc, size_t idx)
{
	if (sc == NULL || sc->vTable == NULL) {
		BadArg("iSequentialContainer.GetElement");
		return NULL;
	}
	if (IsStringTable(sc))
		return NULL; /* variable-sized string values are not this ABI */
	return sc->vTable->GetElement(sc, idx);
}

static int Push(SequentialContainer *sc, void *element)
{
	if (sc == NULL || sc->vTable == NULL || element == NULL)
		return BadArg("iSequentialContainer.Push");
	if (IsStringTable(sc))
		return Unsupported("iSequentialContainer.Push");
	return sc->vTable->Push(sc, element);
}

static int Pop(SequentialContainer *sc, void *result)
{
	if (sc == NULL || sc->vTable == NULL || result == NULL)
		return BadArg("iSequentialContainer.Pop");
	if (IsStringTable(sc))
		return Unsupported("iSequentialContainer.Pop");
	return sc->vTable->Pop(sc, result);
}

static int InsertAt(SequentialContainer *sc, size_t idx, const void *value)
{
	if (sc == NULL || sc->vTable == NULL || value == NULL)
		return BadArg("iSequentialContainer.InsertAt");
	if (IsStringTable(sc))
		return Unsupported("iSequentialContainer.InsertAt");
	return sc->vTable->InsertAt(sc, idx, value);
}

static int EraseAt(SequentialContainer *sc, size_t idx)
{
	if (sc == NULL || sc->vTable == NULL) return BadArg("iSequentialContainer.EraseAt");
	if (IsStringTable(sc)) return Unsupported("iSequentialContainer.EraseAt");
	return sc->vTable->EraseAt(sc, idx);
}

static int ReplaceAt(SequentialContainer *sc, size_t idx, const void *value)
{
	if (sc == NULL || sc->vTable == NULL || value == NULL)
		return BadArg("iSequentialContainer.ReplaceAt");
	if (IsStringTable(sc)) return Unsupported("iSequentialContainer.ReplaceAt");
	return sc->vTable->ReplaceAt(sc, idx, value);
}

static int IndexOf(const SequentialContainer *sc, const void *value,
			  void *args, size_t *result)
{
	if (sc == NULL || sc->vTable == NULL || value == NULL || result == NULL)
		return BadArg("iSequentialContainer.IndexOf");
	if (IsStringTable(sc)) return Unsupported("iSequentialContainer.IndexOf");
	return sc->vTable->IndexOf(sc, value, args, result);
}

static int Append(SequentialContainer *destination, SequentialContainer *source)
{
	Iterator *iterator = NULL;
	void *element;
	int result = 1;

	if (destination == NULL || source == NULL ||
	    destination->vTable == NULL || source->vTable == NULL)
		return BadArg("iSequentialContainer.Append");
	if (destination == source)
		return BadArg("iSequentialContainer.Append");
	if (IsStringTable(destination) || IsStringTable(source))
		return Unsupported("iSequentialContainer.Append");
	if (destination->ElementSize != source->ElementSize) {
		iError.RaiseError("iSequentialContainer.Append", CONTAINER_ERROR_INCOMPATIBLE);
		return CONTAINER_ERROR_INCOMPATIBLE;
	}

	iterator = NewIterator(source);
	if (iterator == NULL)
		return CONTAINER_ERROR_NOMEMORY;
	if (iterator->GetFirst == NULL || iterator->GetNext == NULL) {
		result = CONTAINER_ERROR_WRONG_ITERATOR;
		goto cleanup;
	}
	for (element = iterator->GetFirst(iterator);
	     element != NULL;
	     element = iterator->GetNext(iterator)) {
		result = Add(destination, element);
		if (result <= 0)
			break;
	}

cleanup:
	/* DeleteIterator is deliberately reached for success and every Add/error
	 * exit; the generic registry also handles placement and string iterators. */
	if (DeleteIterator(iterator) < 0 && result > 0)
		result = CONTAINER_ERROR_WRONG_ITERATOR;
	return result > 0 ? 1 : result;
}

SequentialContainerInterface iSequentialContainer = {
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
	NULL, /* Load: reserved for concrete-prefix alignment */
	NULL, /* GetElementSize: reserved for concrete-prefix alignment */
	Add,
	GetElement,
	Push,
	Pop,
	InsertAt,
	EraseAt,
	ReplaceAt,
	IndexOf,
	Append,
};
