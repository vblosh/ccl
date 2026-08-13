# `error.c` audit

## Scope and dependencies

- Physical source: `src/error.c`.
- Public surface: the global `ErrorInterface iError` declared in
  `include/containers.h`.
- Runtime dependencies: `CurrentAllocator` from `memorymanager.c`; C library
  `strlen`, `strcpy`, and `fprintf`; every module that calls `iError`.
- Mutable process-global state: `iError.RaiseError` and the singly linked
  `UserErrorMessages` list. The implementation has no teardown and is not
  thread-safe; those are existing design properties, not findings by
  themselves.

## API and invariant inventory

| API | Behavior and invariants |
| --- | --- |
| `RaiseError(fname, code, ...)` | Default handler resolves `code`, writes one diagnostic to `stderr`, and returns `NULL`. Extra variadic arguments are ignored. |
| `EmptyErrorFunction(...)` | No-op handler; always returns `NULL`. |
| `StrError(code)` | Searches user registrations newest-first, then the built-in sentinel-terminated table; unknown codes return the sentinel text `"Unknown error"`. Returned storage is library-owned. |
| `SetErrorFunction(fn)` | Returns the previous handler. A non-NULL argument replaces the global handler; NULL is a query. |
| `NullPtrError(fname)` | Calls the current global handler with `CONTAINER_ERROR_BADARG`, then returns that code. The implementation names this helper `BadArgError`. |
| `AddError(code, message)` | Allocates a list node and a private message copy with the current allocator; prepends it; returns 1 or `CONTAINER_ERROR_NOMEMORY`. Newer duplicate codes shadow older entries and built-ins. |

The built-in table must cover the public error codes emitted by library code,
and its final `{0, "Unknown error"}` record must remain the sentinel. A failed
`AddError` must not link a partial node. `SetErrorFunction(NULL)` must not
mutate the handler.

## Historical pre-fix defects

The E1-E2 findings below are the pre-fix audit baseline and are retained as
historical evidence. The current direct test coverage is described below.

### E1 — Three live public error codes stringify as `"Unknown error"` (medium)

`ErrorMessagesTable` at lines 6-27 omits:

- `CONTAINER_ERROR_NOTFOUND` (`-2`), returned throughout list, dictionary,
  suffix-tree, vector, bit-string, and string-collection code;
- `CONTAINER_ERROR_DIVISION_BY_ZERO` (`-21`), raised by ValArray arithmetic;
- `CONTAINER_ERROR_WRONG_ITERATOR` (`-24`), raised by iterator validation in
  list, dlist, vector, heap, string-list, and ValArray code.

These constants are public in `containers.h` and are actively used, so the
documented conversion API produces a false fallback message for real library
errors. A table-driven test of every public code reproduces the mismatch
without undefined behavior.

### E2 — `AddError(code, NULL)` dereferences NULL (medium)

Line 42 calls `strlen(message)` without validating `message`. This is a public
interface entry, and all other bad pointer inputs in this interface have a
defined `CONTAINER_ERROR_BADARG` path. Under ASan/UBSan, a direct call with a
NULL message faults instead of returning an error and preserving the registry.
The fix should raise/return `CONTAINER_ERROR_BADARG` before allocating. A code
policy check (for example requiring a user-code range) is not claimed because
the header does not currently encode that policy.

## Historical coverage baseline and current coverage

Before the dedicated suite, no test called `iError` directly; other tests only
exercised the default handler incidentally and did not assert message mapping,
handler replacement, allocation failure, or registration precedence. The current
`unittests/error_test.c` is registered as `test_error` and directly covers those
paths. The current coverage checker reports 100% line and branch coverage for
`src/error.c`.

## Required test matrix

1. Table-test every public built-in error constant and the unknown sentinel;
   explicitly regress E1.
2. Verify `SetErrorFunction(custom)`, query via NULL, restoration, return of the
   prior pointer, and actual dispatch through `NullPtrError`.
3. Capture `stderr` for the default handler and assert resolved message and
   function name; test `EmptyErrorFunction` separately.
4. Add a user error and prove the message was copied by modifying the caller's
   buffer afterward.
5. Register duplicate user codes and a built-in code and verify newest-first
   shadowing.
6. Use a deterministic allocator that fails the node allocation and then the
   message allocation; assert `CONTAINER_ERROR_NOMEMORY`, no partial
   registration, and the correct free count.
7. Pass a NULL message and assert E2's `BADARG` behavior under sanitizers.
8. Restore `CurrentAllocator` and `iError.RaiseError` in fixture cleanup so
   this process-global suite cannot contaminate other tests.

Target coverage: all six interface entries, 100% of table lookup outcomes,
both allocation failures, and both `SetErrorFunction` branches. The permanent
global registration list means tests should use unique codes rather than
assuming isolation by deletion.

## Implementation handoff

Add the three missing table rows, then guard `message == NULL` before the first
allocation. Preserve the existing API signatures and newest-registration-wins
behavior. Run this unit before modules whose assertions depend on accurate
error text.
