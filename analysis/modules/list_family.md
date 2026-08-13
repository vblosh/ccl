# Typed singly linked list family audit

## Scope and ownership

- Ownership unit: generator implementation `src/listgen.c`, instantiated by
  `src/intlist.c`, `src/doublelist.c`, and `src/longlonglist.c`; public generator
  template `include/listgen.h` is reached through `intlist.h`, `doublelist.h`,
  and `longlonglist.h`.
- Each instantiation exports one global interface (`iintList`, `idoubleList`,
  `ilonglongList`) and concrete list, element, iterator, and interface types.
  The generator is not a separately compiled translation unit.
- The typed list header is deliberately layout-compatible with generic `List`.
  Almost all behavior is a cast-and-forward adapter over `iList` in
  `src/list.c`; list nodes own an inline copy of the scalar value. Pointers
  returned by `GetElement`, `Front`, `Back`, `ElementData`, `Advance`, and
  iterator accessors are borrowed and become stale after the relevant mutation
  or destruction.
- A heap-created header from `Create`, `CreateWithAllocator`, `InitializeWith`,
  `Copy`, `GetRange`, `SelectCopy`, `Load`, or `SplitAfter` is owned by the
  caller. `Append` consumes and frees the second list header. `InsertIn` copies
  the second list and leaves the argument owned by its caller. `SplitAfter`
  transfers, rather than copies, the suffix nodes to a new header. `Init` and
  `InitWithAllocator` initialize caller storage; such a header must be cleared,
  not passed to a finalizer that frees the header.

## Dependencies and invariants

- Direct dependencies are `iList`, `iError`, `CurrentAllocator`,
  `ContainerAllocator`, `ContainerHeap`/`iHeap`, `Mask`, observers, iterator and
  compare/save/read callback types, raw `memcpy`/`memcmp`, stdio, and the
  private generic layouts in `ccl_internal.h`.
- Header invariant: `ElementSize == sizeof(DATA_TYPE)`; `count == 0` iff both
  `First` and `Last` are NULL; otherwise `Last->Next == NULL`, following `Next`
  from `First` visits exactly `count` nodes and ends at `Last`; every node was
  obtained from the configured allocator or heap and is released exactly once.
- Type/vtable invariant: every object published as a typed list has its matching
  typed global vtable, including objects returned indirectly by delegated
  methods. The typed header, element, iterator, and interface layouts must stay
  ABI-compatible with the generic layouts used through casts.
- Mutation invariant: structural changes and in-place replacement increment
  `timestamp`, invalidating extant iterators. Read-only operations reject
  mutation without changing links, values, count, or timestamp.
- Allocator invariant: the allocator that allocated a header or node must free
  it. `UseHeap` is only legal on an empty list. `Append` already requires equal
  allocators; any splice/transfer must enforce the same rule or preserve
  per-node provenance.
- Scalar comparison is currently bytewise for search/equality. Consequently
  integer equality is natural on supported representations, but `+0.0` and
  `-0.0` differ and NaN equality follows representation, not C numeric
  equality. Tests should characterize this unless the public contract is
  deliberately changed.

## Function and wrapper inventory

| Area | Generator-owned behavior |
| --- | --- |
| Scalar adapters | `Contains`, `Add`, `CopyElement`, `ReplaceAt`, `PushFront`, `PopFront`, `InsertAt`, `Erase`, `EraseAll`, and `IndexOf` take/return scalar values and forward their addresses to `iList`. `Contains` collapses every negative `IndexOf` result, including BADARG, to false. |
| Lifetime/type restoration | `Create`, `CreateWithAllocator`, `Init`, `InitWithAllocator`, `InitializeWith`, `Copy`, `SelectCopy`, and `Load` call the generic implementation and then `SetVTable`. `GetAllocator`, `GetElementSize`, and `Sizeof`/`SizeofIterator` expose metadata. |
| Typed element links | `NextElement`, `ElementData`, `SetElementData`, and `Advance`; `FirstElement`, `LastElement`, and `Skip` are generic delegates installed by `SetVTable`. |
| Iteration | `NewIterator` and `InitIterator` use generic iterator construction, then `SetupIteratorVTable` replaces `Iterator.Replace` with `ReplaceWithIterator`; `DeleteIterator` is delegated. |
| Sorting | A generator-local pointer-table quicksort (`QSORT`, `shortsort`, `Sort`) is selected by each wrapper's `COMPARE_EXPRESSION`; it does not use the list's `Compare` callback. |
| Delegated generic methods | `Size`, flags, `Clear`, `Finalize`, `Apply`, `Equal`, error/destructor setters, `Save`, `GetElement`, `EraseAt`, `InsertIn`, `Reverse`, `GetRange`, `Append`, compare setter, `UseHeap`, `AddRange`, `Back`, `Front`, `RemoveRange`, rotations, in-place `Select`, and `SplitAfter` are populated in the typed global vtable on first successful `SetVTable`. |
| Interface holes | `EraseRange` is declared and initialized NULL but is never populated. `Compare` is the typed byte comparator. Unlike `ListInterface`, the typed interface omits `GetHeap`. |

