#include "containers.h"
#include "ccl_internal.h"

#include <stdint.h>

typedef enum RangeKind {
    RANGE_GENERIC,
    RANGE_ARRAY,
    RANGE_FILTER,
    RANGE_TRANSFORM,
    RANGE_TAKE,
    RANGE_DROP,
    RANGE_TAKE_WHILE,
    RANGE_DROP_WHILE,
    RANGE_CONCAT
} RangeKind;

struct Range {
    RangeKind Kind;
    size_t ElementSize;
    const ContainerAllocator *Allocator;
    ErrorFunction RaiseError;
    Range *Parent;
    union {
        struct {
            GenericContainer *Container;
        } Generic;
        struct {
            const unsigned char *Data;
            size_t Count;
        } Array;
        struct {
            Range *Source;
            RangePredicate Predicate;
            void *Arg;
        } Predicate;
        struct {
            Range *Source;
            RangeTransformFunction Function;
            void *Arg;
        } Transform;
        struct {
            Range *Source;
            size_t Count;
        } Count;
        struct {
            Range *Left;
            Range *Right;
        } Concat;
    } Data;
};

struct RangeCursor {
    RangeKind Kind;
    const Range *Owner;
    int Status;
    int Finished;
    union {
        struct {
            Iterator *Iterator;
            GenericContainer *Container;
            size_t Expected;
            size_t Produced;
            int Started;
        } Generic;
        struct {
            const unsigned char *Data;
            size_t Count;
            size_t Position;
            size_t ElementSize;
        } Array;
        struct {
            RangeCursor *Source;
            RangePredicate Predicate;
            void *Arg;
            int Matched;
        } Predicate;
        struct {
            RangeCursor *Source;
            RangeTransformFunction Function;
            void *Arg;
            void *Buffer;
        } Transform;
        struct {
            RangeCursor *Source;
            size_t Count;
            size_t Position;
        } Count;
        struct {
            RangeCursor *Left;
            RangeCursor *Right;
            int ReadingRight;
        } Concat;
    } Data;
};

static int AllocatorIsValid(const ContainerAllocator *allocator)
{
    return allocator != NULL && allocator->malloc != NULL &&
           allocator->free != NULL && allocator->realloc != NULL &&
           allocator->calloc != NULL;
}

static int RaiseGlobal(const char *name, int error)
{
    iError.RaiseError(name, error);
    return error;
}

static int RaiseRange(const Range *range, const char *name, int error)
{
    ErrorFunction function = iError.RaiseError;

    if (range != NULL && range->RaiseError != NULL)
        function = range->RaiseError;
    function(name, error);
    return error;
}

static Range *AllocateRange(const ContainerAllocator *allocator,
                            RangeKind kind, size_t elementSize)
{
    Range *result = allocator->calloc(1, sizeof(*result));

    if (result != NULL) {
        result->Kind = kind;
        result->ElementSize = elementSize;
        result->Allocator = allocator;
        result->RaiseError = iError.RaiseError;
    }
    return result;
}

static int FromGenericWithAllocator(GenericContainer *source,
                                    size_t elementSize,
                                    const ContainerAllocator *allocator,
                                    Range **result)
{
    Range *range;

    if (result == NULL)
        return RaiseGlobal("iRange.FromGeneric", CONTAINER_ERROR_BADARG);
    *result = NULL;
    if (source == NULL || source->vTable == NULL)
        return RaiseGlobal("iRange.FromGeneric", CONTAINER_ERROR_BADARG);
    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (!AllocatorIsValid(allocator))
        return RaiseGlobal("iRange.FromGeneric", CONTAINER_ERROR_BADARG);
    range = AllocateRange(allocator, RANGE_GENERIC, elementSize);
    if (range == NULL)
        return RaiseGlobal("iRange.FromGeneric", CONTAINER_ERROR_NOMEMORY);
    range->Data.Generic.Container = source;
    *result = range;
    return 1;
}

static int FromGeneric(GenericContainer *source, size_t elementSize,
                       Range **result)
{
    return FromGenericWithAllocator(source, elementSize, CurrentAllocator,
                                    result);
}

static int FromSequentialWithAllocator(SequentialContainer *source,
                                       const ContainerAllocator *allocator,
                                       Range **result)
{
    GenericContainer *generic;
    size_t elementSize;

    if (result == NULL)
        return RaiseGlobal("iRange.FromSequential", CONTAINER_ERROR_BADARG);
    *result = NULL;
    if (source == NULL)
        return RaiseGlobal("iRange.FromSequential", CONTAINER_ERROR_BADARG);
    generic = (GenericContainer *)source;
    if (generic->vTable == NULL)
        return RaiseGlobal("iRange.FromSequential", CONTAINER_ERROR_BADARG);
    elementSize = iSequentialContainer.GetElementSize(source);
    if (elementSize == 0)
        return CONTAINER_ERROR_INCOMPATIBLE;
    return FromGenericWithAllocator((GenericContainer *)source, elementSize,
                                    allocator, result);
}

static int FromSequential(SequentialContainer *source, Range **result)
{
    return FromSequentialWithAllocator(source, CurrentAllocator, result);
}

static int FromArrayWithAllocator(const void *data, size_t count,
                                  size_t elementSize,
                                  const ContainerAllocator *allocator,
                                  Range **result)
{
    Range *range;

    if (result == NULL)
        return RaiseGlobal("iRange.FromArray", CONTAINER_ERROR_BADARG);
    *result = NULL;
    if (elementSize == 0 || (count != 0 && data == NULL) ||
        (count != 0 && elementSize > SIZE_MAX / count))
        return RaiseGlobal("iRange.FromArray", CONTAINER_ERROR_BADARG);
    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (!AllocatorIsValid(allocator))
        return RaiseGlobal("iRange.FromArray", CONTAINER_ERROR_BADARG);
    range = AllocateRange(allocator, RANGE_ARRAY, elementSize);
    if (range == NULL)
        return RaiseGlobal("iRange.FromArray", CONTAINER_ERROR_NOMEMORY);
    range->Data.Array.Data = data;
    range->Data.Array.Count = count;
    *result = range;
    return 1;
}

