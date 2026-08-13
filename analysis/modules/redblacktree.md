# `redblacktree.c` audit

## Scope and dependencies

- Physical source: `src/redblacktree.c`; public surface is
  `RedBlackTreeInterface iRedBlackTree` in `include/containers.h`.
- Dependencies: `CurrentAllocator`, `iError`, caller comparison/error/
  destructor callbacks, and C byte-copy routines. The representation and all
  node helpers are private to this source.
- The current structure is a conventional red-black tree with ordinary NULL
  child links. Each node owns a separately allocated copied key and value;
  `root == NULL` denotes an empty tree. Every path must have equal black
  height, red nodes must have black children, keys must remain ordered, and
  `count` must equal live nodes.

## API, helper, and ownership inventory

| Area | Entries and intended behavior |
| --- | --- |
| Construction/metadata | `Create`, `GetElementSize`, `GetCount`, flag getters/setter, error/compare/destructor setters, and `Sizeof`. |
| Data operations | `Add`, extended `Insert`, `Find`, and `Erase`/`Remove`; keys and fixed-size values are copied. |
| Tree mechanics | `new_node`, `destroy_node`/`destroy_nodes`, left/right rotations, fixups, and the default byte comparator. |
| Traversal/lifetime | `Apply`, `Clear`, `Finalize`, `NewIterator`, and `DeleteIterator`. |

The tree captures `CurrentAllocator` for its header, nodes, keys, and element
copies. On successful insertion it owns exactly one `ElementSize` value and
one `KeySize` key per node. `Find` returns a borrowed pointer to the owned
value; erase/clear/finalize call the destructor once on that value and release
both allocations through the captured allocator. No public operation exposes
internal nodes.

## Historical pre-fix compatibility and correctness defects

RB1-RB7 below describe the implementation at the original audit baseline.
The current source uses the NULL-child representation above and the dedicated
suite exercises the repaired constructor, ownership, balancing, lifecycle,
and iterator paths. The one current iterator limitation is called out in RB6.

### RB1 - the public constructor produces an unusable tree (critical, ASan/UBSan)

`Create` only sets `ElementSize`, `VTable`, and `Allocator` (lines 48-56). It
does not store the `KeySize` argument, install `DefaultCompareFunction` or an
error callback, or allocate/initialize the root sentinel. Consequently the
first ordinary `Add` reaches `treenode->left` with `treenode == NULL` at line
191. A public GCC 15 ASan/UBSan probe using `Create(sizeof(int),sizeof(int))`
and `Add` reports null-member access followed by a near-null SEGV. `Find` and
`Erase` have the same root assumption. There is no public workaround for the
missing root or key size.

### RB2 - key storage cannot satisfy the advertised variable `KeySize` (critical)

`RedBlackTreeNode.Key` has only `MINIMUM_ARRAY_INDEX` (one) byte, while all key
copies use runtime `Tree->KeySize`. Nodes are allocated as plain
`sizeof(RedBlackTreeNode)` objects in fixed blocks. Storing any key larger than
one byte will overwrite the following storage once RB1 starts assigning the
constructor argument. Node allocation needs a checked, aligned stride based on
`offsetof(RedBlackTreeNode, Key) + KeySize`, or keys need separately owned
storage. Tests must include 1-byte, `int`, odd-sized, and alignment-sized keys.

### RB3 - both rotations destroy the original pivot key (critical)

`left_rotation` and `right_rotation` save `tmp_key` as a pointer to `n->Key`,
then overwrite `n->Key` before copying `tmp_key` into the child (lines 121-127
and 133-139). The saved pointer aliases the already overwritten bytes, so a
rotation duplicates the replacement key instead of moving the original key.
This breaks search ordering and can turn distinct leaves into apparent
duplicates. Save the pivot key in non-aliasing storage sized for `KeySize` (or
rotate links without copying keys) and check all LL/RR/LR/RL shapes.

### RB4 - size/accounting is permanently wrong (high)

No executable statement increments or decrements `tree->count`; it is only
read by `GetCount` and `Sizeof`. Even if construction is repaired, successful
insertions and removals therefore continue to report size zero and `Sizeof`
reports only the header. Update count exactly once after a committed distinct
insert/removal and leave it unchanged on duplicates and allocation failures.

### RB5 - node allocation is unchecked and block ownership is lost (critical)

`get_node` does not test the block allocation at line 102 before incrementing,
zeroing, and returning the pointer. Insert also allocates two nodes before
`CopyObject`; if the value allocation fails, it retains those nodes and does
not roll back. Moreover, `CurrentBlock` is advanced in place and overwritten
when a later block is allocated, with no list of original block bases. Thus no
future implementation of `Clear`/`Finalize` can recover all node blocks from
the existing fields. Keep explicit block ownership, preserve allocator
failure atomicity, and test failure at header, root, block, and value stages.

