# `observer.c` audit

## Scope and dependencies

- Physical source: `src/observer.c`.
- Public surface: global `ObserverInterface iObserver` with `Subscribe`,
  `Notify`, and `Unsubscribe`.
- Dependencies: generic container prefix (`Flags` field), `iError`, libc
  `calloc`/`realloc`, and callbacks invoked synchronously by container code.
- Process-global state: a sparse `ObserverVector` in chunks of 25 and `vsize`.
  Slots with `ObservedObject == NULL` are free.

## API and invariant inventory

`Subscribe(object, callback, operations)` sets the subject's
`CONTAINER_HAS_OBSERVER` flag, allocates/grows the table, and inserts one
relationship. Duplicate relationships are currently allowed. `Notify` builds
a two-entry `ExtraInfo` array and synchronously invokes each relationship for
the exact object whose flag mask intersects `operation`; its return is the
number called. `Unsubscribe` supports three selectors: NULL object removes all
entries for a callback, NULL callback removes all entries for an object, and
both non-NULL remove exact pairs; both NULL is a no-op.

The documented preconditions are non-NULL object, non-NULL callback, and
nonzero operations for Subscribe; non-NULL object and nonzero operation for
Notify. The vector must remain zero-filled outside active records. Failed
subscription must not create a relationship or observably modify the subject.

## Historical pre-fix defects (confirmed at the audit baseline)

### O1 — `Subscribe(NULL, ...)` dereferences NULL before validation (critical)

Lines 17-21 cast and read `gen->Flags` without any argument checks. This
violates the documented `CONTAINER_ERROR_BADARG` result and is a direct
ASan/UBSan crash.

### O2 — NULL callback and zero-operation subscriptions are accepted (high)

The documented BADARG inputs are stored at lines 47-49. A NULL callback with a
matching nonzero flag later causes an indirect call through NULL at line 65.
A zero mask consumes a permanent table slot and sets the subject observer flag
despite being incapable of notification. Validate all three inputs before
touching the object or table.

### O3 — `Notify` fails its documented BADARG contract (medium/high)

NULL object or zero operation should return `CONTAINER_ERROR_BADARG` and raise
an error. Lines 54-70 instead scan and normally return zero. A NULL object can
also match no active slot only because free slots have NULL object but zero
flags; relying on that representation is not validation.

### O4 — Failed subscription leaves `CONTAINER_HAS_OBSERVER` set (medium)

Lines 20-21 modify the subject before initial allocation or growth. If either
allocation fails, the function returns `CONTAINER_ERROR_NOMEMORY` but
`iGeneric.GetFlags` observes a changed object with no new relationship. Move
the flag update after successful insertion (or roll it back). This is
deterministic with wrapped allocation failure.

### O5 — Observer allocation bypasses the library allocator (medium)

Lines 23 and 37 use libc directly rather than `CurrentAllocator`. This makes
the public library's allocator selection ineffective for observer state,
prevents deterministic library-level OOM testing, and can mismatch projects
that require all library allocations to use a supplied allocator. Other global
services such as error registration use `CurrentAllocator`. Store the
allocator used by the vector or adopt a clear process-global allocator policy;
do not free/realloc memory through a later, different allocator.

The retained `CONTAINER_HAS_OBSERVER` bit after successful unsubscription is a
performance/stale-state issue, but not treated as a confirmed functional defect
because containers merely make an empty `Notify` call.

## Implementation and verification status (2026-08-08)

`src/observer.c` now validates every documented `Subscribe` and `Notify`
precondition before dereferencing arguments, commits the observer relationship
before setting `CONTAINER_HAS_OBSERVER`, and reports allocation failures without
changing the subject or existing table entries. Observer storage uses the
allocator active at first successful allocation and retains that allocator for
all later growth. Grown chunks are fully zeroed, and notification captures its
initial table extent before invoking callbacks.

`unittests/observer_test.c` covers invalid arguments and error dispatch,
initial and growth allocation failures, exact callback/filter/extra-info
delivery, duplicate registrations, all unsubscribe selectors, 25-slot growth
and reuse, callback self/other removal, bounded callback subscription during
notification, and List Add/Clear/Finalize integration. The isolated GCC run
reports 93.67% line coverage and 90.00% taken-branch coverage for
`src/observer.c`. ASan/UBSan passes with leak detection disabled; LeakSanitizer
cannot initialize in this workspace because the host ptrace policy causes its
fatal startup diagnostic.

## Current coverage

`unittests/observer_test.c` is the dedicated observer suite and is
auto-discovered by `unittests/CMakeLists.txt`. It directly covers callbacks,
filters, removal modes, growth/reuse, errors, allocation behavior, reentrant
notification, and List integration; container-family suites also exercise
observer integration independently. The normal CTest run passes
`test_observer`. The preceding paragraph describes only the historical
pre-suite baseline.

## Required test matrix

1. Subscribe one callback, verify subject flag, notify matching and
   nonmatching operations, exact object/operation/ExtraInfo values, and count.
2. Multiple callbacks, duplicate registrations, combined masks, and distinct
   observed objects; verify stable insertion/slot order only if chosen as part
   of the contract.
3. Exercise all three Unsubscribe selectors, multiple removals, no match, both
   NULL, and slot reuse after removal.
4. Fill exactly 25 slots, grow to 50, verify the boundary slot and zeroed tail,
   remove/reuse first/middle/last slots, then notify all expected callbacks.
5. Subscribe invalid object/callback/mask independently and assert BADARG,
   captured error, no allocation, no flag mutation, and no sanitizer failure
   (O1/O2).
6. Notify NULL object and zero operation independently and assert BADARG and no
   callback (O3).
7. Fail initial allocation and first growth deterministically; assert NOMEMORY,
   prior subscriptions intact, and subject flags unchanged for the failed
   relationship (O4/O5).
8. Callback unsubscribes itself and another callback during notification;
   document and assert the chosen iteration semantics. Callback subscription
   during notification should likewise be bounded to the notification's
   starting table extent to avoid accidental reentrant growth loops.
9. Container integration: observe Add/Clear/Finalize on one representative
   List or Vector and verify callbacks occur at the documented before/after
   state boundaries.

Because state is process-global with no reset API, each test must unsubscribe
everything it installs. Allocation-boundary tests may need a test-only reset or
must run in isolated executables.

## Implementation handoff

Add contract validation before the first dereference, then make insertion
transactional: allocate/find the slot, populate it, and only then set the
subject flag. Route storage through a consistent allocator and capture the
notification scan boundary before invoking callbacks. Run invalid-input and
growth/OOM tests under ASan/UBSan before container integration tests.

## Final integration verification

LeakSanitizer cannot initialize in the current local workspace because of its
ptrace restriction, so a current local ASan/UBSan+LSan integration pass cannot
be claimed. The earlier untraced pass is historical campaign evidence recorded
on 2026-08-08 and does not supersede the current focused-run limitation.