static int FromArray(const void *data, size_t count, size_t elementSize,
                     Range **result)
{
    return FromArrayWithAllocator(data, count, elementSize, CurrentAllocator,
                                  result);
}

static int ValidateRangeSlot(Range **range, const char *name)
{
    if (range == NULL || *range == NULL || (*range)->Parent != NULL)
        return RaiseGlobal(name, CONTAINER_ERROR_BADARG);
    return 1;
}

static int WrapPredicate(Range **range, RangeKind kind,
                         RangePredicate predicate, void *arg,
                         const char *name)
{
    Range *source;
    Range *wrapper;

    if (ValidateRangeSlot(range, name) < 0)
        return CONTAINER_ERROR_BADARG;
    source = *range;
    if (predicate == NULL)
        return RaiseRange(source, name, CONTAINER_ERROR_BADARG);
    wrapper = AllocateRange(source->Allocator, kind, source->ElementSize);
    if (wrapper == NULL)
        return RaiseRange(source, name, CONTAINER_ERROR_NOMEMORY);
    wrapper->RaiseError = source->RaiseError;
    wrapper->Data.Predicate.Source = source;
    wrapper->Data.Predicate.Predicate = predicate;
    wrapper->Data.Predicate.Arg = arg;
    source->Parent = wrapper;
    *range = wrapper;
    return 1;
}

static int Filter(Range **range, RangePredicate predicate, void *arg)
{
    return WrapPredicate(range, RANGE_FILTER, predicate, arg,
                         "iRange.Filter");
}

static int TakeWhile(Range **range, RangePredicate predicate, void *arg)
{
    return WrapPredicate(range, RANGE_TAKE_WHILE, predicate, arg,
                         "iRange.TakeWhile");
}

static int DropWhile(Range **range, RangePredicate predicate, void *arg)
{
    return WrapPredicate(range, RANGE_DROP_WHILE, predicate, arg,
                         "iRange.DropWhile");
}

static int Transform(Range **range, size_t outputElementSize,
                     RangeTransformFunction transform, void *arg)
{
    Range *source;
    Range *wrapper;

    if (ValidateRangeSlot(range, "iRange.Transform") < 0)
        return CONTAINER_ERROR_BADARG;
    source = *range;
    if (outputElementSize == 0 || transform == NULL)
        return RaiseRange(source, "iRange.Transform", CONTAINER_ERROR_BADARG);
    wrapper = AllocateRange(source->Allocator, RANGE_TRANSFORM,
                            outputElementSize);
    if (wrapper == NULL)
        return RaiseRange(source, "iRange.Transform",
                          CONTAINER_ERROR_NOMEMORY);
    wrapper->RaiseError = source->RaiseError;
    wrapper->Data.Transform.Source = source;
    wrapper->Data.Transform.Function = transform;
    wrapper->Data.Transform.Arg = arg;
    source->Parent = wrapper;
    *range = wrapper;
    return 1;
}

static int WrapCount(Range **range, RangeKind kind, size_t count,
                     const char *name)
{
    Range *source;
    Range *wrapper;

    if (ValidateRangeSlot(range, name) < 0)
        return CONTAINER_ERROR_BADARG;
    source = *range;
    wrapper = AllocateRange(source->Allocator, kind, source->ElementSize);
    if (wrapper == NULL)
        return RaiseRange(source, name, CONTAINER_ERROR_NOMEMORY);
    wrapper->RaiseError = source->RaiseError;
    wrapper->Data.Count.Source = source;
    wrapper->Data.Count.Count = count;
    source->Parent = wrapper;
    *range = wrapper;
    return 1;
}

static int Take(Range **range, size_t count)
{
    return WrapCount(range, RANGE_TAKE, count, "iRange.Take");
}

static int Drop(Range **range, size_t count)
{
    return WrapCount(range, RANGE_DROP, count, "iRange.Drop");
}

static int Concat(Range **left, Range **right)
{
    Range *wrapper;

    if (left == NULL || right == NULL || *left == NULL || *right == NULL ||
        left == right || *left == *right || (*left)->Parent != NULL ||
        (*right)->Parent != NULL)
        return RaiseGlobal("iRange.Concat", CONTAINER_ERROR_BADARG);
    if ((*left)->ElementSize != (*right)->ElementSize)
        return RaiseRange(*left, "iRange.Concat",
                          CONTAINER_ERROR_INCOMPATIBLE);
    wrapper = AllocateRange((*left)->Allocator, RANGE_CONCAT,
                            (*left)->ElementSize);
    if (wrapper == NULL)
        return RaiseRange(*left, "iRange.Concat", CONTAINER_ERROR_NOMEMORY);
    wrapper->RaiseError = (*left)->RaiseError;
    wrapper->Data.Concat.Left = *left;
    wrapper->Data.Concat.Right = *right;
    (*left)->Parent = wrapper;
    (*right)->Parent = wrapper;
    *left = wrapper;
    *right = NULL;
    return 1;
}

static RangeCursor *AllocateCursor(const Range *range)
{
    RangeCursor *cursor = range->Allocator->calloc(1, sizeof(*cursor));

    if (cursor != NULL) {
        cursor->Kind = range->Kind;
        cursor->Owner = range;
    }
    return cursor;
}

