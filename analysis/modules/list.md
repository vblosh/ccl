# `list.c` audit

## Scope and representation

- Physical source: `src/list.c`; public surface: `ListInterface iList` in
  `include/containers.h`; private header/node/iterator layouts are in
  `include/ccl_internal.h`.
- This is a singly linked list of fixed-width inline byte copies. A live list
  has `count == 0` iff `First == Last == NULL`; otherwise following `Next` from
  `First` reaches exactly `count` nodes, ends at `Last`, and `Last->Next` is
  NULL.
- Nodes come either directly from the list's captured `ContainerAllocator` or
  from an optional `ContainerHeap`. Every node must be returned through the
  same ownership mechanism that created it. A heap and direct-node ownership
  regime cannot be mixed accidentally.
- `GetElement`, Front/Back, iterator results, and direct ListElement helpers
  return borrowed pointers. They become stale after removal, clear, sort,
  transfer, or finalization. `CopyElement` and PopFront copy bytes to caller
  storage.
- A destructor, when configured, owns logical element teardown and must receive
  `node->Data` exactly once before an element is discarded. Byte-copying
  pointer-valued elements is shallow unless a separate clone policy is added.

Mutation invariants require failure-atomic count/First/Last/topology updates,
one timestamp advance per successful logical operation, no mutation under
`CONTAINER_READONLY`, and balanced observer events. Placement `Init` headers
remain caller-owned, whereas Create headers are allocator-owned; the API needs
destruction semantics that distinguish them.

## Public API and helper inventory

| Area | Entries and behavior |
| --- | --- |
| Lifecycle/configuration | Create/CreateWithAllocator, Init/InitWithAllocator, InitializeWith, Copy, Clear, Finalize, flags, comparator/error/destructor setters, Size/Sizeof/GetElementSize/GetAllocator. |
| Node allocation | `NewLink`, checked public mutators, `_nd` helpers, optional UseHeap/GetHeap. |
| Element operations | Add, PushFront, PopFront, InsertAt, ReplaceAt, Erase/EraseAll, EraseAt (`RemoveAt`), Contains, IndexOf, GetElement/CopyElement, Front/Back. |
| Range/list operations | AddRange, InsertIn, Append, EraseRange, RemoveRange, GetRange, Reverse, rotations, Sort, Select/SelectCopy, Equal. |
| Direct node surface | FirstElement, LastElement, NextElement, GetElementData, SetElementData, Advance, Skip, SplitAfter. |
| Callbacks | Apply, default/custom comparison, observer notification, error and destructor callbacks. |
| Iteration | NewIterator/InitIterator/DeleteIterator/SizeofIterator and first/next/previous/current/seek/position/replace-or-remove helpers. |
| Persistence | Save/Load with a List GUID, raw header, and default raw element records or caller callbacks. |

The two public range removers have no coherent shared boundary contract:
GetRange and RemoveRange behave as half-open `[start,end)`, while EraseRange
appears intended as inclusive but implements neither convention safely.

## Ownership transfers, allocator behavior, and persistence

CreateWithAllocator captures its allocator for the header and direct nodes.
Copy preserves the source allocator; GetRange, SelectCopy, and Load instead use
CurrentAllocator. UseHeap may create a separate backing heap. Append is written
as a consuming transfer of `l2` nodes followed by freeing `l2`'s header;
SplitAfter transfers a suffix to a new header; InsertIn first copies its source
then transfers the copy's nodes. Those operations are correct only if allocator,
heap, destructor, comparator, vtable, and header ownership remain compatible.

Heap-created iterators and nodes are owned; placement iterators and Init headers
are borrowed caller storage. The iterator embeds only a one-byte trailing
buffer but read-only traversal copies an entire element into it.

Save writes a GUID, the process-native `List` structure (including pointers,
callbacks, allocator, padding, and flags), then records. Load trusts serialized
ElementSize/count/flags, allocates with CurrentAllocator, and restores only
flags and byte values. It does not restore comparator, destructor, error
callback, heap, derived vtable, or allocator. The format is ABI-dependent and
has no version, endianness, or element-type identity.

## Historical pre-fix correctness defects and compatibility hazards

### L1 - `EraseAll` advances through a freed node (critical, ASan)

EraseInternal frees a matching `rvp`, then assigns `previous = rvp` and reads
`rvp->Next` at lines 829-830. The all-matches path therefore performs a
heap-use-after-free on its first removal and can also retain a freed `previous`
for later relinking. Save `next` before destruction/free and update previous
only when the current node survives.

### L2 - AddRange rollback leaves corrupted state (critical)

Each Add_nd increments count and timestamp. On a later allocation failure,
the nonempty-list rollback frees appended nodes and resets Last but does not
restore count or timestamp; it also omits destructors for rolled-back owned
values. If the original list was empty, the cleanup branch does not free the
partial prefix at all: First/count still describe those nodes while Last is
reset to NULL. On success it adds an extra timestamp and reports the already-
decremented `n` (zero) to observers. Preserve the original complete state and
commit once, or explicitly define safe partial-success semantics.

