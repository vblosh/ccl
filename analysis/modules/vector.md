# `vector.c` audit

## Scope and representation

- Physical source: `src/vector.c`; public surface: `VectorInterface iVector`
  in `include/containers.h`; private layouts are in `include/ccl_internal.h`.
- The container stores `count` fixed-width elements contiguously in an owned
  `contents` allocation of `capacity * ElementSize` bytes. Elements are copied
  byte-for-byte; the optional destructor is called on selected removal paths.
- A live heap-created vector owns its header and contents through its captured
  `ContainerAllocator`. `Init` instead places the header in caller storage but
  still allocates contents. The current interface has no distinct finalizer for
  a placement header, although `Finalize` always frees the header.
- Returned element pointers (`GetElement`, `Front`, `Back`, `GetData`, and
  iterator results) are borrowed into vector storage. Any relocation or
  mutation can invalidate them. `CopyElement` and `PopBack` copy a value to
  caller storage. `CopyTo` is different: it allocates a NULL-terminated pointer
  array plus one separately allocated byte-copy per element, all with the
  vector allocator; the caller must free every element and then the array with
  that same allocator.

The core representation invariants should be:

1. `count <= capacity`, `ElementSize > 0`, and live contents provide at least
   `capacity * ElementSize` bytes.
2. The live logical range is exactly `[0, count)`; spare bytes do not own
   elements and must never be passed to the destructor.
3. Every allocation is released by the allocator that created it, with no
   attempt to free an interior element address.
4. Every successful mutation advances `timestamp`; an iterator whose saved
   timestamp differs must fail before reading or writing storage.
5. Read-only operations leave count, capacity, bytes, flags, ownership, and
   timestamp unchanged.
6. If a destructor is installed, every logically discarded owned element is
   destroyed exactly once; byte-moving an element transfers its ownership and
   must not duplicate it.

## Public API and helper inventory

| Area | Entries and intended behavior |
| --- | --- |
| Construction/lifetime | `Create`, `CreateWithAllocator`, `InitializeWith`, `Init`, `Clear`, `Finalize`, `Copy`; `grow` and `ResizeTo` manage capacity. |
| Metadata/configuration | `Size`, `Sizeof`, `GetElementSize`, `GetCapacity`, `SetCapacity`/`Reserve`, `GetFlags`, `SetFlags`, `GetAllocator`, `SetErrorFunction`, `SetCompareFunction`, `SetDestructor`. |
| Element access | `GetElement`, `CopyElement`, `CopyTo`, `GetData`, `Front`, `Back`. Direct pointer APIs deliberately reject read-only vectors because they expose writable storage. |
| Single-element mutation | `Add`, `PushBack`, `PopBack`, `Insert`, `InsertAt`, `ReplaceAt`, `Erase`, `EraseAll`, `EraseAt`. |
| Range/vector mutation | `AddRange`, `InsertIn`, `Append`, `RemoveRange`, `Resize`, `Reverse`, `RotateLeft`, `RotateRight`, `Select`. |
| Queries/derived vectors | `Contains`, `IndexOf`, `Equal`, `Mismatch`, `GetRange`, `IndexIn`, `SearchWithKey`, `SelectCopy`, `CompareEqual`, `CompareEqualScalar`. |
| Algorithms/callbacks | `Sort`, `Apply`, the default byte comparator, observer notifications, and error helpers. |
| Iteration | `NewIterator`, `InitIterator`, `DeleteIterator`, `SizeofIterator`; first/last/next/previous/current/seek/position and iterator replacement/removal helpers. |
| Persistence | `Save`, `Load`, raw-byte default callbacks, and caller-supplied per-element save/read callbacks. |

Normal successful mutators generally return 1, empty/no-op operations often
return 0, and errors are negative container codes. Several implementations do
not currently preserve those conventions consistently.

## Allocator, iterator, and persistence behavior

`CreateWithAllocator`, ordinary growth, `Copy`, `CopyTo`, heap iterators, and
rotations normally use the vector's captured allocator. `Create`, `Init`,
`InitializeWith`, `Load`, `IndexIn`, and `SelectCopy` select
`CurrentAllocator`; `Reverse` alone uses system `malloc/free`. `GetRange`
constructs through the object's vtable, which becomes unsafe for the typed
derived interface described in `vector_family.md`. Allocation sizes and count
arithmetic are unchecked for overflow throughout the unit.

