# `priorityqueue.c` audit

## Scope and dependencies

- Physical source: `src/priorityqueue.c`; public surface is
  `PQueueInterface iPQueue` in `include/containers.h`.
- Implements a Fibonacci min-heap using `iHeap` for node storage. It also uses
  `CurrentAllocator`, `iError`, a degree-consolidation table, and raw memory
  operations.
- Invariants: `count==0` iff Root/Minimum are NULL; roots form one circular
  doubly linked list, children form circular sibling lists, parent/degree/mark
  agree, Minimum has the least key, and every node is owned exactly once by a
  live backing heap. Consolidation table storage must use the queue allocator.

## Function and contract inventory

| Area | Entries/contract |
| --- | --- |
| Lifetime | `Create`, `CreateWithAllocator`, `Clear`, `Finalize`, plus internal destructive helper used by Union. |
| Basic queue | `Add`/`Push`, `Size`, `Front`, `Pop`, priority clamping to `CCL_PRIORITY_MIN/MAX`. |
| Fibonacci mechanics | root/sibling insertion/removal, `heaplink`, `consolidate`, `ExtractMin`, Cut/CascadingCut, key replacement helpers. |
| Value operations | `Copy`, `Equal`, `Union`, `Sizeof`. |
| Dependencies | node allocation/iteration/free through `iHeap`; dynamic `lognTable` for consolidation. |

## Historical pre-fix defects (confirmed at the audit baseline)

### P1 - popping an empty queue dereferences NULL (critical)

`Pop` calls `ExtractMin` unconditionally. `ExtractMin` reads `ret->Child` before
checking `ret` (lines 503-508). Public empty-pop under UBSan reports null member
access at line 508 and ASan then reports a near-null SEGV. Return the documented
empty sentinel before extraction and make `ExtractMin` itself defensive.

### P2 - popping from a queue with multiple roots returns `INT_MIN` (high)

`removerootlist` overwrites the removed node's key with `INT_MIN` whenever it
was not the sole root (line 391). `Pop` later returns `x->Key`. Public reproducer
with keys 5 and 9 returns `-2147483648` while copying the value for key 5 and
leaving front key 9. Save key/value before structural removal; free/recycle the
node only after copying.

### P3 - node stride from `iHeap` is misaligned (critical, dependency)

Adding a second four-byte value triggers UBSan misaligned accesses throughout
node initialization/linking because the heap advances by an unrounded stride.
This is rooted in H1 in `heap.md`; the priority test is the smallest real-world
reproducer and must stay as a sanitizer regression.

### P4 - `Clear` changes only count and leaves a live heap (critical)

Lines 575-579 leave Root, Minimum, children, node storage, and consolidation
state untouched. After clear, `Size` says zero while `Front` still returns the
old element; later Add/Pop violates count/list invariants. Clear the backing
heap and reset every structural field while retaining allocator/vtable/config.

### P5 - Union cannot safely combine independently owned node heaps (critical)

Union splices `hb` nodes into `ha`'s root list, then `destroyheap(hb)`. That
helper uses system `free` instead of `hb->Allocator`, leaks `hb->Heap`, and
zeros/frees the control block (lines 141-145). The returned queue cannot free
the transferred nodes because they remain in hb's backing heap. Custom
allocators can also be invalidly freed immediately. Implement ownership-safe
union (for example move values into one queue, or support multiple backing
heaps) and define consumption semantics; reject incompatible element sizes or
allocators.

### P6 - creation publishes a queue after backing-heap failure (high)

`CreateWithAllocator` does not validate allocator/element size and never checks
`iHeap.Create` at line 157. A failed second allocation returns a non-NULL queue
whose first Add dereferences NULL. Free the control block and return NOMEMORY on
partial failure.

### P7 - consolidation storage bypasses the captured allocator and loses state on failure (high)

`checkcons` assigns system `realloc` directly to `h->lognTable` (lines
422-424), losing the old pointer when realloc fails; `destroyheap` uses system
free, while normal `Finalize` never frees `lognTable` at all. Use a temporary
pointer with `h->Allocator->realloc`, preserve the old table on failure, and
free it during Clear/Finalize according to retained-capacity policy.

### P8 - extracted nodes are never returned to the backing heap (high)

`Pop` removes nodes structurally but never calls `iHeap.FreeObject`. Storage is
retained until finalization, and heap iteration used by Copy/Equal can still see
removed objects (sole-root removal does not even mark key `INT_MIN`). Recycle
after output/key capture, coordinated with a fixed heap live-slot mechanism.

### P9 - ExtractMin ignores consolidation allocation failure (high)

`consolidate` can return NOMEMORY, but line 525 discards the result after the
root list has already been dismantled. Pop still reports success with possibly
inconsistent Root/Minimum state. Either preallocate before destructive changes
or propagate failure while preserving/reconstructing a valid queue.

### P10 - Copy/Equal iterate allocation history, not logical queue contents (high)

