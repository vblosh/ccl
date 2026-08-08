# `searchtree.c` audit

## Scope and dependencies

- Physical source: `src/searchtree.c`; public surface is
  `BinarySearchTreeInterface iBinarySearchTree` in `include/containers.h`.
- Dependencies: `CurrentAllocator`, `iError`, caller compare/destructor
  callbacks, and the private AVL-node layout in this file.
- Invariants: every live node owns one copied `ElementSize` value; ordering is
  defined consistently per tree; balance factors match subtree heights and
  remain in LEFT/BALANCED/RIGHT; `count` equals reachable nodes; root is NULL
  exactly when empty; clear leaves a reusable tree header; and node callbacks
  receive element data, not private headers.

## Function and contract inventory

| Area | Functions and behavior |
| --- | --- |
| Lifecycle/metadata | `Create`, `Clear`, `Finalize`, count/flags/sizeof, compare/error/destructor setters. |
| Node mechanics | node allocation, left/right insertion and destruction, AVL rotations, recursive insertion, iterative deletion/rebalancing. |
| Public data operations | `Add`, extended `Insert`, `Erase`, `Find`/`Contains`, in-order `Apply`. |
| Comparison/bulk | default byte comparator, structural `Equal`, ownership-transferring `Merge`. |
| Iteration | `NewIterator` is a stub returning NULL; `DeleteIterator` returns success without work. |

## Confirmed defects

### ST1 - default `Add` crashes on the second distinct element (critical)

`Add` passes the tree object itself as the comparator's third argument, while
`DefaultCompareFunction` interprets that address as `CompareInfo *` and reads
`ContainerLeft`. The resulting tree pointer is invalid/NULL. A two-`int` Add
reproducer deterministically reports a null `BinarySearchTree` access at line
668 and segfaults under ASan/UBSan. `Add` must construct the same `CompareInfo`
as extended `Insert` and `Find`.

### ST2 - erasing the root leaves `tree->root` dangling (critical)

Deletion starts with `ap[0] = tree->root`, `k = 1`, and for a root match sets
`y = &ap[0]->left` instead of `&tree->root`. It then frees `z` (the root), but
never replaces the header root pointer. Adding one value, erasing it, and
calling `Contains` produces an ASan heap-use-after-free. Use an explicit
sentinel/link-to-root or special-case the root link, and cover root deletion
with zero, one, and two children.

### ST3 - `Clear` destroys the live container header (critical)

After freeing nodes, `Clear` calls `btreeZero(tree)`, erasing its vtable,
element size, allocator, flags, callbacks, and destructor. A subsequent Add
dereferences a NULL allocator at line 113 under ASan/UBSan. This also makes
metadata calls invalid and silently discards configuration. Reset only root,
count, and mutation state; retain the construction-time header and policy.

### ST4 - `Merge` zeroes its new header before its first allocation (critical)

`Merge` calls `Create`, immediately applies `btreeZero(merge)`, then
`InsertLeft`, which dereferences the erased allocator. Any valid non-NULL merge
therefore crashes. Once that is removed, merge must also reject incompatible
comparators/allocators and safely transfer roots/counts without making final
cleanup free nodes through the wrong allocator.

### ST5 - comparator configuration is global, not per tree (high)

Nodes have no comparator field. `SetCompareFunction(tree, fn)` writes
`tree->VTable->Compare`; all trees share `iBinarySearchTree`, so changing one
tree retroactively changes ordering/search behavior for every other tree.
Two simultaneously live trees cannot use different comparators. Add a
per-instance compare member, initialized to the default, and route all
operations/equality through it while leaving the interface's `Compare`
function pointer as the advertised default.

### ST6 - empty-tree equality dereferences NULL (high)

`Equal` accepts two compatible empty trees, then unconditionally calls
`compareNodes(left->root, right->root, ...)`; `compareNodes` immediately reads
`left->hidden`. Return true when both roots are NULL and false for a one-sided
NULL root before descending.

### ST7 - destructors receive private node headers instead of element data
(high)

`RemoveLeft`, `RemoveRight`, `destroy_left`, `destroy_right`, and `Remove`
call `DestructorFn(node)`. Other container contracts pass the stored element;
here that is `node->data`. Client destructors consequently interpret internal
pointers as their payload and can corrupt memory. Make every path call once on
the value before freeing its node.

### ST8 - iterator API is advertised but not implemented (medium)

`NewIterator` always returns NULL, preventing traversal through the common
iterator interface; `DeleteIterator` always returns 1. This is an explicit
functional gap rather than sanitizer UB. Implement in-order iteration with
mutation timestamp validation, or formally remove the interface in a separate
breaking release. The compatibility-preserving choice is implementation.

