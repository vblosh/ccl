# `pool.c` analysis

## Scope and dependencies

Physical source: `src/pool.c` (563 lines). Public access is exclusively through `iPool` (`include/containers.h:161-170`); its implementation functions are static. A pool uses the explicit `ContainerAllocator *` passed to `Create`, or snapshots `CurrentAllocator` when passed `NULL`.

Internally an `Allocator` owns 20 reusable-node buckets. Each `MemoryNode_t` participates in a circular active list while in a pool and has an index derived from 4096-byte boundaries. Allocations are aligned to eight bytes; the first node is at least 8192 bytes and embeds the `Pool` object itself.

## Function and API inventory

| Function/API | Responsibility |
| --- | --- |
| `newAllocator` | Check/round requested node size, reuse a suitable exact-bucket or oversized sink node, otherwise call the selected allocator's `malloc`. |
| `allocator_free` | Split a submitted node chain between cached buckets and immediate frees according to free thresholds. |
| `destroyAllocator` | Free every cached node through the pool's selected allocator. |
| `PoolAlloc` / `iPool.Alloc` | Align a request, bump the active node if possible, otherwise reuse/create/reorder nodes. |
| `PoolCalloc` / `iPool.Calloc` | Allocate `n * size` bytes from the pool and zero them. |
| `PoolClear` / `iPool.Clear` | Reset the embedded node and return all other nodes to the internal allocator for reuse/free. |
| `PoolFinalize` / `iPool.Finalize` | Return all pool nodes and destroy cached nodes. |
| `newPool` / `iPool.Create` | Select allocator, zero-create internal allocator, create first node, embed/initialize pool. |
| Disabled `PoolRealloc` | `#if 0`; not compiled and not an API. |

## Invariants

- Every backing allocation and release for a pool uses the same stored `ContainerAllocator`.
- The circular active list has valid `next/ref` back-references; `self` remains the embedded first node.
- `first_avail <= endp`; returned blocks are aligned and never overlap before `Clear`.
- Clearing invalidates all allocations, restores the self-node cursor, and leaves the pool reusable.
- Finalizing releases every node and the separate `Allocator` object exactly once.
- Failure to obtain a new node must not modify the active list or consume existing space.

## Historical pre-fix defects (confirmed at the audit baseline)

### High: every successful pool leaks its `Allocator`

`newPool` obtains `pool_allocator` with `m->calloc` at line 521. `PoolFinalize` frees all nodes via `destroyAllocator` but never calls `m->free(allocator)`. A counting-allocator probe observed two successful backing allocations and only one free after `iPool.Finalize`.

### High: first-node allocation failure leaks its `Allocator`

If line 524's `newAllocator` fails, `newPool` returns null without freeing `pool_allocator`. A failure-injection probe (calloc succeeds, following malloc fails) observed one allocation and zero frees.

### High: `PoolCalloc` does not detect multiplication overflow

Line 454 performs unchecked `size *= n`. `iPool.Calloc(pool, SIZE_MAX / 2 + 1, 2)` reproducibly returned a non-null zero-length pool pointer. Callers may then access it as the requested huge array.

## Resolution status

The allocator-object ownership defects are fixed: `PoolFinalize` now releases the
separate `Allocator` object after returning all nodes, and `newPool` releases it
when initial-node allocation fails. `PoolCalloc` rejects products that exceed
`SIZE_MAX` before touching pool state. `PoolAlloc` also rejects null pools and
alignment-overflow requests; creation rejects a missing current allocator or
required allocation callback.

`unittests/pool_test.c` covers allocator ownership and failure injection,
alignment/non-overlap, ordinary and oversized node reuse, repeated clear cycles,
calloc zeroing and overflow, allocator snapshots, near-`SIZE_MAX` requests, and
the `iDebugMalloc` lifecycle plus randomized live ranges. The dedicated coverage run reports 91.72% line coverage
and 77% of branches taken (98% branch instrumentation). Direct ASan/UBSan
execution passes all nine tests. CTest's leak-enabled ASan mode is unavailable
in the execution environment because LeakSanitizer reports that it cannot run
under ptrace; no sanitizer error is reported with leak detection disabled.

## Current coverage

`unittests/pool_test.c` is auto-discovered by the unit-test build and exercises
the public `iPool` interface. The `#ifdef TEST` main at the end of `pool.c` is
still stale (`newPool` now requires an allocator and it calls the nonexistent
`PoolDestroy` name), so it remains intentionally unused.

## Required tests

The required cases are covered by `pool_test.c`: counting/failure
allocators, boundary alignment and overlap, ordinary and sink buckets,
single- and multi-node clear/reuse cycles, calloc zeroing/overflow,
allocator snapshots, near-`SIZE_MAX` requests, and randomized live ranges.

## Recommended fix order

The pool implementation and its dedicated tests are complete. `pooldebug` is a
separate implementation and remains covered by its own module work.

## Final integration verification

LeakSanitizer cannot initialize in the current local workspace because of its
ptrace restriction, so a current local ASan/UBSan+LSan integration pass cannot
be claimed. The earlier untraced pass is historical campaign evidence recorded
on 2026-08-08 and does not supersede the current focused-run limitation.