Heap iterators own one allocation containing the iterator and a read-only
element buffer. Placement iterators use caller memory. Iterator results point
into vector storage for writable vectors and into the iterator buffer for
read-only vectors. Structural and replacement operations are intended to be
fail-fast through timestamps.

The persisted format is a GUID, a raw in-memory `Vector` header, then element
records. The default record function writes exactly `ElementSize` raw bytes;
custom callbacks may use `arg`. `Load` reconstructs with `CurrentAllocator`
and restores only serialized element size, count, flags, and element bytes; it
does not restore allocator, callbacks, comparator, destructor, or vtable.

## Confirmed correctness defects and compatibility hazards

### V1 - `RemoveRange` frees addresses inside `contents` and corrupts byte
offsets (critical, ASan)

For every removed element, lines 684-696 call `AL->Allocator->free(p)` where
`p` points at the vector's inline contents, whether or not a destructor exists.
The first call attempts to free the whole contents allocation prematurely and
later calls free interior pointers. `p` also starts at the allocation base,
not at `start`. The final `memmove` adds `start` and `end` as bytes to an
already advanced pointer instead of multiplying indexes by `ElementSize`.
Any non-empty range removal can therefore invalid-free, read freed storage,
or move unrelated bytes. Destruction should cover `[start,end)` once, followed
by one element-scaled move; the contents allocation must remain owned.

### V2 - `SetCapacity` writes the old allocation using the new size (critical,
ASan) and destroys retained data

After allocating `newContents`, line 896 calls `memset(AL->contents, 0,
ElementSize * newCapacity)`. Growing beyond the old capacity writes past the
old allocation. Even when in bounds, it zeros the source immediately before
copying it, so retained elements become zero. Shrinking also drops tail
elements without invoking the destructor. Populate the new allocation from
the old logical prefix, destroy only truncated elements, then commit and free
the old allocation; failure must leave the object unchanged.

### V3 - `Append` writes beyond the new logical end (critical, ASan)

Lines 1094-1097 assign `AL1->count = newCount` and then compute the copy
destination from that new count. The appended bytes start one whole resulting
vector past the correct destination and commonly overflow the allocation.
The destination must use the old count, with overflow/self-append and exact
capacity handled before committing count. The current `ResizeTo` return of 0
also makes an exact-capacity append fail despite sufficient storage.

### V4 - selection is incorrect and mishandles owned elements (critical)

`SelectCopy` skips copying whenever `i == offset`; a newly allocated result
with a leading selected prefix therefore retains zero-filled bytes, and an
all-true mask returns all zeros. In-place `Select` advances its source pointer
only when a mask byte is true and advances the destination only on a move, so
even a mask such as `[false,true]` copies the wrong element. It may call the
destructor on a destination slot and then copy that destroyed slot onto itself,
while omitted source elements are not reliably destroyed. `Select` also
ignores read-only state and neither function preserves comparator/destructor
semantics. Add masks for leading/trailing/alternating/none/all selections with
destructor accounting.

### V5 - scalar comparisons inspect the vector header, not its elements; mask
reuse loses its length (high)

Both branches of `CompareEqualScalar` increment `pleft` but compare `left`
instead (lines 1941-1949), so every result is based on bytes at the `Vector`
header. The default branch calls `memcmp(left,right,ElementSize)` and the custom
branch repeatedly calls the comparator with the header. `CompareEqual` and
`CompareEqualScalar` additionally call `iMask.Clear` on a reusable mask;
`iMask.Clear` sets `length` to zero, and neither function restores it. A reused
result thus reports size zero despite writes to its backing allocation.

### V6 - range extraction has an inclusive-end off-by-one (high)

The library's analogous ValArray operation treats `end` as inclusive, but
`GetRange` computes `top = end - start`, copies only `top` elements, and sets
that count. A singleton `[i,i]` becomes empty and `[start,end]` omits `end`.
The derived allocation is also made via `AL->VTable->Create`, which is an ABI
error when the stored vtable is a generated typed interface.

### V7 - `Resize` does not implement a safe logical resize (critical ownership
hazard)

- Growing delegates to `ResizeTo`, which only reserves capacity and never
  changes `count` or initializes new logical elements.
