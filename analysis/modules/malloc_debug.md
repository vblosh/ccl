# `malloc_debug.c` analysis

## Scope and dependencies

Physical source: `src/malloc_debug.c`; public object `iDebugMalloc` is declared in `include/containers.h:78`. The unit wraps libc `malloc/free` and reports errors through `iError.RaiseError`.

Each allocation is intended to be:

```
[size_t SIGNATURE][size_t total_size][aligned user bytes][size_t MAGIC]
```

`AllocatedMemory` tracks total wrapped bytes internally, but no public accessor exists.

## Function and API inventory

| Entry point | Behavior | Important paths |
| --- | --- | --- |
| `Malloc` / `iDebugMalloc.malloc` | Aligns the request, allocates header/user/footer, zeroes the aligned user region, writes sentinels. | Zero/small/aligned requests; backing allocation failure; arithmetic overflow. |
| `Free` / `iDebugMalloc.free` | Ignores null; checks the leading signature and trailing magic; poisons and frees a valid allocation; raises `BADPOINTER` or `BUFFEROVERFLOW`. | Null, valid, bad signature, damaged footer, repeat free. |
| `Realloc` / `iDebugMalloc.realloc` | Null delegates to `Malloc`; equal aligned size returns unchanged; all actual size changes allocate/copy/free, preserving the original if allocation fails. | Null, same alignment class, shrink including zero, grow success/failure, bad pointer, arithmetic overflow. |
| `Calloc` / `iDebugMalloc.calloc` | Multiplies `n * size` and delegates to the already-zeroing `Malloc`. | Normal and multiplication-overflow requests. |

## Invariants

- The stored size is the complete wrapped allocation size; `Free` uses it both to locate `MAGIC` and to update accounting.
- The returned pointer is `sizeof(size_t)` aligned and begins after exactly two header words.
- A failed `realloc` must leave the original allocation valid and unchanged.
- `calloc` must reject a product that cannot be represented in `size_t`.
- Diagnostic checks themselves must not access memory outside a live wrapped allocation.

## Confirmed defects

### Fixed: in-place shrink corrupted metadata

At lines 88-92, `Realloc` stores the aligned **user** size in the header, although `Free` expects the **total wrapped** size. It also places the footer relative to the user pointer while `Free` later locates it relative to the allocation base using the corrupted value. A sanitizer-built probe performing `malloc(64)`, `realloc(..., 16)`, and `free` reproducibly invokes `iError` with `CONTAINER_ERROR_BUFFEROVERFLOW` on an uncorrupted allocation and leaks it. `realloc(..., 0)` additionally forms `newsize - sizeof(size_t)` and writes before the user region.

### Fixed: allocation-size arithmetic was unchecked

Lines 23-24 can wrap both during alignment and while adding the three metadata words. On 64-bit Linux, the sanitizer probe showed `iDebugMalloc.malloc(SIZE_MAX)` returning non-null. The result has no usable requested region and violates allocation semantics. `Realloc` repeats the unchecked alignment at line 81.

### Fixed: `Calloc` multiplication was unchecked

Line 105 wraps `n * siz`. The probe `calloc(SIZE_MAX / 2 + 1, 2)` returned non-null after the product became zero. This can make a caller believe a huge zeroed array exists when only metadata was allocated.

### Fixed: advertised bad/double-free diagnostics performed invalid reads

`Free` unconditionally subtracts two words and reads `*ip` before it can establish ownership. A pointer near the start of a foreign allocation causes a heap-buffer-overflow in the checker itself. A second call after a successful `Free` reads freed storage and is a use-after-free under ASan. This directly contradicts the file header's stated ability to check foreign and twice-freed blocks. Header sentinels can diagnose some in-bounds fabricated pointers, but cannot safely prove arbitrary pointer ownership.

## Implemented fixes and tests

`src/malloc_debug.c` now validates alignment, overhead addition, aggregate
accounting, and `calloc` multiplication before allocating. Reallocations that
change the aligned size use a new wrapped block and copy the preserved prefix;
this gives shrink and zero-size reallocations a valid header/footer and leaves
the old block untouched when the new allocation fails. A live-allocation
registry validates ownership before any user pointer is dereferenced. A
bounded (256-entry) tombstone history reports ordinary repeated frees without
reading freed storage; a newly allocated block removes a reused address from
that history.

`unittests/malloc_debug_test.c` exports `ccl_get_test_suite()` and covers
aligned/zeroed requests, 64-to-16 shrink regression, grow and zero realloc,
null operations, overflow rejection, calloc overflow and zero products, footer
diagnostics, foreign pointer, repeated free, and bad realloc diagnostics. A separate
linker-wrapped sanitizer probe forces the growth backing allocation to fail
and verifies the original contents remain valid.

## Existing coverage

No dedicated tests exist. The legacy `tests/test.c` globally selects `iDebugMalloc`, indirectly exercising ordinary allocation/free through unrelated containers, but it does not cover realloc, direct calloc, sentinels, error callbacks, overflow, or allocation failure and is not part of current CTest.

## Verification evidence

1. Sanitizer suite build and run:
   `cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer ...`
   completed all 9 tests with ASan/UBSan and `detect_leaks=0`.
2. The same suite with `ASAN_OPTIONS=detect_leaks=1` reached process shutdown,
   then LeakSanitizer aborted with its environment warning that it does not
   work under ptrace. This is an execution-environment limitation; no ASan or
   UBSan finding was reported. Leak checking should be repeated outside this
   runner.
3. The linker-wrapped failure probe (`-Wl,--wrap=malloc`) forced the 128-byte
   growth allocation to return `NULL`; the old 32-byte block remained intact
   and was freed cleanly under ASan/UBSan.
4. GCC coverage run of the focused suite reports **90.77% line coverage**
   (120 of 130 executable lines) and **100% branch execution**, with 87.50% of
   branches taken at least once (`gcov -b -c`). This exceeds the unit gates of
   80% lines and 70% branches. Accounting is exercised by every alloc,
   realloc, and free path; `AllocatedMemory` has no public accessor.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