static int DeleteCursorInternal(RangeCursor *cursor)
{
    const ContainerAllocator *allocator;
    int result = 1;
    int childResult;

    if (cursor == NULL)
        return 1;
    allocator = cursor->Owner->Allocator;
    switch (cursor->Kind) {
    case RANGE_GENERIC:
        if (cursor->Data.Generic.Iterator != NULL)
            result = iGeneric.DeleteIterator(cursor->Data.Generic.Iterator);
        break;
    case RANGE_FILTER:
    case RANGE_TAKE_WHILE:
    case RANGE_DROP_WHILE:
        result = DeleteCursorInternal(cursor->Data.Predicate.Source);
        break;
    case RANGE_TRANSFORM:
        childResult = DeleteCursorInternal(cursor->Data.Transform.Source);
        if (childResult < 0)
            result = childResult;
        if (cursor->Data.Transform.Buffer != NULL)
            allocator->free(cursor->Data.Transform.Buffer);
        break;
    case RANGE_TAKE:
    case RANGE_DROP:
        result = DeleteCursorInternal(cursor->Data.Count.Source);
        break;
    case RANGE_CONCAT:
        result = DeleteCursorInternal(cursor->Data.Concat.Left);
        childResult = DeleteCursorInternal(cursor->Data.Concat.Right);
        if (result > 0 && childResult < 0)
            result = childResult;
        break;
    case RANGE_ARRAY:
        break;
    }
    allocator->free(cursor);
    return result < 0 ? result : 1;
}

static int OpenInternal(const Range *range, RangeCursor **result)
{
    RangeCursor *cursor;
    int status;

    cursor = AllocateCursor(range);
    if (cursor == NULL)
        return RaiseRange(range, "iRange.Open", CONTAINER_ERROR_NOMEMORY);
    switch (range->Kind) {
    case RANGE_GENERIC:
        cursor->Data.Generic.Container = range->Data.Generic.Container;
        cursor->Data.Generic.Expected =
            iGeneric.Size(range->Data.Generic.Container);
        cursor->Data.Generic.Iterator =
            iGeneric.NewIterator(range->Data.Generic.Container);
        if (cursor->Data.Generic.Iterator == NULL) {
            range->Allocator->free(cursor);
            return CONTAINER_ERROR_NOMEMORY;
        }
        if (cursor->Data.Generic.Iterator->GetFirst == NULL ||
            cursor->Data.Generic.Iterator->GetNext == NULL) {
            DeleteCursorInternal(cursor);
            return RaiseRange(range, "iRange.Open",
                              CONTAINER_ERROR_WRONG_ITERATOR);
        }
        break;
    case RANGE_ARRAY:
        cursor->Data.Array.Data = range->Data.Array.Data;
        cursor->Data.Array.Count = range->Data.Array.Count;
        cursor->Data.Array.ElementSize = range->ElementSize;
        break;
    case RANGE_FILTER:
    case RANGE_TAKE_WHILE:
    case RANGE_DROP_WHILE:
        status = OpenInternal(range->Data.Predicate.Source,
                              &cursor->Data.Predicate.Source);
        if (status < 0) {
            range->Allocator->free(cursor);
            return status;
        }
        cursor->Data.Predicate.Predicate = range->Data.Predicate.Predicate;
        cursor->Data.Predicate.Arg = range->Data.Predicate.Arg;
        break;
    case RANGE_TRANSFORM:
        status = OpenInternal(range->Data.Transform.Source,
                              &cursor->Data.Transform.Source);
        if (status < 0) {
            range->Allocator->free(cursor);
            return status;
        }
        cursor->Data.Transform.Buffer =
            range->Allocator->malloc(range->ElementSize);
        if (cursor->Data.Transform.Buffer == NULL) {
            DeleteCursorInternal(cursor);
            return RaiseRange(range, "iRange.Open",
                              CONTAINER_ERROR_NOMEMORY);
        }
        cursor->Data.Transform.Function = range->Data.Transform.Function;
        cursor->Data.Transform.Arg = range->Data.Transform.Arg;
        break;
    case RANGE_TAKE:
    case RANGE_DROP:
        status = OpenInternal(range->Data.Count.Source,
                              &cursor->Data.Count.Source);
        if (status < 0) {
            range->Allocator->free(cursor);
            return status;
        }
        cursor->Data.Count.Count = range->Data.Count.Count;
        break;
    case RANGE_CONCAT:
        status = OpenInternal(range->Data.Concat.Left,
                              &cursor->Data.Concat.Left);
        if (status < 0) {
            range->Allocator->free(cursor);
            return status;
        }
        status = OpenInternal(range->Data.Concat.Right,
                              &cursor->Data.Concat.Right);
        if (status < 0) {
            DeleteCursorInternal(cursor);
            return status;
        }
        break;
    }
    *result = cursor;
    return 1;
}

static int Open(const Range *range, RangeCursor **result)
{
    if (result == NULL)
        return RaiseGlobal("iRange.Open", CONTAINER_ERROR_BADARG);
    *result = NULL;
    if (range == NULL || range->Parent != NULL)
        return RaiseGlobal("iRange.Open", CONTAINER_ERROR_BADARG);
    return OpenInternal(range, result);
}

static int CursorError(RangeCursor *cursor, int error)
{
    cursor->Status = error;
    return error;
}

