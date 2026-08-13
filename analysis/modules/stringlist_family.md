# String list generator family audit

## Scope and generation topology

This family is one implementation compiled twice through macros:

| File | Role | Character type and primitives | Exported interface |
| --- | --- | --- | --- |
| `src/stringlistgen.c` | Shared implementation body; not a standalone translation unit | Supplied by its wrapper | Supplied by `DATA_TYPE` |
| `src/stringlist.c` | Narrow instantiation | `char`, `strlen`, `strcpy`, `strcmp` | `iStringInterface` |
| `src/wstringlist.c` | Wide instantiation | `wchar_t`, `wcslen`, `wcscpy`, `wcscmp` | `iwStringInterface` |

`include/stringlistgen.h` generates the element, list, iterator, and vtable
types. `include/stringlist.h` and `include/wstringlist.h` select the narrow and
wide types. Both implementations depend on `CurrentAllocator` and
`ContainerAllocator`, `iError`, `iObserver`, `iHeap`, `Mask`, `qsortEx`, stdio,
and the generic callback types from `containers.h`.

The wrappers contain no behavior beyond macro selection and distinct GUIDs.
Most diagnostic strings nevertheless say `iStringList`, `StringList`, or even
`iList` in both instantiations, making wide-list reports ambiguous. The coverage
manifest correctly treats the three physical files as one `stringlistgen`
family and `src/stringlistgen.c` as its coverage source.
`unittests/stringlist_family_test.c` is the dedicated active suite for both
instantiations.

## Representation, invariants, allocation, and ownership

Each node contains `Next` followed by an inline, null-terminated string. The
list header stores the vtable, logical count, flags, mutation timestamp,
`ElementSize`, first/last links, compare and error callbacks, optional heap,
captured allocator, and optional element destructor. An iterator stores the
generic iterator dispatch table, a magic value, owning list, index/current and
previous links, captured timestamp, and a private string buffer used for
read-only traversal.

The intended invariants are:

- `count == 0` iff `First == NULL && Last == NULL`; otherwise following `Next`
  from `First` visits exactly `count` nodes, reaches `Last`, and
  `Last->Next == NULL`.
- Every node owns one complete string copy. Its allocation must be at least
  `offsetof(node, Data) + (length + 1) * sizeof(CHARTYPE)`, with checked
  addition and multiplication.
- A node is freed by the same allocator or heap that allocated it, exactly
  once. A configured destructor runs exactly once before every logical element
  is discarded or overwritten.
- Heap-created headers from `Create*`, `InitializeWith`, `Copy`, `GetRange`,
  `SelectCopy`, `Load`, and `SplitAfter` are caller-owned until `Finalize`.
  `Init*` initializes caller storage and records non-owning placement state;
  `Finalize` releases owned node storage without freeing a caller-provided
  header.
- `Copy`, `GetRange`, `SelectCopy`, and `InsertIn` should duplicate strings.
  `Append` and `SplitAfter` transfer nodes and consume or alter the source as
  described below; transfer is safe only when allocator/heap provenance is
  compatible.
- `GetElement`, `Front`, `Back`, `FirstElement`, `LastElement`, `ElementData`,
  `Advance`, and writable iterator access return borrowed pointers. They are
  invalid after removal, replacement/reallocation, clear/finalize, or a splice
  that transfers the node.
- Every structural change, replacement, and reorder increments `timestamp` so
  pre-existing iterators reject access with `CONTAINER_ERROR_OBJECT_CHANGED`.
- Read-only blocks every operation that changes strings, links, order, count,
  ownership, or callbacks. It may return defensive copies where an API would
  otherwise expose mutable storage.
- `ElementSize` consistently describes the character unit: current narrow and
  wide constructors initialize it to `sizeof(CHARTYPE)`, and
  `GetElementSize` reports that value.

