# `sequential.c` audit

## Scope and dependencies

- Physical source: `src/sequential.c`.
- Public surface: global `SequentialContainerInterface iSequentialContainer`.
- Dependencies: `iError`; the concrete sequential container object prefix;
  concrete vtable ordering/signatures; concrete iterator layouts.
- Role: generic-prefix dispatch plus eight sequential operations and a
  generic iterator-based `Append` algorithm.

## Function and invariant inventory

The generic portion exposes `Size`, flags, clear/search/erase/finalize/apply,
equality/copy/error-function/size, iterator lifecycle, and save. The sequential
portion exposes `Add`, `GetElement`, `Push`, `Pop`, `InsertAt`, `EraseAt`,
`ReplaceAt`, `IndexOf`, and `Append`.

`Size`, `GetFlags`, and `SetFlags` read the assumed common object prefix
directly; other functions dispatch through the assumed common vtable. A valid
sequential protocol requires identical prefix field offsets, identical vtable
entry order and compatible function types. `Append(dst, src)` intends to copy
each source element through an iterator and `Add`, leaving the source intact.
Any iterator it allocates must be released on all exits.

## Historical pre-fix defects

S1-S7 below describe the pre-fix audit baseline. The resolution and active
adapter suite later in this document are authoritative for current behavior.

### S1 — Every sequential-specific dispatch is shifted into the wrong concrete
vtable entry (critical)

`SequentialContainerInterface` places `Add` immediately after `Save` (header
and lines 185-212). Public concrete interfaces such as List, Dlist, Vector, and
string collection retain `Load` and `GetElementSize` between `Save` and their
sequential portion. Consequently line 126 dispatches `iSequentialContainer.Add`
to the concrete `Load` entry, line 131 dispatches `GetElement` to
`GetElementSize`, and later calls remain shifted. The call signatures are also
incompatible, causing undefined behavior and likely invalid FILE access on the
first Add. This is deterministic with an ordinary `List` and is not repairable
by changing one offset in the algorithm; the public protocol and concrete
prefixes must agree.

ValArray and BitString diverge even earlier by omitting generic entries and by
using scalar element signatures, so they cannot safely use the current adapter
either.

### S2 — `SizeofIterator` treats its container argument as an iterator
(critical)

The public signature takes `const SequentialContainer *`. Lines 107-112 cast
that container to `SequentialIterator`, read a supposed owner pointer from an
offset deep inside the object, then call through it while passing the original
container as though it were the concrete argument. The generic counterpart
correctly calls `gen->vTable->SizeofIterator(gen)`. An ordinary list/vector
call is a direct invalid-pointer/ASan reproducer.

### S3 — `DeleteIterator` relies on a false owner-field offset (critical)

Like G1, lines 100-105 assume the owner immediately follows the base
`Iterator`. List, dlist, vector, and ValArray put a magic number there. Deleting
their iterator through this adapter dereferences the magic value. Iterator
ownership must be standardized or deletion must be an iterator operation.

### S4 — `Append` leaks its iterator on success and every Add failure (high)

Lines 170-181 allocate with `NewIterator` but never call `DeleteIterator`.
LeakSanitizer reports one allocation per append for ordinary heap-backed
iterators. The early return at lines 176-178 leaks as well. Cleanup is required
on all exits after successful iterator construction.

### S5 — `Append` dereferences NULL when iterator allocation fails (high)

Line 172 calls `it1->GetFirst` without checking `NewIterator` at lines 170-171.
A concrete container using a fail-after-N allocator deterministically returns
NULL and produces a sanitizer crash instead of propagating
`CONTAINER_ERROR_NOMEMORY`.

### S6 — `Append` neither validates inputs nor element compatibility (high)

NULL `g2` is dereferenced during `NewIterator`; NULL `g1` is reached during
`Add`. More importantly, appending a source whose element size is smaller than
the destination causes destination `Add` implementations to copy beyond the
source iterator element buffer. `CONTAINER_ERROR_INCOMPATIBLE` exists and
concrete `Vector`/List/Dlist append functions perform element-size checks.
The generic adapter must obtain element sizes through a standardized protocol
before copying. Self-append also invalidates its own source iterator after the
first Add and silently returns success with incomplete results; define and
test either a snapshot behavior or a BADARG rejection.