static int NextGeneric(RangeCursor *cursor, const void **element)
{
    Iterator *iterator = cursor->Data.Generic.Iterator;
    void *current;
    size_t currentSize;

    if (!cursor->Data.Generic.Started) {
        cursor->Data.Generic.Started = 1;
        current = iterator->GetFirst(iterator);
    } else {
        current = iterator->GetNext(iterator);
    }
    if (current != NULL) {
        if (cursor->Data.Generic.Produced >= cursor->Data.Generic.Expected)
            return CursorError(cursor, CONTAINER_ERROR_OBJECT_CHANGED);
        ++cursor->Data.Generic.Produced;
        *element = current;
        return 1;
    }
    if (cursor->Data.Generic.Produced < cursor->Data.Generic.Expected)
        return CursorError(cursor, CONTAINER_ERROR_OBJECT_CHANGED);
    currentSize = iGeneric.Size(cursor->Data.Generic.Container);
    if (currentSize != cursor->Data.Generic.Expected)
        return CursorError(cursor, CONTAINER_ERROR_OBJECT_CHANGED);
    if (cursor->Data.Generic.Expected != 0 &&
        iterator->GetPosition != NULL &&
        iterator->GetPosition(iterator) == (size_t)-1)
        return CursorError(cursor, CONTAINER_ERROR_OBJECT_CHANGED);
    cursor->Finished = 1;
    return 0;
}

static int NextArray(RangeCursor *cursor, const void **element)
{
    if (cursor->Data.Array.Position >= cursor->Data.Array.Count) {
        cursor->Finished = 1;
        return 0;
    }
    *element = cursor->Data.Array.Data +
               cursor->Data.Array.Position * cursor->Data.Array.ElementSize;
    ++cursor->Data.Array.Position;
    return 1;
}

static int NextFilter(RangeCursor *cursor, const void **element)
{
    int status;

    for (;;) {
        status = iRange.Next(cursor->Data.Predicate.Source, element);
        if (status <= 0)
            return status < 0 ? CursorError(cursor, status) : status;
        status = cursor->Data.Predicate.Predicate(*element,
                                                   cursor->Data.Predicate.Arg);
        if (status < 0)
            return CursorError(cursor, status);
        if (status > 0)
            return 1;
    }
}

static int NextTransform(RangeCursor *cursor, const void **element)
{
    const void *input;
    int status = iRange.Next(cursor->Data.Transform.Source, &input);

    if (status <= 0)
        return status < 0 ? CursorError(cursor, status) : status;
    status = cursor->Data.Transform.Function(input,
                                             cursor->Data.Transform.Buffer,
                                             cursor->Data.Transform.Arg);
    if (status < 0)
        return CursorError(cursor, status);
    if (status == 0) {
        RaiseRange(cursor->Owner, "iRange.Next",
                   CONTAINER_ERROR_WRONGELEMENT);
        return CursorError(cursor, CONTAINER_ERROR_WRONGELEMENT);
    }
    *element = cursor->Data.Transform.Buffer;
    return 1;
}

static int NextTake(RangeCursor *cursor, const void **element)
{
    int status;

    if (cursor->Data.Count.Position >= cursor->Data.Count.Count) {
        cursor->Finished = 1;
        return 0;
    }
    status = iRange.Next(cursor->Data.Count.Source, element);
    if (status < 0)
        return CursorError(cursor, status);
    if (status == 0) {
        cursor->Finished = 1;
        return 0;
    }
    ++cursor->Data.Count.Position;
    return 1;
}

static int NextDrop(RangeCursor *cursor, const void **element)
{
    int status;

    while (cursor->Data.Count.Position < cursor->Data.Count.Count) {
        status = iRange.Next(cursor->Data.Count.Source, element);
        if (status <= 0)
            return status < 0 ? CursorError(cursor, status) : status;
        ++cursor->Data.Count.Position;
    }
    status = iRange.Next(cursor->Data.Count.Source, element);
    return status < 0 ? CursorError(cursor, status) : status;
}

static int NextTakeWhile(RangeCursor *cursor, const void **element)
{
    int status = iRange.Next(cursor->Data.Predicate.Source, element);

    if (status <= 0)
        return status < 0 ? CursorError(cursor, status) : status;
    status = cursor->Data.Predicate.Predicate(*element,
                                               cursor->Data.Predicate.Arg);
    if (status < 0)
        return CursorError(cursor, status);
    if (status == 0) {
        cursor->Finished = 1;
        *element = NULL;
        return 0;
    }
    return 1;
}

static int NextDropWhile(RangeCursor *cursor, const void **element)
{
    int status;

    while (!cursor->Data.Predicate.Matched) {
        status = iRange.Next(cursor->Data.Predicate.Source, element);
        if (status <= 0)
            return status < 0 ? CursorError(cursor, status) : status;
        status = cursor->Data.Predicate.Predicate(*element,
                                                   cursor->Data.Predicate.Arg);
        if (status < 0)
            return CursorError(cursor, status);
        if (status == 0) {
            cursor->Data.Predicate.Matched = 1;
            return 1;
        }
    }
    status = iRange.Next(cursor->Data.Predicate.Source, element);
    return status < 0 ? CursorError(cursor, status) : status;
}

static int NextConcat(RangeCursor *cursor, const void **element)
{
    int status;

    if (!cursor->Data.Concat.ReadingRight) {
        status = iRange.Next(cursor->Data.Concat.Left, element);
        if (status != 0)
            return status < 0 ? CursorError(cursor, status) : status;
        cursor->Data.Concat.ReadingRight = 1;
    }
    status = iRange.Next(cursor->Data.Concat.Right, element);
    return status < 0 ? CursorError(cursor, status) : status;
}

static int Next(RangeCursor *cursor, const void **element)
{
    int result;

    if (cursor == NULL)
        return RaiseGlobal("iRange.Next", CONTAINER_ERROR_BADARG);
    if (element == NULL) {
        RaiseRange(cursor->Owner, "iRange.Next", CONTAINER_ERROR_BADARG);
        return CursorError(cursor, CONTAINER_ERROR_BADARG);
    }
    *element = NULL;
    if (cursor->Status < 0)
        return cursor->Status;
    if (cursor->Finished)
        return 0;
    switch (cursor->Kind) {
    case RANGE_GENERIC: result = NextGeneric(cursor, element); break;
    case RANGE_ARRAY: result = NextArray(cursor, element); break;
    case RANGE_FILTER: result = NextFilter(cursor, element); break;
    case RANGE_TRANSFORM: result = NextTransform(cursor, element); break;
    case RANGE_TAKE: result = NextTake(cursor, element); break;
    case RANGE_DROP: result = NextDrop(cursor, element); break;
    case RANGE_TAKE_WHILE: result = NextTakeWhile(cursor, element); break;
    case RANGE_DROP_WHILE: result = NextDropWhile(cursor, element); break;
    case RANGE_CONCAT: result = NextConcat(cursor, element); break;
    default: result = CursorError(cursor, CONTAINER_INTERNAL_ERROR); break;
    }
    if (result == 0)
        cursor->Finished = 1;
    return result;
}