`NO_GC` is defined by `containers.h` in the supported build, so `Clear_nd`
walks and frees nodes or finalizes `Heap`. Without `NO_GC`, it simply drops all
links and relies on an external collector. Most individual removal paths do
not consistently honor `Heap`, however, and `UseHeap` is a stub that always
returns `CONTAINER_ERROR_NOT_EMPTY`, even for an empty list.

## Public operation inventory

Both generated vtables expose the same entries in the same order. `EraseAt`
is the public slot backed by the function named `RemoveAt`.

| Area | Operations | Implemented behavior and important boundaries |
| --- | --- | --- |
| Construction | `Create`, `CreateWithAllocator`, `Init`, `InitWithAllocator`, `InitializeWith` | Zero a header and install the generated vtable, default comparator, global error callback, selected allocator, character-unit size, and owned/placement-header state. Bulk initialization validates its input and finalizes a partial result on failure. |
| Lifetime | `Clear`, `Finalize` | `Clear` releases nodes, resets count/links/heap and flags, and advances the timestamp. `Finalize` calls `Clear`, releases an owned custom vtable, and frees only an allocator-owned header. Both reject a read-only object. |
| Metadata/configuration | `Size`, `Sizeof`, `GetElementSize`, `GetFlags`, `SetFlags`, `GetAllocator`, `SetAllocator`, `SetErrorFunction`, `SetCompareFunction`, `SetDestructor`, `UseHeap` | Query accounting/policy, replace callbacks, or move an empty heap header to a new allocator. Null callback means query for compare/error/destructor. `UseHeap` is unimplemented. |
| Add/replace | `Add`, `PushFront`, `InsertAt`, `AddRange`, `ReplaceAt`, `SetElementData` | Duplicate strings at tail/front/index/in bulk or replace a value. `InsertAt(count)` delegates to `Add_nd`; `SetElementData` may relocate a node and updates the caller's node pointer. |
| Remove | `PopFront`, `Erase`, `EraseAt`, `EraseRange`, `Clear` | Remove first, first compare-equal, indexed, ranged, or all nodes. Empty pop returns zero; absent erase returns `CONTAINER_ERROR_NOTFOUND`. |
| Splice/reorder | `InsertIn`, `Append`, `Reverse`, `Sort`, `SplitAfter` | `InsertIn` copies then splices another list; `Append` transfers all nodes and frees the second header; `SplitAfter` transfers a suffix to a new header; reverse/sort relink in place. |
| Search/access | `Contains`, `IndexOf`, `GetElement`, `CopyElement`, `Front`, `Back` | Search through the active comparator or return/copy a positional string. `IndexOf` writes a zero-based index only on success. Mutable pointer access currently rejects read-only. |
| Copy/comparison/range | `Copy`, `Equal`, `GetRange` | Deep-copy all strings, compare lists only when comparator function pointers match, or copy half-open `[start,end)` with `end` clamped to count. |
| Filtering | `Select`, `SelectCopy` | Retain mask-selected nodes in place or make a selected deep copy. Mask length must equal list count. |
| Callback traversal | `Apply` | Calls the callback for every string and ignores callback return values. A read-only list supplies a temporary copy intended to prevent mutation. |
| Raw node traversal | `FirstElement`, `LastElement`, `NextElement`, `ElementData`, `Advance`, `Skip` | Expose links/data directly, advance a caller-held link, or skip up to `n` links. First/last reject read-only because they expose mutable nodes. |
| Iterators | `NewIterator`, `InitIterator`, `DeleteIterator`, `SizeofIterator`; iterator `GetFirst`, `GetNext`, `GetPrevious`, `GetCurrent`, `Seek`, `GetPosition`, `Replace` | Heap or placement iterator, traversal, clamped seek, current position, replace/delete current, and timestamp validation. Both constructors initialize the same supported dispatch entries; `GetLast` is not implemented. |
| Persistence | `Save`, `Load` | Write/read the family GUID, versioned fixed-width stream metadata, and length-prefixed string payloads. A saver callback may replace record output; a loader callback may supply each payload. |

