# `heap.c` audit

## Scope and dependencies

- Physical source: `src/heap.c`; public surface is allocator-style
  `HeapInterface iHeap` in `include/containers.h`; representation and iterator
  types are in `ccl_internal.h`.
- Dependencies: `CurrentAllocator`, `iError`, `ListElement` layout, and a fixed
  `CHUNK_SIZE` of 1000.
- The heap owns a growable pointer table and fixed-size object blocks. Every
  returned object must meet C alignment requirements, belong either to the
  allocated prefix or exactly once to the free list, and be reusable after
  `FreeObject`. Iteration must return each currently live object exactly once.

## Function and contract inventory

| Area | Entries/contract |
| --- | --- |
| Construction | `Create` allocates the control block; `InitHeap` initializes caller storage, rounds small objects for freelist metadata, and captures/defaults allocator. |
| Allocation | `NewObject` lazily creates pointer table/blocks, reuses free-list entries first, grows the pointer table, and advances block position/timestamp. |
| Reclamation | `FreeObject` links an object into the free list; `Clear` releases blocks/table but retains control object; `Finalize` clears then releases it. |
| Accounting | `Sizeof`/`GetHeapSize` reports heap storage. |
| Iteration | create/delete iterator and first/next/previous/last/current/position access, skipping freed objects and validating magic. |

## Historical pre-fix defects (confirmed at the audit baseline)

### H1 - object stride is not aligned (critical)

`InitHeap` enforces only a minimum size, not an alignment multiple, and
`NewObject` advances by raw `ElementSize` (line 76). A priority queue of
four-byte elements requests a stride that is not divisible by eight. UBSan
reports repeated misaligned `PQueueElement` member accesses beginning at
`priorityqueue.c:552` for the second node. Round stride up to `_Alignof(max_align_t)`
with checked arithmetic and allocate based on that stride.

### H2 - freed objects are not skipped by iterators (high)

`FreeObject` writes `INVALID_POINTER_VALUE` to `Next`, then immediately
overwrites `Next` with the free-list link (lines 108-117). Skip helpers detect
only the sentinel, so it can never recognize a normally freed slot. Public
reproducer allocating A/B, freeing A, and calling iterator `GetFirst` returns A
instead of B. Store freelist linkage and live/free state without destroying the
marker, or maintain an independent occupancy map/header.

### H3 - `Clear` retains a dangling free list (critical)

Lines 122-134 free every block and reset most fields but never set `FreeList`
to NULL. Calling NewObject after free-object then Clear takes the old dangling
node from line 41 and writes through freed storage. Reset it as part of the
empty-state invariant; add an ASan regression for heap reuse.

### H4 - forward iterator bounds use table capacity, not allocated blocks (critical)

`BlockCount` is pointer-table capacity (initially 1000), while `CurrentBlock`
is the last allocated block. `SkipFreeForward` advances until `BlockNumber >=
BlockCount` and compares `BlockNumber == BlockCount` for the partial-last-block
test (lines 197-210). Once free-slot marking works, it can enter any of 999 NULL
block pointers and perform pointer arithmetic/dereference. Bound against
`CurrentBlock`, with `BlockIndex` only for that last block.

### H5 - backward/last/current iteration mishandles freed and boundary slots (high)

`SkipFreeBackwards` tests the heap's `BlockIndex` rather than iterator position
before decrementing, allowing `BlockPosition` underflow. `GetLast` never skips
a freed last slot. `GetCurrent` can expose the next/uninitialized position
because first/next advance internal position after returning. Iterator position
semantics need one consistent current index and shared live-slot scanning.

### H6 - iterator mutation timestamp is unused (high)

`NewIterator` stores a timestamp derived from positions rather than the heap's
`timestamp`, and no accessor checks it. Free/reuse/clear can silently change or
invalidate iteration. Snapshot `heap->timestamp` and return OBJECT_CHANGED
before accessing storage after mutation.

### H7 - allocator/null/failure validation is incomplete (medium)

