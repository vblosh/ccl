# `smallpool.c` analysis

## Scope and dependencies

Physical source: `src/smallpool.c` (526 lines). It is a legacy standalone pool implementation using libc allocation directly and `roundup` from `include/ccl_internal.h`. CMake includes it in `ccl`, but `include/containers.h` declares none of its functions. Object inspection shows six externally defined symbols: `newPool`, `PoolAlloc`, `PoolCalloc`, `PoolClear`, `PoolDestroy`, and `SetMaxFree`.

Its data structures and node-cache algorithm are the predecessor of `pool.c`: 20 buckets, 4096-byte boundaries, an 8192-byte minimum node, circular active-node list, and the `Pool` embedded in its first node.

## Function and API inventory

| Function | Responsibility |
| --- | --- |
| `SetMaxFree` | Convert a byte threshold to boundary units and update allocator free-budget state. |
| `newAllocator` | Round/reuse/allocate a memory node. |
| `allocator_free` | Cache nodes in indexed/sink buckets or return them to libc according to threshold. |
| `destroyAllocator` | Free all cached nodes with libc `free`. |
| `PoolAlloc` | Pointer-aligned bump allocation plus node reuse/creation/reordering. |
| `PoolCalloc` | Allocate and zero one byte-count (not the two-factor signature of public `iPool.Calloc`). |
| `PoolClear` | Reset self node and cache/free all other nodes. |
| `PoolDestroy` | Return/free all nodes and free the separate allocator. |
| `newPool` | Allocate allocator and initial node, then embed/initialize pool. |

Declarations for `newPool_debug` exist but have no definition/use. The `#ifdef TEST` smoke main is not part of CTest.

## Invariants

- Active nodes form a valid circular list with correct `ref` links and a permanent embedded `self` node.
- Returned nonzero ranges are pointer-aligned, in bounds, and non-overlapping until clear/destroy.
- Clear invalidates allocations and leaves the pool reusable; destroy releases nodes and allocator.
- A failed creation or growth allocation must not leak or corrupt list state.
- Byte thresholds must not silently truncate on platforms where `size_t` exceeds 32 bits.

## Historical pre-fix defects (resolved)

The findings below describe the pre-fix audit baseline; the implementation
status and dedicated private-prototype suite below are current.

### High: initial-node allocation failure leaks the allocator

`newPool` allocates `pool_allocator` at line 487. If `newAllocator` fails at line 490, it returns null without `free(pool_allocator)`. This is the same structurally confirmed failure-path leak as the newer `pool.c` implementation.

### Medium: `SetMaxFree` truncates byte counts to 32 bits

Line 133 casts `size_t in_size` to `uint32_t` before alignment. On 64-bit targets, any threshold above `UINT32_MAX` silently wraps, and near-`UINT32_MAX` alignment itself wraps to zero. The externally emitted function therefore cannot represent its declared `size_t` input domain.

## Integration status and current coverage

`unittests/smallpool_test.c` references these symbols through private
prototypes. Because the public header exposes only `iPool`, normal users still
cannot call this implementation without private declarations; nevertheless
the functions remain linkable from the static archive. This is separate
shipped code, not generated code and not an alias of `iPool`, so the dedicated
private-prototype suite remains the appropriate ownership unit.

## Required tests

1. Build this unit in an isolated target with private prototypes so the linker selects `smallpool.o`; do not accidentally test `iPool` instead.
2. Basic create, aligned allocations for `0, 1, 7, 8, 9`, fill patterns, calloc zeroing, clear/reuse, and destroy under ASan/UBSan/leak detection.
3. Exhaust the first node; exercise multiple indexed nodes and an oversized `> 81920` sink node; repeat clear cycles to force cache reuse.
4. Link-wrap or seam libc allocation to fail allocator creation, initial-node creation, and later growth independently. Assert the initial-node regression has balanced allocation/free counts and later failure preserves existing allocations.
5. Source-inclusive white-box tests for `SetMaxFree` at `0, 1, 4095, 4096, 4097, UINT32_MAX`, and values above `UINT32_MAX`; assert explicit rejection or a correctly widened representation.
6. Near-`SIZE_MAX` `PoolAlloc` requests must return null without state mutation.
7. Random allocation/clear sequences with range non-overlap checks and a final leak-free destroy.

## Recommended disposition and order

The legacy API is retained. Its creation-leak and threshold-width findings are
resolved, private prototypes are exercised by the dedicated suite, and the
implementation remains separately covered from `pool.c`.

## Implementation status

The legacy API is retained and now has a private-prototype unit suite in
`unittests/smallpool_test.c`. `newPool` releases its allocator when initial
node creation fails; `SetMaxFree` stores boundary counts as `size_t` and uses
division-based ceiling arithmetic so thresholds do not narrow or wrap; and
null pool operations return safely. The suite covers lifecycle, alignment,
calloc, indexed and sink-node reuse, clear cycles, near-`SIZE_MAX` requests,
threshold boundaries, and randomized disjoint ranges.

The focused coverage run reports 157/173 lines (90.75%) and 75/94 branches
(79.79%) for `src/smallpool.c`. The focused ASan+UBSan executable passes with
leak detection disabled because this environment's LeakSanitizer cannot
suspend its traced test process; the same executable reaches all six tests
without sanitizer diagnostics.

## Final integration verification

The final untraced integration run passed with ASan and UBSan. LeakSanitizer is
unavailable in the current ptrace-restricted environment, so no leak-enabled
pass is claimed here.
