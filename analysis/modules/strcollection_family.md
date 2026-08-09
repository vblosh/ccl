# String collection generator family audit

## Scope and generation topology

This family consists of a shared implementation body and two separately compiled
macro instantiations:

| File | Instantiation | Character operations | Exported interface |
| --- | --- | --- | --- |
| `src/strcollectiongen.c` | Template; not compiled directly | Supplied by wrapper macros | Supplied by wrapper macros |
| `src/strcollection.c` | `strCollection`, `char` | `strlen`, `strcmp`, `strcpy`, `strstr`, `GetLine` | `istrCollection` |
| `src/wstrcollection.c` | `WstrCollection`, `wchar_t` | `wcslen`, `wcscmp`, `wcscpy`, `wcsstr`, `WGetLine` | `iWstrCollection` |

The public vtables are declared in `include/containers.h`; the concrete headers
are in `include/ccl_internal.h`. The two object layouts are field-for-field
equivalent except for `contents`. Both store `StringCompareFn`, even though the
header also declares an incompatible `WStringCompareFn` type. The generator
depends on `CurrentAllocator`/`ContainerAllocator`, `iError`, `iVector`, `iMask`,
`qsortEx`, stdio, and the line readers in `fgetline.c`.

There is currently no dedicated string-collection unit test. The coverage
manifest groups all three physical files under `strcollectiongen`, with the
generator as the coverage source. This audit describes the implementation in
the current shared worktree; none of these three production files was changed.

The wrapper/template scheme has several maintainability hazards. The narrow
wrapper's file banner incorrectly names `wstrcollection.c`; `STRSTR` is defined
twice there; `SNPRINTF`, `_TCHAR`, `SC_IGNORECASE`, `CHUNKSIZE`, and the
`stricmp` declarations are unused. More importantly, ordinary byte operations
remain embedded in supposedly generic code. A wide build therefore compiles
cleanly in many configurations while executing byte-counting logic on
`wchar_t *` data.

## Representation, invariants, and ownership

A live collection contains a vtable, logical `count`, flags, an array of string
pointers, pointer-array `capacity`, a mutation `timestamp`, error and compare
callbacks, optional comparison context, the allocator captured at construction,
and an optional destructor callback.

The intended invariants are:

- `0 <= count <= capacity`; `contents[0..count)` are the logical elements and
  remaining slots are not elements;
- each non-null logical string is a separately allocated, null-terminated copy
  owned by exactly one collection and allocated/freed by that collection's
  captured allocator;
- the pointer array is owned by the same allocator and is large enough for every
  access; capacity arithmetic and allocation multiplication do not wrap;
- mutation either succeeds atomically or leaves the header, strings, count,
  capacity, and ownership unchanged;
- structural change or replacement increments `timestamp`, invalidating every
  existing iterator before it can return stale storage;
- read-only prevents every operation that can change the pointer array, string
  bytes, ordering, flags other than through `SetFlags`, or ownership;
- a destructor, when installed, is called once for each logical string just
  before that string is released or discarded, never for the pointer array;
- `Create*` returns an allocator-owned header and `Finalize` releases it.
  `Init*` merely initializes caller-supplied storage, but the implementation has
  no ownership bit and `Finalize` still frees that header, so placement objects
  must be cleared and have their pointer array released by some explicit future
  API rather than finalized. At present there is no safe complete teardown API
  for placement objects;
- returned string pointers (`GetElement`, `Front`, `Back`, iterator accessors,
  and `GetData`) are borrowed, mutable views. They become invalid after removal,
  replacement, clear/finalize, or any operation that frees the relevant string;
- results from `Copy`, `CopyTo`, `GetRange`, `FindText`, `IndexIn`, and
  `SelectCopy` should be deep, independently owned copies. `CastToArray` is
  naturally a vector of borrowed string pointers unless its contract explicitly
  promises deep copies;
- a `CopyTo` result owns both its null-terminated pointer array and every copied
  string, all allocated with the source allocator. Because no matching public
  release helper exists, callers must retain the allocator and free strings then
  the array themselves.

The implementation violates most of the ownership and atomicity invariants on
at least one path. It deliberately accepts a null element in `Add`, but almost
all comparison, search, size, sort, text, persistence, and output operations
unconditionally dereference elements. Null strings therefore cannot currently
be considered a supported stable value despite that acceptance path and the
comment on `DuplicateString`.

