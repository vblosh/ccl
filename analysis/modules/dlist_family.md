# Typed doubly linked-list family audit

## Scope and ownership

- Generator implementation: `src/dlistgen.c`, instantiated by
  `src/intdlist.c`, `src/doubledlist.c`, and `src/longlongdlist.c` as
  `iintDlist`, `idoubleDlist`, and `ilonglongDlist`.
- Public generated types/interfaces come from `include/dlistgen.h` through
  `intdlist.h`, `doubledlist.h`, and `longlongdlist.h`. Most operations adapt
  or cast-forward to generic `iDlist` in `src/dlist.c`.
- A node owns an inline copy of one scalar. Element pointers returned by
  GetElement/Front/Back/link helpers/iterators are borrowed and become stale
  after removal, clear, or finalization. Heap-created list headers and
  iterators are caller-owned; placement Init/InitIterator storage is borrowed
  and must not be freed as a heap object.
- Append consumes the second header and transfers its nodes; InsertIn and
  Splice have distinct generic transfer semantics that must not create shared
  ownership. SplitAfter transfers suffix nodes to a new header. Copy,
  GetRange, SelectCopy, Load, and InitializeWith allocate independent headers
  and nodes.

## Dependencies and representation invariants

- Dependencies: `iDlist`, `iHeap`, `iError`, `iObserver`, `CurrentAllocator`,
  `ContainerAllocator`, masks, comparison/destructor/save/read callbacks,
  stdio, and the private generic layouts in `ccl_internal.h`.
- Header invariant: `ElementSize == sizeof(DATA_TYPE)`; count zero iff First
  and Last are NULL; otherwise `First->Previous == NULL`, `Last->Next == NULL`,
  following Next and Previous visits the same `count` nodes in opposite order,
  and every adjacent pair has reciprocal links.
- ABI invariant: typed header, node, iterator, and interface layouts must match
  every generic object that the generator casts to them. Every object returned
  as a typed list must carry the matching typed vtable.
- Mutation invariant: every structural change and in-place value replacement
  increments timestamp exactly once and old iterators report OBJECT_CHANGED
  before touching links. Read-only rejection leaves all state unchanged.
- Allocator invariant: the allocator/heap that creates a header or node must
  release it. Any node-transfer operation must reject incompatible allocators
  or preserve allocation provenance.
- Default equality/search is bytewise through generic Dlist. For double this
  distinguishes `+0.0` from `-0.0` and treats NaNs by representation; tests
  should characterize that compatibility behavior unless a numeric equality
  contract is deliberately introduced.

## API, wrapper, and helper inventory

| Area | Generated behavior |
| --- | --- |
| Scalar adapters | Contains, Add, CopyElement, ReplaceAt, PushFront/Back, PopFront/Back, InsertAt, Erase/EraseAll, IndexOf, and SetElementData convert scalar arguments to addresses and call generic iDlist. |
| Construction/type restoration | Create, CreateWithAllocator, Init, InitWithAllocator, InitializeWith, Copy, SelectCopy, and Load delegate then call `SetVTable`; Finalize delegates but discards its status. |
| Metadata | typed Sizeof/SizeofIterator/GetElementSize/GetAllocator plus generic Size/flags/error/destructor/comparator setters populated lazily. |
| Iteration | NewIterator and InitIterator create a generic iterator, then `SetupIteratorVTable` saves generic Replace and installs a scalar replacement thunk. |
| Typed sorting | local pointer-array quicksort and shortsort, with one `COMPARE_EXPRESSION` supplied by each instantiation. |
| Direct link helpers | typed NextElement and SetElementData; generic First/Last/GetElementData/Advance/Skip/MoveBack are installed by cast. PreviousElement remains unpopulated. |
| Generic delegates | Clear, Apply, Equal, Save, GetElement, EraseAt, Splice, Reverse, GetRange, Append, UseHeap, AddRange, InsertIn, RemoveRange, rotations, Select, SplitAfter, and related metadata operations. |

Normal scalar success is 1, empty pop is 0, and generic failures are negative
container error codes. The current Contains, Finalize, and InitIterator wrappers
do not preserve those error conventions, as detailed below.

## Confirmed compatibility and correctness defects

### DD1 - typed list headers are not layout-compatible with generic Dlist
(critical, ASan/UBSan)

Generic `struct Dlist` contains `FreeList` between First and Compare; generated
`intDlist`/`doubleDlist`/`longlongDlist` omit it. Every field from Compare
through DestructorFn is therefore read eight bytes early on a 64-bit build.
Generic Create allocates and initializes a 96-byte Dlist, then the generator
casts it to an 88-byte typed header. Consequences confirmed by a public probe:

- typed GetAllocator reads generic Heap and returns NULL instead of
  CurrentAllocator;