Generic return conventions flow through the adapters: success is normally 1,
empty `PopFront` is 0, not-found is `CONTAINER_ERROR_NOTFOUND`, and bad index,
read-only, bad argument, incompatible allocator/type, and allocation failures
use the corresponding negative container error. `Sort` returns 1 for zero/one
element lists, negative error for NULL/read-only/allocation failure, and 1 after
relinking a larger list.

## Historical pre-fix compatibility defects (confirmed at the audit baseline)

### D1 - `PopFront` inserts instead of removing (critical)

`PopFront` at `listgen.c:63-66` calls `iList.PushFront`. A sanitizer smoke run
starting with `[20,10]` and output value 99 returned success and produced
`[99,20,10]`, size 3, leaving the output unchanged. Route it to
`iList.PopFront`; cover empty, one, many, NULL output, read-only, and endpoint
repair.

### D2 - normal typed finalization frees static storage (critical, ASan)

`SetVTable` stores the address of the global typed interface in each object.
Delegated generic `Finalize` frees any vtable whose address is not `&iList`
(`list.c:351-353`). `iintList.Finalize(iintList.Create())` therefore attempts
to free global `iintList`; ASan reports a bad free. The generic finalizer needs
an ownership distinction for dynamically cloned vtables, not an address test
that treats every subclass/global vtable as heap storage. This blocks all
ordinary leak-clean lifecycle tests until repaired.

### D3 - typed iterator ABI is stale and replacement dispatch is incompatible (critical)

Generic `struct ListIterator` now contains `Magic` between `Iterator` and `L`;
the generated iterator does not. `SetupIteratorVTable` writes `ListReplace` at
the offset occupied by generic `timestamp`/`ElementBuffer`, so a fresh typed
iterator immediately reports OBJECT_CHANGED. This reproduced under
ASan/UBSan. In addition, the public return type is `Iterator *`, whose Replace
contract takes `void *data`, but the installed function actually takes a scalar
`DATA_TYPE` and is forced through an incompatible function-pointer cast. An
ordinary `it->Replace(it, &value, direction)` therefore has undefined ABI and
does not pass the requested value. Keep one ABI-compatible iterator layout and
signature; test both allocated and placement iterators plus replace/remove in
both directions.

### D4 - double and long-long sorting compare pointer positions (critical)

The wrappers in `doublelist.c:4` and `longlonglist.c:5` define
`COMPARE_EXPRESSION(A,B)` as `B > A ? -1 : B != A`, comparing slots in the
temporary pointer array. Four-value sanitizer smokes remained completely
unsorted for both types. The int wrapper correctly dereferences `Data`.
Generate a value comparator for all three types (or honor `l->Compare`) and
cover both the short-sort and quicksort paths.

### D5 - sort violates the published compare/timestamp contracts (high)

All typed sorts ignore the callback installed by `SetCompareFunction`; even the
working int path is hard-coded ascending. `Sort` also relinks nodes without
incrementing `timestamp`, so an iterator cannot detect that its positional
meaning changed. Test custom descending comparison and post-sort iterator
invalidation after selecting one consistent contract.

### D6 - `SetVTable(NULL)` turns recoverable failures into null dereferences (high)

`SetVTable` dereferences unconditionally. Thus allocation failure from Create
or Copy, invalid/short input from Load, and invalid mask/input to SelectCopy
crash in the adapter instead of returning NULL. Guard NULL before touching the
vtable. Use a deterministic failing allocator plus bad Load/SelectCopy inputs
to cover every caller.

