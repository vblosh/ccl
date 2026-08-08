# `fgetline.c` audit

## Scope and dependencies

- Physical source: `src/fgetline.c`.
- Internal exported functions: `GetLine` and `WGetLine`, declared in
  `include/ccl_internal.h`; `GetDelim` and `WGetDelim` are private engines.
- Consumers: narrow and wide `strcollectiongen.c` instantiations via the
  `GETLINE` macro, especially `CreateFromFile`.
- Runtime dependencies: caller-supplied `ContainerAllocator`, `iError`, and C
  stream functions `fgetc`/`getwc`.

## Function and invariant inventory

Both functions read through but do not include a newline, append a terminator,
and return the number of stored elements. EOF before any element returns EOF;
EOF after a partial final line returns that line's length. `*n` is capacity in
elements, not bytes. A NULL buffer or zero capacity causes an initial BUFSIZ
allocation; growth doubles capacity. All allocation and release operations
must use the supplied allocator. On the current allocation-failure path the
buffer is freed and `*LinePointer` is set to NULL.

`GetLine` is the narrow wrapper around `GetDelim(..., '\n', ...)`.
`WGetLine` is the wide wrapper around `WGetDelim(..., L'\n', ...)`.

## Confirmed defects

### F1 — Exact-capacity wide final line causes heap-buffer-overflow (critical)

When a wide line ends at EOF with exactly `*n` characters, lines 127-140 grow
space for the terminator. Line 129 requests `*n + 1` **bytes**, while `*n` is a
count of `wchar_t` elements. The realloc commonly shrinks an initial
`BUFSIZ * sizeof(wchar_t)` allocation to `BUFSIZ + 1` bytes; line 141 then
writes at wide index `BUFSIZ`, far beyond the allocation. An input containing
exactly BUFSIZ wide characters and no newline is a deterministic ASan
reproducer. The requested size must be `(*n + 1) * sizeof(wchar_t)`, with
checked arithmetic.

### F2 — Wide EOF is tested with the wrong type and constant (high,
portability)

`getwc` returns `wint_t` and signals `WEOF`. Lines 83, 105, and 124 instead use
`int` and compare with `EOF`. C does not require `WEOF == EOF` or that `wint_t`
fits in `int`. On implementations where they differ, end-of-file is consumed
as data repeatedly, causing unbounded growth or incorrect output. Use `wint_t
c`, a `wint_t` delimiter, and compare with `WEOF`.

### F3 — NULL allocator is dereferenced instead of rejected (high)

Neither validator at lines 15-19 or 86-90 checks `mm`. A valid empty input
state immediately calls `mm->realloc` at line 22 or 93. Both exported internal
functions therefore crash on a NULL allocator rather than return
`CONTAINER_ERROR_BADARG`. This is directly sanitizer-reproducible.

### F4 — Negative capacities bypass validation and lead to invalid writes or
oversized allocation (high)

The capacity is an `int`, but validation rejects only the combination of a
non-NULL buffer and zero capacity. With `*LinePointer == NULL` and `*n < 0`,
line 21 takes the `else` branch, leaves `p == NULL`, and the first character is
written through NULL. With a non-NULL buffer, `len >= *n` is immediately true
and signed negative sizes are converted to huge `size_t` allocation requests.
`*n <= 0` is valid only for the conventional empty state where capacity is
zero; negative values must return `CONTAINER_ERROR_BADARG`.

## Existing coverage

There is no direct unit test. Collection file-loading tests may transitively
read ordinary short narrow lines, but no test targets wide input, boundary
growth, EOF-without-newline, caller buffers, invalid arguments, or allocator
failure. The disabled `#ifdef TEST` main is narrow-only, uses an obsolete
four-argument call, and is not part of CMake.

## Required test matrix

Run narrow and wide cases symmetrically unless specifically marked:

1. Empty file returns EOF and leaves a valid allocated buffer state.
2. Empty line, one-character line, normal newline-terminated line, and final
   line without newline; verify returned length and terminator.
3. Embedded NUL input: verify returned element count separately from C-string
   interpretation.
4. Reuse a caller buffer with sufficient capacity and assert no realloc.
5. Cross BUFSIZ to force one and multiple growths; assert content and capacity.
6. Exact BUFSIZ characters followed by EOF for narrow and wide; the wide case
   is the F1 sanitizer regression.
7. Exact BUFSIZ characters followed by newline (growth-before-delimiter path).
8. Deterministically fail initial realloc, doubling realloc, and final
   terminator realloc; verify error callback, return code, free behavior, and
   pointer state.
9. Every invalid argument independently: stream, line-pointer pointer,
   capacity pointer, allocator, non-NULL buffer with zero capacity, and
   negative capacity; regress F3/F4.
10. Wide non-ASCII content under a known UTF-8 locale, and a platform-guarded
    assertion that EOF handling uses `WEOF`/`wint_t` (F2).
11. Stream read error (for example a read side deliberately failed via a
    cookie stream where available) to document the present EOF-equivalent
    result; do not change that behavior without a contract decision.

Use a counting allocator and temporary streams. The target is full branch
coverage except impractical `INT_MAX`/allocation-overflow paths, which should
instead be guarded by checked arithmetic and tested with boundary capacities.

## Implementation handoff

Fix F1 first and run the exact-capacity wide test under ASan/UBSan. Then correct
the wide character types/constants and centralize argument/capacity validation
for both engines. Preserve newline exclusion and partial-final-line semantics,
because `CreateFromFile` relies on them.

## Implementation and test evidence

The confirmed defects are fixed in `src/fgetline.c`:

- Both entry points now reject a NULL allocator and negative capacities before
  dereferencing either argument.
- Wide input uses `wint_t`, `WEOF`, and a `wint_t` delimiter.
- Wide growth and terminator allocations use element counts multiplied by
  `sizeof(wchar_t)`, with checked capacity and byte-size arithmetic. Narrow
  capacity doubling and terminator growth are also guarded against `int`
  overflow.
- Existing partial-final-line, delimiter exclusion, allocator ownership, and
  allocation-failure cleanup behavior are preserved.

`unittests/fgetline_test.c` exports `ccl_get_test_suite()` and covers narrow
and wide empty/basic lines, embedded NULs, caller-owned buffers, repeated
growth, exact `BUFSIZ` EOF and delimiter boundaries, initial/doubling/final
allocation failures, all documented invalid argument classes, and UTF-8 wide
content.

Ad-hoc GCC sanitizer verification (ASan + UBSan, leak detection disabled by
the execution environment's ptrace restriction) passed all 12 cases. The same
suite passed an unsanitized run. GCC gcov measured 90.76% line coverage and
80.43% of branches taken at least once (100% of branches instrumented),
exceeding the unit gates of 80% and 70%.

LeakSanitizer itself could not attach in this environment (`LeakSanitizer has
encountered a fatal error`; it reports that it does not work under ptrace).

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