- Shrinking does not reject read-only vectors and does not advance timestamp.
- Tail destructors run before `realloc`; if allocation then fails, count and
  storage remain old but their elements have already been destroyed.
- `realloc(contents,0)` may free storage and return NULL; this is treated as a
  recoverable allocation failure while retaining a dangling old pointer.

Capacity reserve and logical resize need separate, failure-atomic contracts.

### V8 - insertion and aliasing paths violate bounds/value semantics (high)

`InsertAt` moves `(count - idx + 1)` elements rather than `count - idx`, moving
one spare element unnecessarily. If an error callback authorizes an index
beyond count, the function reserves through that index but increments count
only once, leaving the written element outside the logical range. `AddRange`,
`InsertAt`, and `InsertIn` do not safely handle sources borrowed from their own
contents: `realloc` can invalidate the source, and subsequent `memcpy` can
overlap. Self `InsertIn`/Append require an explicit snapshot or overlap-safe
algorithm.

### V9 - callback/query argument handling can crash or return wrong matches
(high)

- `Contains` always passes `&ci`; when caller `ExtraArgs` is non-NULL, `ci` is
  uninitialized, and in the NULL case `ci.ExtraArgs` is accidentally made
  self-referential.
- `IndexOf` writes `*result` on a match without validating `result`.
- `Mismatch` writes `*mismatch = 0` before its NULL check.
- `SearchWithKey` validates `startidx` but starts `p` at element zero, so it
  reports indexes starting at `startidx` while comparing earlier elements. It
  also does not validate `item`.
- `IndexIn` does not check result-vector allocation before adding, and accesses
  index vectors through a mutable-pointer API that rejects read-only objects.

### V10 - iterator initialization and invalidation are incomplete (high,
ASan/UBSan)

`InitIterator` does not set `GetPosition` or `Magic`, although Seek, position,
and most traversal helpers require the vector magic value. It also reports
errors as `NewIterator`. Calling `DeleteIterator` on placement storage frees
caller memory, with no ownership marker distinguishing it from `NewIterator`.
`GetNext` omits the magic check; `GetFirst` and `GetCurrent` omit timestamp
checks. A newly created iterator stores index `(size_t)-1`, while `GetNext`
tests it against `count-1`, so direct next traversal does not begin at element
zero. Iterator replacement can leave `Current` pointing at a removed/moved
slot and ignores traversal failure before mutation. Tests must cover both
construction modes, stale iterators, bad magic, deletion ownership, read-only
buffers, and every direction at both ends.

### V11 - mutation/read-only/timestamp/observer rules are inconsistent (high)

`Sort`, `Select`, and shrinking `Resize` mutate read-only vectors. Successful
rotations and selection do not advance timestamp; writable `Apply` can alter
values without invalidation. `Clear` resets timestamp to zero rather than
advancing it and resets all flags, silently dropping observer/read-only-related
state after it has accepted the call. Sort and rotations also omit observer
events. Establish which value changes invalidate iterators, then apply one
rule consistently.

### V12 - allocator and size failures are not contained (high)

`CreateWithAllocator` dereferences a NULL allocator and accepts zero element
size. Constructors, growth, range counts, and all byte-size multiplications
lack `SIZE_MAX` checks. `InitializeWith` dereferences NULL data for nonzero
counts; `Init` dereferences NULL placement storage. `Reverse` bypasses the
captured allocator with system `malloc/free`, preventing reliable custom
allocator accounting. `SetDestructor` cannot clear a destructor because NULL
is treated only as a query.

### V13 - copy/destructor and placement ownership contracts are unsafe (high)

`Copy` byte-copies elements but does not copy `DestructorFn`. For elements that
own pointers this either makes the copy non-owning and leaky or, if changed to
copy the destructor without deep-copy semantics, would double-destroy shared
resources. The public contract needs an explicit clone/ownership policy.
`Init` uses caller storage, but the only `Finalize` frees that header through
the captured allocator. Finalizing a stack/embedded initialized vector is an
invalid free. `PopBack(NULL)` similarly removes an element without calling its
destructor or returning its bytes, so ownership is silently lost.

### V14 - persistence is ABI-dependent and insufficiently validated (high)