They walk `src->Heap`, inheriting the freed-slot iterator defects and allocation
order rather than Fibonacci-tree membership. `Equal` also leaks both iterators
on either mismatch return and never verifies `obj2` before dereference. Traverse
logical roots/children or define a safe queue iterator; equality should compare
priority/value multisets independent of internal consolidation history.

### P11 - Sizeof and public validation are incorrect (medium)

`Sizeof` adds `count * sizeof(PQueue)` instead of node/heap/table storage.
`Add(NULL)`, `Front(NULL)`, `Front(nonempty,NULL)`, `Finalize(NULL)`, and
`Union` NULL/incompatible operands can fault. `Add` permits NULL data and leaves
reused/new element bytes stale/zero depending path, which needs a defined rule.
Validate arguments and report actual owned allocation.

### P12 - key/math portability has hidden truncation and aliasing assumptions (medium)

Internal `comparedata` accepts `int` while public keys are `intptr_t`, and
`ReplaceKeyData` stores old keys in `int`. The active `ceillog2` type-puns a
double/unsigned union with hardcoded endianness and assumes 32-bit unsigned;
degree/table shifts use signed `1 << Log2N`. Use `intptr_t` throughout and a
portable integer ceiling-log implementation with checked table bounds.

## Current coverage

`unittests/priorityqueue_test.c` is the dedicated seven-test suite and is
auto-discovered by `unittests/CMakeLists.txt`. It covers lifecycle and empty
operations, ordering/value association, clamping/reuse, consolidation,
copy/equality, randomized reference sequences, union, and allocator failures.
The normal CTest run passes `test_priorityqueue`; the no-test statement was
historical pre-fix coverage information.

## Required test matrix

1. Empty Front/Pop/Clear/Size/Finalize behavior, explicitly regressing P1
   under ASan/UBSan; one-element add/peek/pop and reuse.
2. Two and many mixed/duplicate/negative priorities; assert stable value-key
   association and nondecreasing popped keys. The two-root case must regress P2
   and the second node must regress P3.
3. Boundary keys below/at/above `CCL_PRIORITY_MIN/MAX` and full-width
   `intptr_t` values; assert the documented clamping rule without truncation.
4. Clear empty/nonempty/consolidated queues, assert Front empty, reuse, and
   balanced storage/destruction (P4/P8).
5. Sequences large enough to exercise consolidation degrees, children, repeated
   ExtractMin, and allocator failure during table growth. After each step compare
   with a reference multiset.
6. Copy after adds/pops/consolidation; mutate independently. Equal must cover
   NULLs, different element sizes/count/data/key/insertion histories and clean
   every iterator/resource on mismatch.
7. Union empty-empty, empty-nonempty, nonempty-empty, and two nonempty queues;
   same/different allocators and element sizes, explicit consumed-input
   semantics, all values popped in order, and balanced custom allocations.
8. Failing/counting allocator for queue block, backing heap, heap table/block,
   and consolidation realloc; state must remain valid and no allocation family
   may be crossed.
9. BADARG table, `Sizeof` at lifecycle states, randomized model-based queue
   sequences, and final ASan/UBSan/leak runs.

This covers the public surface plus root insertion/removal, linking and
consolidation branches. Replace/decrease-key helpers are not public and should
not be exposed solely for coverage; dead aborting code can be removed or tested
only if the API is restored.

## Luna handoff

Land heap H1-H4 first. Then fix P1/P2/P4/P8 around one safe ExtractMin contract,
followed by allocator-safe creation/consolidation/finalization, and redesign
Union ownership before enabling its tests. Copy/Equal should be based on logical
membership, not raw heap iteration. Keep every production fix protected by the
minimal reproducer above.

## Verification status

`src/priorityqueue.c` now validates public NULL arguments, handles empty
Front/Pop safely, preserves key/value association through extraction, clears
all structural and backing-heap state, recycles extracted nodes, and captures
the queue allocator for consolidation-table growth and destruction. Copy and
Equal traverse logical root/child membership, and Union builds an owned merged
queue before consuming both non-empty inputs; incompatible element sizes are
rejected. Priority keys remain `intptr_t` internally and are clamped only at
the public int bounds.

The focused `unittests/priorityqueue_test.c` suite (including a randomized
reference sequence) passes under ASan/UBSan and reports 89.77% line coverage
and 74.40% branch coverage for
`src/priorityqueue.c`. LeakSanitizer cannot initialize in this execution
environment's ptrace setup, so sanitizer verification uses leak detection
disabled; allocator-failure and finalization paths are covered by the focused
suite.

## Final integration verification

LeakSanitizer cannot initialize in the current local workspace because of its
ptrace restriction, so a current local ASan/UBSan+LSan integration pass cannot
be claimed. The earlier untraced pass is historical campaign evidence recorded
on 2026-08-08 and does not supersede the current focused-run limitation.