Read-only behavior is internally inconsistent. `GetElement`, `GetData`,
`Front`, and `Back` reject read-only objects even though they are declared with
const collection parameters, while `Apply` exposes mutable strings and
`RemoveRange`/`Select` mutate without checking the flag. `Finalize` also refuses
read-only objects, so a copied or loaded read-only collection cannot be released
without first clearing the flag.

## Narrow and wide behavior

The intended semantic difference is only the character type and corresponding
C-library operations. Actual behavior differs in important ways:

| Operation | Narrow behavior | Wide behavior |
| --- | --- | --- |
| Duplicate/search/sort | Correct byte-string primitives for valid non-null strings | Correct `wcs*` primitives in the macro-selected paths |
| `PopFront`, `PopBack`, `Sizeof` | Counts bytes with `strlen` | Incorrectly casts to `char *` and stops at the first zero byte inside a `wchar_t` |
| Default `Save`/`Load` | Length is characters/bytes; payload includes one-byte terminator | Length is characters but allocation and I/O remain bytes, producing undersized objects and truncated records |
| `WriteToFile` | Writes each byte string followed by `\n` | Uses byte `strlen` on wide storage and byte `fwrite`; output is not a valid wide-text representation |
| Text-file input | `GetLine`, newline removed | `WGetLine`, locale/stream-orientation dependent; newline removed |
| GUID | Ends in `0x88` | Ends in `0x89`, so default loaders distinguish formats |
| Compare callback API | `StringCompareFn(const void **, const void **, CompareInfo *)` | Object stores that same type, while public `WStringCompareFn` has `const wchar_t *` operands and is not used by `SetCompareFunction` |

Compatibility fixes should retain the existing narrow GUID and decode valid
narrow records. Wide persistence has never represented ordinary wide strings
correctly; it needs a clearly specified byte-length encoding (or a versioned
format) and bounds-checked legacy rejection rather than pretending corrupt old
records are valid.

## Public API inventory

The narrow and wide vtables have the same entry order and responsibilities.
`EraseAt` is implemented by the helper named `RemoveAt`.

| Area | Public entries | Semantics and notable boundaries |
| --- | --- | --- |
| Construction | `Create`, `CreateWithAllocator`, `Init`, `InitWithAllocator`, `InitializeWith` | Establish allocator/vtable/default compare function; zero capacity is accepted and grows on first insertion; bulk initialization is meant to duplicate all `n` strings |
| Lifetime | `Clear`, `Finalize` | Destroy logical strings; `Clear` keeps pointer capacity and resets flags/timestamp, while `Finalize` also frees array, non-global vtable, and header; both currently reject read-only |
| Metadata/configuration | `Size`, `Sizeof`, `GetElementSize`, `GetCapacity`, `SetCapacity`, `GetFlags`, `SetFlags`, `GetAllocator`, `SetErrorFunction`, `SetCompareFunction`, `SetDestructor` | Report accounting/policy or replace callbacks; element size means `sizeof(void *)`; `SetFlags` returns old flags |
| Single-element mutation | `Add`, `PushBack`, `PushFront`, `Insert`, `InsertAt`, `ReplaceAt`, `EraseAt`, `Erase`, `EraseAll` | Duplicate a caller string, replace/remove owned storage, and maintain order; `Insert` is insertion at zero; not-found is `CONTAINER_ERROR_NOTFOUND` |
| Bulk mutation | `AddRange`, `Append`, `InsertIn`, `RemoveRange`, `Reverse`, `Sort`, `Select` | Add/splice/remove/reorder a sequence; these should preserve independent ownership and invalidate iterators |
| Element access | `Contains`, `IndexOf`, `GetElement`, `Front`, `Back`, `GetData`, `CopyTo` | Search under the active compare callback or return borrowed storage/deep copy; `IndexOf` writes a zero-based result only on success |
| Queue/stack access | `PopFront`, `PopBack` | Return required length including terminator and optionally copy into a caller buffer; current null-buffer behavior differs between front and back |
| Comparison/copy | `Equal`, `Mismatch`, `Copy`, `GetRange`, `CompareEqual`, `CompareEqualScalar` | Whole-collection equality/mismatch, independent copies/ranges, or per-position equality masks |
| Filtering/indexing | `IndexIn`, `SelectCopy` | Gather zero-based indices from a `Vector<size_t>` or select by a same-length `Mask` |
| Text search | `FindFirst`, `FindNext`, `FindText`, `FindTextIndex`, `FindTextPositions` | Substring search; first/next return a one-based position with zero as not-found, while `FindNext`'s `start` is a zero-based scan index; positions vector is `[element_index, character_offset, ...]` |
| Conversion | `CastToArray` | Intended to return `Vector<void *>` containing borrowed string pointers |
| Iteration | `NewIterator`, `InitIterator`, `DeleteIterator`, `SizeofIterator`; iterator `GetFirst`, `GetNext`, `GetPrevious`, `GetCurrent`, `Seek`, `GetPosition`, `Replace` | Heap or caller-buffer iterator, bidirectional/positional traversal, mutation detection, replace/delete current element |
| Binary persistence | `Save`, `Load` | GUID, raw object header, then length-prefixed records; optional callbacks replace payload I/O but not the generator's length decoding |
| Text persistence | `CreateFromFile`, `WriteToFile` | One collection element per line, without retaining newline characters |

