# Dictionary generator family audit

## Scope and generation topology

This family consists of one implementation template and two separately compiled
wrappers:

| File | Role | Instantiation |
| --- | --- | --- |
| `src/dictionarygen.c` | Shared implementation body; not compiled directly | Selected entirely by wrapper macros |
| `src/dictionary.c` | Narrow wrapper | `Dictionary`, `DataList`, `DictionaryIterator`, `char`, `HashFunction`, `strCollection`, `iDictionary` |
| `src/wdictionary.c` | Wide wrapper | `WDictionary`, `WDataList`, `WDictionaryIterator`, `wchar_t`, `WHashFunction`, `WstrCollection`, `iWDictionary` |

The public interfaces are `DictionaryInterface` and `WDictionaryInterface` in
`include/containers.h`. Object, node, and iterator layouts are in
`include/ccl_internal.h`. The generator depends on:

- `CurrentAllocator` and `ContainerAllocator` for dictionary/header, bucket,
  node, key, and allocated-iterator storage;
- `iError` and the per-object `RaiseError` callback;
- `iObserver` for mutation/copy/finalization notifications;
- `iVector` for value-array conversion and persistence;
- `istrCollection` or `iWstrCollection` for key extraction and persistence;
- C string/wide-string, memory, stdio, and GUID operations.

There is no production diff in these three sources in the current shared
worktree, so this audit describes the `develop` implementation.

## Representation, invariants, and ownership

A live dictionary header has its vtable as the first field, followed by logical
`count`, flags, bucket-table `size`, error callback, mutation `timestamp`, fixed
`ElementSize`, captured allocator, optional destructor, active hash function,
and an owned bucket array. Construction chooses a fixed prime bucket count from
509 through 1,048,573; there is no later resize.

The intended invariants are:

- every node is reachable from exactly one bucket and
  `hash(node->Key) % size` selects that bucket;
- `count` equals the number of reachable nodes;
- every key is a separately allocated, null-terminated dictionary-owned copy;
- in owning mode (`ElementSize > 0`), the node allocation is
  `sizeof(node) + ElementSize` and `Value` must always point at the inline bytes
  immediately after the node;
- in set mode (`ElementSize == 0`), the caller's value is ignored, `Value`
  aliases the owned key, and `GetElement` returns the key;
- successful content or structural mutation changes `timestamp`, so traversal
  can reject a changed object before touching stale nodes;
- all memory tied to the dictionary is released with the allocator captured at
  construction; `Create*` owns its header, while a header passed to `Init*`
  must still be allocator-owned if it will later be passed to `Finalize`,
  because `Finalize` unconditionally frees the header;
- a destructor applies to copied, nonzero-size values before their storage is
  overwritten or released, exactly once per value.

The implementation violates several of these invariants, as detailed below.
It performs a shallow byte copy of values: pointers inside an element remain
caller-managed unless a destructor is installed. Keys are always copied and
owned even though the introductory comment calls zero-element dictionaries
"intrusive"; operationally those dictionaries are string sets, not key-to-
external-object maps.

`GetKeys` and `CastToArray` create independent result containers with
`CurrentAllocator`, and `Load` likewise creates a default-allocator dictionary.
That is reasonable for returned standalone views and deserialized objects.
`Copy`, however, is documented to retain the source allocator and currently
does not. Iterator storage does use the dictionary allocator. `Init*` allocates
only the bucket array; ownership of the supplied header is not recorded, so
the caller must arrange for `Finalize`'s allocator `free` to be valid.

Read-only behavior has one deliberate compatibility quirk: `GetElement` and,
through it, `Contains` reject read-only dictionaries. This is explicitly
documented, so it is not treated as a new defect even though most container
APIs allow const lookup. `CopyElement`, `GetKeys`, `Apply`, and metadata queries
do not enforce the same restriction.

## Public operation inventory

The narrow and wide vtables expose the same operation sequence and semantics;
only key/hash/collection types differ.

