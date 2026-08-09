# Typed vector family (`vectorgen.c` / `vectorsize_t.c`) audit

## Scope and generated surface

- Generator body: `src/vectorgen.c`; public template declaration:
  `include/vectorgen.h`; sole compiled wrapper: `src/vectorsize_t.c`, which
  defines `DATA_TYPE size_t` and exports `isize_tVector`.
- The generated interface advertises nearly the entire generic vector surface
  with scalar-by-value adapters: metadata, lifecycle, callbacks, element and
  range mutation, queries, sorting, selection, rotations, iterators, and
  persistence.
- Most behavior delegates to `iVector`; local wrappers adapt scalar values for
  Add/Contains/Erase/Push/Insert/Replace/IndexOf/CompareEqualScalar, restore a
  typed vtable on returned objects, adapt iterator replacement, and provide
  typed Create/Init/InitializeWith/Sort/Sizeof helpers.
- No concrete installed public header declares `size_tVector` and
  `isize_tVector`. A consumer must know to define `DATA_TYPE` as `size_t` and
  include `vectorgen.h` manually. The template leaves macros behind, including
  `INTERFACE_NAME` because the cleanup misspells `ITERFACE_NAME`.

The ownership unit is the generator plus its one wrapper. The generic defects
in `vector.md` still affect delegated calls, but the findings below are
specific to the generated ABI and adapters.

## Required ABI and ownership invariants

1. Any object cast between `Vector` and `size_tVector` must have identical
   `sizeof`, alignment, and field offsets for every field used by either side.
2. The same is required for `VectorIterator` and `size_tVectorIterator`.
3. Every returned typed object must carry `&isize_tVector`, while the generic
   finalizer must not free that static global interface.
4. Scalar adapters must take values by value at the typed surface and pass
   their addresses only across the generic `void *` boundary. Output adapters
   need writable caller pointers; function-pointer casts cannot substitute for
   ABI-compatible wrappers.
5. Header, contents, iterator, and derived-result allocations retain their
   creating allocator. Placement headers/iterators remain caller-owned.
6. Typed persistence must reject a generic vector record whose serialized
   element size/type is not `size_t` before exposing typed scalar operations.

## API and helper inventory

| Area | Generated behavior |
| --- | --- |
| Scalar adapters | `Add`, `Contains`, `Erase`, `EraseAll`, `PushBack`, `PopBack`, `Insert`, `InsertAt`, `ReplaceAt`, `IndexOf`, `CompareEqualScalar`. |
| Typed construction/results | `Create`, `CreateWithAllocator`, `Init`, `InitializeWith`, `Copy`, `GetRange`, `Load`; `SetVTable` restores the typed interface. |
| Typed metadata/algorithm | `Sizeof`, constant `GetElementSize`, `SizeofIterator`, and a local `Sort`. |
| Iterator adaptation | `NewIterator`, `InitIterator`, `SetupIteratorVTable`, and scalar `ReplaceWithIterator`; the generic Replace function is saved as `VectorReplace`. |
| Cast-delegated methods | Size/flags/capacity, Clear/Finalize/Apply/Save, accessors, range mutation, comparison setup, allocator/destructor access, selection, resize, rotations, CompareEqual, and related vector operations are lazily copied from `iVector`. |

## Allocator, iterator, and persistence behavior

Typed `CreateWithAllocator` passes the requested allocator to generic Vector;
typed `Create`, `Init`, `InitializeWith`, and generic Load-derived paths use
`CurrentAllocator`. Once safely constructed, generic growth, copy, iterator,
and finalization paths are intended to use the allocator stored in the generic
object. The generated layout currently exposes the wrong allocator offset, and
generic Finalize confuses the static typed vtable with an owned allocation.
Placement Init has the same unresolved header-ownership problem as the generic
unit.

`NewIterator` obtains a generic heap iterator, then the generator saves its
pointer-based Replace callback and installs a scalar adapter. `InitIterator`
does the same in caller storage. Traversal otherwise remains generic, including
timestamp/read-only behavior; this design is valid only if iterator layouts
and the base Replace ABI agree, which they currently do not.

