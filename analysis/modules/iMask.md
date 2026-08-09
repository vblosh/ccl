# `iMask.c` audit

## Scope and dependencies

- Physical source: `src/iMask.c`; object layout is private in
  `include/ccl_internal.h`, while `MaskInterface iMask` is public through
  `include/containers.h`.
- Dependencies: process-global `CurrentAllocator`, global `iError`, and the C
  memory functions.
- Representation invariant: one allocation contains `{length, Allocator}` and
  exactly `length` addressable bytes in `data`; `Allocator` must be the allocator
  that owns that allocation. Each byte is treated logically as false when zero
  and true when nonzero.

## Function and contract inventory

| Entry | Contract/invariants |
| --- | --- |
| `CreateFromMask(n,data)`, `Create(n)` | Allocate a fixed-length mask with the current allocator; copy `n` bytes or zero-initialize them. |
| `Copy(src)` | Return an independent same-length/value mask; NULL currently returns NULL. |
| `Size`, `Sizeof` | Return logical length and object-plus-data footprint; NULL reports 0 / base object size. |
| `SetElement`, `GetElement` | Access a byte only when `index < length`; invalid access must report `INDEX` without touching storage. |
| `Clear` | Zero all elements. A mask has no capacity field or append API, so its fixed logical length must remain usable. |
| `And`, `Or` | Destructively combine equal-length masks; reject NULL/incompatible operands. |
| `Not` | Normalize each byte to logical negation (zero becomes 1, nonzero becomes 0). |
| `PopulationCount` | Count nonzero bytes. |
| `Finalize` | Free with the allocator that created the object. |

## Confirmed defects

### M1 — out-of-range `GetElement` reports an error and then reads out of bounds (critical)

Lines 50-53 do not return after raising `CONTAINER_ERROR_INDEX`. An ASan
reproducer using a three-element mask and `GetElement(mask, 100)` reports a
heap-buffer-overflow at line 53. Return a non-success value immediately after
the error (the interface's `int` result makes 0 the least disruptive existing
sentinel).

### M2 — `Copy` assigns the wrong owning allocator (critical)

`CreateFromMask` allocates with the *current* allocator, then lines 24-25
replace the copy's owner with the source's allocator. With source allocator A
and current allocator B, finalizing the copy calls A's `free` for a B-owned
allocation. A counting-allocator reproducer observed `A.free=1, B.free=0` when
only the copy was finalized. Allocate the copy directly with `src->Allocator`
and leave that owner recorded; do not allocate with one allocator and label it
with another.

### M3 — `Clear` destroys the only size/capacity bound (high)

Lines 55-59 zero the data and set `length=0`. There is no separate capacity and
no operation that can restore the length, so every subsequent `SetElement`
fails. This also breaks existing callers: vector and narrow/wide string
collection comparison routines reuse an adequately sized result mask by
calling `iMask.Clear`, write comparison bytes directly, and return a mask whose
public `Size` is now zero. Preserve the fixed length and only zero `data`.

### M4 — several public NULL inputs dereference NULL (medium)

`SetElement`, `Clear`, `Finalize`, and `PopulationCount` dereference their mask
without validation. `Not` does validate but reports the wrong operation name
(`iMask.And`). These are directly reachable public interface entries and are
inconsistent with `And`, `Or`, `GetElement`, and `Size`. Add BADARG handling,
and report `iMask.Not` for `Not`.

### M5 — allocation-size addition is unchecked (medium)

`n + sizeof(Mask)` can wrap for an extreme public `size_t n`, after which the
initialization writes beyond the undersized allocation. Reject
`n > SIZE_MAX - sizeof(Mask)` as an allocation failure/bad size before calling
the allocator.

## Existing coverage

ValArray tests create and finalize masks and test selection, but there is no
dedicated suite for mask logic, errors, allocator ownership, clearing, or
failure branches. The existing tests do not expose M1-M5.

## Required test matrix

1. Create zero/nonzero lengths, with NULL and explicit source data; verify
   values, `Size`, `Sizeof`, and population count.
2. Set/get zero and nonzero values at first/last index; reject index exactly at
   size and far beyond it under ASan (M1).
3. Clear a populated mask, assert every byte is zero, size unchanged, and the
   last element can be set again (M3).
4. Truth-table `And`, `Or`, and `Not`; test normalized negation, self-operation,
   incompatible sizes, and NULL operands.
5. Copy independence plus allocator A/current allocator B ownership and exact
   allocation/free counters (M2).
6. Deterministic allocation failure and checked-size overflow; ensure the
   proper error is raised and no partial allocation leaks.
7. Exercise all documented NULL paths, including finalization and population
   count, and assert operation names/codes (M4).
8. Integration regression: reuse one mask across two vector/string comparison
   calls and assert its public size and values remain correct.

This matrix covers every line and all material branches and should exceed 80%
line/70% branch coverage. Run with ASan/UBSan and leak detection, restoring the
global allocator/error handler in fixture cleanup.

## Luna handoff

Priority order: M1 and M2 first (memory safety/allocator correctness), then M3
(cross-module functional break), then M4/M5. Preserve the fixed-length byte-mask
representation and public signatures.

## Implementation and validation

The confirmed defects are fixed in `src/iMask.c`: out-of-range reads now return
after reporting `INDEX`, copies allocate and free through the source allocator,
`Clear` preserves the fixed length, public NULL paths report `BADARG`, `Not`
reports its own operation, and allocation-size addition is checked.

`unittests/imask_test.c` covers creation/copying, bounds, clear/reuse, boolean
operations, allocator ownership, allocation failures/overflow, NULL handling,
and vector/string comparison reuse. The dedicated suite passes under normal
and ASan/UBSan builds. iMask-only gcov results are 97.83% line and 98.08% branch
coverage (above the 80%/70% thresholds). LeakSanitizer could not complete in
the hosted runner because it cannot attach to its traced worker thread; the
same ASan/UBSan run passes with leak checking disabled.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