Important current return quirks to characterize before changing behavior are:

- mutators normally return `1`, not the new count despite old comments;
- `FindFirst`/`FindNext` use one-based success values;
- `WriteToFile` returns `0` for a successfully created empty file;
- `Mismatch(a, a)` returns zero, while two distinct empty collections currently
  return one;
- `PopFront(NULL buffer)` reports the required length without removing, whereas
  `PopBack(NULL buffer)` removes and frees the element;
- `SetErrorFunction(NULL collection)` returns the global error callback;
- `GetAllocator(NULL)` returns null without raising an error, and `Sizeof(NULL)`
  returns the header size.

Additional validation boundaries are inconsistent but observable: zero-length
`AddRange` returns success before validating either pointer; empty
`RemoveRange` returns zero before checking read-only or indices; callback
setters treat a null callback as a query; `FindText*` return null rather than an
allocated empty result when there are no matches; `GetRange` is intended to use
a half-open end; `Sort` increments the timestamp even for empty/singleton
collections; `Apply` ignores callback return values; and `GetElementSize` and
`SizeofIterator` ignore their collection arguments. Tests should explicitly
lock down safe, useful conventions while fixes normalize only cases that lead
to corruption, ambiguity, or inconsistent behavior between equivalent APIs.

## Private helper inventory

- `encode_ule128` and `decode_ule128` write/read persistence lengths.
- `NullPtrError`, `doerrorCall`, `ReadOnlyError`, `BadArgError`, `IndexError`,
  and `NoMemoryError` route diagnostics. Names are hard-coded to narrow
  `istrCollection`/`strCollection` even in the wide instantiation.
- `DuplicateString` allocates and copies one narrow/wide string using the
  collection allocator. It returns null both for null input and allocation
  failure, leaving many callers unable to distinguish those cases.
- `Resize` applies the growth expression `capacity + 1 + capacity/4`;
  `ResizeTo` replaces the pointer array.
- `EraseInternal` implements first/all matching erase; `Erase` and `EraseAll`
  are thin selectors. `Insert` similarly forwards to `InsertAt(0, ...)`.
- `Strcmp` is the default macro-selected comparator used by search/equality and
  `qsortEx`.
- `SaveHeader`, `DefaultSaveFunction`, and `DefaultLoadFunction` implement the
  current binary format around the public `Save`/`Load` entry points.
- `GetPosition`, `GetNext`, `GetPrevious`, `GetFirst`, `Seek`, `GetCurrent`, and
  `ReplaceWithIterator` are the iterator implementation.
- Every other static function is installed directly in `istrCollection` and
  `iWstrCollection` as catalogued above.

## Persistence format and ownership contract

`Save` writes, in order, the 16-byte family GUID, a raw `sizeof(ElementType)`
memory image, and one record per logical element. The default record is ULEB128
string length excluding the terminator, followed by `length + 1` bytes. `Load`
uses only `count` and `Flags` from the raw header, constructs with
`CurrentAllocator`, decodes a length before every record even when a custom
reader is supplied, allocates `length + 1` bytes, and calls the reader.

Consequences that must be documented and tested:

- the raw header makes the file ABI-, pointer-width-, endianness-, and
  `sizeof(wchar_t)`-dependent and unnecessarily discloses process pointer
  values for the vtable, callbacks, context, and allocator;