- typed Sizeof reports an 88-byte empty object, omitting the actual header
  tail;
- typed Sort reads generic Heap as `l->Allocator`; a normal three-int Sort
  reports null `ContainerAllocator` access at line 278 and segfaults under
  ASan/UBSan.

If UseHeap is enabled, Sort instead treats a ContainerHeap as a
ContainerAllocator and dispatches through unrelated function pointers. Add
`FreeList` in the exact generic position or stop overlaying distinct public
structs; enforce every `sizeof`/`offsetof` relation with compile-time asserts.

### DD2 - typed iterator layout and Replace ABI are incompatible (critical,
ASan)

Generic `DListIterator` places a `long long Magic` immediately after the base
Iterator. The generated iterator omits Magic, adds a Previous field, and puts
`DlistReplace` beyond the generic object. Generic NewIterator allocates 104
bytes; `SetupIteratorVTable` writes its saved function pointer at typed offset
104. ASan reports an eight-byte heap-buffer-overflow at `dlistgen.c:127`.

Independently, public NewIterator returns `Iterator *`, whose Replace signature
is `(Iterator *, void *, int)`, but the installed thunk actually accepts a
scalar DATA_TYPE and is forced through an incompatible cast at line 128. GCC's
`-Wcast-function-type` diagnoses this for int, double, and long long. A normal
`it->Replace(it,&value,direction)` therefore passes pointer ABI where the thunk
expects integer or floating-point ABI. Use the exact generic iterator layout
and keep Replace's `void *` signature; the thunk can validate and dereference
the typed value internally.

### DD3 - the three public headers cannot coexist (high, compile compatibility)

`dlistgen.h` has one global `__dlistgen_h__` guard. After including
`intdlist.h`, later `doubledlist.h` and `longlongdlist.h` silently skip their
generated declarations. A strict C11 consumer including all three fails with
unknown `doubleDlist`/`idoubleDlist` at first use. Template macros also leak:
the cleanup undefines misspelled `ITERFACE_NAME` rather than `INTERFACE_NAME`
and leaves CONCAT/EVAL and other generator definitions visible. Make the
template safely re-entrant with type-specific guards/names and complete macro
cleanup; test all header orders in C and supported C++ inclusion mode.

### DD4 - `SetVTable(NULL)` converts recoverable failures into crashes
(critical, ASan/UBSan)

`SetVTable` writes `result->VTable` before checking anything (line 304).
Create/CreateWithAllocator allocation failure, Copy/SelectCopy failure, and
invalid/truncated Load therefore dereference NULL rather than returning NULL.
An empty-file call to `iintDlist.Load` reports the generic read error, then
UBSan reports null member access and ASan reports a SEGV in SetVTable. Guard
NULL before restoration and preserve the original error result on every path.

### DD5 - typed persistence accepts incompatible scalar types and widths
(critical, ASan)

Generic Dlist persistence uses one GUID and a raw Dlist header for every typed
family member. Typed Load does not validate saved ElementSize or scalar type;
it merely installs its vtable, while typed GetElementSize always reports
`sizeof(DATA_TYPE)`. Saving a double list, loading it through iintDlist, and
then adding an int makes generic new_dlink copy the saved eight-byte element
width from the wrapper's four-byte scalar temporary. ASan reports a
stack-buffer-overflow at `dlist.c:46`. Equal-width double/long-long files avoid
the overflow but silently reinterpret values. At minimum reject width mismatch
before changing the generic vtable; a portable format needs a version and
scalar type identifier rather than raw pointers/function fields.

### DD6 - typed Sort has three additional independent correctness failures
(critical after DD1)

Even with header layout repaired:

- double and long-long instantiations define `COMPARE_EXPRESSION(A,B)` as
  `B > A ? -1 : B != A`, comparing temporary pointer-array positions instead
  of `(*A)->Data` and `(*B)->Data`; the shortsort path therefore leaves values
  unsorted;
- all three sorts ignore the comparator installed by SetCompareFunction;
- relinking writes only Next, never repairs any Previous pointer or
  `First->Previous`, and does not increment timestamp (lines 288-296).

Backward traversal, PopBack, reverse, and extant iterators can consequently
follow stale topology after sort. Prefer a typed wrapper over the already
bidirectional generic Sort, or repair one shared algorithm to honor Compare,
rebuild both links/endpoints, and invalidate iterators.

### DD7 - lazy global-vtable population exposes NULL public methods (high)

Each exported typed interface is statically initialized with many NULL slots.
The first successful SetVTable mutates the global interface in place. Before
that event, calls such as Size/Clear/GetElement are NULL function calls; the
initialization is also an unsynchronized data race. PreviousElement remains
NULL even afterward although generic iDlist implements it. Publish a complete
const/static interface at program initialization (using real wrappers where
types differ) and populate every advertised supported slot.

