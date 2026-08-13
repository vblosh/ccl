# `bitstrings.c` audit

## Scope and dependencies

- Physical source: `src/bitstrings.c`; public `BitStringInterface iBitString`
  is declared in `include/containers.h`, with private object/iterator layouts in
  `include/ccl_internal.h` and this source.
- Dependencies: `CurrentAllocator`, `iError`, stdio persistence, and C memory
  routines. The implementation assumes `CHAR_BIT==8` throughout despite a
  partially disabled 16-bit table.
- Core invariant: `count` is the logical bit length; `capacity` is allocated
  bytes; `contents` owns at least `ceil(count/8)` addressable bytes; unused high
  bits in the final byte are zero/canonical; all allocation/reallocation/free
  uses the captured allocator; mutations update `timestamp` and honor
  `CONTAINER_READONLY`.

## API and helper inventory

- Lifetime/metadata: `Create`, `CreateWithAllocator`, `Init`, `Copy`, `Clear`,
  `Finalize`, `Size`, `Sizeof`, `Get/SetFlags`, `Get/SetCapacity`,
  `GetAllocator`, `GetElementSize`, and the currently inert
  `SetErrorFunction`.
- Element/sequence operations: `Add`/`PushBack`, `GetElement`, `SetElement`,
  `ReplaceAt`, `PopBack`, `Insert`/`InsertAt`, `Erase`/`EraseAt`, `IndexOf`,
  `Apply`, `Append`, `AddRange`, `GetRange`, and `Reverse`.
- Bit algebra: allocating and assigning `Or`, `And`, `Nand`, `Xor`, `Not`,
  subset-style `LessEqual`, left/right shifts, range `Memset`,
  `PopulationCount`, and `BitBlockCount`.
- Conversion/search/storage: `StringToBitString`, `Print`, `InitializeWith`,
  `Contains` via `bitBitstr`, `GetData`, `CopyBits`, `Save`, and `Load`.
- Iterator operations: heap/buffer initialization, first/next/current, and
  deletion. The public base iterator also declares previous/last/seek/position/
  replace slots.

## Historical pre-fix defects

The BS1-BS14 findings below are the pre-fix audit baseline and are retained as
historical evidence. The implementation and verification sections later in this
document describe the current source and test state.

### BS1 — `Or` reads beyond the shorter operand (critical)

Lines 368-371 iterate through the larger logical length and index both input
buffers. ASan reports a heap-buffer-overflow at line 370 for a one-bit operand
ORed with a 1000-bit operand. Copy the common prefix only and copy the longer
operand's remaining bits, with final-byte masking.

### BS2 — `AddRange` reads past input and does not maintain length (critical)

For an empty destination and an eight-bit, one-byte input, line 608 reads
`data[1]`; ASan reports a stack-buffer-overflow. The zero-shift case also
performs a shift by `CHAR_BIT`, writes starting one byte too far (`idx=1`), and
the full-byte loop never increments `count`. Reimplement using a clear bit
ordering contract and exact source-byte bounds; regress aligned and unaligned
destination lengths.

### BS3 — custom allocator contracts are broken (critical)

`CreateWithAllocator` ignores its allocator parameter: a counting reproducer
requested B while A was current and observed two A allocations, no B
allocation, and `GetAllocator()!=B`. `expandBitstring` then uses raw system
`realloc` rather than `b->Allocator->realloc`, which is invalid for custom
allocations. `Init` likewise hardcodes `CurrentAllocator`. Thread the requested
allocator through object/content creation, growth, failure cleanup, and free.

### BS4 — `Copy` returns a logically empty object (high)

It copies bytes but never copies `count` (nor flags as a considered policy).
Copying parsed `"1010"` produces `Size==0`. Set the logical length and ensure
padding/capacity remain canonical; test independence.

### BS5 — `Not` and `NandAssign` implement the wrong results (high)