- custom callbacks are not truly self-describing: their saver must emit the
  same length prefix expected unconditionally by `Load`, and a custom reader's
  `arg` does not receive the decoded length unless the caller arranged its own
  side channel;
- flags, including read-only, survive; allocator, vtable, callbacks, compare
  context, and destructor do not;
- malformed counts and lengths are trusted without allocation limits or
  checked arithmetic;
- narrow default round trips can work on the same ABI for non-null strings;
  wide default round trips cannot safely work because character counts are
  used as byte counts.

A compatibility-preserving hardening path is to validate and continue loading
existing valid narrow records while introducing a versioned, fixed-width,
pointer-free header for new output. If output format change is out of scope,
at least reject impossible counts/lengths and all wide default persistence
before allocation or partial output.

## Confirmed correctness defects

### SC1 - bulk add/append installs borrowed pointers and causes double frees (critical)

`AddRange` uses `memcpy` on the caller's pointer array instead of duplicating
strings. `Append` delegates directly to it. The destination consequently frees
caller-owned strings or strings still owned by the source; finalizing both
collections double-frees the same allocations. `AddRange` must duplicate each
element transactionally with the destination allocator. `Append` must leave
the source independently usable. Test stack/static input, two collections with
different tagged allocators, self-append, and allocation failure after several
copies.

### SC2 - `SetCapacity` corrupts memory, loses strings, and leaks truncated elements (critical)

After allocating `newContents`, `SetCapacity` mistakenly clears
`SC->contents` for `newCapacity` slots. Growing writes beyond the old pointer
array; both growing and shrinking erase pointers before copying; shrinking
never destroys removed strings. The new allocation remains uninitialized.
Allocate/zero the new array, copy the retained prefix, destroy a truncated
suffix, then publish atomically. Reject or define capacity zero and overflow.

### SC3 - wide pop/size/persistence/output treats wide storage as bytes (critical)

`PopFront`, `PopBack`, and `Sizeof` call `strlen((char *)wide_string)`.
Default persistence records character counts but allocates and transfers that
many bytes. `WriteToFile` also byte-counts and byte-writes wide data. On common
little-endian platforms even ASCII wide strings appear one byte long; loaded
objects are undersized and later `wcs*` calls read out of bounds. Centralize
character-to-byte checked arithmetic and use `STRLEN`/`sizeof(CHAR_TYPE)`.
Specify whether wide text output is locale-encoded (`fputws`) or a fixed
encoding.

### SC4 - `Select` frees retained strings and leaves dangling pointers (critical)

For a mask such as `[0,1]`, `Select` frees slot zero, moves slot one into slot
zero, then cleanup frees slot one—which is the same retained pointer. The
result's sole logical element is dangling and is freed again later. Mixed masks
also leak or double-call destructors. Compact by moving retained pointers,
destroy each rejected original exactly once, clear the tail, update count and
timestamp, and honor read-only. Cover none/all, leading/trailing rejects, and
alternating masks under ASan with a counting destructor.

### SC5 - `GetRange` always returns an empty object and corrupts the source on OOM (critical)

The loop never increments `idx` or `result->count`, repeatedly overwrites
`result->contents[0]`, and returns a logical size of zero. On duplicate failure
its cleanup frees `SC->contents` (the source pointer array) instead of result
storage. It also fails to check result creation, and a result made through the
global `Create` may use `CurrentAllocator` while its strings are duplicated
with the source allocator. Test empty, `[0,0)`, full,
middle, clamped end, start greater than end, and deterministic failure at each
allocation; the source must remain intact.

### SC6 - pop buffer arithmetic underflows/overflows (critical)

Both pop functions compute `buflen - 1` when `buflen == 0`. `PopFront` writes
`buffer[tocopy]`, one past a normally exact `len`-sized buffer; `PopBack` writes
at `tocopy - 1` and returns an empty string for a two-byte buffer holding a
one-character prefix. Copies are byte-sized even in the wide build. Define one
contract: returned required character count including terminator, remove/no-
remove semantics independent of output pointer, copy at most `buflen-1`
characters, terminate only when `buflen > 0`, and invoke the destructor once.

### SC7 - resize invokes the element destructor on the pointer array (critical)