Return conventions are mostly `1` for success rather than the new count stated
by several comments. Empty `Clear` and successful `Finalize` also return one;
empty `Select` and `PopFront` return zero. `Contains` maps not-found to zero but
also maps every negative search result to zero after doing its own argument
diagnostic. `Seek` clamps every index at or above the last position to the last
node. `SetFlags` can always clear read-only and returns the previous flags.
`SetAllocator` returns a relocated header rather than an error code and is only
defined for an empty, heap-created list.

## Private helper inventory

- `ErrorReadOnly` and `NullPtrError` format/route errors. The former uses the
  list callback; the latter always uses global `iError`.
- `NewLink` allocates and copies one inline string. It is the ownership
  primitive used by add, copy, ranges, load, and selection-copy.
- `DefaultStringListCompareFunction` uses the wrapper-selected `strcmp` or
  `wcscmp`.
- `Clear_nd`, `Add_nd`, `RemoveAt_nd`, and `IndexOf_nd` bypass public validation
  and notification for internal callers.
- `lcompar` adapts two node-pointer array entries to the active list comparator
  for `qsortEx`.
- `Seek`, `GetNext`, `GetPosition`, `GetPrevious`, `GetCurrent`, `GetFirst`,
  and `ReplaceWithIterator` implement iterator behavior. `NewIterator`,
  `InitIterator`, and `DeleteIterator` manage iterator storage.
- `DefaultSaveFunction` writes the string payload bytes; `Save` writes the
  explicit payload byte length before invoking it, and `Load` performs exact
  default reads (or delegates each payload to `loadFn`).
- Forward declarations for `IndexOf_nd`, `RemoveAt_nd`, `Create*`, and
  `Finalize` allow their earlier callers. Every other static function is
  installed directly in the public vtable or catalogued above.

## Historical pre-fix narrow versus wide behavior

The following table records the pre-fix audit baseline. Current allocation and
persistence behavior is described in the operation and persistence sections;
the dedicated family suite is authoritative for regressions.

The intended difference is only `char` versus `wchar_t` and the matching C
library routines. The implementation frequently treats character counts as
byte counts:

| Path | Narrow instantiation | Wide instantiation |
| --- | --- | --- |
| Node allocation (`NewLink`) | Generally enough storage, with layout-dependent over-allocation under `NO_C99`; no overflow check | Allocates roughly one byte per extra wide character, then `wcscpy` overflows the node |
| In-place `ReplaceAt` | Overflows whenever the replacement is longer than the original node capacity | Same defect, compounded by the original undersized node |
| `SetElementData` realloc | Size formula omits checked layout arithmetic; use-after-realloc defects apply | Also omits multiplication by `sizeof(wchar_t)` |
| Read-only `Apply` and iterator buffers | Character count happens to equal byte count; iterator buffers are still uninitialized and failure-unchecked | Buffers allocate character counts as bytes and `wcscpy` overflows them |
| `Sizeof` | Mirrors the current narrow node allocation formula | Mixes one wide-character width with a character count and does not report the proper wide storage requirement |
| Default persistence | Writes no terminator; load passes nonterminated bytes to `strlen`, causing out-of-bounds reads | Writes only `wcslen` bytes, usually a fraction of the wide payload, then performs unsafe `wcslen`/copy on load |
| GUID | `13327ea7-78ed-4fd2-95ed-1c110eb9719c` | `a0543963-cbac-4afb-09c0-a14079328d7b` |

All variable-size arithmetic should be centralized around checked
character-to-byte conversion and `offsetof(node, Data)`. A wide-string smoke
test longer than the inline `Data[1]` slot should currently reproduce a heap
overflow under ASan.

## Persistence and compatibility

Current `Save` writes, in order:

1. The 16-byte narrow or wide GUID.
2. Fixed-width magic, version, encoding, character-unit size, flags, and an
   element count.