static int DeleteCursor(RangeCursor *cursor)
{
    if (cursor == NULL)
        return RaiseGlobal("iRange.DeleteCursor", CONTAINER_ERROR_BADARG);
    return DeleteCursorInternal(cursor);
}

static size_t GetElementSize(const Range *range)
{
    if (range == NULL) {
        RaiseGlobal("iRange.GetElementSize", CONTAINER_ERROR_BADARG);
        return 0;
    }
    return range->ElementSize;
}

static void SetErrorRecursive(Range *range, ErrorFunction function)
{
    range->RaiseError = function;
    switch (range->Kind) {
    case RANGE_FILTER:
    case RANGE_TAKE_WHILE:
    case RANGE_DROP_WHILE:
        SetErrorRecursive(range->Data.Predicate.Source, function);
        break;
    case RANGE_TRANSFORM:
        SetErrorRecursive(range->Data.Transform.Source, function);
        break;
    case RANGE_TAKE:
    case RANGE_DROP:
        SetErrorRecursive(range->Data.Count.Source, function);
        break;
    case RANGE_CONCAT:
        SetErrorRecursive(range->Data.Concat.Left, function);
        SetErrorRecursive(range->Data.Concat.Right, function);
        break;
    case RANGE_GENERIC:
    case RANGE_ARRAY:
        break;
    }
}

static ErrorFunction SetErrorFunction(Range *range, ErrorFunction function)
{
    ErrorFunction old;

    if (range == NULL) {
        RaiseGlobal("iRange.SetErrorFunction", CONTAINER_ERROR_BADARG);
        return iError.RaiseError;
    }
    old = range->RaiseError;
    if (function != NULL)
        SetErrorRecursive(range, function);
    return old;
}

static int FinishCursor(RangeCursor *cursor, int result)
{
    int closeResult = DeleteCursorInternal(cursor);

    if (result >= 0 && closeResult < 0)
        return closeResult;
    return result;
}

static int ForEach(const Range *range, RangeVisitFunction function, void *arg)
{
    RangeCursor *cursor;
    const void *element;
    int status;

    if (range == NULL || function == NULL)
        return RaiseRange(range, "iRange.ForEach", CONTAINER_ERROR_BADARG);
    status = Open(range, &cursor);
    if (status < 0)
        return status;
    while ((status = Next(cursor, &element)) > 0) {
        status = function(element, arg);
        if (status <= 0)
            return FinishCursor(cursor, status);
    }
    return FinishCursor(cursor, status < 0 ? status : 1);
}

static int Fold(const Range *range, void *accumulator,
                RangeFoldFunction function, void *arg)
{
    RangeCursor *cursor;
    const void *element;
    int status;

    if (range == NULL || accumulator == NULL || function == NULL)
        return RaiseRange(range, "iRange.Fold", CONTAINER_ERROR_BADARG);
    status = Open(range, &cursor);
    if (status < 0)
        return status;
    while ((status = Next(cursor, &element)) > 0) {
        status = function(accumulator, element, arg);
        if (status < 0)
            return FinishCursor(cursor, status);
        if (status == 0) {
            RaiseRange(range, "iRange.Fold", CONTAINER_ERROR_WRONGELEMENT);
            return FinishCursor(cursor, CONTAINER_ERROR_WRONGELEMENT);
        }
    }
    return FinishCursor(cursor, status < 0 ? status : 1);
}

static int FindIf(const Range *range, RangePredicate predicate, void *arg,
                  void *result)
{
    RangeCursor *cursor;
    const void *element;
    int status;

    if (range == NULL || predicate == NULL || result == NULL)
        return RaiseRange(range, "iRange.FindIf", CONTAINER_ERROR_BADARG);
    if (range->ElementSize == 0)
        return RaiseRange(range, "iRange.FindIf",
                          CONTAINER_ERROR_INCOMPATIBLE);
    status = Open(range, &cursor);
    if (status < 0)
        return status;
    while ((status = Next(cursor, &element)) > 0) {
        status = predicate(element, arg);
        if (status < 0)
            return FinishCursor(cursor, status);
        if (status > 0) {
            memcpy(result, element, range->ElementSize);
            return FinishCursor(cursor, 1);
        }
    }
    return FinishCursor(cursor, status);
}

static int AnyOf(const Range *range, RangePredicate predicate, void *arg)
{
    RangeCursor *cursor;
    const void *element;
    int status;

    if (range == NULL || predicate == NULL)
        return RaiseRange(range, "iRange.AnyOf", CONTAINER_ERROR_BADARG);
    status = Open(range, &cursor);
    if (status < 0)
        return status;
    while ((status = Next(cursor, &element)) > 0) {
        status = predicate(element, arg);
        if (status < 0)
            return FinishCursor(cursor, status);
        if (status > 0)
            return FinishCursor(cursor, 1);
    }
    return FinishCursor(cursor, status);
}

