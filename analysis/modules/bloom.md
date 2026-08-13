# `bloom.c` audit

## Scope and dependencies

- Physical source: `src/bloom.c`; public surface is `BloomFilterInterface
  iBloomFilter` in `include/containers.h`.
- Dependencies: `CurrentAllocator`, `iError`, `rand`, and libm (`log`, `round`).
- Representation invariants: `0 <= count <= MaxNbOfElements`, `nbOfBits > 0`,
  `HashFunctions >= 1`, `bits` owns `ceil(nbOfBits/8)` zero-initialized bytes,
  and the flexible seed tail owns one seed per hash function. All allocations
  must be released through the allocator captured at creation.

## Function and contract inventory

| Entry/helper | Contract/invariants |
| --- | --- |
| `Hash(key,len,seed)` | MurmurHash2-style deterministic byte hash; may receive any valid byte address, aligned or not. |
| `CalculateSpace(n,p)` | Return total object/seed/bit storage for a filter dimensioned for `n > 0` and finite `0 < p < 1`; invalid parameters return 0 with BADARG. |
| `Create(n,p)` | Compute dimensions, allocate object and bitset, generate seeds, start empty, and retain the owning allocator. |
| `Add(filter,key,len)` | Set every selected bit and increment inserted-element count, rejecting invalid/full filters. |
| `Find(filter,key,len)` | Return 0 for definitely absent, 1 for possibly present, BADARG for invalid inputs. |
| `Clear` | Zero bits and restore the empty-filter state. |
| `Finalize` | Free bitset then object through the captured allocator. |

## Historical pre-fix defects

The B1-B7 findings below are the pre-fix audit baseline and are retained as
historical evidence. The implementation and test evidence later in this
document describes the current source and test state.

### B1 — Murmur hash performs misaligned and aliasing-unsafe loads (critical)

Line 57 casts arbitrary byte input to `unsigned int *` and dereferences it.
The public key is `const void *` and has no alignment requirement. UBSan on a
four-byte key at `buffer + 1` reports a misaligned `unsigned int` load at line
57; strict-aliasing is violated as well. Load four bytes with `memcpy` into an
`uint32_t` (and define the intended byte order) before mixing.

### B2 — `Clear` does not reset `count` (high)

It zeros the bits but leaves the capacity counter unchanged. A one-element
filter accepts its first Add, returns success from Clear, then rejects the next
Add as `CONTAINER_FULL`. Set `count=0` after clearing the bitset.

### B3 — sizing uses the wrong Bloom-filter formula (high)

The documented denominator is `(ln 2)^2`, approximately `0.480453`, but
`TWICE_LOG_2` is `2*ln(2)`, approximately `1.386294`. For `n=100,p=0.01` this
computes about 332 bits rather than about 959 and two hashes rather than seven,
so the advertised false-positive probability is not met. Use the squared
constant/formula consistently in `Create` and `CalculateSpace`, with checked
rounding.

### B4 — zero/degenerate dimensions are accepted and invoke UB or create an
always-positive filter (high)

`nbOfElements==0` reaches division by zero at line 93 (confirmed by UBSan).
Finite probabilities sufficiently close to 1 can round `nbOfBits` and/or `k`
to zero; then `Find` executes zero hash checks and returns 1 for every key.
`NaN` also bypasses both probability comparisons and reaches undefined
floating-to-integer conversion. Require `n>0`, `isfinite(p)`, `0<p<1`, checked
finite dimensions, and clamp mathematically valid rounded dimensions to at
least one bit/hash.

### B5 — `Add` dereferences invalid public arguments (medium)

Unlike `Find`, `Add` dereferences NULL `b` before validation and passes NULL or
zero-length keys into `Hash`. Validate all three arguments before the fullness
check and return/raise BADARG consistently.

### B6 — size arithmetic is unchecked (medium)

Object-plus-seeds and bit-count-to-bytes computations can overflow for large
inputs or extreme probabilities, causing undersized allocations followed by
writes. `CalculateSpace` can wrap and report a plausible small value. Validate
floating results against `SIZE_MAX` and use checked additions/multiplications.

### B7 — `Clear` raises the wrong operation name (low)

The NULL path reports `iBloomFilter.Find` rather than `iBloomFilter.Clear`,
making diagnostics misleading.

## Historical coverage baseline and current coverage

The historical test added five integers, checked two present keys and one
absent key, and finalized. It did not clear, fill, validate sizing, use
unaligned keys, inject allocation failures, inspect errors, or run edge
probabilities. The current `unittests/bloom_test.c` is registered as
`test_bloom` and covers those cases in the implementation and test evidence
below.

## Required test matrix

1. Formula-table `CalculateSpace`/Create dimensions for representative
   `(n,p)` pairs, including expected `m` and `k` computed independently (B3).
2. Create/add/find for key lengths 1, 2, 3, 4, 5, long keys, binary keys with
   zero bytes, and deliberately unaligned addresses under UBSan (B1); inserted
   keys must never be false negatives.
3. Fill exactly to maximum, assert monotonic Add returns, reject one extra,
   clear, assert prior keys absent where deterministically possible, and refill
   to maximum (B2).
4. Reject `n=0`, probabilities 0, 1, below/above range, `NAN`, and infinities;
   cover values near both valid boundaries without zero bits/hashes (B4).
5. NULL filter/key and zero key length for Add/Find/Clear/Finalize with exact
   BADARG operation names (B5/B7).
6. Counting/failing allocator: first allocation failure, bitset failure with
   object cleanup, successful ownership after changing `CurrentAllocator`, and
   balanced finalization.
7. Checked arithmetic boundaries for seed tail, bitset, and total space (B6).
8. Deterministic `srand` setup where needed; do not write a flaky assertion
   that an arbitrary absent key can never be a false positive.

This reaches every function, switch tail, allocation/error path, full/clear
transition, and hash loop, sufficient for 80% line/70% branch coverage. Run
ASan/UBSan/leak checks and link libm.

## Luna handoff

Fix B1 first, then B3/B4 checked dimensioning, B2 state reset, and B5-B7.
Changing hash byte loading can change serialized/in-memory filter behavior,
but no persistence API exists; keep Add and Find exactly consistent.

## Implementation and test evidence

`src/bloom.c` now assembles MurmurHash2 words from bytes in defined
little-endian order, uses the squared-log sizing formula, clamps valid rounded
dimensions to at least one bit and one hash, and checks floating-point and
allocation arithmetic before allocating. `Add` validates all public inputs,
`Clear` resets both bits and count, and NULL `Clear`/`Finalize` paths report
their own operation names. The allocator captured at creation owns both
allocations through finalization.

`unittests/bloom_test.c` covers formula-table allocation dimensions, binary and
unaligned keys, fill/clear/refill transitions, invalid and boundary
probabilities, argument diagnostics, allocator failures/ownership, and size
overflow. The focused suite passes under ASan/UBSan with leak detection
disabled; LeakSanitizer is unavailable in this ptrace-restricted environment.
The current GCC/gcov checker reports 96.99% line coverage and 90.54%
taken-branch coverage for `src/bloom.c`.

## Current local integration verification

The current unsanitized CTest run passes all 36 registered tests. No local LSan
pass is claimed because LeakSanitizer cannot initialize under the workspace's
ptrace restriction.