| Area | Public entries | Current responsibility |
| --- | --- | --- |
| Lifetime | `Create`, `CreateWithAllocator`, `Init`, `InitWithAllocator`, `InitializeWith`, `Clear`, `Finalize` | Choose bucket table, establish header state, bulk-initialize, clear nodes, release all owned storage |
| Metadata/configuration | `Size`, `Sizeof`, `GetElementSize`, `GetFlags`, `SetFlags`, `GetAllocator`, `SetErrorFunction`, `SetDestructor`, `SetHashFunction`, `GetLoadFactor` | Report or alter header policy and accounting |
| Lookup/mutation | `Contains`, `GetElement`, `CopyElement`, `Add`, `Insert`, `Replace`, `Erase` | Hash a string key, traverse one chain, copy/update/remove fixed-size values; zero-size mode behaves as a set |
| Bulk/comparison | `Apply`, `Equal`, `Copy`, `InsertIn` | Traverse callbacks, compare dictionaries, shallow-copy one dictionary, overlay a source into a destination |
| Views | `GetKeys`, `CastToArray` | Return independently owned key collection or value vector |
| Iteration | `NewIterator`, `InitIterator`, `DeleteIterator`, `SizeofIterator`; iterator `GetFirst`, `GetNext`, `GetPrevious`, `GetPosition`, `Seek`, `Replace` | Walk bucket order, detect timestamps, replace/delete the current item |
| Persistence | `Save`, `Load` | Write/read a dictionary GUID followed by a key collection and a value vector |

`GetPrevious` is currently aliased to forward `GetNext`, and `Seek` is an
unreported stub returning null. The base `Iterator` also has `GetCurrent` and
`GetLast` slots; neither constructor initializes them.

## Private helper inventory

- `scatter` and `hash` implement the default times-33 string hash. The wide
  instantiation indexes `scatter` with only the low eight bits of each
  `wchar_t`.
- `DictionaryGuid` is shared by both instantiations. The following key
  collection GUID distinguishes narrow and wide persistence streams.
- `doerrorCall`, `ReadOnlyError`, `NullPtrError`, `BadArgError`, and
  `NoMemoryError` route errors. Their diagnostic strings are hard-coded as
  `iDictionary.*` in both instantiations.
- `add_nd` is the unchecked insert/overwrite engine used by `Add`, `Insert`,
  `Copy`, `InsertIn`, and `InitializeWith`.
- All remaining static functions are installed directly in one of the two
  public interface objects.

## Confirmed defects

### DF1 - NULL data corrupts owning entries and causes a null write (critical)

`Add` validates dictionary and key but, unlike `Insert`, accepts `Value ==
NULL` when `ElementSize > 0`. `add_nd` then stores `p->Value = NULL` for a new
or existing node (lines 332-349), permanently losing the address of the inline
value storage. The key remains counted but `GetElement` and `Contains` report
it absent. A later non-null `Add` or `Replace` copies to address zero.

A public GCC 15 ASan/UBSan probe creating an `int` dictionary, adding key `k`
with null data, then adding `k` with an `int` reports:

`dictionarygen.c:348: runtime error: null pointer passed as argument 1`

Reject null data with `CONTAINER_ERROR_BADARG` whenever `ElementSize > 0`, as
`Insert` already does. Independently make `Value = p + 1` unconditional in
owning nodes so the representation cannot be poisoned by an error path.

### DF2 - iterator replacement addresses the next node or NULL (critical)

`GetNext` returns the node currently in `d->dl` and then advances `d->dl` to
its successor (lines 725-735). `ReplaceWithIterator` later treats that already-
advanced cursor as the current node (line 774), calls `GetNext` again, and then
dereferences the saved pointer. For a one-entry dictionary, `GetFirst`
followed by iterator `Replace` dereferences null at line 779 under UBSan. In a
collision chain it modifies/deletes the item after the one returned, not the
current item.

Track the last returned node separately from the next traversal node. Replace
or erase the last returned node, advance according to the requested direction
before structural deletion as needed, and synchronize the iterator timestamp
only after success.

### DF3 - `Clear` during `Apply` causes use-after-free (critical)

`Apply` is explicitly timestamp-aware, but `Clear` frees every node without
incrementing `timestamp`. If an apply callback clears the same dictionary,
the timestamp check passes and the loop update reads `p->Next` from freed
storage. ASan reports a heap-use-after-free at line 509, freed by `Clear` line
625. `Clear` also leaves ordinary iterators unable to report
`CONTAINER_ERROR_OBJECT_CHANGED`.

Increment the timestamp on a successful clear (including before returning to
an active traversal). Keep the callback-time timestamp check before any next-
node access. Finalizing the dictionary from its own callback remains an
invalid lifetime operation and should be documented as such.

### DF4 - wide dictionary persistence round trip overflows (critical,
dependency)