static int AllOf(const Range *range, RangePredicate predicate, void *arg)
{
    RangeCursor *cursor;
    const void *element;
    int status;

    if (range == NULL || predicate == NULL)
        return RaiseRange(range, "iRange.AllOf", CONTAINER_ERROR_BADARG);
    status = Open(range, &cursor);
    if (status < 0)
        return status;
    while ((status = Next(cursor, &element)) > 0) {
        status = predicate(element, arg);
        if (status < 0)
            return FinishCursor(cursor, status);
        if (status == 0)
            return FinishCursor(cursor, 0);
    }
    return FinishCursor(cursor, status < 0 ? status : 1);
}

static int CountIf(const Range *range, RangePredicate predicate, void *arg,
                   size_t *result)
{
    RangeCursor *cursor;
    const void *element;
    size_t count = 0;
    int status;

    if (range == NULL || predicate == NULL || result == NULL)
        return RaiseRange(range, "iRange.CountIf", CONTAINER_ERROR_BADARG);
    status = Open(range, &cursor);
    if (status < 0)
        return status;
    while ((status = Next(cursor, &element)) > 0) {
        status = predicate(element, arg);
        if (status < 0)
            return FinishCursor(cursor, status);
        if (status > 0) {
            if (count == SIZE_MAX) {
                RaiseRange(range, "iRange.CountIf",
                           CONTAINER_ERROR_BUFFEROVERFLOW);
                return FinishCursor(cursor,
                                    CONTAINER_ERROR_BUFFEROVERFLOW);
            }
            ++count;
        }
    }
    status = FinishCursor(cursor, status < 0 ? status : 1);
    if (status > 0)
        *result = count;
    return status;
}

