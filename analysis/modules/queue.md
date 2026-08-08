# `queue.c` audit

## Scope and dependencies

- Physical source: `src/queue.c`; public surface is `QueueInterface iQueue` in
  `include/containers.h`.
- This is a thin FIFO owner around a `List`. It depends on `iList`, `iError`,
  `CurrentAllocator`, and the allocator retained by the underlying list.
- Invariant: a live queue has `VTable == &iQueue`, a non-NULL live `Items`, and
  the queue control block and list are owned by the same allocator. Queue size,
  front, and back are exactly the corresponding list state.

## Function and contract inventory

| Entry | Contract/invariants |
| --- | --- |
| `Create`, `CreateWithAllocator` | Allocate the wrapper, create its list with the same element size/allocator, clean up a partial failure, and install the vtable. |
| `Size` | Return the number of queued elements. |
| `Sizeof` | Return wrapper plus underlying-list footprint; NULL reports static wrapper size. |
| `Enqueue` | Append one copied element through `iList.Add`. |
| `Dequeue` | Remove/copy the oldest element through `iList.PopFront`. |
| `Front`, `Back` | Copy, without removal, the oldest/newest item; return 0 when empty. |
| `Clear` | Remove all underlying items while retaining the queue. |
| `Finalize` | Capture the list allocator, finalize the list, then free the wrapper with that allocator. |
| `GetData` | Return the owned underlying list for integration/advanced operations. |

## Confirmed defects

### Q1 - most entry points crash for a NULL queue (medium)

Only `Front`, `Back`, `GetData`, and the special `Sizeof(NULL)` case validate
the queue. `Size`, `Enqueue`, `Dequeue`, `Clear`, and `Finalize` dereference
`Q->Items` immediately (lines 10-42). The surrounding container API normally
returns/raises BADARG for NULL public objects, so this is an inconsistent and
unsafe public surface. Add guards with correct operation names while
preserving `Sizeof(NULL)`; decide and test whether `Size(NULL)` returns zero or
BADARG consistently with other `Size` APIs.

### Q2 - `CreateWithAllocator` dereferences a NULL allocator (medium)

Line 48 calls `allocator->malloc` without validating or defaulting it. Either
reject NULL with BADARG or treat it as `CurrentAllocator`; the latter matches
several allocator-aware constructors. This must be an explicit consistent
choice, not a crash.

### Q3 - `Back(NULL,...)` reports the wrong operation name (low)

Its error path says `iQueue.Front` at line 82. Use `iQueue.Back` so captured
diagnostics identify the failing call.

No FIFO defect was found for valid queue/list objects; implementation work is
primarily validation and direct coverage. The exposed `GetData` can be used to
mutate or even finalize the list, so callers must treat the returned pointer as
borrowed. That is an API hazard, not a source fix without a compatibility
decision.

## Existing coverage

No source in `tests/` or `unittests/` calls `iQueue`. All behavior is currently
covered, if at all, only indirectly through `iList` tests.

## Required test matrix

1. Create an integer queue; assert empty Size/Front/Back/Dequeue, then enqueue
   several values and verify FIFO Dequeue plus unchanged Front/Back peeks after
   each transition.
2. Clear empty/nonempty queues, reuse after clear, and finalize; compare
   `Sizeof` growth and `Sizeof(NULL)` behavior.
3. Verify copied-value semantics by mutating caller input after Enqueue.
4. Through `GetData`, assert identity and list ordering without transferring
   ownership; cover `GetData(NULL)`.
5. Counting/failing allocator: wrapper failure, list-creation failure with
   wrapper cleanup, success with balanced finalize, and ownership after
   changing `CurrentAllocator`.
6. BADARG matrix for every queue-taking entry and NULL element/output pointers;
   verify Q3's captured operation name. Underlying list return codes should be
   forwarded unchanged.
7. Run randomized model-based sequences of enqueue/dequeue/clear/front/back
   against a small reference array to exercise state combinations.

These tests cover every queue line/branch; list internals remain charged to the
list unit. Run ASan/UBSan and leak checks.

## Luna handoff

Implement Q1-Q3 as a small validation patch and add the dedicated wrapper
suite. Coordinate the NULL-allocator rule with other `CreateWithAllocator`
units; do not change `GetData` ownership or FIFO semantics.

## Implementation status (2026-08-08)

Q1-Q3 are implemented in `src/queue.c`. NULL queue operations now report
BADARG with operation-specific names (while `Sizeof(NULL)` remains the static
wrapper size), `CreateWithAllocator` defaults a NULL allocator to
`CurrentAllocator` and rejects an unusable allocator, and `Back` reports its
own operation name. `unittests/queue_test.c` covers FIFO/copy semantics,
clear/reuse, list views, allocator rollback and ownership, the BADARG matrix,
and randomized model sequences.

The focused GCC/gcov run reports 63/63 lines (100.00%) and 33/34 branches
(97.06%) for `src/queue.c`. The dedicated suite passes with ASan+UBSan when
leak detection is disabled; this environment's leak-enabled run cannot attach
to the test process under its ptrace restriction, so LeakSanitizer aborts
before reporting allocations.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
