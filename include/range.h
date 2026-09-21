#ifndef CCL_RANGE_H
#define CCL_RANGE_H

#include "containers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Range Range;
typedef struct RangeCursor RangeCursor;
typedef struct RangeQuery RangeQuery;

/* Predicates return a positive value for true, zero for false, or a
 * negative CONTAINER_ERROR_* value.  Negative callback results are propagated
 * without being reported again through the range error function. */
typedef int (*RangePredicate)(const void *element, void *arg);

/* Transform and fold callbacks return a positive value on success or a
 * negative CONTAINER_ERROR_* value.  Transform output storage is supplied
 * by the range cursor and has the outputElementSize passed to Transform.
 * Callbacks must not write beyond their supplied storage. */
typedef int (*RangeTransformFunction)(const void *input, void *output,
                                      void *arg);
typedef int (*RangeFoldFunction)(void *accumulator, const void *element,
                                 void *arg);

/* Visitors return a positive value to continue, zero to stop normally, or a
 * negative CONTAINER_ERROR_* value. */
typedef int (*RangeVisitFunction)(const void *element, void *arg);

/*
 * A RangeQuery is a caller-owned fluent facade over Range.  Query methods
 * return their receiver so calls can be chained.  Error is sticky: after the
 * first negative result, later methods return the receiver without doing
 * work.  ErrorSource names the fluent operation that recorded Error.
 *
 * The method pointers are initialized by an iRangeQuery source constructor.
 * A source constructor initializes all query state. A query must not be copied
 * while it owns a range, and must be finalized before it is initialized again.
 */
struct RangeQuery {
    unsigned int Signature;
    Range *Range;
    int Error;
    const char *ErrorSource;
    int LastResult;
    unsigned int Flags;

    RangeQuery *(*Where)(RangeQuery *, RangePredicate, void *);
    RangeQuery *(*Select)(RangeQuery *, size_t, RangeTransformFunction, void *);
    RangeQuery *(*Take)(RangeQuery *, size_t);
    RangeQuery *(*Skip)(RangeQuery *, size_t);
    RangeQuery *(*TakeWhile)(RangeQuery *, RangePredicate, void *);
    RangeQuery *(*SkipWhile)(RangeQuery *, RangePredicate, void *);
    RangeQuery *(*Concat)(RangeQuery *, RangeQuery *);

    RangeQuery *(*ForEach)(RangeQuery *, RangeVisitFunction, void *);
    RangeQuery *(*Aggregate)(RangeQuery *, void *, RangeFoldFunction, void *);
    RangeQuery *(*First)(RangeQuery *, RangePredicate, void *, void *);
    RangeQuery *(*Any)(RangeQuery *, RangePredicate, void *);
    RangeQuery *(*All)(RangeQuery *, RangePredicate, void *);
    RangeQuery *(*Count)(RangeQuery *, RangePredicate, void *, size_t *);
    RangeQuery *(*ToVector)(RangeQuery *, Vector **);
    RangeQuery *(*Finalize)(RangeQuery *);
};

typedef struct tagRangeQueryInterface {
    RangeQuery *(*FromSequential)(RangeQuery *, SequentialContainer *);
    RangeQuery *(*FromSequentialWithAllocator)(
        RangeQuery *, SequentialContainer *, const ContainerAllocator *);
    RangeQuery *(*FromGeneric)(RangeQuery *, GenericContainer *, size_t);
    RangeQuery *(*FromGenericWithAllocator)(
        RangeQuery *, GenericContainer *, size_t, const ContainerAllocator *);
    RangeQuery *(*FromArray)(RangeQuery *, const void *, size_t, size_t);
    RangeQuery *(*FromArrayWithAllocator)(
        RangeQuery *, const void *, size_t, size_t,
        const ContainerAllocator *);
} RangeQueryInterface;

typedef struct tagRangeInterface {
    /* Sources borrow their container or array storage.  Result is set to NULL
     * on failure.  A NULL allocator selects CurrentAllocator. */
    int (*FromSequential)(SequentialContainer *source, Range **result);
    int (*FromSequentialWithAllocator)(SequentialContainer *source,
                                       const ContainerAllocator *allocator,
                                       Range **result);
    int (*FromGeneric)(GenericContainer *source, size_t elementSize,
                       Range **result);
    int (*FromGenericWithAllocator)(GenericContainer *source,
                                    size_t elementSize,
                                    const ContainerAllocator *allocator,
                                    Range **result);
    int (*FromArray)(const void *data, size_t count, size_t elementSize,
                     Range **result);
    int (*FromArrayWithAllocator)(const void *data, size_t count,
                                  size_t elementSize,
                                  const ContainerAllocator *allocator,
                                  Range **result);

    /* Unary adaptors replace *range only after successful construction. Range
     * handles have unique ownership; retained aliases of wrapped inner nodes
     * must not be passed to adaptors or Finalize.  Callback arg pointers are
     * borrowed and must remain valid while the pipeline can invoke them. */
    int (*Filter)(Range **range, RangePredicate predicate, void *arg);
    int (*Transform)(Range **range, size_t outputElementSize,
                     RangeTransformFunction transform, void *arg);
    int (*Take)(Range **range, size_t count);
    int (*Drop)(Range **range, size_t count);
    int (*TakeWhile)(Range **range, RangePredicate predicate, void *arg);
    int (*DropWhile)(Range **range, RangePredicate predicate, void *arg);

    /* On success, Concat replaces *left and sets *right to NULL.  On failure,
     * both input handles remain unchanged. */
    int (*Concat)(Range **left, Range **right);

    /* Next returns 1 for an element, 0 at end, or a negative error.  The
     * element pointer is valid until the next Next call or cursor deletion. */
    int (*Open)(const Range *range, RangeCursor **result);
    int (*Next)(RangeCursor *cursor, const void **element);
    int (*DeleteCursor)(RangeCursor *cursor);

    /* GetElementSize validates only that range is non-NULL; callers must pass
     * the current outermost handle.  Passing NULL as function queries the
     * current error handler without changing it. */
    size_t (*GetElementSize)(const Range *range);
    ErrorFunction (*SetErrorFunction)(Range *range, ErrorFunction function);

    /* Terminal algorithms open their own cursor and do not consume range.
     * FindIf result must point to at least GetElementSize(range) writable
     * bytes. */
    int (*ForEach)(const Range *range, RangeVisitFunction function, void *arg);
    int (*Fold)(const Range *range, void *accumulator,
                RangeFoldFunction function, void *arg);
    int (*FindIf)(const Range *range, RangePredicate predicate, void *arg,
                  void *result);
    int (*AnyOf)(const Range *range, RangePredicate predicate, void *arg);
    int (*AllOf)(const Range *range, RangePredicate predicate, void *arg);
    int (*CountIf)(const Range *range, RangePredicate predicate, void *arg,
                   size_t *result);
    int (*ToVector)(const Range *range, Vector **result);

    int (*Finalize)(Range *range);
} RangeInterface;

extern RangeInterface iRange;
extern RangeQueryInterface iRangeQuery;

#ifdef __cplusplus
}
#endif

#endif /* CCL_RANGE_H */