`Not` stores the number of processed **bytes** in `result->count`; NOT of the
four-bit `1010` returns size 1 and prints `1`, not four-bit `0101`.
`NotAndAssign` performs ordinary AND in its main loop and uses logical `!`
where bitwise `~`/masking is needed; `1010 NandAssign 1100` returns `1000`
instead of `0111`. Preserve the left logical length for assigning operations
and mask unused/tail bits.

### BS6 — `Append` iterates byte capacities as bit indices (high)

Lines 717-724 sum rounded byte capacities and compare that number to the left
bit count. Appending two bits `11` to four bits `1010` returned length 8 and
`1111 1010`, rather than length 6 and `101011`. Iterate exactly `b2->count`
bits, handle self-append through a snapshot or defined rejection, and update
count/timestamp once safely.

### BS7 — substring search is incorrect and has edge underflow (high)

`bitBitstr` builds its shift table from `text` at line 893 rather than `Pat`.
Its one-byte-pattern control flow cannot succeed: `Contains("0","0")`
returned 0. An empty pattern also makes `patByteLength-1` underflow. Replace or
repair against a straightforward bitwise reference and validate NULL/empty
inputs.

### BS8 — `LessEqual` ignores half (or more) of the data (high)

Lines 310-311 divide the rounded byte count by 8 while reading
`unsigned int` units (and those typed reads have alignment/effective-type
issues). A 40-bit left value containing only the highest bit was reported as a
subset of an all-zero right value. Implement bytewise subset comparison over
the full logical domain, defining unequal-length zero extension explicitly.

### BS9 — capacity shrinking leaves `count` beyond allocation (high)

Public `SetCapacity` allocates a smaller byte buffer but never truncates or
rejects an existing larger logical count. Subsequent valid-by-count access can
read out of bounds. Either reject capacity below required bytes or define and
implement truncation; checked arithmetic is also required in
`BYTES_FROM_BITS`, rounding, and growth.

### BS10 — padding bits leak into population/algebra results (high)

Unused final-byte bits are not consistently masked. `NotAssign` on three-bit
`000` flips the whole byte; the visible bits become `111`, but
`PopulationCount` returns 8 rather than 3. All writers and allocating algebra
must canonicalize padding, and population/block counting must independently
mask to logical length.

### BS11 — NULL/index validation has direct crashes or unsafe access (high)

`Equal` reads `bsl->Flags`/`bsr->Flags` before its NULL check; `InsertAt` does
not reject `idx > count`; `IndexOf` unconditionally writes through `result`;
`PopBack`, flag access, and several mutators dereference NULL. `SetElement`
raises INDEX correctly but returns the assigned bit, so a successful zero
write is reported as failure. Normalize public BADARG/INDEX handling without
access after errors.

### BS12 — read-only and iterator mutation invariants are not enforced (high)

Only a few methods check `CONTAINER_READONLY`; Add, insert/erase, shifts,
assignment algebra, range writes, append, and capacity changes still mutate a
read-only bit string. Mutations (except `SetFlags`) do not increment
`timestamp`, so iterators fail to detect modification. Apply the existing
container policy consistently.

### BS13 — iterator table/state and persistence cleanup are incomplete (medium)

`NewIterator`/`InitIterator` leave `index` and `bit` uninitialized, map
`GetPrevious` to `GetNext`, and leave the remaining public iterator slots
uninitialized. `DeleteIterator` always frees, including an iterator initialized
in caller storage. Separately, `Load` leaks its newly created object when the
contents read is truncated. Define heap-versus-buffer iterator ownership,
fully initialize supported slots (NULL unsupported slots at minimum), and
finalize on every Load failure.

### BS14 — additional algebra/range loops use byte counts as bit counts (medium)

`AndAssign` tests a byte count for bit remainder, `Nand` has an incorrect
partial-byte rewrite, and multiple operations process `1 + count/8` even when
the count is byte-aligned. These create extra-byte reads/writes and inconsistent
unequal-length semantics. Consolidate all operations around helpers for
`ceil(count/8)`, a last-byte mask, and zero-extension.