3. For each element, a fixed-width payload byte length followed by the payload;
   the default saver writes the string bytes without its terminator.

`Load` validates the GUID and stream metadata, creates a new list with
`CurrentAllocator`, checks payload-unit and size bounds, reads each payload
exactly (or invokes `loadFn`), appends a reconstructed terminated string, and
unwinds the partial result on failure.

Consequences:

- The current metadata and payload lengths are fixed-width and pointer-free;
  encoding and character-unit fields let `Load` reject incompatible streams.
- Default records omit the terminator intentionally; `Load` allocates one and
  appends it after reading the declared payload.
- Saved counts and lengths are range-checked before allocation and iteration,
  and short reads fail and clean up the partial list.
- `loadFn` is called for each payload when supplied; `arg` is passed through to
  both callbacks.
- Read-only flags survive loading; because `Clear` rejects read-only lists,
  callers must clear that flag before finalizing a loaded read-only list.

The current format preserves the existing family GUIDs as type identifiers but
does not accept the old raw-header representation. The versioned,
pointer-free metadata and explicit payload byte lengths are the current stream
contract; malformed, legacy, wrong-character-width, and truncated input is
rejected rather than partially published.

## Historical pre-fix correctness concerns

SL1-SL16 below record the pre-fix audit baseline. The implementation evidence
and dedicated suite later in this document describe current behavior.

### SL1 - wide strings overflow nearly every allocated destination (critical)

`NewLink`, `SetElementData`, read-only `Apply`, and read-only iterator paths use
character counts directly as allocation byte counts. `NewLink` also lacks
overflow checks. Correct every allocation before enabling ordinary wide-list
lifecycle tests; cover empty, one-character, non-ASCII, long, and arithmetic
overflow inputs where a synthetic boundary is feasible.

### SL2 - `ReplaceAt` unconditionally copies into the old node capacity (critical)

Nodes do not record capacity, yet `ReplaceAt` calls `strcpy`/`wcscpy` in place.
Replacing `"a"` with a long string overwrites the heap. Reallocate or replace
the node transactionally, repair `First`/`Last` and predecessor links, and do
not invoke the old destructor until allocation succeeds.

### SL3 - default save/load is memory-unsafe and custom load is nonfunctional (critical)

Default save omits terminators; load calls string-length routines on raw,
nonterminated payload. Wide I/O uses character counts as byte counts. The raw
header is unportable and untrusted lengths/counts are unchecked. Short length
input returns a partial success, and `loadFn` is never invoked. Exercise all
truncation points and hostile lengths under ASan/UBSan after defining a safe,
versioned contract.

### SL4 - `EraseRange` removes the wrong links and breaks core invariants (critical)

The function does not check read-only, treats `end` inconsistently, positions
`start_pos` at or before the requested start, removes nodes after it, computes
`end-start+1` but loops for one fewer removal, cannot remove the first node,
can assert on valid singleton/end ranges, never repairs `Last`, and does not
increment `timestamp`. It also always uses the allocator instead of the heap.
Replace it with a clearly half-open `[start,end)` implementation and test every
endpoint on zero-, one-, and multi-element lists.

### SL5 - `SetElementData` has realloc use-after-free and endpoint defects (critical)

It does not reject `*pple == NULL`, does not verify membership reliably before
using the node, omits allocation multiplication/overflow checks, never checks
`realloc` failure, and reads `le->Next` after realloc may have freed `le`. It
does not update `Last` when the tail moves and does not call the destructor.
Use a saved successor/predecessor, checked allocation, and atomic publication;
reject foreign/null links.

### SL6 - iterator construction and read-only iteration use uninitialized memory (critical)

`NewIterator` leaves `ElementBuffer`, `Previous`, `GetLast`, and `Replace`
uninitialized. `InitIterator` leaves those fields plus `GetPosition`
uninitialized. Read-only `GetFirst`/`GetNext`/`GetPrevious` free the garbage
buffer; `GetCurrent` can return it before any copy. Resizes are byte-sized for
wide strings and next/previous allocation failures are unchecked. Deletion
never releases a valid buffer. Fully initialize both iterator forms, define
placement teardown, and test all dispatch slots.