### DD8 - wrapper status handling is incompatible with generic Dlist (high)

Contains maps every negative IndexOf result, including BADARG, to false.
Finalize ignores generic Finalize and always returns 1; the confirmed
`iintDlist.Finalize(NULL)` result is success instead of
`CONTAINER_ERROR_BADARG`. InitIterator likewise ignores the generic result,
continues setup, and returns 1. This hides NULL/read-only/allocation errors and
can initialize caller storage after a failed call. Return delegated status
unchanged and only perform adapter work after success.

### DD9 - SplitAfter returns a generic-vtable object as typed (critical)

SetVTable installs generic SplitAfter directly. Generic SplitAfter creates its
result with `&iDlist`; no typed wrapper restores the derived vtable. Calling
the returned object's `VTable->Add` through the typed declaration then sends a
scalar to generic Add's `const void *` parameter, an immediate ABI-invalid
dispatch. The generic operation also leaves the suffix First->Previous link
pointing into the original list. Wrap every derived-object return, restore the
typed vtable only on success, and verify that the two resulting link chains are
fully detached and independently owned.

### DD10 - inherited generic AddRange never terminates for nonzero input
(critical)

The typed vtable delegates AddRange to `dlist.c:271-300`. Its `while (n > 0)`
advances the data pointer but never decrements n. Any nonempty typed AddRange
continues reading past the input and allocating until a memory/sanitizer
failure. Fix the generic loop and cover n=0/1/many, read-only, NULL, mid-range
allocation failure, timestamp, and observer count through all three typed
interfaces.

### DD11 - destructor and transfer ownership inherited from generic Dlist is
unsafe (high)

Typed Clear/Finalize delegate to generic Clear, which calls DestructorFn with
the private node pointer (`dlist.c:206-207`) rather than node Data; owning
destructors can free/corrupt link fields. EraseAt omits the destructor, while
ReplaceAt and value-based Erase pass data, so callback semantics differ by
removal route. Append/InsertIn/Splice/SplitAfter also need tagged-allocator and
heap tests: transferring nodes between different allocators or from a source
heap to a non-owning destination makes later cleanup free storage through the
wrong mechanism. These fixes live primarily in generic dlist.c but block a
safe typed-family lifecycle.

### DD12 - generator entry signatures retain ignored size parameters (medium)

Create, CreateWithAllocator, Init, InitWithAllocator, and InitializeWith ignore
their element-size arguments and force sizeof(DATA_TYPE), as compiler warnings
confirm. Fixed scalar width is appropriate, but silently accepting zero or a
different width through a generic-shaped signature obscures errors and enables
caller assumptions that disagree with Load/GetElementSize. Either validate the
argument equals sizeof(DATA_TYPE) for compatibility, or make a deliberate
versioned API change that removes it.

## Existing coverage

There is no dedicated typed Dlist test source. The coverage manifest names the
three instantiations as one `dlistgen` unit, but the current unit-test CMake
entry is only a placeholder exclusion. Legacy tests do not exercise the typed
interfaces. No active checks cover header coexistence, layout offsets,
iterators, sort, scalar persistence boundaries, NULL restoration, custom
allocators, or the nonterminating AddRange delegate.

## Required ASan/UBSan and coverage matrix

Until DD3 is repaired, use one test translation unit per scalar type and link
them into a single executable; also keep a compile-failure regression proving
all public headers must eventually coexist. After repair, include all three in
the primary consumer test.

1. Construction and representation: Create/CreateWithAllocator, placement
   Init, InitializeWith empty/one/many, exact element size, typed/generic
   allocator identity, Sizeof, Clear/reuse, and correct heap-versus-placement
   cleanup. Compile-time assert all generic/typed header, node-data, iterator,
   and interface offsets/sizes (DD1/DD12).
2. Sequence surface and link oracle: Add, fixed AddRange, PushFront/Back,
   PopFront/Back with and without output, InsertAt at 0/middle/count,
   ReplaceAt, CopyElement, GetElement/Front/Back, EraseAt, Erase/EraseAll,
   Reverse, RemoveRange, rotations, Contains/IndexOf, and SetElementData.
   After every mutation traverse both directions and assert reciprocal links,
   endpoints, count, values, and timestamp.
3. Iterator matrix: allocated and placement initialization; empty/one/many;
   first/next/previous/current/seek/boundaries; value replacement and NULL
   removal in both directions; wrong iterator, readonly, and mutation
   invalidation. ASan must regress the 104-byte DD2 overflow; compiler checks
   must reject incompatible Replace casts. Delete only allocated iterators.
4. Sorting: NULL, empty, singleton, read-only, failing temporary allocation,
   and sizes 2,8,9 plus larger sorted/reverse/random/equal-heavy data to cover
   shortsort and both quicksort partition/stack branches. Verify forward and
   backward order, endpoints, custom descending Compare, and old iterator
   invalidation for int/double/long long (DD1/DD6).
