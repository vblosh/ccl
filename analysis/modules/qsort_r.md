# `qsort_r.c` audit

## Scope and dependencies

- Physical source: `src/qsort_r.c`, a vendored Bentley/McIlroy quicksort.
- It has no declaration in CCL public headers and no in-tree caller, but defines
  the externally visible global symbol `qsort_r`.
- Dependencies are only C pointer/memory semantics and a BSD-style comparator
  `(thunk, left, right)`.

## Function and invariant inventory

- `med3` chooses a median pivot using the thunk-aware comparator.
- `swapfunc`, `swap`, and `vecswap` exchange byte ranges, selecting long-word
  swaps only for aligned, appropriately sized elements.
- `qsort_r(base,n,size,thunk,cmp)` uses insertion sort for fewer than seven
  items and for a partition with no swaps; median-of-three for medium inputs;
  pseudomedian-of-nine for more than 40; groups equal keys around the pivot;
  recursively handles one partition and tail-iterates the other.
- For ordinary valid inputs, the invariant is a permutation of the original
  byte records ordered by the comparator, with the same thunk on every call.

## Historical pre-fix defect (confirmed at the audit baseline)

### QR1 — the unnamespaced symbol conflicts with platform `qsort_r` ABIs
(critical)

On glibc, `_GNU_SOURCE` declares:

`qsort_r(base, n, size, compar(left,right,arg), arg)`.

This file defines the incompatible BSD order:

`qsort_r(base, n, size, thunk, compar(thunk,left,right))`.

Compiling this source with `-D_GNU_SOURCE` on the current Linux environment
fails with a direct conflicting-types diagnostic against `<stdlib.h>`. Because
the source is included in the static library and the symbol is global, it can
also satisfy/collide with an application's libc `qsort_r` reference and turn
the application comparator pointer into `thunk` (and vice versa), causing an
ABI-level crash. CCL neither declares nor calls this implementation, so it
must not export the platform-reserved name. Prefer removing it from the CCL
target if truly unused; otherwise rename it to a CCL-private symbol and declare
one explicit internal comparator ABI.

## Additional portability risk (not reproduced as an ordinary-input failure)

`swapfunc` narrows byte counts from `size_t` to `int`, and the alignment macro
forms a nonportable pointer-minus-null expression. Very large record widths
can truncate. These should be replaced with `size_t` and `uintptr_t` during
the QR1 cleanup, but ordinary randomized inputs from 0-255 records, ascending/
descending thunks, duplicates, and 13-byte records passed ASan/UBSan.

## Current coverage

The implementation is now the private `ccl_qsort_r` symbol. It is directly
covered by `unittests/qsort_r_test.c`, auto-discovered by
`unittests/CMakeLists.txt`; the suite also calls the native libc `qsort_r` ABI
to prove the symbols do not collide. The former no-caller/no-test statement
describes only the historical pre-fix baseline.

## Required test matrix

If the source is retained under a private CCL name:

1. Sizes 0, 1, 2, 6, 7, 8, 40, 41, and large arrays to force insertion,
   median-of-three, pseudomedian-of-nine, recursion, and tail iteration.
2. Already sorted, reverse, all equal, organ-pipe, alternating, duplicate-heavy,
   and fixed-seed randomized inputs; compare to a reference sorted copy.
3. Ascending and descending behavior controlled solely through thunk; assert
   every comparator invocation receives the exact thunk.
4. Element widths 1, `sizeof(long)`, aligned multiples, odd/misaligned widths,
   and structs with payload bytes; verify the complete record permutation.
5. Build with `_GNU_SOURCE` while including `<stdlib.h>` to regress QR1: the
   renamed/private implementation must compile and libc `qsort_r` must remain
   callable with its native signature.

These cases exercise all algorithm branches above 80% line/70% branch and must
pass ASan/UBSan. If the unused file is removed from the target, replace unit
coverage with a symbol/build regression proving CCL does not define `qsort_r`.

## Luna handoff

Resolve QR1 before spending effort on algorithm tests. The cleanest compatible
change is to exclude this unused object from `CCL_SOURCES`; otherwise rename it
and make the comparator contract private. Do not publish another platform-
ambiguous `qsort_r` declaration.

## Resolution

QR1 is resolved by renaming the implementation to the CCL-private symbol
`ccl_qsort_r` while retaining the BSD-style `(thunk, left, right)` comparator
through an explicit internal typedef. The source no longer defines the
platform-reserved `qsort_r`; it compiles with `_GNU_SOURCE`, and the unit suite
also calls glibc's native `(left, right, thunk)` `qsort_r` to prove the ABIs do
not collide. Swap byte counts now use `size_t`, alignment uses `uintptr_t`,
and zero-size/no-op calls return before alignment arithmetic.

`unittests/qsort_r_test.c` covers insertion, median-of-three,
pseudomedian-of-nine, duplicate/equal-key partitioning, recursion/tail
iteration, all required sizes, odd/aligned widths, full record permutation,
descending thunks, and native libc ABI compatibility. Isolated GCC coverage is
100.00% lines and 100.00% branch execution (98.72% of branches taken). The
isolated ASan/UBSan run passes all three tests; LeakSanitizer cannot be
validated in the current ptrace-restricted local environment.
