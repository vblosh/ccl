# `qsortex.c` audit

## Scope and dependencies

- Physical source: `src/qsortex.c`; public declaration is `qsortEx` in
  `include/containers.h`.
- Dependencies: `CompareFunction`/`CompareInfo`; in-tree consumers include
  vector, list, dlist, and narrow/wide collection sorting.
- No allocation is performed. The operation must preserve complete records,
  permute only `num * width` bytes, and forward the same `CompareInfo *` to
  every comparison.

## Function and invariant inventory

- `swap` exchanges exactly `width` bytes and is a no-op for identical records.
- `shortsort` uses selection sort for partitions of at most eight elements.
- `qsortEx` partitions larger ranges around the middle record, explicitly
  stacks one side in fixed arrays, processes the other, and drains the stack.
  `num < 2` or `width == 0` is an intentional no-op.

## Confirmed defect

### QE1 — partition-size comparison forms a pointer before the array (medium)

At line 88, `higuy - 1 - lo` subtracts one **byte**, not one element. When the
pivot is the minimum record, `higuy == lo`; merely forming `higuy - 1` creates
a pointer before the array, which is undefined C behavior even though it is
not dereferenced. For other partitions the expression also compares a left
byte count reduced by one against the right byte count, rather than comparing
element ranges symmetrically. Rewrite the decision using valid differences,
for example the left byte extent `higuy - lo` and right byte extent
`hi - loguy`, without constructing out-of-range pointers.

## Validation result

A fixed-seed differential run covered `n=0..255`, 100 duplicate-heavy datasets
per size, and both ascending and descending comparators under ASan/UBSan; all
ordinary outputs were correctly ordered. No additional ordinary-input defect
was reproduced. NULL `base`/`comp`, overflowed `num*width`, and inconsistent
comparators remain caller-precondition/hardening concerns rather than claimed
defects because this qsort-like API has no error return.

## Existing coverage

Many container Sort operations exercise `qsortEx` indirectly, but there is no
direct suite proving record preservation, CompareInfo forwarding, cutoff and
partition branches, or the minimum-pivot QE1 case.

## Required test matrix

1. No-op cases `num=0`, `num=1`, and `width=0`; comparator must not run and
   sentinel bytes must remain untouched.
2. Sizes around cutoff (2, 7, 8, 9), larger arrays, already sorted, reverse,
   all equal, duplicate-heavy, minimum-middle-pivot (QE1), maximum-middle-pivot,
   organ-pipe, and fixed-seed random arrays.
3. Ascending/descending comparators using `CompareInfo.ExtraArgs`; assert the
   exact pointer plus optional `ContainerLeft/Right` metadata is forwarded.
4. Widths 1, 3, `sizeof(long)`, and a padded struct with unique identity and
   checksum fields. Compare against libc `qsort` keys and separately prove the
   complete multiset of records is preserved.
5. Integration tests through vector/list/dlist/string collection Sort,
   including duplicate elements and custom comparators.

These tests force `shortsort`, both partition-stack branches, swaps/no-swaps,
and stack draining, sufficient for 80% line/70% branch coverage. Run with
ASan/UBSan; there should be no allocation/leak work in this unit.

## Luna handoff

Apply the small QE1 pointer-arithmetic fix and add the minimum-pivot regression
first, then the direct branch matrix. Preserve the public signature and the
current unstable-sort behavior.

## Resolution

QE1 is fixed by comparing the valid byte extents `higuy - lo` and
`hi - loguy`; no pointer before the base array is formed. `qsortEx` retains its
public signature and unstable partition behavior. The dedicated
`unittests/qsortex_test.c` suite covers no-op inputs, cutoff and pivot cases,
duplicate-heavy and large partitions, record permutation at widths 1, 3,
`sizeof(long)`, and padded records, CompareInfo identity/metadata forwarding,
and vector/list/dlist/string-collection integration.

The isolated GCC coverage run reports 100.00% line coverage and 100.00%
branch execution (92.11% of branches taken). The ASan+UBSan run with
`ASAN_OPTIONS=detect_leaks=1` passes all five tests with no leaks.
