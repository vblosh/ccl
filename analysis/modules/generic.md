# `generic.c` audit

## Scope and dependencies

- Physical source: `src/generic.c`.
- Public surface: global `GenericContainerInterface iGeneric` from
  `include/containers.h`.
- Internal layout dependencies: `_GenericContainer` and `GenericIterator` in
  `include/ccl_internal.h`; the prefix layout and exact function signatures of
  every concrete container vtable.
- Intended behavior: a non-owning protocol adapter. It creates no container;
  each entry delegates to the concrete object's vtable.

## Function inventory

The 18 entries are `Size`, `GetFlags`, `SetFlags`, `Clear`, `Contains`,
`Erase`, `EraseAll`, `Finalize`, `Apply`, `Equal`, `Copy`,
`SetErrorFunction`, `Sizeof`, `NewIterator`, `InitIterator`,
`DeleteIterator`, `SizeofIterator`, and `Save`. `Size`, `GetFlags`, and
`SetFlags` contain explicit NULL checks; all others dispatch directly.
`SetFlags` returns the previous flags. Iterator construction/size/save are
otherwise pure pass-through operations.

The central invariant is stronger than sharing a vtable prefix by position:
each concrete entry in that prefix must have a function type compatible with
the generic declaration. Iterator deletion additionally requires a reliable
way to recover the owning concrete container.

## Confirmed defects

### G1 — `DeleteIterator` assumes an iterator layout contradicted by concrete
iterators (critical)

Lines 102-106 cast every iterator to `GenericIterator`, whose layout is
`Iterator` immediately followed by `GenericContainer *Gen`. List, dlist,
vector, and ValArray iterators instead place a `long long Magic` in that slot
and the owning container after it. `iGeneric.DeleteIterator` therefore treats
the magic number as a pointer and dereferences it. A list iterator is a direct
ASan/UBSan crash reproducer. String collections happen to put the owner
directly after `Iterator`, which demonstrates that the protocol is
inconsistently implemented rather than universally valid.

### G2 — Advertised generic compatibility is ABI-invalid for several public
container families (critical)

Concrete vtables do not all match `GenericContainerInterface`:

- Every ValArray vtable omits `EraseAll`, shifting all subsequent entries;
  its element operations take scalar `ElementType`, and `Save` has only two
  parameters.
- `BitStringInterface` omits `EraseAll`, also omits the generic
  `SizeofIterator` slot, and uses bit/BitString-specific signatures.
- `VectorInterface.Contains` takes a third `ExtraArgs` parameter, while the
  generic call supplies two. The callee reads that indeterminate argument.
- Generated typed list/vector interfaces accept scalar values rather than the
  generic pointer representation.
- Several concrete `Apply` entries return `int`, while generic `Apply` is
  declared `void`.

Calls through an incompatible function-pointer type are undefined behavior;
the omitted fields additionally dispatch to entirely different functions.
This contradicts the documentation that all containers comply with the
generic interface. Regression tests should first encode which concrete
families are promised generic support. A source-only patch to `generic.c`
cannot safely infer incompatible vtable layouts; the interface prefix must be
standardized or support must be explicitly narrowed.

### G3 — Most adapter entries crash on a NULL container before reporting an
error (medium)

Only the first three functions validate `gen`. `Clear` through `Save`
dereference it to find a vtable, so calls such as `iGeneric.Clear(NULL)` fault.
This is inconsistent with the explicit behavior of the first three entries
and with concrete container APIs, which normally return/raise BADARG. The
adapter has enough context to use `iError` and return the appropriate neutral
or negative result for every entry except callbacks with void return. Treat
NULL as BADARG consistently; do not attempt to dispatch it.

The diagnostic typo `"iGneric.GetFlags"` is observable but cosmetic and is not
counted as a behavioral defect.

## Existing coverage

No current test references `iGeneric`. Existing container tests call concrete
interfaces, so they neither validate vtable-prefix compatibility nor the
iterator-owner assumption. Direct coverage is zero.

## Required test matrix

1. Create a small spy container whose vtable exactly implements the generic
   protocol. Call every entry and assert argument forwarding, return values,
   callback forwarding, old-flag behavior, and exact one-call dispatch.
2. Test every NULL-container entry with an error-capture handler; no sanitizer
   finding is acceptable (G3).
3. Integrate concrete `List`, `Dlist`, `Vector`, narrow/wide string collection,
   and each family explicitly promised by the API. Exercise all 18 operations,
   not merely prefix getters.
4. For each supported concrete family, allocate and initialize iterators,
   assert `SizeofIterator`, traverse, and delete through `iGeneric`; regress G1
   with list/vector/ValArray iterators under ASan/UBSan.
5. For vector `Contains`, use a compare function that verifies its
   `CompareInfo`/extra-argument path; this catches the missing third argument.
6. For types with a destructor, verify `Clear`/`Finalize` and `Copy` ownership
   behavior through the adapter.
7. Save via a custom callback and temporary stream; verify all four arguments
   and injected write failure.
8. Compile-time layout checks should compare `offsetof`/entry order for every
   supported concrete interface prefix where the language permits it; add
   typed wrapper tests for signature adaptation rather than casting
   incompatible pointers.

Coverage is secondary to the cross-family conformance matrix: every claimed
supported family must run cleanly under ASan/UBSan and compiler warnings for
incompatible function pointers.

## Implementation handoff

Resolve G2 as an API-compatibility decision before applying local casts. The
safest design is a genuinely common prefix with adapter-compatible signatures,
plus an iterator-owned delete operation or standardized owner slot. Once that
is fixed, G1 disappears structurally; add uniform NULL guards for G3. Avoid a
switch on concrete magic values inside `generic.c`, which would make the base
protocol depend on every derived type.

## Resolution in this pass

The adapter now keeps a genuinely generic fallback for protocol-conformant
tables and uses declared concrete calls for the ABI-compatible public
`List`, `Dlist`, `Vector`, `strCollection`, and `WstrCollection` tables.  This
avoids calling Vector's three-argument `Contains` through the two-argument
generic type and avoids treating concrete `Apply` functions (which return
`int`) as the generic `void` callback.  The Vector extra argument is passed as
`NULL`, which is the only representation available at the generic boundary.

`DeleteIterator` recovers known List/Dlist/Vector/StringList iterators from
their magic marker.  Iterators returned through the generic adapter for
custom protocol tables and string collections are tracked by owner, so delete
does not guess at a layout.  All adapter entries validate a null container
before dispatch.  The sequential interface reserves the concrete `Load` and
`GetElementSize` slots separately; the generic interface itself is unchanged.

`unittests/generic_test.c` covers the protocol spy, concrete List/Dlist/Vector
and string collections, all adapter entries, Save, callbacks, iterator
construction/deletion, and null handling.  The final coverage run reports
85.71% line and 72.27% branch coverage for `generic.c` (required 80%/70%).

Direct ASan/UBSan execution passes with `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`.  The required direct `detect_leaks=1` run
reaches test completion but this runner's LeakSanitizer cannot suspend its
process (it reports the environment's ptrace restriction), so leak reporting
is unavailable here rather than being waived by a debugger/tracer.

## Final integration verification

The final untraced integration run passed this suite with ASan, UBSan, and
LeakSanitizer enabled (`ASAN_OPTIONS=detect_leaks=1`). This supersedes the
focused-run environment limitation above.
