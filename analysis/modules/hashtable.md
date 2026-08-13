# `hashtable.c` audit

## Scope and dependencies

- Physical source: `src/hashtable.c`; public surface is
  `HashTableInterface iHashTable` in `include/containers.h`; `HashTable`,
  `HashEntry`, `HashIndex`, and iterator representations are in
  `include/ccl_internal.h`.
- Dependencies: `iPool` for table/entry lifetime, `iError`, C allocation for
  load scratch buffers, C streams, and caller-owned key storage. The current
  design stores key pointers rather than copying keys, so a key must remain
  byte-for-byte valid for the lifetime of its entry.
- Core invariants: `max` is a `2^n-1` bucket mask; every live entry occurs in
  exactly one bucket selected by `hash & max`; `count` equals live entries;
  values have `ElementSize` writable bytes; the free list contains only
  removed entries of a compatible size; structural mutations invalidate
  iterators; and all constructed/copied/loaded/merged tables retain a valid
  vtable, allocator/error policy, hash function, and element size.

## Function and contract inventory

| Area | Functions and behavior |
| --- | --- |
| Construction/metadata | `Create`, unimplemented `Init`, `Finalize`, `Size`, `Sizeof`, `GetElementSize`, flags, error/destructor/hash setters. |
| Lookup/mutation | `DefaultHashFunction`, `find_entry`, `Add`, `GetElement`, `Contains`, `Replace`, `HashSet`, `Remove`, `Clear`, `Resize`. |
| Bulk operations | `Copy`, `Overlay`, `Merge`, callback-driven `Search` and `Apply`. |
| Persistence | ULEB128 key-length helpers, default value callbacks, `Save`, and `Load`. |
| Iteration | bucket cursor `first`/`next`; first/next/current/replace; allocated and caller-buffer iterator construction/deletion. Reverse traversal is intentionally aliased to forward traversal. |

## Historical pre-fix defects

The HT1-HT9 findings below are the pre-fix audit baseline and are retained as
historical evidence. The implementation status later in this document records
the current source and test state.

### HT1 - missing lookup dereferences a null entry and returns a bogus pointer
(critical)

`find_entry(..., val == NULL)` returns the address of the bucket/link even when
`*link == NULL`. `GetElement` tests the link pointer rather than `*link`, then
evaluates `(*v)->val` at line 277. UBSan reports member access through a null
`HashEntry`; on this platform the result is the non-NULL address `0x20`, so
`Contains` can report absent keys as present and a caller dereference crashes.

Reproducer: create an `int` table and call `GetElement(table, "missing", 7)`.
Return NULL unless the located entry itself is non-NULL.

### HT2 - entries do not reserve `ElementSize` value storage (critical)

`find_entry` allocates only `sizeof(HashEntry)` but copies `ElementSize` bytes
to the trailing `val[1]`. Padding happens to hide writes of at most eight bytes
on the audited ABI; larger values overwrite neighboring pool storage. Allocate
`offsetof(HashEntry, val) + ElementSize` with checked arithmetic. A recycled
entry is safe only because a table has one immutable element size.

### HT3 - `Resize` uses an uninitialized iterator and corrupts bucket storage
(critical)

The loop calls `first(&hi)` without initializing `hi.ht`. Even resizing an
empty table deterministically crashes in `next` at line 697 under ASan/UBSan.
Beyond that first failure, the function calculates `% new_max` although
`new_max` is a mask, treats the bucket-pointer array as packed entry storage,
writes an entry over it, assigns `next` one record past that location, and
never installs bucket heads. Rebuild by allocating exactly `new_max + 1`
bucket pointers, walking the old buckets, and relinking each existing entry by
`hash & new_max`; accept only valid mask sizes (or normalize the requested
bucket count) and preserve the old state on allocation failure.

### HT4 - constructed iterators cannot work and allocated iterators leak
(critical)

`Create` never initializes `Allocator`, so `NewIterator` dereferences NULL.
Both `NewIterator` and `InitIterator` also set `result->ht` but never
`result->hi.ht`; first traversal therefore dereferences an uninitialized table
pointer. `DeleteIterator` is a no-op, leaking every allocated iterator, while
`GetCurrent` dereferences `Current` before first/after end. Initialize the full
cursor, distinguish owned from caller-buffer iterators, and make current-state
queries safe.

### HT5 - copied and merged tables have incomplete headers (critical)

`Copy` does not initialize `VTable`, `ElementSize`, `Flags`, `RaiseError`,
`Allocator`, timestamp, or destructor. `Merge` similarly omits the vtable,
element size, flags/error/allocator/destructor and allocates its array while
`res->ElementSize` is indeterminate. Empty+empty merge returns NULL because
`new_vals` intentionally remains NULL but is treated as allocation failure.
Allocation results are also unchecked. Tests must use every public operation
on results, not merely inspect copied values.

### HT6 - loaded keys are all dangling (critical)

`Load` repeatedly passes the same `keybuf` to `Add`. Entries retain that
pointer; the buffer is reused for subsequent keys and freed before return.
Thus all loaded keys alias overwritten, freed storage. A lookup on a loaded
nonempty table is a deterministic use-after-free candidate. Allocate durable
key bytes from the result's pool for each record. On any truncated record the
current code raises FILE_READ but returns a partial table; fail atomically and
return NULL after finalizing the partial result.

### HT7 - removal result, mutation state, and destructor argument are wrong
(high)