## Historical coverage baseline and current coverage

The historical manual test parsed/printed one long value, grew by repeated Add,
copied, erased/inserted, shifted, ranged, searched, counted, and performed one
AND, but mostly printed diagnostics and did not fail on mismatches. The current
`unittests/bitstrings_test.c` is registered as `test_bitstrings` and covers the
boundary, algebra, persistence, iterator, allocator, and read-only cases listed
in the implementation status below.

## Required test matrix

1. Lifetime table for counts around byte/word boundaries (0,1,7,8,9,31,32,
   33,63,64,65), copy independence (BS4), clear/reuse, size/capacity, and
   checked overflow/allocation failures.
2. Counting/failing allocators for object/content/growth failures and correct
   ownership after changing `CurrentAllocator` (BS3).
3. Reference-model every element and sequence operation at first/middle/last
   positions, invalid indices/NULLs, self-append, and AddRange with source and
   destination offsets 0-7 (BS2/BS6/BS9/BS11).
4. Exhaustive small truth tables and randomized larger reference checks for
   allocating/assigning OR/AND/NAND/XOR/NOT, unequal lengths, aliasing, padding,
   subset comparison, shifts 0,1,7,8,count,count+1, and Memset ranges
   (BS1/BS5/BS8/BS10/BS14).
5. Parse/print round trips with separators/prefixes, empty/invalid/truncated
   output buffers, InitializeWith/CopyBits, reverse/ranges, population and run
   counts at every final-byte width.
6. Exhaustive short Contains versus a naive bit reference, including empty,
   equal, absent, cross-byte, and pattern-longer-than-text cases (BS7).
7. Read-only matrix over every mutator and iterator invalidation after every
   permitted mutation (BS12); complete first/next/current/previous/seek/last
   behavior for heap and caller-buffer iterators (BS13).
8. Save/load round trip, empty value, NULL stream, header/content truncation,
   fwrite/fread failure, flags policy, and leak-free cleanup (BS13).

Use table-driven helpers rather than tests coupled to raw internal padding.
The breadth should comfortably exceed 80% line/70% branch coverage; run each
logical group with ASan, UBSan, and leak detection.

## Implementation and verification (2026-08-08)

`src/bitstrings.c` now uses exact `ceil(count / CHAR_BIT)` byte sizing,
canonical tail masking, captured allocator ownership, and checked capacity
growth.  Sequence operations, append/range copying, all algebra operations,
subset comparison, shifts, search, parsing/printing, counts, iterators, and
persistence were rebuilt around those invariants.  Mutating operations reject
read-only objects and advance the iterator timestamp; `Load` finalizes the
partially created object on truncated input.

`unittests/bitstrings_test.c` covers lifetime and boundary sizes, copy
independence, custom allocators, range/append aliasing, algebra truth cases,
padding, search, shifts, persistence, iterators, read-only behavior, and
invalid arguments. The current GCC/gcov checker reports 86.89% line coverage
and 73.33% of branches taken (97.87% of branch sites executed).

The isolated ASan+UBSan run passes with `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.  LeakSanitizer itself is
unavailable in this environment: enabling `detect_leaks=1` terminates with
“LeakSanitizer does not work under ptrace” before reporting a leak result.

## Luna handoff

This unit needs a staged repair. First establish canonical byte-count/mask and
allocator helpers; fix BS1-BS4 and BS9 memory/ownership invariants. Next fix the
reference-tested algebra/search/append/AddRange paths (BS5-BS8, BS10, BS14).
Finally normalize validation/read-only/timestamps, iterators, and persistence
(BS11-BS13). Re-run the same sanitizer target after each stage because many
symptoms share the broken byte-count arithmetic.

## Current local integration verification

The current unsanitized CTest run passes all 36 registered tests. The focused
ASan/UBSan run passes with `detect_leaks=0`. LeakSanitizer cannot initialize in
this ptrace-restricted workspace, so no local LSan pass is claimed; run the
leak-enabled suite outside that restriction for final leak verification.