Typed Save delegates directly to the generic raw-header format. Typed Load
loads a generic Vector with `CurrentAllocator` and then overwrites its vtable
with the typed interface. No size_t type marker or width check is performed,
so persistence is only safe after validating the generic record before type
restoration; custom callbacks do not repair the header/type ambiguity.

## Confirmed generator-specific defects

### VF1 - typed and generic vector layouts are incompatible (critical,
ASan/UBSan)

Clang record layouts confirm:

| Field | generic `Vector` offset | generated `size_tVector` offset |
| --- | ---: | ---: |
| VTable/count/Flags | 0/8/16 | 0/8/16 |
| ElementSize/contents | 24/32 | 24/32 |
| capacity | 40 | absent |
| timestamp | 48 | 20 |
| CompareFn | 56 | 40 |
| RaiseError | 64 | 48 |
| Allocator | 72 | 64 (`Heap` is at 56) |
| DestructorFn | 80 | 72 |
| total size | 88 | 80 |

Generic Create allocates a generic 88-byte object and adapters cast it to the
80-byte generated declaration. Direct typed field reads after `contents` see
unrelated generic fields. In particular local typed `Sort` treats generic
`capacity` as a comparator function pointer (for a normal capacity of 20,
effectively address `0x14`) and calls it through `qsortEx`. Public users who
inspect the declared typed fields see wrong timestamps/callbacks/allocator.
Make the layouts exactly identical with compile-time `sizeof`/`offsetof`
assertions, or use an opaque typed handle and wrappers without structure
overlay.

### VF2 - finalizing any typed vector frees the static global vtable (critical,
ASan)

`SetVTable` stores `&isize_tVector`, but generic `Finalize` frees every vtable
that is not exactly `&iVector` (vector.c lines 821-824). The first ordinary
`isize_tVector.Finalize` therefore passes static storage to the vector
allocator, producing an invalid free and corrupting the shared interface.
Vtable ownership must never be inferred from pointer inequality; generated
global vtables are static and non-owning.

### VF3 - generated `PopBack` pushes instead, and cannot return an output value
(critical correctness)

The public slot is declared `int (*PopBack)(VECTOR_TYPE *, DATA_TYPE result)`.
Its wrapper calls `iVector.PushBack((Vector *)AL, &result)` rather than PopBack.
Thus a typed pop appends the supplied scalar and grows the vector. Even with
the delegate corrected, pass-by-value cannot copy the removed element back to
the caller. Change the typed API to `DATA_TYPE *result` (allowing NULL if the
generic contract permits it) and delegate to generic PopBack.

### VF4 - typed iterator layout and Replace calling convention are incompatible
(critical, ASan/UBSan)

Generic `VectorIterator` is 120 bytes before its overallocated element buffer
and places `Magic`, vector, index, timestamp, flags, and current at offsets
64,72,80,88,96,104. The generated iterator is also 120 bytes only by accident:
it omits Magic, shifts vector/index/timestamp/flags/current eight bytes earlier,
stores a full `size_t ElementBuffer` at 104, and puts `VectorReplace` at 112.
`SetupIteratorVTable` writes the saved function into offset 112, which is the
generic iterator's element buffer, not a function field.

The installed replacement thunk accepts `DATA_TYPE` by value but is cast to
the base `Iterator` signature accepting `void *`. GCC diagnoses this with
`-Wcast-function-type`. A normal `it->Replace(it,&value,direction)` therefore
interprets the pointer bits as the replacement scalar, then passes the address
of those bits to generic replacement. Use the exact generic layout and retain
the base `void *` ABI; validate/dereference inside a compatible wrapper.

### VF5 - `SetVTable(NULL)` turns normal failures into NULL dereferences
(critical)