### SL7 - iterator positioning and mutation semantics are inconsistent (high)

`Seek` returns a node pointer rather than string data, clamps out-of-range
indices, ignores timestamp mismatch, and does not populate a read-only buffer.
`GetPosition` type-puns through unrelated `struct ListIterator` and lacks a
null check. `ReplaceWithIterator` moves before replacing/removing; at the last
element a forward move fails but mutation proceeds, leaving cursor semantics
unclear. Iterator timestamp checks occur after some boundary early returns, so
mutation may be hidden. Define cursor behavior first, then cover both directions
and every boundary.

### SL8 - `InsertIn` and `Append` have unsafe splice ownership (critical)

`InsertIn(0, source)` inserts after the first destination element, and insertion
at the end does not update destination `Last`. Its copied nodes use the source
allocator but are later freed through the destination allocator. Notification
passes the already-freed copied header. `Append` similarly transfers nodes
without checking allocator/heap compatibility, frees the second header without
freeing an owned custom vtable, and does not reject self-append. Tagged
allocators, self operations, empty/nonempty combinations, and observer capture
are mandatory regression cases.

### SL9 - `AddRange` failure rollback leaves a corrupted logical list (critical)

Each `Add_nd` increments count/timestamp. On later failure, `AddRange` frees the
new suffix and restores only `Last`; it does not restore count/timestamp or
`First` when the original list was empty. On success it increments timestamp
again and reports observer count as zero because `n` has been consumed. It also
does not validate individual pointers. Test deterministic failure at every
element from both empty and populated lists.

### SL10 - destruction and heap handling are inconsistent (high)

`PopFront` never calls the destructor and always allocator-frees; `EraseRange`
also ignores heap; `Clear_nd` finalizes a heap without visibly applying the
element destructor; other remove/select paths vary between `Data` and
`&Data`. `UseHeap` can never establish a heap. A counting destructor and tagged
heap/allocator should prove exactly-once cleanup for replace, every removal,
selection, clear, and finalize.

### SL11 - allocator and placement-header contracts are unsafe (high)

`CreateWithAllocator` dereferences a null allocator. `InitWithAllocator`
accepts one and fails later. `SetAllocator` frees the old header, which is
invalid for placement objects; it silently returns null for nonempty/null-
allocator cases and reports the flag value `CONTAINER_READONLY` instead of the
error code on read-only. `GetRange`/`SelectCopy` use `CurrentAllocator` rather
than preserving the source allocator. Establish explicit header ownership and
allocator propagation rules.

### SL12 - copy, initialization, and derived-object policy is incomplete (high)

`InitializeWith` does not validate `Data`/entries and ignores every `Add_nd`
failure, returning a partial object. `Copy` preserves read-only flags before it
finishes; if a later node allocation fails, `Finalize(result)` refuses to clear
it and leaks. It does not copy the destructor. `GetRange`, `SelectCopy`, and
`SplitAfter` fail to preserve a coherent set of vtable, comparator, error,
flags, destructor, and allocator policies. Test subclassed vtables/callbacks
and allocation failure after each node.

### SL13 - `SplitAfter` trusts arbitrary node pointers (critical)

The function never proves that `pt` belongs to `l`. A foreign link can detach a
different list and make `l->count -= count` underflow while changing `l->Last`.
For a valid tail it returns null rather than an empty suffix. Validate
membership before mutation and make failure atomic.

### SL14 - mutation timestamps and endpoint metadata are incomplete (high)

`Sort` relinks without incrementing timestamp. `EraseRange` changes links/count
without timestamp or `Last` repair. `SetElementData` misses `Last`. `Append`
increments destination timestamp even when appending an empty list, while
several internal additions increment it once per element and again per bulk
operation. Pick a consistent invalidation rule and assert it after every
mutator; exact increment magnitude need not be public, but unchanged versus
changed must be reliable.