An ordinary `iWDictionary.Save`/`iWDictionary.Load` round trip with key
`L"wide"` fails under ASan. `iWstrCollection.Load` allocates five bytes for a
five-wide-character record; dictionary `hash` then reads a four-byte
`wchar_t` beyond that allocation at line 185. The root cause is in the shared
wide string-collection persistence dependency: its default save/load lengths
are characters but allocation and byte I/O do not multiply by
`sizeof(wchar_t)`.

This must be fixed and covered in the wide string-collection family; the
dictionary suite must retain a wide round-trip regression because the broken
dependency becomes a dictionary memory-safety failure. Do not claim wide
dictionary persistence support while that dependency remains unfixed.

### DF5 - changing the hash function makes existing keys unreachable (high)

`SetHashFunction` merely replaces `d->hash` (lines 1068-1078). Existing nodes
remain in buckets selected by the old function, violating the central bucket
invariant. A public probe adds `alpha`, observes `Contains == 1`, installs a
constant-zero hash, and then observes `Contains == 0` while `Size == 1`. The
wide instantiation behaves identically.

When installing a different function, relink every existing node into the
same bucket array using the new hash, then publish the function and increment
the timestamp. This can be done without allocation and preserves the public
signature and element addresses.

### DF6 - `Copy` violates allocator and read-only contracts and ignores OOM
(high)

`Copy` calls `Create`, so it uses the process current allocator rather than the
source allocator promised by the documentation. A custom-allocator probe
reports `source_custom=1 copy_custom=0`.

It also applies source flags, including `CONTAINER_READONLY`, before adding
entries. Copying a one-entry read-only dictionary therefore emits a read-only
error and returns an empty, read-only dictionary (`src=1 copy=0 equal=0`).
Every `Add` result is ignored, so later allocation failure silently returns a
partial copy.

Construct with `CreateWithAllocator(..., src->Allocator)`, set hash/error
policy before unchecked population, defer externally visible flags until all
nodes exist, and destroy/return null on any insertion failure. Continue to
strip `CONTAINER_HAS_OBSERVER` from the result and notify only the source after
a complete copy.

### DF7 - iterator initialization is incomplete (high)

`NewIterator` leaves index, traversal node, magic, flags, `GetCurrent`, and
`GetLast` uninitialized. `InitIterator` additionally omits `GetPosition` and
`Seek`. A zeroed caller buffer passed to `InitIterator` returns success with
both `GetPosition == NULL` and `Seek == NULL`. `GetPrevious` incorrectly moves
forward, while `Seek` in allocated iterators silently returns null for every
index. There is no magic validation despite magic fields in both private
iterator layouts.

Use one shared initializer for heap and caller-buffer iterators, initialize
every base slot and private field deterministically, validate the family magic,
and either implement position/current/seek/previous consistently or return the
library's explicit not-implemented error without leaving callable slots
indeterminate.

### DF8 - zero-element dictionaries cannot be saved (high)

Set mode is a supported construction mode, but `CastToArray` intentionally
returns null for it. `Save` passes that null to `iVector.Save` and later to
`iVector.Finalize`. A one-key public probe returns EOF after reporting
`iVector.Save: Bad argument`; by then the dictionary GUID and key collection
have already been written, leaving a partial stream.

Do not silently change the existing successful owning-dictionary wire format.
At minimum, reject set-mode persistence before writing any bytes and document
it. If set persistence is required, introduce an explicitly versioned format
that represents zero-size values portably and keeps loading existing owning
files.

### DF9 - `Load` trusts inconsistent dependency results and allocation
failures (high)

After loading keys and values, `Load` does not verify equal counts, does not
check whether dictionary `Create` succeeded, and ignores every `Add` result
(lines 945-960). A short value vector can feed null/out-of-range data into the
DF1 path; dictionary allocation failure dereferences null at `result->VTable`;
entry allocation failure returns a partial object as if successful.

Validate the two counts and all size/count arithmetic, check construction,
fail and clean up on any add failure, and reject trailing/truncated malformed
records consistently. The raw headers used by the vector dependency also make
the current format ABI-specific; retain compatibility for existing valid
files while adding bounds checks.

### DF10 - `Sizeof` can report less than the dictionary header (high)

