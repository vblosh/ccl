# `deque.c` audit

## Scope and dependencies

- Physical source: `src/deque.c`; public surface is `DequeInterface iDeque` in
  `include/containers.h`.
- Dependencies: `CurrentAllocator`, `iError`, `iObserver`, `DlistElement` from
  `ccl_internal.h`, stdio persistence, and raw allocation/copy routines.
- Intended link invariant: `tail` is the left end with `Previous == NULL`,
  `head` is the right end with `Next == NULL`; following `Next` from tail or
  `Previous` from head visits exactly `count` nodes. Empty requires both ends
  NULL and count zero. Every structural mutation must repair both ends and
  increment `timestamp`.

## Function and contract inventory

| Area | Entries and behavior |
| --- | --- |
| Lifetime | `Init`, `Create`, `Clear`, `Finalize`, allocator/destructor/error-function setup. |
| End operations | `PushBack`/`Add`, `PushFront`/`AddLeft`, `PopFront`, `PopBack`, `Front`, `Back`. |
| Search/mutation | `Contains`, `Erase`, `EraseAll`, `Reverse`, `Apply`, `Equal`, `Copy`. |
| Metadata | `Size`, `Sizeof`, flags getters/setter, destructor/error setters. |
| Persistence | `Save` writes a header and elements directly or through a callback; `Load` reconstructs with direct/callback reads. |
| Iteration | heap/placement iterator creation, first/next/previous/current, deletion, and footprint. Iterator snapshots the deque timestamp. |

## Historical pre-fix defects

The D1-D12 findings below are the pre-fix audit baseline and are retained as
historical evidence. The implementation status later in this document describes
the current source and test state.

### D1 - empty `PushFront` creates an unreachable node (critical)

`AddLeft` sets `head = tail` before assigning `tail = newNode` (lines 129-132).
On an empty deque, count becomes one and tail is live but head stays NULL.
Public reproducer reports `Size()==1` while `Front` returns empty; finalization
cannot reach the node. Assign both ends to `newNode` in the empty case.

### D2 - pop copies from freed storage and leaves dangling ends (critical)

Both pop implementations save `Data`, free its node, then `memcpy` from that
freed address (lines 187-195 and 240-245). Copy output before destructor/free.
`PopFront` also fails to set the new head's `Next=NULL`, and both single-element
paths leave the opposite end pointing at freed memory. Repair links/endpoints
before callbacks and release. ASan coverage must use a nontrivial element size
and allocator quarantine to reliably expose the UAF.

### D3 - `Clear` frees only the current head and leaks the rest (critical)

It starts at `head` but advances through `tmp->Next` (lines 153-159); by
invariant the head's `Next` is NULL. A deque built by repeated `PushBack`
therefore releases one of N nodes, then discards tail and count. Walk
`Previous` from head (or `Next` from tail), destroying every node once.

### D4 - erase corrupts endpoints and `EraseAll` traversal (critical)

`EraseInternal` unlinks neighbors but never updates `head` or `tail`. Deleting
either endpoint leaves a dangling public endpoint. After a deletion it assigns
`tmp=tmp->Previous` and then immediately advances `tmp=tmp->Next`, so
`EraseAll` can revisit/skip nodes and use changed links. Save the next traversal
node first, update both endpoints, decrement once, and continue in one
direction.

### D5 - most traversal-based operations inspect only one element (high)

`Contains`, `Copy`, `Equal`, `Apply`, and `Save` start at `head` and follow
`Next`, although head is the node whose `Next` is NULL. With three `PushBack`
values, the public `Contains` reproducer finds only the newest (`0,0,1`) while
Size is three. `Save` dereferences NULL on its second loop iteration. Choose a
single documented logical order and traverse the matching direction throughout.

### D6 - `Reverse` follows the wrong direction and corrupts multi-node links (high)

Starting at head, line 359 saves `Next` (normally NULL), swaps only the first
node, then swaps head/tail. The remaining nodes retain old links. Reverse must
visit all nodes (saving the direction that reaches the next one), swap both
links for each, then exchange endpoints.

### D7 - structural changes do not invalidate iterators, and invalidation can call NULL (high)

Only `SetFlags` increments `timestamp`; add/pop/erase/clear/reverse do not.
Iterators silently traverse stale/freed nodes after mutation. Fresh deques also
initialize `RaiseError` to NULL, so the one timestamp mismatch that can occur
calls a NULL function pointer at lines 528/547/575. Initialize the handler,
increment timestamp for every structural change, and check before touching
current nodes.

### D8 - iterator state and return types are inconsistent (high)

`NewIterator` leaves `index` and `Current` uninitialized. `GetCurrent` returns
the node pointer while first/next/previous return `node->Data`. `GetNext` uses
`count-1`, which underflows for empty deques. `GetPrevious` restarts at head but
walks `Next`, again reaching only one node. Fully initialize state and make all
iterator accessors return element data with safe empty/boundary checks.