### SL15 - null/read-only/error handling contains direct crashes and semantic gaps (high)

`SetCompareFunction` reads `l->Compare` before checking `l`. `GetPosition`
dereferences null. `Finalize(NULL)` does not raise the normal null diagnostic.
Read-only blocks `GetElement`/`Front`/`Back` and finalization, while
`EraseRange` ignores it. Error names and codes vary (`CONTAINER_READONLY`
versus `CONTAINER_ERROR_READONLY`, `iList`, `StringList`, missing dot). Error
callback tests should assert the code and operation category without freezing
accidental spelling until names are normalized.

### SL16 - size/accounting and comparator details are misleading (medium)

`ElementSize` remains zero. `Sizeof` lacks correct wide byte scaling and does
not overflow-check. `Equal` requires identical comparator
function pointers rather than merely using a documented comparison policy.
Search calls pass `&node->Data` while sort/default comparison pass
`node->Data`; these addresses coincide but the inconsistent types complicate
custom comparators. Specify exact accounting and callback operand contracts.

## Required unit-test matrix for 80% line / 70% branch coverage

Build one family suite that links and actively invokes both `iStringList` and
`iwStringList`; otherwise wide-only template branches and byte/character bugs
remain invisible. A small invariant helper should check count, endpoints,
termination, traversal length, and expected strings after every successful and
failed mutation. Run the suite under ASan/UBSan and through the coverage gate.

1. **Construction and lifetime:** narrow/wide `Create`, custom allocator
   creation, placement `Init*`, `InitializeWith` for zero/one/many, allocator
   identity, flags, `Size`, `Sizeof`, `GetElementSize`, clear/reuse, and
   heap-header finalize. Placement finalization must release only owned
   storage; caller-provided placement headers must not be freed. Cover
   null/failing allocator behavior.
2. **Basic sequence operations:** add empty/short/long/non-ASCII strings,
   push front, insert at zero/middle/count and beyond count, replace shorter/
   equal/longer, copy element, get/front/back, pop with and without output,
   erase found at first/middle/last and absent, and erase-at each position.
   Include duplicate values and empty/singleton endpoint repair.
3. **Ranges and bulk updates:** `AddRange` with zero/one/many and null entries;
   `GetRange` empty, `[0,0)`, full, middle, clamped end, start==count, and
   start>end; `EraseRange` analogous cases under the chosen half-open contract.
   Fail allocation at each bulk node and prove exact rollback.
4. **Copy/splice/ownership:** independent copy, equal true/false/self/null and
   mismatched comparators; `InsertIn` at zero/middle/count into empty/nonempty;
   append empty/nonempty in both combinations; reject self-append; split after
   first/middle/last and reject foreign/null nodes. Use two tagged allocators to
   detect cross-free, and verify source ownership/consumption contracts.
5. **Ordering/search/callbacks:** contains/index found at every position,
   absent, null arguments, result-null, and a case-insensitive or reverse custom
   comparator with `ExtraArgs`; reverse empty/one/many twice; sort empty/one,
   sorted/reverse/equal-heavy lists, read-only, custom order, and temporary-array
   allocation failure; apply writable mutation behavior and read-only defensive
   copy behavior for short and long strings.
6. **Masks:** select/select-copy with empty, all-zero, all-one, leading/trailing
   rejects, alternating values, and bad-length/null masks. Verify selected-copy
   independence, timestamp changes, destructor counts, read-only rejection,
   and allocation failure after several selected nodes.
7. **Raw node helpers:** first/last on empty/nonempty/read-only; next and
   advance through end; skip by zero, middle, exact length, and beyond; element
   data; `SetElementData` for head/middle/tail with shrinking/growing values and
   null/foreign links. Check endpoint repair and iterator invalidation.