`Save` writes raw pointers/function pointers/padding in `Vector`, leaks process
representation into the file, and accepts any nonzero partial header write as
success. `Load` trusts serialized `ElementSize`, `count`, and their product;
malicious/corrupt values can overflow allocation sizing or produce huge reads.
The single GUID carries no version, endianness, ABI, or element-type identity.
Default scalar persistence is consequently only same-build raw-byte storage,
not a portable format. Use a fixed-width versioned header with checked sizes,
and require custom callbacks/type identifiers for nontrivial elements.

## Existing test status

There is no dedicated vector source under `unittests/`. `tests/test.c` contains
legacy create/add/insert/erase/pop/equality examples, and newer collection and
dictionary suites use vectors indirectly, but none covers the public surface
or the defects above. The coverage manifest declares a `vector` unit without
an owning suite, so neither the 80% line nor 70% branch gate can currently be
demonstrated.

## Required ASan/UBSan and coverage test matrix

1. Basic creation, default/explicit capacity, add/growth, access/copy APIs,
   front/back/pop, clear/reuse/finalize, zero and invalid arguments, and every
   read-only branch.
2. Insert/erase/erase-all at front/middle/back; empty, singleton, duplicate,
   exact-capacity, out-of-range callback, self/borrowed-source, AddRange,
   InsertIn, Append, RemoveRange, and Resize/Reserve sequences. Keep direct
   ASan regressions for V1-V3 and realloc-to-zero behavior.
3. Destructor-counted pointer elements across replace, erase, ranges, select,
   clear, resize, pop with/without output, copy, and allocation failure. Assert
   exactly-once destruction and document copy ownership.
4. Contains/IndexOf/Equal/Mismatch/custom comparator and CompareInfo forwarding;
   SearchWithKey with nonzero starts/partial keys; IndexIn success and failure.
5. GetRange singleton/clamped/full/inverted cases; all selection masks;
   CompareEqual/vector-scalar with new, reusable exact/larger, incompatible,
   empty, and allocation-failure masks.
6. Sort/reverse/rotations (0, 1, count, greater than count), Apply, timestamps,
   stale iterators, observer events, and read-only state preservation.
7. Heap and placement iterators through first/next/last/previous/seek/current,
   position, replace/remove directions, invalid magic, object changes,
   read-only snapshots, empty vectors, and correct destruction behavior.
8. Counting/failing allocator at header, contents, grow/realloc, copy, CopyTo
   partial cleanup, iterator, rotate, ranges, and masks. Assert balanced frees,
   no allocator-family crossings, and failure-atomic state.
9. Save/load default and custom callbacks, empty/nonempty/read-only flags,
   wrong GUID, truncated GUID/header/element, short writes, callback failure,
   hostile size/count headers, and same-width round trips.
10. Randomized model-based operation sequences against a reference byte array,
    checking count/capacity/data after every step. Run the dedicated suite with
    ASan+UBSan and GCC coverage until `src/vector.c` independently reaches at
    least 80% lines and 70% branches.

## Implementation status (2026-08-08)

The dedicated `unittests/vector_test.c` suite now exercises the public vector
surface and passes with both AddressSanitizer and UndefinedBehaviorSanitizer.
The vector implementation fixes the confirmed range-removal, capacity,
append/aliasing, inclusive-range, logical-resize, selection/ownership,
reusable-mask comparison, iterator initialization/invalidation, allocator,
and persistence-header defects.  The persistence format now uses a fixed
versioned scalar header rather than serializing the in-process `Vector`
object.  Placement iterators no longer free caller-owned storage, and
read-only vectors can be finalized safely.

The standalone GCC coverage run reports 81.32% line coverage and 70.32%
branch coverage for `src/vector.c`, meeting the requested gates.  The full
CMake sanitizer run is pending completion of an unrelated in-progress
`SuffixTree.c` edit in the shared worktree.

The vector suite now finalizes the intermediate `IndexIn` result before
reusing its variable, eliminating the previously reported 96-byte leak.
Direct `ASAN_OPTIONS=detect_leaks=1` reaches suite completion, but this
environment's LeakSanitizer terminates afterward because ptrace is blocked;
no leak report is emitted before that runtime limitation.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled after correcting the test-owned `IndexIn` result cleanup. This
supersedes the focused-run environment limitation above.
