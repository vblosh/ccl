# `dlist.c` audit

## Scope and representation

- Physical source: `src/dlist.c`; public surface: `DlistInterface iDlist` in
  `include/containers.h`; private header/node/iterator layouts are in
  `include/ccl_internal.h`.
- Each node owns one fixed-width inline byte copy plus Next/Previous links.
  For a live nonempty list, First->Previous and Last->Next are NULL, walking
  either direction visits exactly count nodes, and every adjacent pair has
  reciprocal links. Empty means First and Last are both NULL.
- Nodes are allocated directly through the captured allocator or from an
  optional ContainerHeap. The header also contains a FreeList used by PopFront
  and PopBack, but current node allocation and Clear do not consume/free that
  list.
- Data pointers, DlistElement handles, and iterator results are borrowed.
  Structural mutation, transfer, clear, or finalization invalidates them.
- Destructor callbacks receive Data and must run exactly once for every owned
  element that is discarded rather than returned by a pop.

Append is intended to consume the second header; InsertIn copies then transfers
the copy's nodes; Splice and SplitAfter move chains. These operations must
preserve allocator/heap/destructor/comparator/vtable provenance and leave each
node owned by exactly one live header.

## API and helper inventory

| Area | Entries and behavior |
| --- | --- |
| Lifecycle/configuration | Create/CreateWithAllocator, Init/InitWithAllocator, InitializeWith, Copy, Clear, Finalize, size/flags/sizeof, allocator/comparator/error/destructor setters, UseHeap. |
| Node mechanics | `new_dlink`, Add_nd, reciprocal sibling/link maintenance, FreeList, direct or heap release. |
| Element operations | Add, PushFront/Back, PopFront/Back, InsertAt, ReplaceAt, Erase/EraseAll/EraseAt, Contains/IndexOf, GetElement/CopyElement, Front/Back. |
| Range/structural operations | AddRange, GetRange, InsertIn, Append, Splice, RemoveRange, Reverse, rotations, Sort, Select/SelectCopy, Equal, SplitAfter. |
| Direct node surface | First/Last/Next/PreviousElement, GetElementData, SetElementData, Advance/MoveBack/Skip. |
| Iteration/callbacks | Heap and placement iterators; next/previous/first/current/seek/replace; Apply, comparison, observer/error/destructor callbacks. |
| Persistence | Dlist GUID plus raw header and default raw records or custom Save/Read callbacks. |

GetRange uses inclusive `[start,end]`; RemoveRange appears intended as half-open
but is not implemented correctly. This differs from generic List GetRange and
must be deliberately documented or unified.

## Allocator, iterator, and persistence behavior

CreateWithAllocator captures the allocator. Copy, GetRange, SelectCopy, and
Load currently create through CurrentAllocator rather than source provenance.
UseHeap may use an explicit allocator and owns its node arena. Transfers are
safe only when heap/header ownership is moved or values are copied.

NewIterator allocates through the list allocator; InitIterator writes caller
storage. Both use a structure ending in ElementBuffer[1], while SizeofIterator
reports only the fixed structure. The base iterator advertises GetLast and
GetPosition, but dlist initialization does not populate them.

Persistence is process-native: Save emits a GUID, raw Dlist header (including
pointers/padding/callbacks), then elements. Load trusts width/count/flags,
allocates through CurrentAllocator, and does not restore comparator,
destructor, error callback, heap, allocator, or derived vtable. There is no
format version, endian/ABI declaration, or element-type identity.

## Confirmed correctness defects and compatibility hazards

### DL1 - PopFront/PopBack leak every direct-allocated node (critical
ownership)

Both pops put the removed node on `l->FreeList`, but new_dlink never reuses
FreeList and Clear traverses only First. With no backing heap, popped nodes are
never freed, including during Finalize. With a heap they remain until whole-
heap destruction. Reuse the free list safely or release nodes immediately;
define whether Pop with NULL output discards through the destructor.

### DL2 - EraseAt(last) frees First and leaves First dangling (critical,
ASan)

`rvp` starts at First. The last-position branch updates Last but never changes
rvp to the old last node; lines 915-922 then destroy/free rvp (the first node).
First still points to freed storage while the detached old last leaks. The next
traversal is a use-after-free. Capture the exact target before relinking and
notify observers before releasing its Data.

### DL3 - EraseAll advances through freed nodes (critical, ASan)

EraseInternal has the same pattern as List: after freeing a match it assigns
`previous = rvp` and reads `rvp->Next`. Consecutive/all removal therefore uses
freed memory and may relink through a freed predecessor. It also never checks
CONTAINER_READONLY, so it mutates protected lists.

### DL4 - RemoveRange neither removes nor frees the requested chain correctly
(critical)