8. **Iterators:** allocated and placement forms; verify every dispatch slot;
   empty/one/many first/current/next/previous/seek/position boundaries;
   writable and read-only narrow/wide traversal; replace and remove in both
   directions; list mutation invalidation before every accessor; wrong magic,
   null input, buffer growth, allocation failure, and correct teardown. Do not
   call `DeleteIterator` on placement storage.
9. **Persistence:** empty and populated narrow/wide round trips under the fixed
   format, embedded non-ASCII wide values, custom save/read callbacks with arg
   propagation, wrong-family GUID, empty file, truncated GUID/header/length/
   payload at every byte boundary, excessive count/length, callback failure,
   read-only saved flags, and cleanup after partial load. Legacy unsafe files
   should be rejected deterministically rather than read out of bounds.
10. **Failure and policy branches:** null list/data/output/stream cases for
    every public family; read-only rejection for every mutator; custom error
    callback capture; comparator/destructor setter query and replace behavior;
    counting destructor across replace/pop/erase/range/select/clear; observer
    events with stable live arguments; `UseHeap` empty/nonempty once implemented;
    and custom-vtable finalization only when its ownership is explicit.

For the branch target, deterministic fail-on-N allocators are essential for
`NewLink`, header creation, copy/range/select partial cleanup, sort table,
iterator construction/buffer growth, realloc, and load. At least one list of
three or more elements is needed for every head/middle/tail link branch. Both
read-only and writable iterators must traverse forward and backward. The
narrow and wide tests should share behavioral assertions but use independent
fixtures so gcov records execution for both macro instantiations. A strict GCC
and Clang compile plus ASan/UBSan run should accompany the numeric coverage
gate; sanitizer success is a completion criterion because the current line and
branch percentages can be reached while the variable-length paths still
overflow.

## Implementation and verification evidence

The family implementation addresses the confirmed SL1-SL6 defects and related
ownership invariants while preserving the narrow and wide GUID identifiers:

- Node and temporary-buffer allocation uses checked character-to-byte sizing
  based on `offsetof(node, Data)` and `sizeof(CHARTYPE)`.
- Replacement and `SetElementData` allocate a complete replacement before
  publishing links, repair `First`/`Last`, and release the old node exactly
  once after success.
- `Erase` delegates destruction and deallocation to the single `ReleaseLink`
  path, so configured destructors run exactly once; a dedicated regression
  test asserts one invocation for a found element.
- `EraseRange` is half-open `[start,end)`, repairs all endpoints, checks
  read-only state, and uses the node allocator/heap release path.
- Iterators initialize every dispatch/state field, resize wide buffers in
  character units, reject stale/wrong iterators, and release owned buffers;
  placement iterators do not free caller storage.
- `Create*`/`Init*` headers track ownership so placement headers can be safely
  finalized, and append rejects incompatible allocator/heap provenance.
- Save/load uses the legacy GUID followed by a versioned, pointer-free header
  with fixed-width fields, explicit character-unit width, byte payload lengths,
  checked count/length arithmetic, terminator reconstruction, and strict
  truncation rejection. Legacy raw-header streams are rejected.

The dedicated `unittests/stringlist_family_test.c` suite has 9 tests covering
both instantiations, lifecycle/mutation, ownership/destructor behavior,
iterators, read-only paths, persistence/truncation, and allocator failures.
Direct GCC gcov aggregation of `src/stringlist.c` and `src/wstringlist.c`
reports 84.35% line and 70.86% branch coverage for the generated family.
Clang ASan/UBSan runs pass with `ASAN_OPTIONS=detect_leaks=0`;
LeakSanitizer cannot run in this environment because its ptrace/interceptor
policy is restricted, so leak conclusions are based on allocator ownership
assertions and sanitizer-safe teardown paths.

## Final integration verification

The final untraced integration run passed with ASan and UBSan. LeakSanitizer is
unavailable in the current ptrace-restricted environment, so no leak-enabled
pass is claimed here.
