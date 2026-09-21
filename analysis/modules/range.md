# Fluent range interface

## Purpose

`src/range.c` provides the original `iRange` interface: source constructors
return status codes, adaptors mutate a `Range **`, and terminals return their
individual result. The original API remains available. The fluent facade adds
a caller-owned `RangeQuery` object whose methods return the same object, so a
pipeline can be built in a C#-style sequence while remaining valid C.

The facade is intentionally thin. It stores the current lazy `Range *` and
delegates construction, cursor evaluation, allocator handling, and recursive
finalization to `iRange`.

## Public shape

`include/range.h` declares:

- `RangeQuery`, containing `Range *Range`, `int Error`,
  `const char *ErrorSource`, `int LastResult`, and the fluent method pointers;
- `RangeQueryInterface iRangeQuery`, containing source constructors;
- source methods `FromArray`, `FromSequential`, `FromGeneric`, and all
  allocator-aware variants;
- fluent adaptors `Where`, `Select`, `Take`, `Skip`, `TakeWhile`, `SkipWhile`,
  and `Concat`;
- fluent terminals `ForEach`, `Aggregate`, `First`, `Any`, `All`, `Count`, and
  `ToVector`, plus `Finalize`.

The query storage belongs to the caller. A source constructor initializes all
query state; it must not be copied while it owns a range and it must be
finalized before being initialized again.

```c
RangeQuery query = {0};
Vector *result = NULL;

iRangeQuery.FromArray(&query, values, count, sizeof(int))
    ->Where(&query, is_even, NULL)
    ->Select(&query, sizeof(int), square, NULL)
    ->Skip(&query, 1)
    ->Take(&query, 10)
    ->ToVector(&query, &result);

if (query.Error < 0)
    fprintf(stderr, "%s: %d\n", query.ErrorSource, query.Error);

if (result != NULL)
    iVector.Finalize(result);
query.Finalize(&query);
```

C function pointers do not have an implicit receiver, so each method receives
the query pointer explicitly. The return value is always that same pointer;
the calls can therefore also be written as separate statements without
checking an intermediate status.

## State and error contract

`Error` starts at zero. Every source, adaptor, and terminal stores its
underlying return value in `LastResult`. A negative result stores the first
negative value in `Error` and the public fluent operation name in `ErrorSource`.
The first error is never replaced.

Every method except `Finalize` begins by checking `Error`. Once it is negative,
the method returns immediately, performs no allocation, callback, or output
write, and leaves the existing diagnostic unchanged. A successful terminal may
return zero (`First` when no element matches, `Any` on an empty range, or `All`
on a nonmatching element); zero is a successful `LastResult`, not a query
error.

The source of a callback, cursor, or allocation failure is the fluent terminal
that observed it, for example `iRangeQuery.ToVector`. The facade does not
retain an internal cursor-stage stack. The underlying `iRange` error handler
still receives the original low-level operation notification.

`Finalize` always runs even when `Error` is negative. It releases the owned
outer range once, marks the query finalized, and is safe to call repeatedly.
After successful `Concat`, the right query is marked consumed, its range is
owned by the left query, and it can only be finalized or reinitialized after
finalization. Failed concatenation leaves both query graphs unchanged.

## Ownership and evaluation

The facade preserves the existing range rules:

1. Source containers, arrays, and callback arguments are borrowed and must
   outlive every terminal evaluation and open cursor.
2. Adaptors are lazy. A pipeline can be evaluated repeatedly while its source
   remains valid; each terminal opens and closes its own cursor.
3. The range allocator captured by the source owns every adaptor, cursor,
   transform buffer, and vector produced by `ToVector`.
4. `Select` may change the element size. `First` and `ToVector` reject an
   opaque generic source whose element size is zero, matching `iRange`.
5. A query cannot be adapted while its range is consumed, finalized, or owned
   by another query.

## Implementation mapping

| Fluent method | Existing operation |
| --- | --- |
| `Where` / `SkipWhile` / `TakeWhile` | `Filter` / `DropWhile` / `TakeWhile` |
| `Select` / `Skip` | `Transform` / `Drop` |
| `Take` / `Concat` | `Take` / `Concat` |
| `ForEach` / `Aggregate` | `ForEach` / `Fold` |
| `First` / `Any` / `All` / `Count` | `FindIf` / `AnyOf` / `AllOf` / `CountIf` |
| `ToVector` / `Finalize` | `ToVector` / `Finalize` |

## Verification

`unittests/range_test.c` retains the existing direct-interface coverage and
adds fluent coverage for adaptor chains, same-object returns, independent
query state, first-error preservation, skipped calls and unchanged output
sentinels, type-changing `Select`, terminal error sources, and repeated
finalization. Build and run `test_range` together with the existing CTest
suite; sanitizer builds should continue to exercise the same ownership paths.
