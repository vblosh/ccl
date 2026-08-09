# Library Source Audit and Test Status

This directory contains the function-by-function audit for every library source
file on `develop`. The 57 physical `src/*.c` files are grouped into 35 ownership
units so textually included generator implementations and their typed wrappers
are analyzed and tested together.

## Final verification

- GCC 15.2 sanitizer build: passed.
- 35 test executables: passed with AddressSanitizer, UndefinedBehaviorSanitizer,
  and LeakSanitizer enabled (`ASAN_OPTIONS=detect_leaks=1`).
- Clang 21.1 build and CTest: 35/35 passed.
- Coverage gate: all 35 units passed at least 80% line and 70% branch coverage.

| Unit | Report | Line | Branch |
|---|---|---:|---:|
| SuffixTree | [SuffixTree](SuffixTree.md) | 94.04% | 81.72% |
| bitstrings | [bitstrings](bitstrings.md) | 86.97% | 73.58% |
| bloom | [bloom](bloom.md) | 96.95% | 90.54% |
| buffer | [buffer](buffer.md) | 91.13% | 81.48% |
| deque | [deque](deque.md) | 85.61% | 71.51% |
| dictionary family | [dictionary](dictionary_family.md) | 84.20% | 71.46% |
| dlist | [dlist](dlist.md) | 82.06% | 70.04% |
| typed dlist family | [dlist family](dlist_family.md) | 95.96% | 75.00% |
| error | [error](error.md) | 100.00% | 100.00% |
| fgetline | [fgetline](fgetline.md) | 90.76% | 80.43% |
| generic | [generic](generic.md) | 85.71% | 72.27% |
| hashtable | [hashtable](hashtable.md) | 86.00% | 70.12% |
| heap | [heap](heap.md) | 90.36% | 74.17% |
| iMask | [iMask](iMask.md) | 97.83% | 98.08% |
| list | [list](list.md) | 82.56% | 71.02% |
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
| redblacktree | [redblacktree](redblacktree.md) | 93.64% | 77.11% |
| scapegoat | [scapegoat](scapegoat.md) | 85.03% | 70.04% |
| searchtree | [searchtree](searchtree.md) | 91.43% | 74.32% |
| sequential | [sequential](sequential.md) | 96.20% | 70.93% |
| smallpool | [smallpool](smallpool.md) | 90.75% | 79.79% |
| string collection family | [string collection](strcollection_family.md) | 80.86% | 70.28% |
| string list family | [string list](stringlist_family.md) | 84.35% | 70.86% |
| ValArray family | [ValArray](valarray_family.md) | 82.24% | 70.70% |
| vector | [vector](vector.md) | 81.32% | 70.32% |
| typed vector family | [vector family](vector_family.md) | 95.09% | 87.10% |

The machine-readable ownership and coverage mapping is in
`unittests/coverage_manifest.json`. Run `coverage-check` from a GCC coverage
build to reproduce the table.

## Compatibility note

Several generated container interfaces and iterator layouts were corrected to
match their implementations. Public function names remain available, but the
affected structs/vtables are not binary-compatible with objects compiled from
the previous `develop` headers. Consumers must rebuild all C and C++ objects
against the updated headers; do not mix old objects with the new static library.