`Remove` returns 1 whenever `HashSet` returns a link address, including an
absent key. Neither add/remove/clear/replace/resize increments `timestamp`, so
iterator invalidation cannot work. Mutations also ignore `CONTAINER_READONLY`.
When removing/clearing, the destructor receives the `HashEntry *`, not the
stored value pointer used by container destructor contracts. Return 1 only
for an actual removal, pass `old->val`, and update timestamp exactly once per
successful structural/content mutation.

### HT8 - private replacement path copies the wrong size from the wrong base
(high)

For an existing entry and non-NULL value, `HashSet` copies
`sizeof(HashEntry) + ElementSize` bytes from the caller's value pointer into
`entry->val`. This reads beyond the input and overwrites beyond value storage.
The current public path uses separate `Replace`, but leaving this helper
latent is unsafe. Copy exactly `ElementSize` or remove the unused branch.

### HT9 - public argument/allocation and persistence validation is incomplete
(high)

`Create` dereferences failed pool/header allocations; `GetElement`, `Replace`,
`Remove`, `Clear`, `Finalize`, metadata accessors, callbacks, and `Load(NULL)`
have missing checks. `Add` accepts a NULL value and then copies it. `Load`
trusts raw serialized header sizes/counts, does not check scratch allocations,
accepts short key reads because it tests only `fread(...) == 0`, and ULEB128
decoding shifts an `int` beyond its width for long encodings. Validate all
inputs, exact read lengths, arithmetic, and encoded length termination before
allocating. Preserve the existing on-disk format unless a versioned format is
introduced separately.

## Historical coverage baseline and current coverage

Before the dedicated suite, documentation examples did not exercise this
implementation; the sanitizer reproducers above established HT1 and HT3, with
source inspection establishing the remaining pre-fix defects. The current
`unittests/hashtable_test.c` is registered as `test_hashtable` and covers lookup,
storage sizing, resize, iterators, copy/merge, persistence, ownership, and
argument validation.

## Required test matrix

1. Empty table metadata; add/get/contains/replace/remove for one and many
   binary keys, embedded NULs, hash collisions, duplicate add, and missing
   keys. Regress HT1 and exact remove results.
2. Values sized 1, 8, 9, 32, and an over-aligned payload; churn add/remove/reuse
   under ASan/UBSan to expose HT2 and free-list corruption.
3. Custom hash forcing one bucket, then default/explicit growth across the
   threshold and explicit valid resize sizes; verify every key before/after,
   plus failed-allocation rollback. Regress empty resize (HT3).
4. NewIterator and buffer iterator over empty/nonempty tables: first/next,
   forward alias, current before/after range, replace/delete, mutation
   invalidation, readonly rejection, and correct ownership/deletion (HT4).
5. Copy into same and separate pools; overlay/merge for empty, disjoint, and
   overlapping inputs, with and without merger callback. Exercise every public
   method and finalize results; verify source independence and headers (HT5).
6. Save/load empty and populated tables with binary/large keys and custom
   value callbacks. After load, overwrite allocator scratch aggressively and
   look up every key (HT6). Cover bad GUID, null/truncated stream at every
   field, malformed/unterminated/overflowing ULEB128, short reads, allocation
   failures, and atomic cleanup.
7. Counting destructor verifies exactly-once calls with `val` for remove,
   clear, and finalize. Error callback tests every bad argument and allocation
   failure. Readonly and timestamps cover all mutators.
8. Search/Apply on empty and populated tables, full scan and early stop; NULL
   callbacks must fail predictably.

This matrix drives every public entry and all collision/iteration/persistence
engines and should exceed 80% line/70% branch coverage. Run separate
ASan/UBSan and leak-enabled builds.

## Compatibility-preserving handoff

Fix HT1-HT4 first: they make elementary lookup, nontrivial values, resizing,
and iteration unsafe. Then make Copy/Merge/Load construct fully valid tables
and give loaded keys pool lifetime. Finally normalize removal/destructor/
timestamp/readonly behavior and harden persistence. Keep borrowed-key behavior
for ordinary `Add` (it is an observable API ownership rule), but make `Load`
own the key copies it necessarily creates. Do not change public signatures or
the serialized format in this pass.

## Implementation status (2026-08-08)

The hashtable unit now has a dedicated `unittests/hashtable_test.c` suite.
The implementation fixes HT1-HT8 and the persistence/argument-validation
parts of HT9 while retaining borrowed keys for ordinary `Add` calls. Entry
storage is sized from `offsetof(HashEntry, val)`, contiguous copied/merged
entries are alignment-padded, resize rebuilds and relinks bucket heads using
the mask, and mutation timestamps/readonly checks are applied to mutators.
Iterators initialize their complete cursor, invalidate safely after changes,
and release allocated iterator storage. `Copy`, `Merge`, and empty merges
initialize complete table headers. `Load` copies serialized keys into the
result pool and fails atomically on short/malformed records.

The focused suite passes under ASan/UBSan with leak detection disabled. The
current GCC/gcov checker reports 85.41% line coverage and 69.88% taken branches
for `src/hashtable.c`, so the branch gate is not met. LeakSanitizer remains
subject to this environment's ptrace restriction.

## Current local integration verification

The current unsanitized CTest run passes all 36 registered tests. No local LSan
pass is claimed because LeakSanitizer cannot initialize under the workspace's
ptrace restriction.