### S7 — The generic-prefix dispatch inherits the cross-family ABI and NULL
failures from `generic.c` (high)

Before the sequential portion, this file duplicates the same incompatible
vtable-prefix assumptions and leaves most NULL arguments unchecked. Fixes must
share one common protocol implementation to prevent the two adapters drifting
again.

## Current coverage

`unittests/sequential_test.c` is the active adapter suite. It directly calls
`iSequentialContainer` across List, Dlist, and Vector, including generic
operations, iterators, cross-container append, NULL handling, readonly and
incompatible cases. Variable-sized string tables are explicitly rejected by
the fixed-size adapter ABI.

## Required test matrix

1. A protocol-conformant spy object covering every generic and sequential
   dispatch entry, with exact argument and return-value checks.
2. Integration matrix for List, Dlist, Vector, narrow/wide string collections,
   and every other family declared sequential after the compatibility decision.
   Invoke every adapter operation; S1 should fail immediately before the fix.
3. `SizeofIterator(container)` must equal the concrete interface result for
   each supported family (S2).
4. New/init/traverse/delete iterator through the adapter under ASan/UBSan and
   leak detection (S3).
5. Append empty/non-empty combinations and verify source unchanged, destination
   order, return value, observer behavior if supported, and no leak (S4).
6. Deterministic iterator-allocation failure and Add failure after 0, 1, and N
   copied elements; assert error propagation and iterator cleanup (S4/S5).
7. NULL source/destination, incompatible element sizes, read-only destination,
   and self-append (S6).
8. Append across different but compatible concrete types (for example List to
   Vector of equal element size) if this is promised; otherwise assert a clear
   `INCOMPATIBLE` result.
9. Custom compare/IndexOf extra arguments, save callback failure, destructor
   counts, and all NULL generic-prefix calls.

Use counting/failing allocators and run every iterator/append case with leak
detection. Coverage gates should include all cleanup and error branches.

## Historical implementation handoff

The repair first aligned the common interface prefix, including the `Load` and
`GetElementSize` slots, then corrected S2/S3 and added structured cleanup and
validation to `Append`. The current adapter reuses the corrected generic-prefix
machinery instead of maintaining a second copy.

## Resolution in this pass

`SequentialContainerInterface` now reserves `Load` and `GetElementSize`
between `Save` and `Add`, matching the concrete List/Dlist/Vector table
prefix.  Generic operations delegate to `iGeneric`, so the NULL policy and
iterator-owner handling cannot drift between adapters.  `SizeofIterator`
passes the container to the concrete table (rather than interpreting it as an
iterator), and `DeleteIterator` uses the corrected generic lifecycle.

`Append` rejects null/self/incompatible inputs, obtains the iterator only after
validation, checks allocation failure, propagates the first Add failure, and
always deletes the iterator on success and error.  Variable-sized string
collections are explicitly rejected by the fixed-size sequential ABI rather
than being called with the wrong Pop/GetElement signatures.  The test suite
covers List/Dlist/Vector operations, cross-container append, source
preservation, incompatible/read-only/self cases, iterator cleanup, string
rejection, all generic entries, and null handling. `GetElementSize` returns
the fixed element size for supported concrete tables and reports
`CONTAINER_ERROR_INCOMPATIBLE` for variable-sized string tables. Coverage is
96.39% lines and 71.91% branches for `sequential.c` (gcov; required 80%/70%).

Direct ASan/UBSan test runs pass with `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`.  This runner's LeakSanitizer cannot attach to
its process (it reports the environment's ptrace restriction) when
`detect_leaks=1` is enabled; no sanitizer error occurs before that
environment-level failure.

## Final integration verification

The final untraced integration run passed with ASan and UBSan. LeakSanitizer is
unavailable in the current ptrace-restricted environment, so no leak-enabled
pass is claimed here.