### D7 - `SplitAfter` publishes a generic object as typed (high)

The delegated generic method creates its result with vtable `&iList` and the
typed adapter does not restore it. Calling methods through the returned
object's typed `VTable` then uses incompatible scalar/pointer signatures; the
interface layout also diverges after `UseHeap` because typed interfaces omit
generic `GetHeap`. Wrap `SplitAfter` with `SetVTable`, add `GetHeap` (or make the
layouts deliberately identical), and assert the returned vtable/type before
using it.

### D8 - generated headers cannot coexist in one translation unit (high)

A strict GCC compile including all three public headers fails with redefinition
of literal `struct LIST_ELEMENT`/`LIST_ELEMENT`. The element tag/typedef is not
type-generated. The headers also leak generator macros because they undefine
the misspelled `ITERFACE_NAME` rather than `INTERFACE_NAME` and leave other
helper macros behind. Individual `intlist.c`, `doublelist.c`, and
`longlonglist.c` do compile cleanly with current GCC and Clang. Generate unique
element names and clean all template macros; add a compile-only consumer that
includes all three headers in both orders.

### D9 - `EraseRange` is a permanent NULL public method (medium)

The typed interface declares and initializes it, but `SetVTable` never assigns
the corresponding `iList.EraseRange`. Any consumer call is a NULL function
call. Either populate and test it or remove it in a deliberate API/ABI change.

### D10 - typed Load does not validate its serialized element width (high)

Generic Load trusts the saved `ElementSize`; the typed wrapper merely changes
the vtable. A file written for another element width can therefore become an
`intList` whose nodes are not int-sized. `GetElementSize` still reports
`sizeof(int)` unconditionally, and a later scalar adapter can make generic code
copy beyond the scalar temporary. Reject a loaded width unequal to
`sizeof(DATA_TYPE)` and clean up using the still-generic vtable. Persistence
format hardening itself belongs with `list.c`, but the typed boundary check is
owned here.

### D11 - inherited allocator transfer hazards require boundary tests (high)

Generic `InsertIn` checks element width but not allocator equality, copies the
source with its allocator, and splices those nodes into the destination, which
later frees them through the destination allocator. `SelectCopy` and
`GetRange` also allocate from `CurrentAllocator`, not necessarily the source
allocator. These are generic defects exposed by the typed interface; use two
tagged allocators to prevent silent cross-free and settle whether derived
copies preserve the source allocator.

## Required unit-test matrix

Use a primary `listgen_test.c` for int behavior after fixing the public-header
collision, with explicit double and long-long boundary cases in the same suite.
If header repair is staged, put each type in a separate test translation unit
and link them into the one `test_listgen` executable; do not accept this as a
replacement for the compile-compatibility regression.

1. **Construction/lifetime and representation:** Create, allocator create,
   placement Init, InitializeWith empty/one/many, `GetElementSize`, both Sizeof
   forms, allocator identity, Clear/reuse, and heap-header Finalize. Assert the
   link/count/Last invariant after every mutation. Use Clear only for placement
   headers.
2. **Scalar sequence surface:** Add, AddRange, PushFront, fixed PopFront with
   and without output, InsertAt at 0/middle/count, ReplaceAt, CopyElement,
   GetElement/Front/Back, EraseAt, Erase/EraseAll with duplicates, RemoveRange
   and EraseRange, rotations, Reverse, Apply, Contains/IndexOf found at each
   position and absent, plus all index/read-only/NULL branches reachable
   through the typed signatures.
3. **Copying and ownership:** independent Copy and GetRange, Equal before/after
   replacement, Append into empty/nonempty (verify second header is consumed),
   InsertIn while source remains intact, Select and SelectCopy for all/none/
   alternating masks and bad mask length, and SplitAfter at middle/last plus
   typed-vtable use of the result. Counting/tagged allocators must prove each
   allocation is freed exactly once by its owner.
4. **Element-link helpers:** First/Last/Next/Skip, ElementData, SetElementData,
   Advance through end, and each documented NULL path. Verify replacements
   invalidate iterators and do not break links.
5. **Iterator matrix:** allocated and placement construction; empty/one/many;
   first/next/current/previous/seek/boundaries/position; Replace with a value,
   remove via NULL, both direction modes; DeleteIterator only for allocated
   iterators; mutation invalidation must return OBJECT_CHANGED without memory
   access errors. Include allocator failure in NewIterator.
