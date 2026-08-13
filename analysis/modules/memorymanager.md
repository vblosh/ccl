# `memorymanager.c` analysis

## Scope and dependencies

Physical source: `src/memorymanager.c` (23 lines). Public declarations are in `include/containers.h` lines 71-84. The unit depends only on the C allocation functions declared through `containers.h` and is the process-wide allocator-selection state used by most containers and by `pool.c` when no allocator is supplied explicitly.

The private `DefaultAllocatorObject` maps directly to `malloc`, `free`, `realloc`, and `calloc`. `CurrentAllocator` initially points at it and is intentionally public. `iAllocator` is the public method table.

## Function and API inventory

| Entry point | Behavior | Important paths |
| --- | --- | --- |
| `SetCurrentAllocator` via `iAllocator.Change` | With a non-null argument, installs that allocator and returns the previous pointer. With `NULL`, leaves state unchanged and returns the current pointer. | Query-only `NULL`; replace; restore a previously returned allocator. |
| `GetCurrentAllocator` via `iAllocator.GetCurrent` | Returns `CurrentAllocator` without mutation. | Default state and post-change state. |
| `CurrentAllocator` | Directly accessible global pointer, also used directly by legacy callers. | Default allocator and caller-installed allocator. |

## Invariants

- `CurrentAllocator` must always point to a live `ContainerAllocator` whose four callbacks remain valid for as long as clients may use it.
- `Change(nonnull)` is an exchange operation: it returns the exact old pointer and installs the exact new pointer.
- `Change(NULL)` is a query, not a request to restore the default allocator.
- Memory must be released/resized by the allocator family that originally allocated it. Changing the global allocator does not migrate existing objects.

## Defect status

No functional defect was confirmed in this unit. The mutable process-global state is not synchronized and therefore is not safe for concurrent allocator changes, but the API makes no thread-safety promise; record this as a design constraint, not a production fix.

## Current coverage

`unittests/memorymanager_test.c` is the dedicated four-test suite and is
auto-discovered by `unittests/CMakeLists.txt`. It exercises the default
allocator lifecycle, allocator exchange/restore, and the NULL query semantics;
the normal CTest run passes `test_memorymanager`. The legacy `tests/test.c:637`
assignment to `CurrentAllocator` remains an indirect, non-CTest smoke path.

## Required tests

1. Assert `GetCurrent()` is non-null and its four callbacks perform a normal allocate/calloc/realloc/free lifecycle.
2. Install a counting allocator; assert `Change` returns the default pointer, `GetCurrent` returns the counting object, and direct `CurrentAllocator` agrees.
3. Call `Change(NULL)` and assert it returns the installed allocator without changing it.
4. Restore the saved default pointer and verify the returned pointer is the counting allocator. Restoration must be cleanup-safe even after an assertion fails so other suites are not contaminated.
5. Verify a container created while the counting allocator is current retains its allocator after the global is restored (best placed in the owning container test, but it protects this integration contract).

Allocation-failure behavior belongs to users of this selector; neither function allocates. Run this suite under ASan/UBSan, and do not run allocator-changing tests concurrently in one process.

## Handoff

Test-only unit; no source change recommended. Process before allocator consumers because its counting/failing allocator fixture can be reused by `pool`, `heap`, and container tests.