### RB6 - historical lifecycle/traversal stubs; iterator replacement remains limited

At the audit baseline, `Apply`, `Finalize`, `Clear`, and iterator construction
were stubs. The current `Apply`, `Clear`, `Finalize`, `NewIterator`, and
`DeleteIterator` implementations are active and covered by the dedicated
suite. `IteratorReplace` remains intentionally unimplemented and returns
`CONTAINER_ERROR_NOTIMPLEMENTED` (`src/redblacktree.c:719-725`); document that
single limitation rather than describing the whole lifecycle as absent.

### RB7 - defensive, readonly, and setter contracts are not enforced (high)

Most public entries immediately dereference the tree/key/data/callback and all
mutators ignore `CONTAINER_READONLY`. `SetFlags`, getters, `Sizeof`, and data
operations have inconsistent NULL behavior. `SetDestructor(NULL)` cannot clear
an installed destructor. Allocation failure in `CopyObject` calls
`Tree->RaiseError`, which RB1 leaves NULL. Normalize BADARG/READONLY handling,
route errors through the instance callback, and allow the documented setter
query/clear behavior without accessing invalid state.

## Boundary behavior to lock down

- Define whether zero element/key size is rejected; validate NULL tree, key,
  data, comparator, callback, and iterator arguments before access.
- Duplicate keys should leave the original value, count, shape, ownership, and
  timestamp unchanged and return one documented status.
- Empty/singleton find and erase, root deletion, minimum/maximum keys, byte keys
  containing zero, and custom comparator context propagation need explicit
  behavior.
- `Clear` must retain construction metadata and callbacks for reuse; `Finalize`
  must accept NULL only according to the common container convention.
- Read-only trees must reject every structural/value mutation without invoking
  a destructor or allocator.

## Current coverage (the following replaces the pre-fix baseline)

`unittests/redblacktree_test.c` is the active focused suite. It covers
lifecycle/ordered operations, variable key sizes, deletion stress, custom
comparison and readonly/error paths, iterator invalidation, and allocation
failures. CMake discovers it through the `_test.c` suite glob; the old
`tests/test.c` placeholder is not the current coverage source.

## Required test matrix

1. Construction for valid key/value sizes and rejected zero/overflow sizes;
   assert initialized comparator/error/root, empty metadata, clear/reuse, and
   finalization. Regress the RB1 first-Add crash under ASan/UBSan.
2. Keys sized 1, 3, `sizeof(int)`, pointer alignment, and a larger struct;
   verify exact byte-key lookup and adjacent-node integrity under ASan (RB2).
3. Deterministic ascending, descending, LL/RR/LR/RL, zigzag, duplicate, and
   randomized unique insertions. After every operation compare count/find/
   in-order output with a sorted reference and validate root blackness, red
   parent rules, ordering, and equal black height (RB3/RB4).
4. Erase absent and singleton roots, extrema, leaves, and every sibling-color/
   near-child/far-child case in the large removal decision tree. Repeat with
   randomized insertion/deletion permutations and invariant checks.
5. Counting destructor and allocator tests for erase, clear, clear twice,
   reuse, and finalize; inject failure at header, root, first/new block, both
   child nodes, and value copy, asserting atomic state and balanced ownership
   (RB5/RB6).
6. Apply empty/nonempty in sorted order; complete iterator first/next/previous/
   current/boundary behavior, allocated ownership, and mutation invalidation
   once implemented (RB6).
7. NULL/readonly matrix over every public entry, error callback routing,
   setter query/reset, old-flag returns, `Sizeof(NULL)`, custom compare context,
   and callbacks that attempt reentrant access (RB7).
8. Long stress runs crossing the 256-node block boundary multiple times, then
   delete/reinsert through the free list and finalize with no leaks.

The historical plan required a deterministic shape corpus plus randomized
differential runs for the insertion and deletion rebalance branches. The
current suite implements that plan and exceeds the 80% line/70% branch gates;
retain the corpus for ASan/UBSan and future leak-enabled regression runs.

## Historical compatibility-preserving handoff

Keep the opaque type and existing interface signatures. The historical repair
order was size-aware node storage, header initialization, allocator-safe
ownership, rotations/accounting, and lifecycle/traversal before deletion-case
validation. Those repairs are present in the current source; future work is
limited to the documented iterator replacement behavior and any additional
coverage expansion.

## Current status (2026-08-13)

The focused GCC/gcov run reports 93.66% line coverage and 76.74% branch
coverage for `src/redblacktree.c`. The dedicated ASan/UBSan suite passes with
leak detection disabled. LeakSanitizer is unavailable in the current
ptrace-restricted environment, so no leak-enabled pass is claimed here.