`ResizeTo` calls `DestructorFn(oldcontents)` and then allocator-free on the
same pointer array. A destructor is otherwise passed individual strings. This
can double-free the array or interpret it as a string, and ordinary growth
spuriously increments destructor counts. Never invoke an element destructor
for representation storage.

### SC8 - replace is non-atomic and leaves a freed logical pointer on OOM (critical)

`ReplaceAt` frees the old string before duplicating the replacement. Null input
or allocation failure returns with `contents[idx]` still pointing to freed
memory; later access/finalization is a use-after-free/double-free. Duplicate
first, then destroy/free and publish. Reject null replacement consistently.

### SC9 - `CastToArray` and `FindTextIndex` pass the wrong addresses (high)

A `Vector<void *>` expects `&contents[i]`, but `CastToArray` passes the string
address itself, so the vector copies the first pointer-sized bytes of text and
treats them as a pointer. `FindTextIndex` promises `Vector<size_t>` but likewise
passes string bytes instead of `&i`. Both outputs are garbage for ordinary
strings. `FindTextPositions`, which correctly adds `&i` and `&idx`, provides
the intended model.

### SC10 - iterator layout/setup is incomplete and `GetPosition` reads the wrong type (high)

`GetPosition` casts a string iterator to `VectorIterator`; the layouts differ,
so it reads the string iterator timestamp/flags area instead of `index`.
`NewIterator` leaves `index` uninitialized and never sets the base `GetLast`
slot. `InitIterator` also omits `GetPosition`, `GetLast`, and deterministic
index initialization. Neither constructor validates null inputs/buffers or has
a family magic value. Use a shared initializer for every base slot and private
field, implement last/position consistently, and test zeroed and nonzero-filled
placement buffers.

### SC11 - several mutations fail to invalidate iterators (high)

`RemoveRange` and `Select` do not increment `timestamp`; `SetCompareFunction`
changes ordering/equality policy without invalidation. `Clear` resets timestamp
to zero, which can equal an old iterator snapshot. `Apply` allows callbacks to
remove/finalize the collection and then continues through potentially freed
storage without a timestamp check. Every structural/replacement/policy change
needs a monotonic timestamp transition; callback traversal must detect change
before the next access.

### SC12 - `RemoveRange` bypasses read-only and accepts reversed ranges (high)

There is no read-only check. If `start > end`, loops are skipped and
`count -= end - start` underflows the subtraction, increasing/corrupting count;
the subsequent state can address outside the pointer array. The function also
does not clear vacated tail slots. Establish half-open `[start,end)` semantics,
clamp only the upper end if retained for compatibility, reject reversed/out-of-
range starts, and leave zero-length removal a no-op.

### SC13 - null elements are accepted but immediately unsafe (high)

`Add(NULL)` succeeds and increments count, while `Contains`, `Equal`, `Sort`,
`Sizeof`, text search, comparisons, save, output, and other operations
dereference each element. `CopyTo` treats a duplicated null as allocation
failure. Either reject null on every insertion path without mutation (simplest
compatibility-safe hardening for valid programs) or define null ordering,
comparison, persistence, and copying everywhere. Do not retain the current
half-supported state.

### SC14 - insertion-at-end is unreachable and empty insertion fails (high)

`InsertAt` rejects `idx >= count`, yet contains an explicit `idx == count`
append branch. It therefore cannot insert into an empty collection or at the
end. Accept `idx <= count` and reject only `idx > count`; cover zero, front,
middle, and exact end before/after resize.

### SC15 - construction failure leaks the header; initialization cleanup leaks strings (high)

`CreateWithAllocator` checks `r1 == NULL` after `InitWithAllocator`, instead of
checking `result == NULL`, so pointer-array allocation failure leaks the
already allocated header. `InitializeWith` leaves `count` zero until every
copy succeeds; on an intermediate failure `Finalize` sees no strings and leaks
all earlier copies. It also dereferences null `data`. Validate inputs and
overflow, advance count after each successful copy, and unwind with the same
allocator.

### SC16 - `InsertIn` has allocator, aliasing, and rollback failures (high)

It duplicates using `newData` (therefore the source allocator) but installs the
strings in `source`, which later frees them with the destination allocator.
On failure it frees the active pointer array without freeing newly duplicated
strings, restores a backup with incomplete metadata rollback, and can mishandle
self-insertion after shifting/resize. Duplicate with the destination allocator
into temporary storage, support or explicitly reject self-insertion, then
commit once all copies succeed.