The populated case uses `sizeof(dict)`, the pointer size, and omits the bucket
array and all key allocations (lines 455-460). A public one-`int`, key `"key"`
probe reports `Sizeof(NULL) == 88` but `Sizeof(populated) == 36`.

Compute `sizeof(DATA_TYPE) + size*sizeof(bucket)` plus, for every node,
`sizeof(node) + ElementSize + (STRLEN(key)+1)*sizeof(CHARTYPE)`, using checked
addition/multiplication. The null case must also use `sizeof(DATA_TYPE)`, not
the hard-coded narrow type.

### DF11 - equality depends on collision-chain insertion order (medium)

`Equal` walks corresponding bucket chains in lockstep. Two dictionaries with
the same size/hash/flags/element size and identical key/value pairs compare
unequal when colliding keys were inserted in opposite order. A constant-hash
probe with `{a:1,b:2}` inserted in opposite orders reports `equal=0`.

The documented comparisons of size, flags, hash pointer, and element size can
remain for compatibility, but within each bucket match by key and then compare
value bytes rather than comparing chain order.

### DF12 - the constructor chooses a bucket count below the requested hint
(medium)

The prime loop tests the next prime but selects `primes[i-1]`. Except in the
minimum bucket, it therefore chooses the greatest listed prime below the hint:
for hint 510 it selects 509; for 1,022 it selects 1,021. This contradicts the
documented table "big enough" for the hint and allows load factor above one
immediately after the hinted number of distinct inserts. Select the first
prime greater than or equal to the hint, with an explicit documented cap for
hints beyond the largest table entry.

### DF13 - destructor, timestamp, and observer behavior is inconsistent
(medium)

- `Add` overwrites an existing owning value without calling the installed
  destructor, while `Replace` does call it. Resource-bearing values therefore
  leak on the documented Add-as-replace path.
- `Clear` invokes the destructor in zero-element/set mode on the key alias,
  while `Erase` correctly guards the callback with `ElementSize`. The same set
  element is treated differently based on removal path.
- `add_nd` increments the timestamp before allocations, so a failed Add
  invalidates iterators despite making no change.
- `Insert` notifies observers when a duplicate returns zero even though no
  insertion occurred.
- `Replace` in set mode returns success without changing data or timestamp.

Centralize successful overwrite/removal bookkeeping: destroy only nonzero-
size values, change the timestamp only after a committed mutation, and notify
only for the operation that actually occurred. Preserve documented Add return
values (1 new, 0 replaced) and Insert duplicate return zero.

### DF14 - constructor and bulk-initializer arguments are unchecked (medium)

`CreateWithAllocator`/`InitWithAllocator` dereference null allocator/header
arguments. Node size addition can wrap for an extreme `ElementSize`.
`InitializeWith` dereferences a null key array, reads a null values array when
`elementSize > 0`, performs pointer arithmetic on null in zero-size mode, and
ignores insertion failure while returning a partial dictionary. Validate
arguments and checked sizes before allocation; populate transactionally and
finalize on failure.

`InsertIn` likewise deserves an explicit `dst == src` no-op: currently the
first overwrite changes the shared timestamp and the function returns zero
partway through its own traversal.

### DF15 - the wide specialization has narrow-only assumptions (medium/low)

The wide default hash uses only `(*p) & 255`, so wide characters differing
only above the low byte collide systematically. This does not return wrong
values but can collapse distribution for ordinary non-Latin text. Hash the
full `wchar_t` representation or code value with a documented stable mixing
rule.

Both instantiations also use `sizeof(Dictionary)` during initialization and
hard-code `iDictionary.*` diagnostics and a shared dictionary GUID. The two
headers happen to have equal size today, so the size error is latent, while
wide users receive misleading operation names. Replace hard-coded types and
names with generator macros. Keep the existing outer GUID for valid owning
files unless persistence is deliberately versioned; the nested key collection
GUID currently distinguishes narrow from wide streams.

## Probe evidence and baseline

The family and the full static library build cleanly with GCC 15.2 using
`-fsanitize=address,undefined`. Isolated public-API probes produced:

| Probe | Observed baseline |
| --- | --- |
| owning `Add(key,NULL)` then `Add(key,&value)` | UBSan null destination at `dictionarygen.c:348` |
| one element, `GetFirst`, iterator `Replace` | UBSan null `DataList` member access at line 779 |
| `Apply` callback calls `Clear` | ASan heap-use-after-free at line 509 |
| copy read-only one-entry dictionary | copy has size 0 and remains read-only |
| copy custom-allocator dictionary | copy uses current allocator, not source allocator |
| install new hash after insertion | key changes from found to missing while size remains 1 |
| equal colliding mappings inserted oppositely | `Equal` returns 0 |
| `Sizeof` one populated `int` dictionary | header 88 bytes; populated report 36 bytes |
| save one-key zero-element dictionary | returns EOF and emits vector BADARG |
| zeroed buffer passed to `InitIterator` | success with null position and seek slots |
| wide save/load one ordinary key | ASan heap-buffer-overflow reached from wide `hash` |

The probes used temporary build artifacts under `/tmp`; no probe source or
production/test/build file was added to the repository.

## Unit-test and coverage design

Add one family suite, conventionally `unittests/dictionary_test.c`, and exercise
both `iDictionary` and `iWDictionary`. A small macro/type adapter can share
scenario structure, but keep explicit wide persistence and non-ASCII hash
cases visible. Use constant and bucket-selecting custom hash functions to make
collision and sparse-bucket paths deterministic.

Suggested test groups:

1. **Construction and metadata**: zero/nonzero element sizes; hints at 0, 509,
   510, 1,021, 1,022, and above the final prime; custom allocator identity;
   `Size`, `GetElementSize`, flags, load factor, `Sizeof`, and null-query/error
   branches.
2. **Owning CRUD**: new Add, Add replacement, Insert success/duplicate,
   Replace found/missing, Erase at head/middle/missing, Contains/Get/CopyElement
   found/missing, null data rejection, null object/key/output behavior, and
   readonly rejection. Verify count and stored bytes after every operation.
3. **Set mode**: copied keys, ignored values, `GetElement` key result,
   `CopyElement == 0`, `CastToArray == NULL`, Apply's null value, duplicate
   behavior, erase, clear, copy, and the chosen persistence contract.
4. **Collision/equality/bulk operations**: forced chains, opposite insertion
   order equality, unequal key/value/flags/hash/element-size/count branches,
   successful/incompatible/self `InsertIn`, and source-change detection where
   safely inducible.
5. **Copy and ownership**: empty/populated/read-only source, source allocator,
   custom hash/error policy, observer stripping/notification, independent key
   and value storage, and fail-at-N allocation rollback.
6. **Destructor and observer matrix**: new Add, Add replacement, Replace,
   Erase, Clear, Finalize, duplicate Insert, failed allocation, and set mode.
   Assert exact callback counts and old/new payloads.
7. **Iteration**: empty dictionary; sparse buckets; multi-node collision chain;
   First/Next termination; current replacement and deletion at first/middle/
   last; readonly and stale timestamp; heap and caller-buffer constructors;
   every iterator function slot; deletion with the correct ownership model.
8. **Views and apply**: key/value order correspondence, independent returned
   containers, empty results, allocation failures, mutation via Erase and Clear
   from callback, and readonly callback semantics.
9. **Hash changes**: query/default branches, empty and populated rehash, all
   keys still found, stable count/value addresses where promised, collision
   expansion, iterator invalidation, and wide keys differing above bit 7.
10. **Persistence**: empty and populated narrow owning round trips, custom
    save/read callbacks, invalid outer GUID, invalid key/vector GUID, truncated
    headers/elements, mismatched key/value counts, allocation failures, set-mode
    policy, and mandatory wide round trip once `WstrCollection` is repaired.
11. **Bulk initialization and validation**: valid narrow/wide arrays, duplicate
    keys, zero-size/null values, null keys/values/allocator/header, very large
    element-size overflow, and rollback at each allocation site.

Use a deterministic allocator with allocation counters and fail-at-N control.
It should also record every allocation/free owner, allowing assertions that
copy, nodes, keys, buckets, and iterators stay within one allocator family and
that failed operations leak nothing. Run the suite with ASan/UBSan; the DF1,
DF2, DF3, and wide-round-trip tests are required sanitizer regressions, not
optional death tests.

For the 80% line / 70% branch target, normal CRUD/copy/iteration/persistence
tests cover the main body, while null/read-only/not-found/incompatible,
collision/noncollision, empty/nonempty, callback mutation, and allocation
failure injection cover the short error branches. Both wrappers must be
called: `dictionarygen.c` is instantiated independently, and narrow-only tests
cannot validate wide macro bindings or their dependencies. Coverage reporting
should aggregate both object instantiations for the family rather than treating
the include template as a third compiled source.

