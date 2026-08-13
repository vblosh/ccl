# `pooldebug.c` analysis

## Scope and dependencies

Physical source: `src/pooldebug.c` (626 lines); public method table `iPoolDebug` is declared at `include/containers.h:172-183`. It depends on `iDebugMalloc` for user blocks, libc `calloc/free` for bookkeeping creation, `iError` indirectly through debug frees, and `memset/abort`. Optional `THREAD_VERSION` code references mutex/thread APIs not represented in the visible `Allocator` structure and is not part of the normal build.

Unlike `pool.c`, this debug implementation allocates every user request separately. `debug_node_t` records up to 64 begin/end pairs; `Pool` tracks the list and statistics.

## Function and API inventory

| Function/API | Responsibility |
| --- | --- |
| `destroyAllocator` | Free nodes in allocator buckets (normally unused by this implementation). |
| `Log` | Conditional verbose diagnostics; compiled out with current `POOL_DEBUG_VERSION == 1`. |
| `CheckIntegrity` | Intended integrity hook; currently empty. |
| `pool_alloc_debug` | Allocate a user block with `iDebugMalloc` and append it to a 64-entry tracking node. |
| `PoolAlloc_debug` / `iPoolDebug.Alloc` | Integrity/log wrapper around `pool_alloc_debug`. |
| `PoolCalloc_debug` / `iPoolDebug.Calloc` | Allocate the multiplied size and zero memory. |
| `pool_clear_debug` | Poison/free all tracked user blocks and tracking nodes; reset statistics. |
| `PoolClear_debug` / `iPoolDebug.Clear` | Integrity/log/thread wrapper around clear. |
| `PoolDestroy_debug` / `iPoolDebug.Finalize` | Clear, destroy owned allocator, and free pool. |
| `newPool_debug` / `iPoolDebug.Create` | Allocate and initialize pool and allocator bookkeeping. |
| `FindPoolFromData` | Given the address of a pointer, locate an allocation containing that pointer and replace it with the owning pool. |
| `SetMaxSize` / `iPoolDebug.SetMaxFree` | Updates free-threshold fields, although debug user allocations do not use allocator buckets. |
| `Sizeof` / `iPoolDebug.Sizeof` | Sum current requested allocation sizes; for null returns `sizeof(Pool)`. |

The legacy `JoinPool`, `LockPool`, and `SetMaxFree` declarations were not part
of `iPoolDebug` and are removed; the debug pool has no public joined-pool
operation. The `PoolCalloc` macro was corrected to match its four-argument
helper.

## Invariants

- Each live user allocation appears exactly once in one tracking node with a half-open `[begin,end)` range.
- All allocations must be released by the same allocator family that created them.
- On tracking-metadata failure, the just-created user block must be released before returning null.
- `Clear` frees all user and metadata allocations, empties `nodes`, zeros `stat_alloc`, and leaves the pool reusable.
- `Finalize` additionally frees the allocator bookkeeping and pool object.
- `Calloc` must reject multiplication overflow.

## Historical pre-fix defects (confirmed at the audit baseline)

### Critical: tracking nodes are freed by the wrong allocator

`pool_alloc_debug` creates `debug_node_t` with libc `calloc` (line 344), but `pool_clear_debug` passes it to `iDebugMalloc.free` (line 417). ASan reproducibly reports an 8-byte read 16 bytes before the tracking allocation in `malloc_debug.c:46`, reached from `pooldebug.c:417`, during an ordinary create/allocate/finalize sequence. Without ASan, the bad-pointer diagnostic is raised and the metadata leaks after being removed from the list.

### Critical: the pool object is freed by the wrong allocator

`newPool_debug` creates the pool with libc `calloc` at line 500, while `PoolDestroy_debug` calls `iDebugMalloc.free(pool)` at line 491. Even a pool with no user allocations performs an invalid read before the libc allocation, raises/aborts under diagnostics, and leaks the pool.

### High: every debug pool leaks its allocator bookkeeping

The allocator is created by libc `calloc` at line 510. Finalize calls `destroyAllocator`, which only drains its empty node buckets, but never frees the `Allocator` object itself.

### High: metadata-allocation failure leaks the user allocation