### SC17 - `SelectCopy` mixes allocators and dereferences null inputs (high)

The result header/array uses `CurrentAllocator`, but `DuplicateString(src, ...)`
allocates its elements with the source allocator. Result finalization frees
them with the current allocator. The function dereferences `src` and `m`
before validation. Create with the source allocator (preferred for copy-like
operations) and duplicate through the result; validate inputs and mask length.

### SC18 - persistence accepts malformed data and returns partial success (high)

`decode_ule128` shifts an `int` by up to more than its width, accepts overly
long encodings, and does not detect size overflow. Raw `count` and `len` are
unbounded. On decode/read failure `Load` breaks and returns a partial collection
as success; a just-allocated, uncounted failing element leaks. Validate canonical
lengths/counts and checked sizes, reject truncation with null return, and fully
unwind. Save must reject null elements and callback failure before claiming
success.

### SC19 - copying and indexing do not consistently preserve allocator/policy or failures (high)

`Copy` uses `CurrentAllocator`, does not preserve error callback, compare
context, or destructor, and aliases any non-global source vtable; finalizers may
then free the same custom vtable twice. `IndexIn` also uses the current allocator,
does not check result creation, and treats only negative `Add` as failure even
though `Add` returns zero on duplicate OOM. Copy-like APIs should use the source
allocator, retain only safely shareable policy, deep-copy content, and unwind on
every nonpositive insertion result.

### SC20 - mismatch/equality boundaries are unsafe or wrong (medium)

`Mismatch` writes through `mismatch` before validating it and reports two
distinct empty collections as mismatched. `Equal`, `Mismatch`, and searches
assume non-null elements. `CompareEqual*` ignores the installed compare callback
and uses raw `STRCMP`, so its equality can disagree with `Equal`/`IndexOf`; a
too-small caller mask is finalized as a side effect. Validate output pointers,
define the empty result, and use a consistent comparison policy. Characterize
mask replacement ownership explicitly.

### SC21 - iterator replacement leaves stale current state (medium)

`ReplaceWithIterator` moves before mutating, then synchronizes only the
timestamp. Replacing frees the string still held in `current`; removing shifts
positions after the pre-move. Boundary direction calls can leave the cursor on
freed or semantically wrong storage. Specify direction behavior, preserve the
next/previous target across deletion, and update `index`, `current`, and
timestamp together.

### SC22 - error reporting and callback types are inconsistent (medium)

Wide errors still identify `istrCollection`/`strCollection`; `PushFront`
silently returns zero for read-only rather than raising the standard error;
`Finalize(NULL)` bypasses `NullPtrError`; and several null helpers silently
return. The public wide compare typedef disagrees with the function actually
stored/called. Normalize diagnostics and return codes without changing valid
operation results, and resolve the compare typedef in an ABI-conscious header
change.

### SC23 - size/accounting and capacity arithmetic are unchecked (medium)

`Sizeof` uses byte `strlen`, crashes on null elements, and does not guard
`capacity - count`; all allocation expressions can wrap. Growth can produce a
capacity equal to count and relies on inconsistent spare-slot tests. Use checked
addition/multiplication and count `(STRLEN(s)+1)*sizeof(CHAR_TYPE)` plus header
and every pointer slot. Tests should assert monotonic, at-least-owned-byte
accounting rather than an ABI-independent exact total.

### SC24 - text search/output null and failure paths return misleading partial results (medium)

`FindText`, `FindTextIndex`, and `FindTextPositions` do not validate collection
or text. Allocation/add failure can return a partial object rather than null.
`WriteToFile` reports empty success as zero and cannot distinguish embedded
newlines on reload. Validate inputs, unwind result containers on failure, and
document the line-oriented format (including empty strings and no escaping).

### SC25 - erase shifts one pointer too many and can read beyond exact-capacity arrays (critical)

`RemoveAt` copies `(count - idx)` pointers and `EraseInternal` does the same,
but only `(count - idx - 1)` successors exist. Normally this reads a spare slot;
objects from `InitializeWith` and `Load` have `capacity == count`, so erasing a
non-last element reads `contents[count]` beyond the allocation. Shift only the
successor count and explicitly null the new tail. Cover exact-capacity two- and
many-element collections under ASan, including first/middle/last erase and
erase-all duplicates.

