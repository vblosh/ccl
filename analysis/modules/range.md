# `range.c` audit

## Scope and representation

- Physical source: `src/range.c`; public interface: `include/range.h` through
  `iRange`.
- The implementation is a lazily evaluated view graph. Each opened cursor is
  single-pass, while the range itself can open fresh cursors and be reused.
  Source nodes borrow a generic container or array; adaptor nodes own their
  child range handles but do not copy source elements.
- A range handle has unique ownership. Successful unary adaptors replace the
  caller's handle and mark the wrapped node as having a parent. Successful
  `Concat` consumes both top-level handles, publishes the combined handle in
  `*left`, and sets `*right` to `NULL`.
- Each `Open` builds an independent cursor tree. Transform cursors own one
  output buffer, so the returned element remains valid only until the next
  `Next` call or cursor deletion.

## Public behavior

| Area | Operations and contract |
| --- | --- |
| Sources | `FromSequential`, `FromGeneric`, and `FromArray`, plus allocator-aware variants. Sequential sources obtain their fixed element size through `iSequentialContainer.GetElementSize`. |
| Lazy adaptors | `Filter`, `Transform`, `Take`, `Drop`, `TakeWhile`, `DropWhile`, and `Concat`. Construction is transactional: failure leaves input handles unchanged. |
| Cursor lifecycle | `Open`, `Next`, and `DeleteCursor`. `Next` returns 1 for an element, 0 at end, or a negative `CONTAINER_ERROR_*`; errors are sticky. |
| Metadata | `GetElementSize` and recursively applied `SetErrorFunction`. |
| Terminals | `ForEach`, `Fold`, `FindIf`, `AnyOf`, `AllOf`, `CountIf`, and `ToVector`; each opens and closes its own cursor and leaves the range reusable. |
| Lifetime | `Finalize` recursively releases the owned range graph using the allocator captured by each node. Borrowed container/array storage is never released. |

Predicates return positive for true, zero for false, and negative for an error.
Transform and fold callbacks must return positive on success; zero is converted
to `CONTAINER_ERROR_WRONGELEMENT`. A visitor may return zero to stop `ForEach`
normally. Empty ranges therefore produce `AnyOf == 0`, `AllOf == 1`, and a
successful empty vector from `ToVector`.

## Invariants and edge cases

1. Borrowed sources must outlive the range and every open cursor. The array
   source cannot detect changes to its storage.
2. Generic cursors snapshot the source size and use the source iterator's
   mutation checks. Early exhaustion, excess elements, or a changed size is
   reported as `CONTAINER_ERROR_OBJECT_CHANGED` and remains sticky.
3. Only a top-level handle (`Parent == NULL`) may be adapted, opened, or
   finalized. Retained aliases of a consumed inner node are invalid.
4. `Concat` requires equal element sizes. Its children may use different
   allocators; each child cursor and range node is still released through its
   own captured allocator.
5. `FromArray` rejects zero element size, nonempty NULL data, and overflowing
   `count * elementSize`. `FromGeneric` may use element size zero for an opaque
   traversal, but copying terminals such as `FindIf` and `ToVector` reject it
   as incompatible.
6. Allocator-aware creation requires all four allocator callbacks. All range,
   cursor, transform-buffer, and `ToVector` allocations retain that ownership.
7. Terminal functions close cursor trees on normal completion, callback stop,
   source errors, and allocation failures. `ToVector` also finalizes a partial
   result before returning an error.

No source defect was reproduced in the 2026-08-13 recheck. The principal API
hazards are contractual: source lifetime is borrowed, array mutation is not
observable, and range handles must not be aliased after an adaptor consumes
them.

## Test coverage

`unittests/range_test.c` registers eight focused cases covering:

- reusable filter/transform/take/drop pipelines;
- while adaptors, concatenation, and every terminal algorithm;
- array, sequential, and generic-protocol sources;
- source mutation and malformed iterator protocols;
- custom allocators and deterministic allocation failures;
- callback errors, sticky cursor failures, and transactional adaptor failure;
- empty-range identities, invalid arguments, overflow, and opaque generic
  element sizes.

The 2026-08-13 GCC/gcov run reports 502/574 executable lines (87.46%) and
295/388 taken branches (76.03%), passing the 80%/70% gates. The suite passes
under AddressSanitizer and UndefinedBehaviorSanitizer with leak detection
disabled. LeakSanitizer itself cannot initialize in the current
ptrace-restricted environment, so no current LSan pass is claimed.

## Reproduction

```text
cmake -S . -B <build> -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build <build> --target test_range
ctest --test-dir <build> -R '^test_range$' --output-on-failure
```

For sanitizer verification use a separate build with
`-DCCL_ENABLE_SANITIZERS=ON`. For coverage use a GCC build with
`-DCCL_ENABLE_COVERAGE=ON` and run `coverage-range` or `coverage-check`.