6. **Sort branches:** NULL, empty, singleton, read-only, temporary-array
   allocation failure, sizes 2/8/9/large to execute `shortsort` and both
   quicksort partition/stack choices, sorted/reverse/random/equal-heavy inputs,
   node endpoint checks, custom descending comparator, and iterator
   invalidation. Assert exact order, not only multiset preservation.
7. **Type boundaries:** int `{INT_MIN,-1,0,1,INT_MAX}` and long long
   `{LLONG_MIN,-1,0,1,LLONG_MAX}` through InitializeWith, search, replace,
   copy, and sort. For double use finite `{-DBL_MAX,-1.0,-0.0,+0.0,1.0,
   DBL_MAX}`, infinities if supported, and separately characterize bytewise
   `+0/-0` and same/different NaN representations; do not demand a NaN sort
   order until a total-order contract exists.
8. **Persistence/type boundary:** empty and populated raw/callback round trips,
   callback failure, wrong GUID, short header/element, and a valid list file
   with the wrong `ElementSize` rejected by each typed loader. Every failed
   load returns NULL without leaks or adapter dereference.
9. **Failure/flags/callbacks:** switchable failing allocator at header, node,
   sort table, iterator, Copy, SelectCopy, and range construction; read-only
   rejection; custom error capture; destructor count on replace/erase/select/
   clear; observer events only where the generic contract promises them.
10. **Consumer compatibility:** compile a tiny C11 and C++-guarded-as-supported
    consumer with all typed headers together, no leaked generator macros, and
    static assertions for generic/typed header, node-data offset, iterator, and
    common vtable member offsets. Run both GCC and Clang syntax checks.

For the 80% line/70% branch gate on `src/listgen.c`, ensure all three
instantiations are linked and invoked so gcov merges their counters. The sort
sizes above are necessary for lines in both sorting algorithms; failure
allocators and NULL/read-only cases cover `SetVTable`, iterator setup, Sort,
allocator, and helper branches. Run the complete suite under ASan/UBSan with
leak detection. Keep crash reproducers out of the permanent passing suite;
turn D1-D10 into positive regressions after the corresponding repairs.

## Coding handoff

Repair in dependency order: align and uniquely generate public ABI types
(D3/D7/D8), make vtable restoration NULL-safe and finalization ownership-safe
(D2/D6), then fix PopFront, EraseRange, typed Load, and sorting semantics
(D1/D4/D5/D9/D10). Add invariant/counting-allocator helpers to the suite before
testing splice/copy operations, since otherwise D11 can appear to pass with the
system allocator. The minimum sanitizer milestone is create/add/iterate/sort/
pop/finalize for every scalar type with no diagnostics; only then broaden to
persistence and allocator-failure coverage.

## Implementation status (2026-08-08)

The typed generator family now restores scalar vtables only for successful
results, rejects serialized element widths that do not match the scalar type,
and finalizes static typed-interface objects through the generic vtable without
freeing the interface global. PopFront delegates to the generic remover;
iterators use the current generic layout and `void *` replacement ABI; all
three scalar sorts use the installed compare callback, update the list
timestamp, and sort through the same pointer-table algorithm. EraseRange,
SplitAfter, GetRange, and InsertIn have typed boundary wrappers (including
allocator-safe range construction); the latter rejects cross-allocator
splices. The typed interface now includes GetHeap and
generated element tags are unique across the three public headers. The scalar
Erase/EraseAll adapters also use a safe unlink/free loop so repeated matches do
not walk a freed node.

`unittests/list_family_test.c` covers construction, scalar boundaries,
PopFront, sorting (including descending comparison), allocated and placement
iterators, iterator invalidation/replacement, range/split operations, masks,
persistence width rejection, heap ownership, and deterministic allocator
failures. The family suite passes under ASan/UBSan with leak detection
disabled. The environment's LSan run remains unavailable because ptrace is
restricted. GCC coverage for the merged three instantiations is 87.80% line
and 74.27% branch in the local run, above the 80%/70% gate.

## Final integration verification

LeakSanitizer cannot initialize in the current local workspace because of its
ptrace restriction, so a current local ASan/UBSan+LSan integration pass cannot
be claimed. The earlier untraced pass is historical campaign evidence recorded
on 2026-08-08 and does not supersede the current focused-run limitation.