### ST9 - defensive semantics and readonly handling are inconsistent (medium)

Construction does not initialize `RaiseError`; most public entries dereference
NULL trees/data/callbacks; all mutators ignore `CONTAINER_READONLY`; `Apply`
ignores callback return values; `Contains` returns NOTFOUND rather than zero;
and SetDestructor cannot clear an installed callback. Preserve established
return values where documented, but reject bad inputs, enforce readonly, and
make callback/timestamp behavior consistent with sibling containers.

## Existing coverage

`tests/test.c` has one legacy smoke function that installs a global double
comparator, inserts ten ascending values, compares two trees, erases one value,
and prints traversal. It is not a focused CTest suite, has no assertions for
ordering/removal content, and bypasses ST1 by replacing the default comparator.
There are no sanitizer, allocation-failure, clear-reuse, root-deletion, merge,
destructor, invalid-input, or balancing-shape tests.

## Required test matrix

1. Default comparator Add of two+ integer/blob values (ST1); custom comparator
   via Add and Insert with verified `CompareInfo.ExtraArgs` propagation.
2. LL, RR, LR, and RL insertion rotations; ascending, descending, zigzag, and
   deterministic randomized unique sets. After each mutation verify sorted
   Apply output, count, Contains/Find, and AVL height/balance through public
   behavior plus test-only internal validation if necessary.
3. Erase absent, sole root, root with left/right/both children, leaf, one-child,
   two-child successor shapes, and sequences triggering every deletion
   rebalance branch. Regress ST2 under ASan.
4. Clear empty/nonempty and reuse with preserved element size, comparator,
   flags, allocator, error callback, and destructor; clear twice and finalize
   after clear (ST3).
5. Two live trees with different comparators and interleaved operations (ST5).
   Equality for same pointer, empty/empty, empty/nonempty, count/size/comparator
   mismatch, equal and unequal nodes/shapes (ST6).
6. Merge success and incompatibility cases, allocation failure, empty sides,
   and source postconditions; finalize all three objects without leaks or
   double frees (ST4).
7. Counting destructor receives exact data addresses/contents once on erase,
   clear, and finalize (ST7). Failing allocator covers header/node allocation
   without count/tree corruption.
8. Iterator empty/forward traversal, current boundaries, allocated ownership,
   and mutation invalidation once implemented (ST8).
9. NULL/zero-size/data/callback cases, readonly rejection for every mutator,
   error callback routing, Apply empty/early-stop semantics, flags and Sizeof.

The four rotation families plus deletion sequences are necessary to reach 70%
branch coverage; the lifecycle/API matrix should exceed 80% line coverage.
Run ASan/UBSan and leak checks, with a randomized differential oracle against
a sorted unique array.

## Compatibility-preserving handoff

Fix ST1-ST4 before expanding behavior: default use, root erasure, clear reuse,
and merge are currently memory-unsafe. Add per-tree comparator state next, then
correct empty equality/destructor arguments and implement traversal. Preserve
copied-value ownership and AVL ordering, public signatures, and the existing
`Insert` duplicate result convention. Treat any proposed change to `Contains`
or Apply callback return semantics as a documented compatibility decision,
not incidental cleanup.

## Verification status (2026-08-08)

`src/searchtree.c` now keeps comparator, allocator, destructor, error callback,
and mutation state per tree instance. Default `Add` supplies the same
`CompareInfo` context as `Insert`/`Find`; root erasure replaces the owning root
link; `Clear` retains the live header and policies; `Finalize` frees through
the instance allocator; and `Merge` rejects incompatible element sizes,
comparators, allocators, destructors, or read-only sources before transferring
roots. Destructors receive stored element addresses, equality handles empty
trees, and the advertised in-order iterator reports mutation invalidation.
Readonly mutators and bad data/callback arguments return container error codes;
`Apply` returns zero when its callback stops traversal.

`unittests/searchtree_test.c` covers default/custom comparison, all AVL
rotation directions, root/leaf/one-child/two-child erasure, randomized
insert/erase ordering, clear/reuse, equality, merge ownership and allocation
failure, destructor payloads, iterator boundaries/invalidation, readonly
handling, and zero-sized/null inputs. The isolated GCC coverage run reports
91.43% lines and 74.32% branches (thresholds: 80%/70%). Clang ASan/UBSan
passes with leak detection disabled; this environment's LeakSanitizer cannot
run under its ptrace policy (`detect_leaks=1` aborts before the suite starts).

## Final integration verification

The final untraced integration run passed with ASan, UBSan, and LeakSanitizer
enabled. This supersedes the focused-run environment limitation above.