static int ToVector(const Range *range, Vector **result)
{
    RangeCursor *cursor;
    Vector *vector;
    const void *element;
    int status;

    if (result == NULL)
        return RaiseRange(range, "iRange.ToVector", CONTAINER_ERROR_BADARG);
    *result = NULL;
    if (range == NULL)
        return RaiseGlobal("iRange.ToVector", CONTAINER_ERROR_BADARG);
    if (range->ElementSize == 0)
        return RaiseRange(range, "iRange.ToVector",
                          CONTAINER_ERROR_INCOMPATIBLE);
    vector = iVector.CreateWithAllocator(range->ElementSize, 4,
                                         range->Allocator);
    if (vector == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    status = Open(range, &cursor);
    if (status < 0) {
        iVector.Finalize(vector);
        return status;
    }
    while ((status = Next(cursor, &element)) > 0) {
        int addStatus = iVector.Add(vector, element);

        if (addStatus <= 0) {
            status = addStatus < 0 ? addStatus : CONTAINER_INTERNAL_ERROR;
            break;
        }
    }
    status = FinishCursor(cursor, status < 0 ? status : 1);
    if (status < 0) {
        iVector.Finalize(vector);
        return status;
    }
    *result = vector;
    return 1;
}

static int FinalizeInternal(Range *range)
{
    const ContainerAllocator *allocator = range->Allocator;

    switch (range->Kind) {
    case RANGE_FILTER:
    case RANGE_TAKE_WHILE:
    case RANGE_DROP_WHILE:
        FinalizeInternal(range->Data.Predicate.Source);
        break;
    case RANGE_TRANSFORM:
        FinalizeInternal(range->Data.Transform.Source);
        break;
    case RANGE_TAKE:
    case RANGE_DROP:
        FinalizeInternal(range->Data.Count.Source);
        break;
    case RANGE_CONCAT:
        FinalizeInternal(range->Data.Concat.Left);
        FinalizeInternal(range->Data.Concat.Right);
        break;
    case RANGE_GENERIC:
    case RANGE_ARRAY:
        break;
    }
    allocator->free(range);
    return 1;
}

static int Finalize(Range *range)
{
    if (range == NULL || range->Parent != NULL)
        return RaiseGlobal("iRange.Finalize", CONTAINER_ERROR_BADARG);
    return FinalizeInternal(range);
}

/* Fluent RangeQuery facade.  The underlying Range API remains the source of
 * truth for construction, lazy adaptors, terminals, and ownership. */
#define RANGE_QUERY_INITIALIZED 1u
#define RANGE_QUERY_FINALIZED   2u
#define RANGE_QUERY_CONSUMED    4u
#define RANGE_QUERY_SIGNATURE   0x52514C59u

static RangeQuery *QueryWhere(RangeQuery *, RangePredicate, void *);
static RangeQuery *QuerySelect(RangeQuery *, size_t, RangeTransformFunction,
                               void *);
static RangeQuery *QueryTake(RangeQuery *, size_t);
static RangeQuery *QuerySkip(RangeQuery *, size_t);
static RangeQuery *QueryTakeWhile(RangeQuery *, RangePredicate, void *);
static RangeQuery *QuerySkipWhile(RangeQuery *, RangePredicate, void *);
static RangeQuery *QueryConcat(RangeQuery *, RangeQuery *);
static RangeQuery *QueryForEach(RangeQuery *, RangeVisitFunction, void *);
static RangeQuery *QueryAggregate(RangeQuery *, void *, RangeFoldFunction,
                                  void *);
static RangeQuery *QueryFirst(RangeQuery *, RangePredicate, void *, void *);
static RangeQuery *QueryAny(RangeQuery *, RangePredicate, void *);
static RangeQuery *QueryAll(RangeQuery *, RangePredicate, void *);
static RangeQuery *QueryCount(RangeQuery *, RangePredicate, void *, size_t *);
static RangeQuery *QueryToVector(RangeQuery *, Vector **);
static RangeQuery *QueryFinalize(RangeQuery *);

static void QueryInstallMethods(RangeQuery *query)
{
    query->Where = QueryWhere;
    query->Select = QuerySelect;
    query->Take = QueryTake;
    query->Skip = QuerySkip;
    query->TakeWhile = QueryTakeWhile;
    query->SkipWhile = QuerySkipWhile;
    query->Concat = QueryConcat;
    query->ForEach = QueryForEach;
    query->Aggregate = QueryAggregate;
    query->First = QueryFirst;
    query->Any = QueryAny;
    query->All = QueryAll;
    query->Count = QueryCount;
    query->ToVector = QueryToVector;
    query->Finalize = QueryFinalize;
}

static RangeQuery *QueryRecord(RangeQuery *query, int status,
                               const char *source)
{
    if (query == NULL)
        return NULL;
    query->LastResult = status;
    if (status < 0 && query->Error >= 0) {
        query->Error = status;
        query->ErrorSource = source;
    }
    return query;
}

static RangeQuery *QueryReady(RangeQuery *query, const char *source)
{
    if (query == NULL)
        return NULL;
    if (query->Signature != RANGE_QUERY_SIGNATURE)
        return QueryRecord(query, CONTAINER_ERROR_BADARG, source);
    if (query->Error < 0)
        return query;
    if ((query->Flags & RANGE_QUERY_INITIALIZED) == 0 ||
        (query->Flags & (RANGE_QUERY_FINALIZED | RANGE_QUERY_CONSUMED)) != 0 ||
        query->Range == NULL)
        return QueryRecord(query, CONTAINER_ERROR_BADARG, source);
    return query;
}

static RangeQuery *QueryInitialize(RangeQuery *query, const char *source)
{
    if (query == NULL)
        return NULL;
    if (query->Signature == RANGE_QUERY_SIGNATURE &&
        (query->Flags & RANGE_QUERY_INITIALIZED) != 0 &&
        (query->Flags & RANGE_QUERY_FINALIZED) == 0)
        return QueryRecord(query, CONTAINER_ERROR_BADARG, source);
    memset(query, 0, sizeof(*query));
    query->Signature = RANGE_QUERY_SIGNATURE;
    query->Flags = RANGE_QUERY_INITIALIZED;
    QueryInstallMethods(query);
    return query;
}

static RangeQuery *QueryFromSequential(RangeQuery *query,
                                       SequentialContainer *source)
{
    int status;

    query = QueryInitialize(query, "iRangeQuery.FromSequential");
    if (query == NULL || query->Error < 0)
        return query;
    status = iRange.FromSequential(source, &query->Range);
    return QueryRecord(query, status, "iRangeQuery.FromSequential");
}

static RangeQuery *QueryFromSequentialWithAllocator(
    RangeQuery *query, SequentialContainer *source,
    const ContainerAllocator *allocator)
{
    int status;

    query = QueryInitialize(query, "iRangeQuery.FromSequentialWithAllocator");
    if (query == NULL || query->Error < 0)
        return query;
    status = iRange.FromSequentialWithAllocator(source, allocator,
                                                &query->Range);
    return QueryRecord(query, status,
                       "iRangeQuery.FromSequentialWithAllocator");
}

static RangeQuery *QueryFromGeneric(RangeQuery *query, GenericContainer *source,
                                     size_t elementSize)
{
    int status;

    query = QueryInitialize(query, "iRangeQuery.FromGeneric");
    if (query == NULL || query->Error < 0)
        return query;
    status = iRange.FromGeneric(source, elementSize, &query->Range);
    return QueryRecord(query, status, "iRangeQuery.FromGeneric");
}

static RangeQuery *QueryFromGenericWithAllocator(
    RangeQuery *query, GenericContainer *source, size_t elementSize,
    const ContainerAllocator *allocator)
{
    int status;

    query = QueryInitialize(query, "iRangeQuery.FromGenericWithAllocator");
    if (query == NULL || query->Error < 0)
        return query;
    status = iRange.FromGenericWithAllocator(source, elementSize, allocator,
                                             &query->Range);
    return QueryRecord(query, status,
                       "iRangeQuery.FromGenericWithAllocator");
}

static RangeQuery *QueryFromArray(RangeQuery *query, const void *data,
                                  size_t count, size_t elementSize)
{
    int status;

    query = QueryInitialize(query, "iRangeQuery.FromArray");
    if (query == NULL || query->Error < 0)
        return query;
    status = iRange.FromArray(data, count, elementSize, &query->Range);
    return QueryRecord(query, status, "iRangeQuery.FromArray");
}

static RangeQuery *QueryFromArrayWithAllocator(
    RangeQuery *query, const void *data, size_t count, size_t elementSize,
    const ContainerAllocator *allocator)
{
    int status;

    query = QueryInitialize(query, "iRangeQuery.FromArrayWithAllocator");
    if (query == NULL || query->Error < 0)
        return query;
    status = iRange.FromArrayWithAllocator(data, count, elementSize, allocator,
                                           &query->Range);
    return QueryRecord(query, status,
                       "iRangeQuery.FromArrayWithAllocator");
}

static RangeQuery *QueryWhere(RangeQuery *query, RangePredicate predicate,
                              void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Where") == NULL ||
        query->Error < 0)
        return query;
    status = iRange.Filter(&query->Range, predicate, arg);
    return QueryRecord(query, status, "iRangeQuery.Where");
}

static RangeQuery *QuerySelect(RangeQuery *query, size_t outputElementSize,
                               RangeTransformFunction transform, void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Select") == NULL ||
        query->Error < 0)
        return query;
    status = iRange.Transform(&query->Range, outputElementSize, transform,
                              arg);
    return QueryRecord(query, status, "iRangeQuery.Select");
}

static RangeQuery *QueryTake(RangeQuery *query, size_t count)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Take") == NULL || query->Error < 0)
        return query;
    status = iRange.Take(&query->Range, count);
    return QueryRecord(query, status, "iRangeQuery.Take");
}

static RangeQuery *QuerySkip(RangeQuery *query, size_t count)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Skip") == NULL || query->Error < 0)
        return query;
    status = iRange.Drop(&query->Range, count);
    return QueryRecord(query, status, "iRangeQuery.Skip");
}