`SetVTable` writes `result->VTable` before checking `result`. Empty/truncated
Load, allocation-failed Copy, invalid/empty GetRange, failed Init, and other
derived-result failures therefore crash instead of returning NULL. `Load`,
`Copy`, `GetRange`, and `Init` all call it unconditionally. Guard NULL before
any member access and only restore type after a successful generic operation.

### VF6 - the exported interface is incomplete until first construction and
still incomplete afterward (high)

`isize_tVector` is initialized with many NULL slots. The first successful
`SetVTable` mutates this shared global lazily and unsafely; calling Size, Clear,
GetElement, or other delegated methods before construction is a NULL function
call, and concurrent first construction is a data race. Even after population,
`Equal`, `deleteIterator`, and `Reserve` remain NULL because SetVTable never
assigns them. Publish a complete statically initialized interface, using real
wrappers wherever signatures differ.

### VF7 - `GetRange` dispatches typed `Create` through the generic function ABI
(critical, ASan for longer ranges)

Once a vector carries the typed vtable, generic `GetRange` calls
`AL->VTable->Create(AL->ElementSize, top)` through `VectorInterface`, but that
slot is actually the generated one-argument `Create(startsize)`. On common C
ABIs the typed function consumes the first argument, so a size_t vector gets
capacity `sizeof(size_t)` rather than the requested range length. The generic
copy can then overflow that result for a longer range. A generic algorithm
must not invoke a signature-incompatible derived vtable; provide a typed range
wrapper built on a safe generic constructor/result path.

### VF8 - `SearchWithKey` passes a scalar as a pointer (critical, ASan/UBSan)

The generic function expects its fifth argument as `void *item`. SetVTable
casts it to a generated function taking `DATA_TYPE item` by value. GCC reports
the incompatible function-type cast. A typed call with scalar `42` makes
generic `memcmp` read address 42. Add a real typed wrapper that passes
`&item`; do not cast between these signatures.

### VF9 - persistence permits generic vectors of incompatible element width or
type (critical, ASan)

All vectors use the same GUID and raw generic header. Typed `Load` delegates to
generic Load and installs `isize_tVector` without checking that serialized
`ElementSize == sizeof(size_t)` or that the payload is a size_t vector. A file
saved from a wider generic element type then makes typed Add/Replace pass a
`sizeof(size_t)` stack temporary to a generic object that copies the serialized
wider width, causing a stack over-read. Equal-width but different types are
silently reinterpreted. A versioned typed identity and exact width validation
are required before vtable restoration.

### VF10 - direct-return operations can lose the typed vtable (high)

`IndexIn` is installed as the generic function without a result wrapper.
Generic `IndexIn` constructs its output with `iVector.Create`, so the returned
object carries `&iVector` while its static type is `size_tVector *`. Invoking
its vtable through generated scalar signatures then passes values where generic
functions expect pointers. Every operation returning a new vector must have a
typed wrapper that validates and restores the typed interface.

### VF11 - wrapper status handling hides failures (high)

`Contains` maps every negative IndexOf status, including BADARG and allocator/
callback failures, to false. `InitIterator` ignores generic InitIterator's
return, always runs setup on caller storage, and always returns 1 even for NULL
vector/buffer failures. `GetElementSize` ignores its object and returns a
plausible width for NULL or an incompatible loaded object. Preserve negative
statuses and only adapt state after delegated success.

### VF12 - strict warning builds currently fail (medium/build gate)

`cc -std=c11 -Wall -Wextra -Wpedantic` reports the incompatible iterator
Replace cast, incompatible SearchWithKey cast, and unused parameters in
`GetElementSize` and `SizeofIterator`. `-Werror` therefore prevents the family
from passing a strict GCC gate. These warnings identify real ABI defects, not
cosmetic cast noise.

### VF13 - placement and macro/public-header contracts are incomplete (medium)

Typed Init inherits generic placement ownership ambiguity: typed Finalize
always delegates to a finalizer that frees the caller-supplied header. The
template header also exposes concrete structure internals despite their ABI
mismatch, has no concrete size_t wrapper header, and leaks generator macros.
Provide a normal installed declaration for the built specialization, keep
implementation layout opaque or exact, fully clean template macros, and define
a placement destruction API that frees only owned contents.

