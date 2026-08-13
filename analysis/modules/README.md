# Library Source Audit and Test Status

This directory contains the function-by-function audit for every library source
file in the current `feature/ranges` worktree. The 58 physical `src/*.c` files
are grouped into 36 ownership units so textually included generator
implementations and their typed wrappers are analyzed and tested together.

## Current verification (2026-08-13)

- GCC 15.2 default build and CTest: 36/36 passed.
- GCC 15.2 AddressSanitizer and UndefinedBehaviorSanitizer build: all 36 test
  executables passed when run with leak detection disabled.
- LeakSanitizer is unavailable in the current ptrace-restricted execution
  environment; enabling `detect_leaks=1` aborts at LSan startup and is not a
  test failure attributable to a library unit.
- The coverage run completed for all 36 units. Thirty-three meet the 80% line
  and 70% taken-branch gates; `dlist`, `hashtable`, and `vector` currently miss
  the branch gate.

| Unit | Report | Line | Branch |
|---|---|---:|---:|
| SuffixTree | [SuffixTree](SuffixTree.md) | 94.04% | 81.72% |
| bitstrings | [bitstrings](bitstrings.md) | 86.89% | 73.33% |
| bloom | [bloom](bloom.md) | 96.99% | 90.54% |
| buffer | [buffer](buffer.md) | 91.13% | 81.48% |
| deque | [deque](deque.md) | 85.61% | 71.51% |
| dictionary family | [dictionary](dictionary_family.md) | 84.20% | 71.46% |
| dlist | [dlist](dlist.md) | 82.00% | **69.94% (fail)** |
| typed dlist family | [dlist family](dlist_family.md) | 95.96% | 75.00% |
| error | [error](error.md) | 100.00% | 100.00% |
| fgetline | [fgetline](fgetline.md) | 90.76% | 80.43% |
| generic | [generic](generic.md) | 85.71% | 72.27% |
| hashtable | [hashtable](hashtable.md) | 85.41% | **69.88% (fail)** |
| heap | [heap](heap.md) | 90.36% | 74.17% |
| iMask | [iMask](iMask.md) | 97.83% | 98.08% |
| list | [list](list.md) | 82.08% | 70.56% |
| typed list family | [list family](list_family.md) | 87.80% | 74.27% |
| malloc_debug | [malloc_debug](malloc_debug.md) | 92.31% | 87.50% |
| memorymanager | [memorymanager](memorymanager.md) | 100.00% | 100.00% |
| observer | [observer](observer.md) | 93.67% | 90.00% |
| pool | [pool](pool.md) | 91.72% | 77.00% |
| pooldebug | [pooldebug](pooldebug.md) | 92.59% | 82.00% |
| priorityqueue | [priorityqueue](priorityqueue.md) | 89.77% | 74.40% |
| qsort_r | [qsort_r](qsort_r.md) | 100.00% | 98.72% |
| qsortex | [qsortex](qsortex.md) | 100.00% | 92.11% |
| queue | [queue](queue.md) | 100.00% | 97.06% |
| range | [range](range.md) | 87.46% | 76.03% |
| redblacktree | [redblacktree](redblacktree.md) | 93.66% | 76.74% |
| scapegoat | [scapegoat](scapegoat.md) | 85.03% | 70.04% |
| searchtree | [searchtree](searchtree.md) | 91.45% | 73.99% |
| sequential | [sequential](sequential.md) | 96.39% | 71.91% |
| smallpool | [smallpool](smallpool.md) | 90.75% | 79.79% |
| string collection family | [string collection](strcollection_family.md) | 80.86% | 70.28% |
| string list family | [string list](stringlist_family.md) | 84.35% | 70.86% |
| ValArray family | [ValArray](valarray_family.md) | 82.24% | 70.70% |
| vector | [vector](vector.md) | 80.96% | **69.35% (fail)** |
| typed vector family | [vector family](vector_family.md) | 95.09% | 87.10% |

The machine-readable ownership and coverage mapping is in
`unittests/coverage_manifest.json`. Run `coverage-check` from a GCC coverage
build to reproduce the table. The target returns nonzero while any unit is
below its gate even though it still writes and prints every unit result.

## Compatibility note

Several generated container interfaces and iterator layouts were corrected to
match their implementations. Public function names remain available, but the
affected structs/vtables are not binary-compatible with objects compiled from
the previous `develop` headers. Consumers must rebuild all C and C++ objects
against the updated headers; do not mix old objects with the new static library.