static RangeQuery *QueryTakeWhile(RangeQuery *query, RangePredicate predicate,
                                  void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.TakeWhile") == NULL ||
        query->Error < 0)
        return query;
    status = iRange.TakeWhile(&query->Range, predicate, arg);
    return QueryRecord(query, status, "iRangeQuery.TakeWhile");
}

static RangeQuery *QuerySkipWhile(RangeQuery *query, RangePredicate predicate,
                                  void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.SkipWhile") == NULL ||
        query->Error < 0)
        return query;
    status = iRange.DropWhile(&query->Range, predicate, arg);
    return QueryRecord(query, status, "iRangeQuery.SkipWhile");
}

static RangeQuery *QueryConcat(RangeQuery *query, RangeQuery *other)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Concat") == NULL ||
        query->Error < 0)
        return query;
    if (other == NULL || other == query) {
        return QueryRecord(query, CONTAINER_ERROR_BADARG,
                           "iRangeQuery.Concat");
    }
    if (other->Signature != RANGE_QUERY_SIGNATURE)
        return QueryRecord(query, CONTAINER_ERROR_BADARG,
                           "iRangeQuery.Concat");
    if (other->Error < 0)
        return QueryRecord(query, other->Error,
                           other->ErrorSource != NULL ? other->ErrorSource
                                                       : "iRangeQuery.Concat");
    if ((other->Flags & RANGE_QUERY_INITIALIZED) == 0 ||
        (other->Flags & (RANGE_QUERY_FINALIZED | RANGE_QUERY_CONSUMED)) != 0 ||
        other->Range == NULL)
        return QueryRecord(query, CONTAINER_ERROR_BADARG,
                           "iRangeQuery.Concat");
    status = iRange.Concat(&query->Range, &other->Range);
    QueryRecord(query, status, "iRangeQuery.Concat");
    if (status > 0) {
        other->Flags |= RANGE_QUERY_CONSUMED;
        other->LastResult = status;
    }
    return query;
}

static RangeQuery *QueryForEach(RangeQuery *query, RangeVisitFunction function,
                                void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.ForEach") == NULL ||
        query->Error < 0)
        return query;
    status = iRange.ForEach(query->Range, function, arg);
    return QueryRecord(query, status, "iRangeQuery.ForEach");
}

static RangeQuery *QueryAggregate(RangeQuery *query, void *accumulator,
                                  RangeFoldFunction function, void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Aggregate") == NULL ||
        query->Error < 0)
        return query;
    status = iRange.Fold(query->Range, accumulator, function, arg);
    return QueryRecord(query, status, "iRangeQuery.Aggregate");
}

static RangeQuery *QueryFirst(RangeQuery *query, RangePredicate predicate,
                              void *arg, void *result)
{
    int status;

    if (QueryReady(query, "iRangeQuery.First") == NULL || query->Error < 0)
        return query;
    status = iRange.FindIf(query->Range, predicate, arg, result);
    return QueryRecord(query, status, "iRangeQuery.First");
}

static RangeQuery *QueryAny(RangeQuery *query, RangePredicate predicate,
                            void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Any") == NULL || query->Error < 0)
        return query;
    status = iRange.AnyOf(query->Range, predicate, arg);
    return QueryRecord(query, status, "iRangeQuery.Any");
}

static RangeQuery *QueryAll(RangeQuery *query, RangePredicate predicate,
                            void *arg)
{
    int status;

    if (QueryReady(query, "iRangeQuery.All") == NULL || query->Error < 0)
        return query;
    status = iRange.AllOf(query->Range, predicate, arg);
    return QueryRecord(query, status, "iRangeQuery.All");
}

static RangeQuery *QueryCount(RangeQuery *query, RangePredicate predicate,
                              void *arg, size_t *result)
{
    int status;

    if (QueryReady(query, "iRangeQuery.Count") == NULL || query->Error < 0)
        return query;
    status = iRange.CountIf(query->Range, predicate, arg, result);
    return QueryRecord(query, status, "iRangeQuery.Count");
}

static RangeQuery *QueryToVector(RangeQuery *query, Vector **result)
{
    int status;

    if (QueryReady(query, "iRangeQuery.ToVector") == NULL ||
        query->Error < 0)
        return query;
    status = iRange.ToVector(query->Range, result);
    return QueryRecord(query, status, "iRangeQuery.ToVector");
}

static RangeQuery *QueryFinalize(RangeQuery *query)
{
    int status;

    if (query == NULL)
        return NULL;
    if (query->Signature != RANGE_QUERY_SIGNATURE)
        return query;
    if ((query->Flags & RANGE_QUERY_FINALIZED) != 0)
        return query;
    if (query->Range != NULL) {
        status = iRange.Finalize(query->Range);
        QueryRecord(query, status, "iRangeQuery.Finalize");
        if (status < 0)
            return query;
        query->Range = NULL;
    }
    query->Flags |= RANGE_QUERY_FINALIZED;
    return query;
}

RangeInterface iRange = {
    FromSequential,
    FromSequentialWithAllocator,
    FromGeneric,
    FromGenericWithAllocator,
    FromArray,
    FromArrayWithAllocator,
    Filter,
    Transform,
    Take,
    Drop,
    TakeWhile,
    DropWhile,
    Concat,
    Open,
    Next,
    DeleteCursor,
    GetElementSize,
    SetErrorFunction,
    ForEach,
    Fold,
    FindIf,
    AnyOf,
    AllOf,
    CountIf,
    ToVector,
    Finalize,
};

RangeQueryInterface iRangeQuery = {
    QueryFromSequential,
    QueryFromSequentialWithAllocator,
    QueryFromGeneric,
    QueryFromGenericWithAllocator,
    QueryFromArray,
    QueryFromArrayWithAllocator,
};