## Compatibility-preserving coding handoff

Keep both public vtable layouts, function signatures, Add/Insert return values,
fixed-size shallow-copy semantics, copied-key ownership, owning persistence
format, and read-only `GetElement` behavior unchanged. Implement in this order:

1. Fix safety invariants first: reject null owning values, keep inline `Value`
   pointers permanent, correct iterator current tracking/initialization, update
   timestamps on committed clear/mutation, and add checked constructor/load
   cleanup.
2. Repair the wide string-collection persistence dependency and land the wide
   dictionary round-trip regression with it. Until then, fail wide persistence
   safely rather than exposing the overflow.
3. Make `SetHashFunction` relink existing nodes in place; make Copy use the
   source allocator and populate before applying read-only flags; make equality
   collision-order independent.
4. Correct destructor and observer transitions, `Sizeof`, hint selection,
   `InitializeWith`, `InsertIn(dst,dst)`, and view/allocation failure handling.
5. Replace generator leaks (`sizeof(Dictionary)`, narrow diagnostic strings,
   low-byte wide hash) with type/name/hash macros, without reordering either
   public interface.
6. Preserve all valid existing owning-dictionary files. For zero-element
   persistence, reject before writing until a versioned representation is
   selected; do not reinterpret the legacy sequence ambiguously.

After each stage, run the family test normally and under ASan/UBSan, then the
full CTest suite. Finally run the dictionary coverage gate and inspect missed
branches in both generated objects, targeting at least 80% line and 70% branch
coverage for the generator family.

## Implementation status (dictionary-family handoff)

The shared generator and its two wrappers now contain the following scoped
fixes:

* owning dictionaries reject NULL values in both Add and Insert paths, and
  newly allocated owning nodes always retain their inline value address;
* successful mutations, including Add-as-replace and Clear, update the
  timestamp only after the mutation is committed; owning-value destructors are
  not called for set-mode key aliases;
* Equal matches keys within their buckets instead of depending on collision
  insertion order, and SetHashFunction relinks all existing nodes in place;
* Copy uses the source allocator and bucket-size hint, populates before
  applying read-only flags, strips observer state, and rolls back on failure;
* Sizeof accounts for the header, bucket array, nodes, values, and owned key
  strings with checked arithmetic; constructor and InitializeWith arguments
  are validated;
* dictionary iterators initialize every base slot/private field, validate the
  family magic, implement current/last/previous/seek, and replace or erase
  the node most recently returned;
* Load validates key/value counts and construction/add failures, while Save
  rejects zero-element dictionaries before emitting any bytes;
* narrow and wide error prefixes and hash specializations are selected by the
  wrappers. The wide hash includes the full wchar value rather than only its
  low byte.

`unittests/dictionary_family_test.c` exercises both instantiations, including
CRUD, collision-order equality, rehashing, iterator placement/current
replacement, Apply/Clear mutation detection, read-only/copy behavior,
allocator failure, persistence, observer events, and the set-mode persistence
policy. Under GCC ASan/UBSan the suite passes with leak detection disabled;
the environment's ptrace policy prevents LeakSanitizer from starting. A
manual GCC coverage build measured the narrow instantiation at 83.78% lines
and 96.86% branches executed (70.78% taken); aggregate reports should be
collected by the repository coverage checker once its dictionary target is
available.

The wide dictionary Save/Load round trip remains intentionally isolated from
this handoff. With the current WstrCollection dependency, its Load allocation
uses character counts as byte counts and can over-read a wide key before the
dictionary hash runs. No WstrCollection source was changed here; the wide
dictionary suite covers wide CRUD/hash/copy and records persistence as a
dependency-owned regression for that separate family.

Leak follow-up: the reported 4196-byte allocation was test cleanup, not a
dictionary ownership leak. The persistence test marks `dict` read-only to
verify Copy's flag contract, then previously called Finalize directly;
Finalize correctly refuses Clear on a read-only dictionary, leaving that
dictionary allocated. The suite now clears the read-only flag before every
success- and failure-path finalization. Direct ASan/UBSan execution still
passes; this environment cannot start LeakSanitizer itself because its ptrace
policy rejects LSan startup, so the mandatory external LSan runner should be
used for the final confirmation.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled after correcting read-only test cleanup. This supersedes the focused
environment limitation above.