### SC26 - `Contains` bypasses the configured comparator based on the first character (medium)

Before calling `strcompare`, `Contains` requires the first code unit to compare
equal with plain `==`. A case-insensitive or normalization-aware comparator
therefore cannot match strings whose first character differs, while `IndexOf`
can. Remove the shortcut or make it valid only for the default comparator; test
that all search/equality APIs agree after installing a custom comparator.

## Compatibility-preserving unit-test plan

Add one `strcollection_family_test.c` executable that invokes both global
interfaces. Use type-specific helpers/macros only inside the test source so the
public ABI is exercised directly. Every test must finalize heap objects, use
safe placement cleanup appropriate to the current ownership contract, and run
under ASan/UBSan. A switchable failing allocator and a tagged allocator pair are
required; error and destructor callbacks should record operation, code, pointer,
and call count without freeing the string block themselves.

1. **Construction and representation:** create capacities 0, 1, and 20; custom
   allocator create; `Init*`; empty and populated `InitializeWith`; allocator,
   element-size, flags, capacity, size, front/back/data checks; clear/reuse and
   heap finalization. Force failure at header, pointer array, and the third
   initialized string. Verify count/capacity/pointer ownership after each step.
2. **Single-element sequence:** add/push/insert at empty, front, middle, exact
   end, and growth boundary; replace; erase-at/erase/erase-all with duplicates;
   reverse empty/one/many; contains/index found/absent. Cover null collection,
   null string, invalid index, and read-only branches with exact return/error
   assertions. Run the same semantic cases with `L"alpha"`, non-ASCII wide
   strings, and an empty wide string.
3. **Bulk ownership:** `AddRange`, `Append`, `InsertIn`, and self-append/insert
   into empty/nonempty objects. Mutate/finalize the source after success and
   prove destination strings remain valid. Tagged allocators must report no
   cross-free. Fail every intermediate duplication and verify transactional
   rollback.
4. **Capacity/range removal:** grow and shrink around count, set capacity zero,
   retain/truncate strings with destructor counts, then add again. Exercise
   `RemoveRange` as `[0,0)`, prefix, middle, suffix, full, end beyond count,
   start at count, start beyond count, and reversed. Assert timestamp movement,
   cleared tail, and read-only rejection.
5. **Copy/range/index:** independent `Copy`, `CopyTo` plus manual allocator-
   correct release, `GetRange` empty/full/middle/clamped/reversed, `IndexIn`
   empty/reordered/repeated indices and bad vector element size. Include invalid
   index, result construction failure, and duplicate failure; source content and
   allocator accounting must remain unchanged.
6. **Comparison/masks:** `Equal` and `Mismatch` for same object, two empty,
   equal, first/middle/last mismatch, and prefix length mismatch. Exercise
   default and case-insensitive custom comparison/context behavior.
   `CompareEqual`, scalar compare, `Select`, and `SelectCopy` cover null, bad
   length, reusable exact/too-small masks, none/all/alternating/leading/trailing
   selection, custom allocators, destructor counts, and read-only.
7. **Text search/conversion:** first/next one-based return convention, start at
   zero/middle/count, empty needle, absent and multiple substring matches;
   validate exact strings from `FindText`, exact indices from `FindTextIndex`,
   exact index/offset pairs from `FindTextPositions`, and exact pointer identities
   in `CastToArray`. Cover empty results, null arguments, result allocation/add
   failure, narrow and non-ASCII wide offsets.
8. **Stack/queue buffers:** pop empty/one/many with null buffer and sizes 0, 1,
   exact, smaller, and larger; use canaries around narrow and wide buffers.
   Assert required length, termination, truncation, removal semantics, order,
   timestamp, and destructor count. Lock one consistent front/back null-buffer
   contract rather than preserving the current contradiction.
9. **Iterators:** allocated and placement iterators over empty/one/many;
   first/last/next/previous/seek/current/position at every boundary; buffers
   prefilled with nonzero bytes; delete only allocated iterators. Replace and
   remove current in both directions at first/middle/last. After every mutator,
   including clear/remove-range/select/sort/reverse/compare-policy change, stale
   iterator access must report `CONTAINER_ERROR_OBJECT_CHANGED` without stale
   memory access. Force allocated-iterator OOM and test null arguments.
