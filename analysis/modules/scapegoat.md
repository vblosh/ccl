# `scapegoat.c` audit

## Scope and dependencies

- Physical source: `src/scapegoat.c`; public surface is
  `TreeMapInterface iTreeMap` in `include/containers.h`, with private `TreeMap`,
  `Node`, and iterator layouts in `include/ccl_internal.h`.
- Dependencies: `CurrentAllocator`, `iError`, `iHeap`, caller comparison/
  error/destructor/persistence callbacks, and stdio. The known `iHeap` stride
  alignment defect also affects TreeMap nodes whose requested element size is
  not suitably aligned.
- Invariants: nodes form an ordered parent-linked binary tree; root parent is
  NULL and every child points back to its parent; `count` equals reachable
  nodes; `max_size` tracks the deletion-rebalance threshold; height is bounded
  by the scapegoat alpha rule; each live node owns one copied element; and
  mutations advance a monotonic iterator timestamp.

## API, helper, and ownership inventory

| Area | Entries/helpers |
| --- | --- |
| Construction/lifetime | `Create`, `CreateWithAllocator`, `InitializeWith`, `Clear`, `Finalize`, allocator/element-size access, flags, callbacks, and `Sizeof`. |
| Ordered operations | `Add`, `AddRange`, replacement-style `Insert`, `GetElement`, `Contains`, `Erase`, `Apply`, `Equal`, and `Copy`. |
| Tree mechanics | `insert`, `Delete`, first/last/next/previous, `find`, `down_link`, `sibling`, subtree counting, tree-to-vine/compression/vine-to-tree rebalancing, and height arithmetic. |
| Iteration | allocated and caller-buffer initialization; first/next/previous/current and replace/delete behavior; iterator sizing/deletion. |
| Persistence | GUID-framed `Save`/`Load` plus default raw-element callbacks. |

The captured allocator owns the TreeMap header and its `ContainerHeap`; nodes
come from that heap and contain inline copied values. `GetElement` and iterator
accessors return borrowed node data. Erase/clear/finalize must call the
destructor exactly once with that data, never with the private node header.
Duplicate handling must reclaim an unused candidate node. Iterator-buffer
initialization is borrowed storage; a heap-created iterator is tree-allocator
owned.

## Historical pre-fix compatibility and correctness defects (resolved)

SG1-SG11 below record the pre-fix audit baseline. The completion evidence and
dedicated suite later in this document describe the current implementation.

### SG1 - the default comparator cannot insert distinct values (critical)

`Add`/`Insert` construct a `CompareInfo`, but pass the caller's raw `ExtraArgs`
to `insert` (lines 513-515 and 551-557). `insert` passes that pointer directly
to `bt->compare`, while `DefaultTreeCompareFunction` requires a
`CompareInfo *`; with the normal NULL extra argument it returns equality for
every pair at lines 808-813. A probe adding two distinct aligned values to a
default TreeMap reports size 1, contains the first, and cannot find the second.
Pass an initialized `CompareInfo` consistently and expose the caller payload
through its `ExtraArgs` member.

### SG2 - element-size rounding causes out-of-bounds reads (critical, ASan)

`CreateWithAllocator` stores `roundup(ElementSize)` as the public/internal
element size (line 845), and Add/Insert/queries copy or compare that rounded
size. Callers only promise the size passed to `Create`. A one-byte element
therefore makes `Add` read eight bytes at line 507; GCC 15 ASan reports a
stack-buffer-overflow. `InitializeWith` advances by the unrounded input size
while each Add reads the rounded size, making arrays especially unsafe.
Preserve the logical element size and round only the private node stride. This
also avoids returning a larger-than-requested `GetElementSize` and serializing
padding bytes.

### SG3 - duplicate candidates leak and `AddRange` never advances input (high)

`insert` returns the existing node for a duplicate, but `Add` ignores that
return and never gives its freshly allocated candidate back to `iHeap`.
`Insert` overwrites its candidate variable with the existing node and likewise
loses the new allocation. Those slots remain unreachable until heap teardown.
`AddRange` additionally copies `Data` on every iteration instead of advancing
by one element (lines 519-539). With a custom integer-key comparator and input
`{1,2,3}`, a probe reports size 1 and no value 2. Reclaim duplicate candidates,
define Add-versus-Insert duplicate status/replacement semantics, and advance a
byte pointer by the logical size.

### SG4 - `Equal` reports unequal trees as equal (high)

After compatibility checks, `Equal` breaks when comparison finds a mismatch
but unconditionally returns 1 (lines 339-347). A probe with same-allocator,
same-comparator trees containing 1 and 2 prints `equal=1`. It also supplies
`t1->aux` rather than a fresh `CompareInfo`; that is normally NULL and makes
the default comparator report equality. Return false on the first mismatch,
verify both traversals terminate together, and pass valid comparison context.

### SG5 - erase calls the destructor with a private node pointer (critical)