### L3 - `EraseRange` is structurally incorrect (critical)

EraseRange computes `end-start+1`, positions `start_pos` incorrectly for start
zero, starts deletion at `rvp->Next`, never enforces read-only, does not repair
Last reliably, and does not advance timestamp. For `[0,...]` it preserves the
first node and removes following nodes; other ranges have inconsistent count
and endpoint behavior. Remove or reimplement this duplicate API around one
well-defined boundary convention.

### L4 - `RemoveRange` violates destructor and heap ownership (critical,
ASan)

The newer half-open RemoveRange frees every removed node directly through
`l->Allocator`, never calls DestructorFn, and never uses `iHeap.FreeObject`.
Heap-backed lists therefore pass heap interior/object storage to the wrong free
path; pointer-owning elements leak their payloads. It also lacks observer
events. Use the normal node-destruction helper for every removed node and test
front/middle/tail/full ranges in both allocation modes.

### L5 - InsertIn and Append have unsafe transfer semantics (critical)

InsertIn copies through the source allocator and then adopts those nodes into
the destination without requiring allocator compatibility. Index zero in a
nonempty destination inserts after the first element; insertion at count does
not update Last. After freeing the temporary copied header, it passes that
freed header pointer to the observer. Count addition is unchecked.

Append consumes and frees `l2` but checks only element width and allocator, not
heap/destructor/comparator/vtable compatibility. Heap-backed nodes remain owned
by `l2`'s heap while its header is lost. `Append(l,l)` creates a cycle and frees
the live header. Establish exact consuming semantics, reject self/incompatible
transfers, and either transfer heap ownership or copy values.

### L6 - heap lifecycle and configuration are unsafe (critical ownership)

Clear finalizes a backing heap without invoking destructors for its live
elements. Direct-node Clear calls the destructor with the private node pointer
`tmp` instead of `tmp->Data`. UseHeap returns success even if iHeap.Create
returns NULL and defaults to CurrentAllocator rather than the list's captured
allocator. Its private `CONTAINER_LIST_SMALL` flag also has value 2, colliding
with public `CONTAINER_HAS_OBSERVER`. Destruction and allocation provenance
must not depend on overlapping flags.

### L7 - iterator read-only buffering overflows and ownership modes are
indistinguishable (critical, ASan)

NewIterator allocates only `sizeof(ListIterator)` and SizeofIterator reports
the same, but read-only traversal copies `ElementSize` bytes into trailing
`ElementBuffer[1]` (only tail padding makes a few small widths appear safe).
Larger values overflow heap or caller placement storage. Allocate/report
`offsetof(ElementBuffer)+ElementSize`.

DeleteIterator always frees through the list allocator, so calling it for an
InitIterator placed on the stack/embedded storage is an invalid free. Add an
ownership marker or separate disposal contracts.

### L8 - iterator navigation and invalidation contracts are inconsistent
(high)

Seek returns a `ListElement *` while all other traversal calls return element
Data; it clamps every oversized index to Last rather than reporting an index
error and does not check timestamp. GetCurrent also omits timestamp validation.
InitIterator accepts a NULL buffer and does not set GetLast. Replace/remove can
leave Current referring to moved/removed state depending direction. Sorting,
rotations, and most selection do not advance timestamp, so existing iterators
silently observe reordered topology.

### L9 - constructors and derived-result failures are not contained (high)

CreateWithAllocator dereferences a NULL allocator and casts ElementSize to int
before checking its upper bound, allowing very large size_t values through
after narrowing. InitWithAllocator dereferences NULL result/allocator and lacks
the same size guard. InitializeWith dereferences NULL Data for nonzero n and
ignores every Add_nd failure, returning a partial list as success. GetRange
does not check Create before writing `result->VTable`; invalid ranges return
NULL while leaking the already-created result. Allocation products in Sizeof
and node sizing are unchecked.

### L10 - direct node APIs bypass read-only, membership, and destruction
(high)

SetElementData does not reject read-only lists, does not verify that `le`
belongs to `l`, and overwrites an owned value without invoking its destructor.
Passing a node from a smaller-width list can overflow that node's allocation.
SplitAfter likewise accepts a foreign node and can underflow count or detach a
different list. A heap-backed suffix is transferred into a result with Heap
NULL, so result finalization uses the wrong ownership path while the source
heap still owns the nodes.

### L11 - Clear/lifecycle metadata and derived vtable ownership are unsafe
(high)

Clear resets Flags and timestamp to zero rather than advancing mutation state,
silently disables observers, and can make stale iterators appear current after
wrap/reset combinations. Finalize assumes every vtable unequal to `&iList` is
heap-owned and frees it; generated/static derived interfaces violate that
assumption. Init headers are caller-owned but Finalize always frees the header.
These require explicit ownership fields/contracts, not pointer heuristics.