`pool_alloc_debug` allocates `mem` first. If the subsequent tracking-node `calloc` fails at line 344, line 345 returns null without `iDebugMalloc.free(mem)`. The block is no longer reachable or tracked.

### High: calloc multiplication is unchecked

Line 382 passes `n * size` without overflow detection. This inherits `iDebugMalloc`'s overflow defect and may return a non-null undersized allocation for a huge array request.

### Medium: range lookup uses non-portable cross-object pointer ordering

`FindPoolFromData` relationally compares pointer values belonging to unrelated allocations. ISO C only defines relational ordering within one array object; the intended address-range lookup should compare `uintptr_t` values after validating arguments. This is a real portability defect even though common flat-address compilers behave as intended.

### Medium: the optional threaded configuration does not compile

Compiling this translation unit with `-DTHREAD_VERSION` fails: `Mutex` and `thread_t` are undefined, `Allocator` has no `mutex` member used by `SetMutex`/`GetMutex`, all mutex/thread functions are undeclared, and the mutex-creation failure path calls an undefined `Free`. This is a confirmed configuration defect, not merely untested runtime behavior.

`CheckIntegrity` being empty and `SetMaxFree` having no effect on the separate-allocation strategy are incomplete diagnostics, but no promised externally observable behavior is clear enough to classify them as confirmed functional defects.

## Current coverage

`unittests/pooldebug_test.c` is picked up by the unit-test glob and exercises
the public `iPoolDebug` table. The local `#ifdef TEST` smoke main also builds
cleanly when linked with the allocator and error support objects; it remains a
manual smoke target rather than a CTest test.

## Required tests

1. Create/finalize an empty pool under ASan/leak detection; then create/allocate/finalize. Both must be clean and catch the allocator-family mismatches.
2. Allocate 1, 64, 65, and 129 blocks to cross tracking-node boundaries; validate `Sizeof`, clear, statistics behavior through observable size, reuse, and finalize.
3. Allocation-failure matrix: pool calloc, allocator calloc, user debug malloc, first tracking-node calloc, and later tracking-node calloc. Assert no leaks and continued pool usability where applicable. Provide explicit test seams or link-time wrappers for libc allocations.
4. Calloc normal product: verify every byte of all elements is zero. Add the overflow regression `SIZE_MAX / 2 + 1, 2`.
5. `FindPoolFromData`: exact beginning, interior, last byte, one-past-end, foreign allocation, and null arguments; verify the pointer is replaced only on success.
6. `Sizeof(NULL)`, empty pool, after mixed allocations, after clear, and after reallocation.
7. Capturing `iError` test to ensure ordinary lifecycle produces no debug-malloc diagnostic.
8. If joined-pool support is retained/exposed, add a subprocess abort test; otherwise remove the unreachable declaration/branch rather than manufacturing internal state in public tests.
9. Either restore the required threaded types/fields/functions and add a `THREAD_VERSION` compile-and-runtime target, or remove that unsupported conditional implementation. A compile-only CI check is the minimum acceptance gate if retained.

## Recommended fix order

Process immediately after `malloc_debug`. First make allocation/free families consistent and release the allocator object; then fix failure rollback and checked multiplication; finally make range lookup portable. The ordinary lifecycle sanitizer test is the acceptance gate before any broader coverage work.

## Remediation status

The confirmed defects are addressed in `src/pooldebug.c`:

- User allocations remain owned by `iDebugMalloc`; tracking nodes, the
  allocator bookkeeping object, and the pool object are allocated and freed
  with libc consistently.
- A tracking-node allocation failure releases the already-created user block.
- `PoolCalloc_debug` checks multiplication overflow and clears the complete
  product, not only one element.
- `FindPoolFromData` validates null arguments and compares `uintptr_t` address
  values using the intended half-open range.
- The unsupported `THREAD_VERSION` implementation and unreachable joined-pool
  branch were removed, leaving the translation unit compilable with that flag;
  the legacy `TEST` build and the `PoolCalloc` macro arity are also repaired.

`unittests/pooldebug_test.c` covers ordinary empty/allocated lifecycle,
tracking-node boundaries and reuse, calloc zeroing/overflow, range lookup,
size accounting, and diagnostic capture. The dedicated coverage target
reports 92.59% line coverage and 82.00% taken branch coverage for this
translation unit (100/108 lines and 41/50 branches).