`Delete` invokes `DestructorFn(p)` at line 140, while `Clear` correctly invokes
it on iterator-returned data. Client destructors therefore interpret the
node's parent/child fields as their value during `Erase` and iterator deletion,
causing wrong frees or corruption for owning payloads. Call the destructor
once on `p->data` before returning the node to the heap. Replacement by
`Insert` or iterator also overwrites old owning data without any destructor.

### SG6 - iterator replacement is unsafe and can violate tree ordering (critical)

`ReplaceWithIterator` always calls `GetNext`, ignores `direction`, and then
copies a non-NULL replacement directly into the current node (lines 651-684).
Replacing an ordering key in place breaks the search-tree invariant; it also
does not advance the tree timestamp. Calling Replace before first positioning
leaves `pos == NULL` and reaches `Delete(NULL)` or `memcpy(NULL,...)`. The
iterator exposes no `GetLast`, `Seek`, or `GetPosition` despite those base
slots, `GetCurrent` performs no timestamp check, and an initialized-buffer
iterator passed to `DeleteIterator` is incorrectly freed. Implement ordered
replacement as erase/reinsert (with duplicate/failure policy), validate current
state, honor direction, and track allocated versus borrowed iterator storage.

### SG7 - erase leaves a dangling comparison context (high)

`Erase` assigns `tree->aux = &cInfo` where `cInfo` is a stack local, then
returns on both found and not-found paths without clearing it (lines 582-594).
Later `Equal` passes this expired pointer to the comparator, and reentrant or
diagnostic use can read dead stack storage. Clear transient context on every
exit and avoid storing call-local context in the persistent object when it can
be passed down explicitly.

### SG8 - readonly, mutation, and argument policies are largely absent (high)

Add, AddRange, Insert, Erase, Clear, and iterator replacement do not enforce
`CONTAINER_READONLY`; setters can change comparison ordering after population;
and most entries dereference NULL tree/data/callback/buffer arguments. Clear
resets timestamp to zero, so an older iterator with the same timestamp can
appear valid after destructive reuse. Non-NULL iterator replacement changes
data without timestamp advancement. Preserve a monotonic generation across
clear, invalidate all mutations, reject comparator changes on nonempty trees
or rebuild, and normalize BADARG/READONLY/error-callback behavior.

### SG9 - constructor/copy failure handling is not atomic (high)

`CreateWithAllocator` does not check `iHeap.Create`; it can return a nominal
tree whose first operation dereferences a NULL heap, and it does not clean up
the header. Size addition/rounding is unchecked. `Copy` does not check its new
tree or Add results and does not preserve the comparator needed to reproduce
the source ordering; it can return a partial or differently ordered tree.
Use checked size arithmetic, unwind each construction stage through the same
allocator, and define which safe metadata Copy preserves (a destructor cannot
be blindly duplicated for shallow owning values).

### SG10 - persistence is ABI-dependent and accepts partial headers (high)

`Save` writes the raw `TreeMap` control structure, including pointers and
function addresses, to disk. The format therefore depends on architecture,
compiler layout, rounded size, and process ABI and discloses meaningless
addresses. Struct reads/writes use byte-sized items but test only `== 0`, so a
nonzero partial `fread`/`fwrite` is accepted as a complete header (lines
899/943). `Load` then trusts serialized `ElementSize`, `count`, and flags for
allocation/loops without range validation. Serialize an explicit versioned
fixed-width header containing only logical metadata, require exact I/O counts,
bound arithmetic, and clean up on every custom/default callback failure.

### SG11 - height arithmetic has a large-count UB boundary (medium)

`vine_to_tree` computes `1 << floor_log2(count + 1)` with an `int` left operand
(line 306). On a 64-bit build, a tree large enough to require a shift of 31 or
more invokes undefined behavior even though `count` is `size_t`; `count + 1`
can also wrap. Use a checked `((size_t)1) << ...` formulation and guard all
count/byte/max-size arithmetic.

## Boundary behavior to lock down

- Reject or explicitly define zero-sized elements; preserve exact requested
  logical size for odd and sub-pointer-sized values and arrays.
- Add should define duplicate ownership/status; Insert should distinguish new
  insertion from replacement without leaking or skipping the old destructor.
- Empty/singleton/extreme lookup and erase, immediate/deep successor deletion,
  and the 3/4 full-rebalance threshold must retain all parent links.
- Returned element pointers are borrowed and invalid after erase/clear;
  mutation during iteration must report OBJECT_CHANGED before dereference.
- Comparator context must always contain both container fields initialized and
  the caller's unchanged `ExtraArgs`; changing ordering on a live tree is not a
  harmless setter operation.

## Current coverage and probe result

The legacy `tests/test.c` routine remains only a print-oriented smoke test, but
the active focused coverage is `unittests/scapegoat_test.c`. That dedicated
suite covers the repaired comparator, odd-size values, ranges, duplicates,
equality/copy, ownership, iterators, readonly/argument paths, rebalance/delete
stress, and persistence failures.

An isolated aligned custom-comparator stress probe inserted 1000 ascending
values, traversed all 1000 in order, erased every even value, and traversed a
reported 500-node tree cleanly under ASan/UBSan with leak detection disabled.
That supports the basic scapegoat rebalance/delete mechanics for this path.
The SG1-SG11 labels above refer to the historical baseline; the dedicated
suite and completion evidence cover the repaired paths.