### D9 - allocation failures are unchecked or misreported (high)

`AddLeft` dereferences a failed allocation; `Add` reports BADARG rather than
NOMEMORY; `Copy` does not check Create/Add failures. `Init(NULL,...)` also
memsets NULL, and a zero element size is accepted. Add deterministic failing
allocator paths with cleanup and correct error codes.

### D10 - persistence trusts raw pointers and fabricates count after short reads (high)

`Save` writes the raw `Deque` control object, including pointers/function
pointers, as its header. `Load` accepts any nonzero partial header/element read,
uses untrusted `ElementSize`/`count` for allocation/loops, and finally assigns
`d->count=D.count` even when the loop stopped early (lines 463-488). This can
produce count/head mismatches and resource exhaustion. Define a pointer-free
versioned header, require exact transfer sizes and checked dimensions, and on
short read fail/clean up rather than publish a partial inconsistent deque.

### D11 - copy allocator/flags and size accounting are wrong (medium)

`Copy` creates with current allocator, later overwrites the allocator with the
source allocator (cross-family free), and self-assigns flags at line 337.
`Sizeof` charges `sizeof(DequeNode)` instead of `sizeof(DlistElement)` per node.
Create the copy directly with the intended allocator (or retain its actual
one), copy compatible metadata deliberately, and report real allocation size.

### D12 - public validation and destructor replacement are incomplete (medium)

Many methods dereference NULL deque/item/callback/stream arguments. Both
destructor setting and error operation names are inconsistent; the destructor
cannot be cleared because NULL is ignored. Add a table-driven validation pass
after core invariants are fixed.

## Historical coverage baseline and current coverage

The pre-fix implementation had no meaningful automated coverage: no test
referenced `iDeque`. The current `unittests/deque_test.c` is registered as
`test_deque` and covers endpoint/lifetime, mutation, copy, reverse, iterators,
callbacks, observers, persistence, and bad-argument behavior.

## Required test matrix

1. Regression D1: each push direction into empty, then every front/back/pop
   combination; mixed pushes must match a reference deque and preserve both
   endpoints through the last removal.
2. Multi-node pop and clear under ASan/leak checking with a large payload and a
   recording destructor; assert output is copied safely and every node/value is
   destroyed exactly once (D2/D3).
3. Erase first/middle/last/only, absent, adjacent duplicates, and all-duplicate
   `EraseAll`; verify size, sequence, endpoints, destructor counts, and reuse.
4. Contains positions, Apply order, Equal differences, independent Copy,
   Reverse empty/one/many/twice, and allocation failure during copy.
5. Iterator matrix for empty/one/many, first-next-end and previous boundaries,
   consistent Current, heap and placement construction, deletion rules, and
   every mutation invalidating with captured OBJECT_CHANGED rather than crash.
6. Persistence round trip using raw elements and callbacks; empty/one/many,
   short header, short element, callback failure, absurd/overflow metadata,
   write failure, and allocator cleanup. Tests should specify a new stable
   header rather than preserve raw pointer bytes.
7. Observer notifications for add/pop/clear/copy/finalize and flags/timestamp;
   callback observation must happen while element bytes remain valid.
8. Failing/counting allocator at control/node/iterator/load/copy allocations;
   BADARG table and correct operation names.

After repairs this exercises every operation and both traversal directions,
enough for 80% line/70% branch. Run ASan/UBSan and leak checks; randomized
model sequences are especially valuable after the deterministic regressions.

## Luna handoff

Treat this as one coherent invariant repair: D1-D4 and D2's lifetime ordering
first, D5-D8 traversal/iterator correctness second, D9-D11 allocation and
persistence third, then validation. Do not patch individual symptoms without
an internal link-invariant checker in tests, because the current failures
share reversed-direction assumptions.

## Implementation status

The deque implementation now maintains the tail-to-head link invariant across
all end operations, erase/clear/reverse, and iterator traversal. Pop output is
copied before destruction/free; every structural mutation advances the
timestamp; and placement-initialized headers are not released by Finalize.
Copy uses the source allocator, checks allocation failures, and deliberately
does not propagate the source destructor because element copies are shallow.
Save/Load use a pointer-free GUID/version header with
exact transfer checks and cleanup on short reads or callback failure.

`unittests/deque_test.c` covers endpoint and duplicate-erase regressions,
search/apply/equality/copy/reverse, heap and placement iterators,
invalidation, callbacks, observer notifications, persistence failures, and
bad arguments. The focused suite passes under ASan/UBSan. GCOV reports 85.61%
line coverage, 100% branch execution, and 71.51% of branches taken at least
once for `src/deque.c`. LeakSanitizer cannot initialize in this
ptrace-restricted workspace, so no local LSan pass is claimed.