The function walks to `end`, chooses `rvpE = rvp->Previous` (the last element
that should have been removed), and links the prefix to that node. It never
destroys or frees any node. It decrements count by `end-start-1`, cannot remove
through the tail because reaching NULL is treated as an assertion failure, and
sets First/Last to removed-side nodes in boundary cases. Replace with one
well-defined half-open detach/destroy algorithm and validate both directions.

### DL5 - Splice creates shared ownership and misses endpoints (critical)

Splice links toInsert's chain into list but leaves the entire toInsert header
unchanged and live. Finalizing either list then frees nodes still reachable by
the other. Insertion before First does not update list->First; insertion after
Last does not update list->Last. It performs no read-only, membership, self,
element-size, allocator, heap, destructor, or overflow checks. Define whether
Splice consumes/empties its source and enforce single ownership atomically.

### DL6 - InsertIn crashes at common endpoints and adopts incompatible nodes
(critical, ASan)

For nonempty lists, idx zero inserts after First. For idx equal to count,
`nle` is NULL and line 865 writes `nle->Previous`, causing a NULL dereference;
Last is not updated. The copied source uses CurrentAllocator and is adopted
without allocator compatibility. Its temporary header is freed, then passed
to the observer (use-after-free). Count arithmetic is unchecked. Implement
front/middle/back cases explicitly and preserve ownership provenance.

### DL7 - Append consumes headers without allocator/heap compatibility
(critical)

Append checks only element size. It can join nodes from a different allocator
or heap, then frees l2's header and loses the only handle to l2's heap. Later
cleanup frees nodes through l1's ownership path or leaks the arena. Self-append
creates a cycle and frees the live object. Comparator/destructor/vtable policy
can also differ. Reject incompatible inputs or copy values; document and test
source consumption.

### DL8 - iterator initialization uses uninitialized state and read-only
buffers overflow (critical, ASan/UBSan)

doinit never initializes index or Current. Calling GetNext/GetCurrent before
GetFirst consumes allocator/caller garbage. NewIterator and SizeofIterator
reserve only a fixed structure whose buffer starts at offset 100 and has four
bytes through tail padding on the observed 64-bit layout; copying a read-only
double or larger element overflows.

GetFirst sets index to 1 and Current to the second node while returning the
first; GetPrevious returns Current before stepping, producing surprising
position/value semantics. Seek returns a node rather than Data, clamps
oversized indexes, and skips timestamp checks. GetCurrent also skips timestamp
checks; read-only GetPrevious returns direct mutable Data. GetLast/GetPosition
remain uninitialized. DeleteIterator always frees placement storage.

### DL9 - Clear ignores the read-only bit and mishandles heap/free-list
lifecycle (critical)

Clear tests `Flags & CONTAINER_ERROR_READONLY` rather than
`Flags & CONTAINER_READONLY`; the negative error code has low flag bits clear,
so normal read-only lists are erased. Heap Clear finalizes all storage without
calling destructors for live values. Direct Clear ignores FreeList and leaks
popped nodes. It then resets flags/timestamp to zero, drops observer state, and
weakens stale-iterator detection.

### DL10 - Erase observer payload is freed before notification (high, ASan)

EraseAt stores `removed = rvp->Data`, frees rvp, and only then calls Notify with
the dangling pointer. Its traversal also decrements position, so observers see
the wrong index for middle elements. EraseInternal passes a private node rather
than Data and likewise needs a stable pre-free callback contract.

### DL11 - construction/bulk failure handling is incomplete (high)

CreateWithAllocator and InitWithAllocator dereference NULL allocator/result and
do not check node-size addition overflow. InitializeWith accepts NULL Data and
ignores Add_nd failure, returning a partial list. AddRange now terminates and
normalizes timestamp on success, but a failure keeps the appended prefix and
its per-node timestamps without documenting partial success or rolling back.
PushBack fails to validate pdata before memcpy. GetRange dereferences a failed
Create result.

### DL12 - direct node and split APIs bypass membership/ownership policy
(critical for heap lists)

SetElementData accepts foreign/stale nodes, ignores read-only and destructor
semantics, and can overflow a node allocated for a smaller element width.
SplitAfter does not verify pt membership. It transfers heap-created nodes into
a result whose Heap is NULL while the source heap retains arena ownership;
result Finalize then uses the wrong free path. It also drops comparator,
destructor, flags, error callback, and derived vtable policy.

### DL13 - mutation and read-only callback behavior is inconsistent
(medium/high)

Apply exposes writable Data even on a read-only list and ignores callback
return. Select does not advance timestamp for normal masks and resets it to
zero for all-false. SetCompareFunction ignores read-only. SetElementData has
the issues above. Sort and rotations now advance timestamp, but every logical
mutation and observer path needs one consistent invalidation rule.