5. Scalar boundaries: INT_MIN/MAX and LLONG_MIN/MAX with negatives/zero; for
   double use finite extrema, infinities where supported, `-0.0`/`+0.0`, and
   separately characterized NaN representations across search/equality/copy/
   sort.
6. Copy/selection/ranges: independent Copy, Equal before/after mutation,
   GetRange, all/none/alternating Select/SelectCopy and bad masks, SplitAfter
   middle/last followed by method calls through the returned object's own
   typed vtable. Verify no cross-links between split results (DD4/DD9).
7. Ownership transfers: Append to empty/nonempty, InsertIn, Splice before/after
   endpoints, SplitAfter, and UseHeap combinations with two tagged allocators.
   Assert consumption/source postconditions and that every node/header is
   released exactly once by its creating allocator/heap (DD11).
8. Destructor/error/flags: counting destructor on ReplaceAt, Erase, EraseAt,
   Select, Clear, and Finalize receives the exact scalar address/value once;
   readonly rejection and custom error capture for every mutator; NULL calls
   preserve negative errors through wrappers (DD8/DD11).
9. Persistence: empty/populated default and custom callback round trips,
   callback failure, wrong GUID, short header/data, allocation failure, saved
   width mismatch, and all cross-type int/double/long-long load combinations.
   Every failed load returns NULL without SetVTable access or leaks (DD4/DD5).
10. Failing allocator stages: header/node, iterator, sort table, Copy,
    SelectCopy, ranges, split, and generic heap creation. State and vtable must
    remain usable after every failure.
11. Consumer compatibility: include all three public headers in every order,
    verify no generator macro leakage, call every non-NULL interface member,
    and compile with GCC/Clang C11 plus `-Wcast-function-type`; add supported
    C++ guards if this public C header is expected to compile as C++ (DD2/DD3/
    DD7).

For the 80% line/70% branch gate, link and invoke all three instantiations so
gcov merges their `src/dlistgen.c` counters. Sort sizes 2/8/9 and equal-heavy
inputs cover shortsort/quicksort decisions; NULL/read-only/failing allocators
cover wrapper and setup branches; all Copy/Load/SelectCopy outcomes cover
SetVTable. Run the completed suite under ASan/UBSan and leak checking. Keep
current crash/hang reproducers separate until converted to positive tests by
their fixes.

## Coding handoff

Repair in this dependency order:

1. Make the public generated headers coexist and make typed header/iterator
   layouts exactly match generic storage, backed by static offset assertions
   (DD1-DD3).
2. Keep Iterator.Replace pointer-based, make SetVTable NULL-safe, and publish a
   fully initialized typed interface with PreviousElement and typed wrappers
   for all ABI-different/derived-object methods (DD2/DD4/DD7/DD9).
3. Replace the local scalar sort with a safe wrapper around one shared
   bidirectional comparator-driven sort, and preserve delegated error statuses
   (DD6/DD8).
4. Reject wrong typed persistence widths/types before vtable restoration and
   settle the ignored-size API contract (DD5/DD12).
5. Fix generic AddRange/destructor/link-transfer ownership before enabling the
   full typed lifecycle suite (DD10/DD11).

The minimum sanitizer milestone is create/add/forward+backward traversal/
iterate/sort/erase/clear/finalize for every scalar type with correct allocator
identity and no diagnostics. Then enable persistence, split/transfer, heap,
and deterministic allocation-failure coverage to reach the stated gates.

## Implementation status (2026-08-08)

The typed generator family now overlays the generic `Dlist` and
`DListIterator` layouts exactly, including `FreeList` and iterator `Magic`,
and all three public headers are independently guarded and coexist in any
include order. Iterator `Replace` remains pointer-based and delegated status
codes are preserved. The exported typed interfaces are fully initialized at
translation time; typed wrappers restore vtables for copied, ranged, loaded,
selected, and split results, and validate scalar widths before accepting a
load. Typed sorting delegates to the generic bidirectional comparator-driven
sort with numeric typed defaults and representation-sensitive ties.

The generic dlist fixes required by this family include terminating and
timestamping `AddRange`, correct range observer counts, releasing sort scratch
storage, timestamping rotations, and detaching the suffix back-link in
`SplitAfter`. `unittests/dlist_family_test.c` covers all three scalar
instantiations, ABI/link invariants, iterator replacement, sorting, AddRange,
split, persistence-width rejection, scalar boundaries, placement storage, and
the complete typed interface surface.

The dedicated suite passes under Clang ASan/UBSan. The aggregate GCC gcov
checker result is 214/223 lines (95.96%) and 66/88 branches (75.00%) across
all three instantiations.
LeakSanitizer remains unavailable in this ptrace-restricted environment.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
