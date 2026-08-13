# `buffer.c` audit

## Scope and dependencies

- Physical source: `src/buffer.c`; it implements both `iStreamBuffer` and
  `iCircularBuffer`, declared in `include/containers.h`.
- Dependencies: `CurrentAllocator`, `iError`, the selected
  `ContainerAllocator`, stdio, and byte-copy/zeroing routines.
- A stream buffer owns one byte allocation. `Size` is the allocation size and
  `Cursor <= Size` must hold before every read/write.
- A circular buffer owns `maxCount * ElementSize` bytes. Its logical occupancy
  must remain in `[0,maxCount]`; the oldest element is at `tail % maxCount` and
  the next insertion is at `head % maxCount`. Every live element must be
  destroyed exactly once when removed/overwritten/cleared if a destructor is
  installed.

## Function and contract inventory

| Entry/helper | Contract/invariants |
| --- | --- |
| Stream `Create` / `CreateWithAllocator` | Use 1024 for a zero requested size, allocate a zeroed backing store, and retain its allocator. |
| `CreateFromFile` | Open a named binary file, size it, allocate `size+1`, read the bytes, and close on every path. |
| `enlargeBuffer` / `Write` | Ensure the cursor plus write size fits, preserve old bytes on allocation failure, copy bytes, and advance the cursor. |
| `Read` | Copy at most the bytes between cursor and `Size`, then advance. |
| `SetPosition`, `GetPosition`, `GetData`, `Size` | Clamp a requested position to capacity and expose cursor/backing-store/capacity state. |
| `Clear`, `Resize`, `Finalize` | Zero/reset, resize while preserving valid state, and release with the captured allocator. |
| `ReadFromFile`, `WriteToFile` | Transfer one capacity-sized block and reset cursor as currently specified. |
| Circular `Create` / `CreateWithAllocator` | Validate nonzero dimensions, allocate control/data storage, and retain allocator. |
| Circular `Add` | Insert at the logical head; return 0 when replacing due to a full ring and 1 otherwise. |
| `PopFront`, `PeekFront` | Observe/remove the oldest live item; a NULL pop destination is allowed, while peek requires output. |
| Circular `Size`, `Sizeof` | Report logical item count and object/storage footprint. |
| Circular `Clear`, `Finalize` | Destroy every live item once, zero/reset the ring, then release allocations. |
| Both `SetDestructor` | Return the old callback and install the requested callback. |

## Historical pre-fix defects

The B1-B7 findings below are the pre-fix audit baseline and are retained as
historical evidence. The implementation status later in this document describes
the current source and test state.

### B1 - shrinking a stream leaves an out-of-range cursor (critical)

`Resize` (lines 180-197) does not clamp `Cursor`. Public-API reproducer:
create 8 bytes, seek to 8, resize to 1, then write an `int`. `Write` enlarges the
one-byte allocation only to six bytes but copies at offset eight. Clang
ASan reports a heap-buffer-overflow at line 101. Clamp the cursor after a
successful shrink and calculate required capacity with checked arithmetic.

### B2 - full circular-buffer insertion destroys the ring invariant (high)

At lines 277-280 a full ring sets only `head=0`; it neither advances `tail` nor
destroys the overwritten element. Capacity-three inserts `1,2,3,4` leave
`Size()==1` (confirmed through the public API, expected occupancy three) and
discard two still-live elements. Maintain monotonic head/tail counters (or
reset both consistently), advance the oldest index on overwrite, and call the
destructor for exactly the overwritten slot.

### B3 - destructor-enabled circular clear writes out of bounds (critical)

`CBClear` starts at `data`, ignores `tail % maxCount`, advances `p` once per
destroyed item, then uses the advanced pointer as the start of `memset`
(lines 361-368). For a full ring, `p` is one-past the allocation and the
following capacity-sized memset is wholly out of bounds. Wrapped rings also
destroy the wrong addresses. Iterate logical indices modulo capacity, then
zero the backing allocation (or each logical slot) from a valid base.

### B4 - size arithmetic and zero-size resize are unsafe (high)

`sizElement * sizeBuffer` at line 336, `Cursor + siz` at line 96, and growth
addition at line 82 are unchecked. Wraparound can allocate too little and
then copy out of bounds. Separately, `realloc(data,0)` may free and return NULL;
the error path retains the freed pointer and later finalization can double
free it. Reject overflowing dimensions/growth and define `Resize(0)` without
relying on implementation-specific realloc behavior.

### B5 - several public pointer arguments are dereferenced unchecked (medium)