### DL14 - copy/derived containers have undefined shallow ownership (high)

Copy byte-copies values but not DestructorFn/heap policy and uses
CurrentAllocator. GetRange and SelectCopy likewise omit comparator,
destructor, error/vtable policy, allocator provenance, and heap strategy.
Pointer-owning values therefore become ambiguous non-owning aliases or leak;
copying a destructor without a clone function would double free. Add an
explicit element clone/ownership contract.

### DL15 - persistence accepts partial raw headers and unsafe widths/counts
(high)

Save and Load use byte-count fwrite/fread for the header but reject only zero;
a positive partial header is treated as success. Load then uses uninitialized
remainder fields for malloc, loop bounds, and flags. Width/count arithmetic is
unchecked, the temporary buffer uses system malloc/free, and callback failure
raises the callback's zero/negative result rather than a stable FILE_READ code.
A restored flag can interfere with partial-result cleanup once Clear's
read-only check is repaired. Use a fixed, fully checked versioned format and
apply flags only after success.

### DL16 - placement/header and status contracts remain ambiguous (medium)

Init places a header in caller storage, but Finalize always allocator-frees it.
Sizeof excludes heap arena/FreeList ownership and can overflow. Sort allocation
failure returns zero rather than NOMEMORY. Contains converts all negative
IndexOf results to false, while IndexOf dereferences a NULL result on a match.
SizeofIterator's unused parameter prevents a strict `-Werror` build. Normalize
these without changing intentional empty/not-found semantics accidentally.

## Existing test status

There is no standalone `unittests/dlist_test.c`. The typed
`dlist_family_test.c` exercises some generic delegate behavior and a generic
persistence compatibility case, but does not cover standalone ownership,
FreeList, heap, transfer, direct-node, error, or generic iterator contracts.
The coverage manifest lists `dlist` separately, so its 80/70 gate is not
currently demonstrated by an owning suite.

## Required ASan/UBSan and 80%/70% test matrix

1. Create/Init/all allocators; push/add/pop at empty/singleton/many states;
   forward/backward invariants after every operation; direct and heap cleanup
   with destructor and allocation counters. Regress FreeList leaks (DL1).
2. EraseAt every position and Erase/EraseAll duplicate sequences under ASan,
   especially last erasure (DL2) and consecutive matches (DL3). Verify exact
   observer Data/index before free and read-only rejection.
3. RemoveRange front/middle/tail/full/reversed/clamped/equal bounds against a
   half-open reference model, with direct/heap nodes and destructors (DL4).
4. InsertIn, Append, Splice, SplitAfter at First/Last/middle and empty sides;
   self/foreign positions, allocator/heap/destructor/comparator/vtable
   mismatch, explicit consumed-source states, balanced cleanup and no sharing.
5. Heap and placement iterators from fresh/empty states through first/next/
   previous/seek/current/replace/remove; GetLast/position availability,
   read-only widths 1/4/8/large, stale mutations, invalid magic, and placement
   disposal.
6. AddRange/InitializeWith/GetRange/Copy/SelectCopy allocation failure at every
   point, exact rollback or documented partial state, overflow/NULL inputs, and
   source allocator/policy preservation.
7. Sort custom/default comparison, forward/backward topology, reverse and
   rotations at 0/1/count/>count; Apply early stop and read-only copies;
   selection masks none/all/leading/trailing/alternating with timestamps and
   destructor counts.
8. Direct element helpers for normal/stale/foreign/heap nodes, movement both
   directions, membership and read-only enforcement, width mismatch and
   destruction on replacement.
9. Save/load empty/nonempty and custom callbacks; wrong GUID, partial GUID/
   header/element, hostile widths/counts, read-only flags, allocation/read
   failures, and nontrivial-element policy under ASan/UBSan.
10. Randomized model-based operations compared with a reference deque. After
    every step validate count, endpoints, reciprocal links, no cycles, and
    exact node ownership. Run GCC coverage until `src/dlist.c` independently
    reaches at least 80% lines and 70% branches.

## Validation status (2026-08-08)

The standalone `unittests/dlist_test.c` suite now covers lifecycle and
destructor ownership, direct and heap nodes, range and transfer boundaries,
iterators, read-only buffers, persistence, and allocation-failure rollback.
`src/dlist.c` coverage is 82.06% of lines and 70.04% of taken branches in the
dedicated `dlist` coverage run. The standalone suite passes under GCC ASan and
UBSan; LeakSanitizer cannot initialize in the ptrace-restricted execution
environment. The typed dlist family suite also passes under the sanitizers.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