`Create(...,NULL)` crashes at line 177 even though `InitHeap(...,NULL)` defaults
to `CurrentAllocator`. `InitHeap(NULL,...)`, NewObject/FreeObject/Clear/Finalize
with NULL, `DeleteIterator(NULL)`, foreign frees, and double frees are unchecked.
The optional debug validator is also wrong for blocks/strides (lines 88-98) and
is compiled out normally. At minimum reject invalid public pointers and double
free without corrupting the free list.

### H8 - allocation/accounting arithmetic is wrong or unchecked (medium)

Block allocation adds an unexplained `sizeof(void *)` per object but indexing
uses only ElementSize (lines 67/73/76). `GetHeapSize` uses a different formula,
double counts the partial block, and reports pointer-table capacity rather than
the recorded `MemoryUsed`. Growth and block-size products can overflow. Use a
single checked stride/allocation formula and return actual owned bytes.

## Current coverage

`unittests/heap_test.c` is the dedicated heap suite and is auto-discovered by
`unittests/CMakeLists.txt`. Its six cases cover alignment/reuse, free-slot
iteration and mutation invalidation, clear/reuse, the 1000-slot boundary and
pointer-table growth, allocator failures/invalid arguments, and bad iterators.
The normal CTest run passes `test_heap`; the historical pre-fix coverage gap no
longer describes the current tree.

## Required test matrix

1. Create/Init with element sizes 0, 1, pointer-aligned, odd, and over-aligned
   supported cases; allocate several objects and assert every pointer alignment
   and non-overlap under UBSan (H1).
2. Allocate A/B/C, free first/middle/last, iterate exact live identities in both
   directions, then reallocate and assert free-list reuse without stale marker
   or duplicate iteration (H2/H5).
3. Free at least one object, Clear, reuse the same heap, and finalize under ASan
   (H3); also clear empty and clear twice.
4. Cross the 1000-object block boundary and eventually pointer-table growth;
   free runs around 999/1000/1001 and verify iterator bounds (H4). Use a test
   build with smaller `CHUNK_SIZE` if supported to cover table growth cheaply.
5. Mutate by allocate/free/clear after iterator creation and assert deterministic
   OBJECT_CHANGED for every accessor; test bad magic, empty heap, first/last,
   current/position semantics, and NULL iterator deletion.
6. Failing/counting allocator for control, table, first block, later block, and
   table realloc; assert old state remains usable and final counts balance.
7. Foreign pointer, interior pointer, and double-free rejection without list
   corruption. Checked-arithmetic boundary inputs must fail cleanly.
8. Assert Sizeof/accounting at empty, one object, full block, second block,
   frees (allocation size unchanged), and clear.

This reaches allocation/reuse/growth and all iterator paths, sufficient for
80% line/70% branch after making `CHUNK_SIZE` test-configurable. Run
ASan/UBSan and leak checks.

## Luna handoff

Fix H1-H4 before priority-queue work because they cause dependent UB. Then
replace iterator position/scanning with one bounded live-slot implementation,
wire timestamp invalidation, and finally validation/accounting. Preserve the
pool allocator API (objects remain heap-owned until Clear/Finalize).

## Verification status

The heap implementation now addresses H1-H8: slots use an aligned private
metadata prefix, allocation and table/block growth use checked arithmetic,
freed and foreign slots are rejected or skipped safely, `Clear` resets the
free-list, iterators are bounded to allocated slots and invalidate on heap
mutation, and `MemoryUsed` reports the owned table/block storage. The focused
`unittests/heap_test.c` suite covers alignment, reuse, iteration, clear/reuse,
the 1000-slot boundary, allocator failures, stale iterators, and invalid
arguments. Coverage is 90.36% lines and 74.17% branches for `src/heap.c`.
The ASan/UBSan run passes with leak detection disabled because LeakSanitizer
cannot initialize under this execution environment's ptrace setup.

## Final integration verification

LeakSanitizer cannot initialize in the current local workspace because of its
ptrace restriction, so a current local ASan/UBSan+LSan integration pass cannot
be claimed. The earlier untraced pass is historical campaign evidence recorded
on 2026-08-08 and does not supersede the current focused-run limitation.