`CreateWithAllocator(NULL)`, `Write(nonnull,NULL,positive)`,
`CreateFromFile(NULL)`, and file-transfer calls with NULL `FILE *` can fault.
`Write` checks only the buffer, unlike `Read`; the file methods check only the
buffer. Return/raise BADARG consistently before libc calls. A zero-byte write
with NULL data may remain valid if documented, but positive length may not.

### B6 - stream file sizing mishandles `ftell` and seek errors (medium)

The `long` from `ftell` is first converted to `size_t`, then narrowed to `int`
to test negativity (lines 59-60), rejecting or misclassifying files above
`INT_MAX`. The second `fseek` is unchecked and `siz+1` can overflow. Keep the
`long`, check it for `<0`, validate conversion/addition, and raise a file error
on every seek/tell failure.

### B7 - destructor setters cannot remove a callback (low)

Both setters assign only when `fn != NULL` (stream has no setter; circular at
250-258). Calling `SetDestructor(cb,NULL)` therefore acts as a query rather
than disabling destruction, unlike the natural setter contract. Either make
NULL assignment clear the callback or document/query this behavior uniformly
across all containers; tests should lock the chosen compatibility rule.

## Historical coverage baseline and current coverage

Only `tests/test.c` lines 591-608 historically exercised stream Create, several
writes, position reset, direct data access, Size, and Finalize. It had no
assertions and was not a dedicated CTest suite. The current
`unittests/buffer_test.c` is registered as `test_buffer` and covers circular
buffers, file I/O, resize, allocation failures, error paths, wraparound, and
destructors.

## Required test matrix

1. Stream create defaults/explicit sizes, zero initialization, exact-boundary
   and growth writes, seek/clamp, partial/end reads, clear, and stable direct
   data access.
2. Regression B1 with shrink below/equal/above cursor, followed by read/write,
   under ASan; test `Resize(0)` and failed realloc without losing ownership.
3. Checked-arithmetic cases for enormous writes and circular dimensions; a
   failing/counting allocator must cover object allocation, data allocation,
   realloc failure, cleanup, and allocator retention after global changes.
4. `tmpfile` round trips and short reads for stream file APIs; named empty and
   binary files for `CreateFromFile`; NULL and failing seek/tell/read paths.
5. Circular FIFO behavior before capacity, at capacity, and through several
   wrap cycles. Regress B2 with capacity one and three and verify return codes,
   size, peek, optional-output pop, and exact retained sequence.
6. Install a destructor that records slot values; test overwrite, pop, clear,
   wrapped clear, empty clear, and finalize. Regress B3 under ASan and assert
   each live value is destroyed exactly once.
7. BADARG table for both interfaces and setter old/new/NULL behavior.

This covers every entry, allocation and file branch, growth/shrink state,
empty/full/wrapped ring transitions, and destructor loop, sufficient for the
80% line/70% branch gate. Run ASan/UBSan and leak checks.

## Luna handoff

Fix B1 and B3 first because they are direct memory-safety failures, then B2
ring state, B4 checked arithmetic/zero resize, and B5-B7 validation and API
consistency. Keep `Size`'s existing capacity meaning for stream buffers unless
an API decision explicitly introduces a separate written length.

## Implementation status (2026-08-08)

The audited defects are now covered in `src/buffer.c`:

- Stream growth and resize use checked capacity arithmetic, clamp the cursor
  after shrinking, and handle a zero-size resize without `realloc(data, 0)`.
- Stream creation, file loading, writes, and file transfer reject invalid
  pointers; file sizing keeps the `long` result from `ftell` until it has been
  validated.
- Circular insertion advances the tail on overwrite and destroys the replaced
  item. Pop, wrapped clear, and finalization destroy each live item exactly
  once. Destructor setters now accept `NULL` to disable callbacks.
- Circular allocation dimensions are checked for multiplication overflow, and
  `Sizeof` reports the fixed object plus allocated ring storage.

`unittests/buffer_test.c` exercises FIFO wrap/overwrite, destructor ordering,
stream shrink/zero resize, allocation failures, file round trips, and BADARG
paths. Direct ASan/UBSan execution passes with `detect_leaks=0`; LeakSanitizer
cannot initialize in this container because the runner is under ptrace. The
gcov gate reports 257/282 lines (91.13%) and 132/162 taken branches (81.48%),
above the required 80%/70% thresholds.

## Current local integration verification

The current unsanitized CTest run passes all 36 registered tests. The focused
ASan/UBSan run passes with `detect_leaks=0`; LeakSanitizer cannot initialize in
this ptrace-restricted workspace, so no local LSan pass is claimed.