## Existing test status

No current test references `isize_tVector` or includes `vectorgen.h`. The
coverage manifest declares the generator ownership unit and CMake exposes a
`coverage-vectorgen` target, but there is no suite capable of producing its
coverage. Generic vector use in legacy/collection tests does not execute most
generator adapters and cannot validate generated ABI behavior.

## Required ASan/UBSan and 80%/70% test matrix

1. Add a public compile/use test for the concrete size_t declaration. Assert
   `sizeof` and every shared `offsetof` against generic Vector and iterator
   layouts at compile time; require strict GCC `-Wall -Wextra -Wpedantic
   -Werror` cleanliness.
2. Before constructing an object, verify every supported exported interface
   slot is callable/non-NULL. Exercise Create/CreateWithAllocator/Init/
   InitializeWith, metadata, flags, allocator, clear/reuse, heap and placement
   destruction, and balanced custom allocation. Finalize under ASan directly
   regresses VF2.
3. Cover every scalar adapter with zero, `SIZE_MAX`, duplicates, front/middle/
   back, missing values, bad arguments, read-only vectors, and custom
   comparator/error callbacks. PopBack must decrease size and copy through an
   output pointer.
4. Cover all delegated range/capacity/access/algorithm slots and ensure every
   returned object (Copy, GetRange, IndexIn, SelectCopy, Load) carries exactly
   `&isize_tVector`. Use a range longer than `sizeof(size_t)` under ASan for
   VF7 and nonzero-key SearchWithKey for VF8.
5. Heap and placement iterator traversal, seek/position/current, scalar
   replace/remove in both directions, read-only element buffers, stale-object
   errors, invalid iterators, setup failure, and correct ownership-aware
   deletion. Inspect stored/replaced values to catch the pointer-bits ABI bug.
6. Typed Sort ascending/custom-comparator/read-only/stale-iterator cases, plus
   reverse, rotations, selection, comparisons, masks, append/insert/range/
   resize behavior inherited from the repaired generic unit.
7. Persistence round trips for empty and populated size_t vectors, custom
   callbacks, wrong GUID/truncation/read failure, generic files with smaller,
   larger, and equal-width non-size_t elements, and allocation failure. No
   incompatible file may acquire the typed vtable.
8. Failing/counting allocator across header/contents/growth/copy/range/
   iterator/load operations. Assert failure statuses are preserved, no NULL
   enters SetVTable, all partial allocations are cleaned, and no static or
   placement address is freed.
9. Run the dedicated generator suite with ASan+UBSan, and aggregate wrapper
   execution against canonical `src/vectorgen.c` until it independently
   reaches at least 80% line and 70% branch coverage. Keep generic `vector.c`
   coverage charged to its separate ownership unit.

## Implementation status (2026-08-08)

The generated `size_t` specialization now uses the exact generic `Vector` and
`VectorIterator` layouts, with compile-time offset/size assertions in the
implementation.  Its vtable is completely initialized at static definition
time; scalar adapters use the generic pointer ABI internally, `PopBack` takes
an output pointer, iterator replacement keeps the base ABI, and all derived
results restore `&isize_tVector` only after successful construction.  Typed
finalization avoids freeing the static interface and tracks placement headers
so only their contents are released.  Typed `Load` rejects records whose
serialized element width is not `sizeof(size_t)` before installing the typed
vtable.  `include/size_tvector.h` provides the concrete public declaration and
cleans up generator macros after inclusion.

`unittests/vector_family_test.c` covers the complete generated interface,
layout contracts, scalar operations, derived vectors, masks, iterators,
persistence, placement, read-only/error paths, and destructor behavior.  The
focused run passes under ASan/UBSan (with leak detection disabled only for the
ptrace-constrained runner); the generator translation unit reaches 95.09% line
and 87.10% taken-branch coverage in the local gcov run.
## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