### L12 - Sort/Select/rotations and Apply do not consistently invalidate or
honor callbacks (medium/high)

Sort relinks all nodes without incrementing timestamp. RotateLeft/Right and the
non-all-zero Select path also omit timestamp; the all-zero path increments it.
Writable Apply can mutate values but ignores callback return and does not
invalidate iterators. SetElementData has the same problem beyond its explicit
timestamp/destructor issues. Define whether value mutation invalidates, then
make traversal-stop and observer behavior consistent.

### L13 - shallow Copy/derived containers lose ownership policy (high)

Copy duplicates element bytes but not DestructorFn or heap policy. For
pointer-owning values, the result either leaks because it is non-owning or
would double-free if the destructor were copied without a clone callback.
GetRange and SelectCopy also omit comparator/error/destructor/vtable policy and
choose CurrentAllocator. Define clone versus borrowed-byte semantics and apply
it across every derived-container operation.

### L14 - persistence trusts a raw, partially readable header (high)

Load reads the header with `fread(&L,1,sizeof(List),stream)` but tests only for
zero. A truncated positive-length header is accepted, leaving the rest of the
stack object uninitialized before ElementSize/count are used for allocation
and loops. Counts and widths are unchecked for overflow/resource limits.
System calloc/free is used for the temporary buffer rather than a captured
allocator. A loaded READONLY flag can also prevent Finalize from cleaning a
later element-read failure, leaking the partial result. Use a fixed-width,
fully checked versioned header and delay policy flags until successful load.

### L15 - return/error conventions and strict build hygiene are inconsistent
(low/medium)

Size(NULL) casts a negative error code to size_t, producing a huge value.
Several methods report bare names rather than `iList.*`; Contains maps all
negative IndexOf outcomes to false. SizeofIterator's unused parameter produces
a `-Wall -Wextra` warning and fails a strict `-Werror` build. Normalize public
status contracts while preserving intentionally query-like NULL behavior such
as Sizeof(NULL).

## Current coverage and test status

`tests/test.c` has legacy list exercises for bulk values, ranges, rotations,
copy/equality, persistence, and iteration, but they are not a focused CTest
suite and many checks are print/manual. The current tree also has dedicated
`unittests/list_test.c` and `unittests/list_family_test.c` suites, both
auto-discovered by `unittests/CMakeLists.txt`; the former owns standalone iList
semantics and the latter covers typed delegates. The normal CTest run passes
both `test_list` and `test_list_family`. Current GCC coverage for `src/list.c`
is 82.08% lines and 70.56% branches; the former “no list_test.c” statement was
the historical pre-fix baseline.

## Required ASan/UBSan and 80%/70% test matrix

1. Create/Init/all allocator variants, zero/huge widths, NULLs, add/push/pop,
   front/back/access/copy, flags, clear/reuse, heap and placement lifecycles.
2. Insert/erase/erase-all at every position with duplicate runs and destructor
   counters. Keep an ASan regression for consecutive EraseAll matches (L1).
3. AddRange success/zero/failure at each allocation with empty/nonempty lists;
   assert exact rollback, count, endpoints, timestamp, destructor and observer
   payload. InitializeWith failure needs the same checks.
4. Both EraseRange and RemoveRange over empty/singleton/front/middle/tail/full,
   equal/reversed/out-of-range bounds, direct and heap nodes; compare with a
   half-open reference model after unifying the contract.
5. InsertIn, Append, SplitAfter with empty and endpoint positions, self/foreign
   nodes, same/different allocators, heaps, comparators, destructors, derived
   vtables, and explicit source-consumption postconditions.
6. Heap/placement iterators for every traversal/seek/position/current/replace/
   remove direction; stale topology/value mutations, invalid magic, read-only
   values wider than tail padding, and ownership-aware deletion under ASan.
7. Sort/reverse/rotations/custom comparator and CompareInfo; Select/SelectCopy
   masks none/all/alternating; Apply early-stop/read-only behavior; assert
   iterator invalidation and observers.
8. Direct node traversal and data helpers with valid, stale, foreign, heap and
   read-only nodes. Assert membership validation and exactly-once destruction.
9. Counting/failing allocators for headers, nodes, heap, copies, ranges, sort
   table, iterator, read-only Apply buffer, and load buffer; balanced frees and
   no allocator-family crossings.
10. Save/load default and custom callbacks, empty/nonempty, wrong GUID,
    truncated GUID/header/elements, hostile count/width, callback failure,
    read-only serialized flags, and nontrivial-element rejection/documentation.
11. Randomized operation sequences against a reference array, validating count,
    order, First/Last, no cycles, and every node count after each step. The
    current dedicated suite records 82.08% lines and 70.56% branches for
    `src/list.c`; retain ASan+UBSan regression coverage as the implementation
    evolves.