10. **Sort/apply/callback policy:** unsorted, already sorted, reverse, equal-
    heavy, empty strings, growth-sized data, and custom descending/case-folding
    comparator; assert exact order. Test ordinary `Apply`, null callback,
    read-only policy, and mutation from callback with safe object-changed exit.
    Verify error-function replacement and destructor calls for every discard
    operation but never pointer-array resize.
11. **Binary persistence:** narrow empty, one, many, empty-string, maximum small
    ULEB boundary lengths 0/127/128/16383/16384, and flags round trips; custom
    callback success/failure. Reject wrong narrow/wide GUID, empty/truncated
    GUID/header/prefix/payload, excessive/overflowing ULEB, huge count/length,
    null element, and injected allocation failure at every stage. Failed loads
    return null with no live allocation. Add a non-ASCII wide round trip only
    after a defined safe wide format exists; until then assert explicit safe
    rejection.
12. **Text files:** temporary narrow and wide files with empty file, blank line,
    final line without newline, several lines, long line, empty string, and
    non-ASCII data under a controlled locale. Check create/write/open failures,
    line order, newline removal/addition, empty-output return contract, and
    allocator cleanup. Do not require byte-identical wide output until its
    encoding is specified.
13. **Overflow and robustness:** allocator stubs reject wrapped sizes; test
    capacities/counts near `SIZE_MAX` through crafted headers without actually
    allocating, zero-length buffers, null result pointers, malformed masks and
    vectors, and null/public-interface calls. All must return errors without
    state change or undefined behavior.

To meet the 80% line / 70% branch gate on `src/strcollectiongen.c`, link and
actively call both wrappers so gcov merges the two instantiations. The normal
matrix covers every vtable entry; failure injection is necessary for
`DuplicateString`, resize, construction, iterator, copy/range/select, vector,
mask, file, and persistence cleanup branches. Sizes around pointer-array growth,
pop buffer boundaries, all/none/alternating masks, empty/single/many iterators,
and ULEB 0/one-byte/multibyte/malformed cases cover the dense conditionals.
Verify the merged report names all three physical sources and does not silently
count only the narrow instantiation.

Run the suite in at least these configurations:

- GCC C11 with warnings and the coverage gate;
- Clang with `-fsanitize=address,undefined`;
- GCC with `-fsanitize=address,undefined` where supported;
- 64-bit little-endian as the baseline, plus a different `wchar_t`/ABI model
  when available for persistence validation.

The immediate repair order should be SC1-SC8 plus SC25 (ownership and memory safety),
SC9-SC18 (broken results, iteration, and persistence), then policy/accounting
issues. Preserve vtable order, public signatures where type-safe, narrow GUID,
one-based find results, and successful-operation return values unless a
separate API/ABI migration explicitly changes them.

## Repair status (2026-08-08)

The shared generator has now been hardened for both wrapper instantiations. The
implemented repair set covers transactional `AddRange`/`Append`/`InsertIn`,
capacity growth and shrink ownership, exact successor shifts for erase paths,
atomic replacement, safe selection compaction, deep range/copy/index results,
correct vector index/pointer insertion, checked pop-buffer handling, wide
character sizing and default binary records, locale wide text output, checked
ULEB decoding, complete load unwinding, iterator setup/position/staleness, and
read-only checks for bulk/removal/selection operations. Null strings are now
rejected at insertion boundaries rather than remaining a partially supported
value. Narrow persistence retains the existing GUID and record ordering; wide
default records retain their GUID but encode payload sizes in wide-character
units and transfer the corresponding byte count safely.

`unittests/strcollection_family_test.c` exercises narrow and wide ownership,
sequence/bulk operations, ranges, masks, search/conversion, pop buffers,
iterators, failure rollback, and narrow/wide persistence. Sanitizer runs pass
with leak detection disabled; LeakSanitizer itself is unavailable in the
ptrace-restricted execution environment. Iterator ownership is now recorded in
the existing private `Flags` word: heap iterators are released by
`DeleteIterator`, while placement iterators (including the generic adapter
route) are detached without freeing caller storage. Direct and generic
placement regressions are included; the generic adapter's historical
incompatible function-pointer dispatch is skipped only in UBSan instrumented
test builds, where it would abort before reaching the ownership check.

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