## Required test matrix

1. Create/CreateWithAllocator/InitializeWith for logical sizes 0,1,3,7,8,9 and
   larger odd structs; arrays of each size, exact `GetElementSize`, node
   alignment, clear/reuse, and finalize under ASan/UBSan (SG2).
2. Default byte comparator and custom comparators that assert every
   `CompareInfo` field/ExtraArgs. Insert ascending, descending, zigzag, and
   randomized unique sets; compare traversal, Contains, GetElement, count, and
   height against a sorted reference after every mutation (SG1).
3. Trigger shallow/deep scapegoat discovery and root/subtree vine compression;
   validate root/child parent links and the height bound. Delete leaf,
   one-child, immediate-successor, deep-successor, root, absent, and sequences
   crossing the 3/4 full-rebalance threshold.
4. Duplicate Add/Insert and AddRange with unique/mixed/duplicate arrays;
   validate return convention, replacement values, destructor calls, count,
   and candidate reclamation with a counting heap allocator (SG3/SG5).
5. Equal for identity, NULL, empty, count/size/flags/allocator/comparator
   mismatch, equal values in different shapes, and first/middle/last unequal
   values. Copy custom-ordered trees, inject every allocation failure, and
   verify independent mutation/finalization (SG4/SG9).
6. Counting destructor on erase, iterator delete, clear, and finalize; owning
   pointer payloads make the node-versus-data mistake fail under ASan. Exercise
   Insert/iterator replacement ownership explicitly (SG5).
7. Heap and buffer iterators on empty/nonempty trees: first/last, next/previous,
   current, unsupported slots, both directions of replace/delete, before-first
   replacement, mutation invalidation, clear/reuse generation, readonly, and
   correct borrowed-buffer deletion policy (SG6/SG8).
8. NULL/readonly matrix over every public entry, setter query/reset,
   comparator change on populated trees, failure callbacks, reentrant compare/
   Apply behavior, and Erase found/not-found followed by context-sensitive
   equality (SG7/SG8).
9. Save/Load empty and populated trees through defaults and short/custom
   callbacks; wrong/truncated GUID/header/data, duplicate/corrupt counts,
   oversized element/count fields, flags, and forced allocation failure. Test
   cross-build compatibility only after defining a portable format (SG10).
10. Deterministic randomized differential runs mixing insert/replace/find/
    erase/iterate/clear, including enough elements to cross multiple heap
    blocks, followed by leak-enabled teardown.

The public/lifecycle matrix plus deterministic rebalance/delete shapes should
exceed 80% line coverage; duplicate, iterator, failure, persistence, and
arithmetic cases are needed for 70% branch coverage. Run the focused suite
under ASan/UBSan and leak checking, accounting separately for the shared
`iHeap` alignment defect until it is fixed.

## Compatibility-preserving handoff

Preserve the opaque TreeMap ABI and copied-value/set semantics. First separate
logical element size from aligned node stride and pass `CompareInfo` correctly;
then repair duplicate/destructor ownership and iterator replacement. Address
readonly/context/failure handling next, and version persistence rather than
trying to perpetuate raw in-memory headers as a portable format.

## Completion evidence (2026-08-08)

Implemented in `src/scapegoat.c`:

- Logical element sizes are retained exactly; only the private heap-node stride
  is rounded. Default comparison and every ordered operation now receive a
  fully initialized `CompareInfo`, including caller `ExtraArgs`.
- `AddRange` advances by one logical element, duplicate candidates are returned
  to the heap, `Equal` stops on mismatches and verifies synchronized traversal,
  and `Copy` preserves the comparator while checking failures.
- Erase, replacement, clear, iterator ownership, readonly mutation checks,
  timestamp invalidation, and ordered iterator replacement now preserve copied
  value ownership. Reuse after clear also clears the heap's stale free-list.
- Construction arithmetic, height arithmetic, argument checks, and persistence
  now use checked logical metadata. Save/Load uses a versioned fixed-width
  little-endian header rather than serializing process pointers, with exact I/O
  checks and cleanup on callback/allocation failures.

Added `unittests/scapegoat_test.c`, covering odd/small elements, default and
custom comparators, range insertion, duplicates, equality, copy, destructor
ownership, iterator replacement/deletion, readonly and argument paths,
rebalance/delete stress, and persistence failures.

Focused direct GCC coverage run (seven tests, source plus heap/error support):

- Lines: 85.03% (735 executable lines; 625 covered)
- Branches taken: 70.04% (464 branches; 325 taken)

Focused Clang ASan/UBSan run passed all seven tests with leak detection disabled.
CTest's configured LeakSanitizer mode cannot start in this environment because
LSan is blocked by the host ptrace policy (`LeakSanitizer has encountered a
fatal error`); no sanitizer memory or undefined-behavior report was observed.

## Final integration verification

The final untraced integration run passed with ASan and UBSan. LeakSanitizer is
unavailable in the current ptrace-restricted environment, so no leak-enabled
pass is claimed here.
