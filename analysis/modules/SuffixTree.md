# `SuffixTree.c` audit

## Scope, public API, and dependencies

- Physical source: `src/SuffixTree.c`; public surface:
  `SuffixTreeInterface iSuffixTree` in `include/containers.h`.
- Public entries are `Create`, `CreateWithAllocator`, `Find`, `Clear`,
  `Finalize`, `Print`, `Apply`, and `Sizeof`.
- The implementation is an immutable suffix tree built with Ukkonen's
  algorithm. It depends on `CurrentAllocator`, `ContainerAllocator`, `iError`,
  C strings/stdio, and private node/path/position structures defined entirely
  in the source file.
- There is no persistence API. A tree cannot be saved or loaded by this unit.

`Create` accepts a NUL-terminated source and copies it into private storage;
later caller mutation or release of the original string is safe. `Find`
returns a positive, one-based source position for a match and
`CONTAINER_ERROR_NOTFOUND` otherwise. For repeated substrings, the code does
not specify that the returned occurrence is the first one. A public probe
confirms that `"ssis"` in `"mississippi"` returns 3, corresponding to offset
2 in normal zero-based indexing.

## Representation and ownership invariants

Each live tree owns:

- one allocator-created `SuffixTree` header;
- one copied source buffer stored with a leading unused byte and an appended
  NUL terminator used as the algorithm's unique terminal symbol;
- one root and every node reachable through `root->sons` and sibling lists.

Each node combines a tree vertex and its incoming edge. Edge labels are
inclusive indexes into `tree_string`; an internal node uses its fixed
`edge_label_end`, while a leaf uses the tree-wide virtual end `e`. The intended
invariants are:

1. The root has no father or siblings. Every non-root node has exactly one
   father and is reachable exactly once through that father's `sons`/
   `right_sibling` chain.
2. Sibling links are reciprocal; the first son has no left sibling; every
   sibling in one chain has the same father.
3. Every edge range is within the copied source, is nonempty, and siblings have
   distinct first characters.
4. A node's `path_position` denotes a valid one-based occurrence of the full
   root-to-node path in the source.
5. Internal suffix links point to the node representing the longest proper
   suffix required by Ukkonen's algorithm; root/single-character links obey
   the special root rules.
6. `e` is the final terminal index after construction, including for boundary
   source lengths. The node count and memory statistic must agree with live
   reachable allocations.
7. Construction is failure-atomic: either a complete searchable tree is
   returned or every partial node/string/header allocation is released by the
   same allocator.

`Print` borrows its `FILE *` only for the call but stores it temporarily in the
tree header. `Apply` traverses nodes without transferring ownership. `Clear`
currently frees nodes only; `Finalize` recursively frees nodes, then the copied
source and header through the captured allocator.

## Internal algorithm/helper inventory

| Area | Helpers and role |
| --- | --- |
| Allocation/topology | `create_node`, `connect_siblings`, `apply_extension_rule_2`; allocate leaves/internal nodes, append a new child, or split an edge while repairing parent/sibling links. |
| Edge metadata | `find_son`, `get_node_label_end`, `get_node_label_length`, `is_last_char_in_edge`; navigate outgoing edges and virtual leaf ends. |
| Tracing | `trace_single_edge`, `trace_string`; compare or skip edge labels and return the final node/edge position. |
| Ukkonen state | `follow_suffix_link`, `create_suffix_link`, `SEA`, `SPA`, and file-global `suffixless`; apply rules 2/3, maintain suffix links, repeated extensions, and the leaf-end trick phase by phase. |
| Construction/search | `ST_CreateTree` copies the source, creates root/initial leaf, and runs phases; `ST_FindSubstring` descends matching edges and returns a path position. |
| Destruction/traversal | `ST_DeleteSubTree`, `ST_PrintNode`, `VisitNode`; recursive sibling/child destruction, rendering, and preorder callback traversal. |
| Public wrappers | `Create`, `CreateWithAllocator`, `Find`, `Clear`, `Finalize`, `ST_PrintTree`, `Apply`, `Sizeof`. |

Algorithm boundaries that need explicit contracts are empty and one-character
sources, empty keys, a key ending inside an edge versus exactly at a node,
full-source/suffix/prefix matches, repeated occurrences, bytes with the high
bit set, very long strings crossing `int32_t`, and sources containing NUL. The
public C-string API inherently stops at the first NUL, so embedded zero bytes
cannot be represented even though the internal algorithm is described in
terms of byte sequences.

## Historical pre-fix defects and compatibility concerns

The ST1-ST14 findings below are the pre-fix audit baseline and are retained as
historical evidence. The implementation and verification sections later in this
document describe the current source and test state.

### ST1 - one-character trees cannot find their only character (high,
confirmed under ASan/UBSan)

The header is zeroed, and `e` is advanced only inside `SPA`. Construction
starts at phase 2 and runs while `phase < tree->length`, where tree length is
source length plus the terminator. Empty and one-character sources execute no
phase, leaving `e == 0`. Leaves ignore their stored end and use `e`, so the
initial leaf beginning at index 1 appears to end at index 0. A sanitizer probe
of `Create("a")` followed by `Find("a")` returns
`CONTAINER_ERROR_NOTFOUND`. Initialize the virtual end consistently for the
base tree and define empty-source/key semantics.

### ST2 - allocation failure is not propagated and can crash or publish a
corrupt tree (critical, ASan/UBSan)

The copied-source failure cleans the header, albeit with the wrong error name,
but every node allocation afterward is unchecked:

- failed root allocation is immediately dereferenced at line 915;
- failed initial-leaf allocation can return a non-NULL tree with no suffixes;
- new-child failure is ignored by SEA and reports rule 2 as applied;
- split failure dereferences a NULL internal node, while failure of its second
  allocation occurs after the original edge has already been modified.

Construction helpers need status propagation and rollback/whole-tree cleanup.
A caller must never receive a partially constructed index.

### ST3 - NULL public inputs are dereferenced before validation (critical)

`Create` and `CreateWithAllocator` call `strlen(text)` before the internal NULL
check. `CreateWithAllocator` also dereferences a NULL allocator. `Find` calls
`strlen(txt)` and then accesses the tree without validating either input.
`Clear` dereferences its tree, and Print calls `fprintf` on the stream before
validating tree or stream. These public BADARG cases become immediate crashes.
Validate at the outermost entry and route errors through a consistent error
function name.

### ST4 - `Clear` leaves an unsafe, internally inconsistent object (critical)

Clear frees all nodes and sets `root = NULL` while retaining the source,
length, header, historical heap statistic, and callbacks. Subsequent Find,
Print, and Apply dereference the NULL root. There is no insertion/rebuild API,
so the retained object cannot become a useful suffix tree again. Either define
Clear as a safe empty searchable/printable state with consistent metadata, or
remove it only in a breaking API change; Finalize-after-Clear and repeated
Clear must remain safe.

### ST5 - split/new-child mutation is not failure-atomic (critical ownership
and topology hazard)

In the split path, the original edge start is changed immediately after
allocating the internal node and before allocating the new leaf. Parent and
sibling topology is then rewired without checking either result. A failed
second allocation leaves a modified tree that does not contain the required
suffix and may leave `suffixless` pointing into incomplete topology. The
new-child path likewise appends a NULL allocation as though successful. Stage
both allocations before changing any reachable edge/link, or destroy the
entire partial tree on construction failure.

### ST6 - construction state is file-global and not reentrant/thread-safe
(high)

`suffixless` is a static global shared by every tree construction. Concurrent
creates race on it, and a nested Create from an allocator/error callback can
replace the outer construction's pending suffix-link state. The result can
cross-link the wrong tree or dereference a node freed with another failed
construction. Make pending suffix-link state local to `ST_CreateTree`/SPA/SEA
or a field in the tree under construction.

### ST7 - `Apply` exposes an undocumented private node pointer and cannot stop
traversal (high compatibility hazard)

VisitNode calls the callback on the synthetic root and every private `NODE *`,
but NODE is not publicly declared, so clients cannot safely interpret the
value as a suffix, edge, or element. The callback's integer result is ignored,
preventing the early-stop convention used by other container Apply APIs.
Apply after Clear also dereferences NULL. Define a public immutable visit
record (edge bounds/path position/leaf flag) or an explicit callback contract,
and propagate callback termination consistently.

### ST8 - `Sizeof` reports historical allocation, not current ownership
(medium)

`heap` is an unsigned counter incremented for header, source, and nodes, but it
is never decremented by Clear. `Sizeof` after Clear therefore reports memory
for nodes already freed. It can also wrap before the size_t return type for
large trees. Track live allocation in `size_t`, or document a distinct peak/
cumulative statistic under a differently named API.

### ST9 - the object header is incompletely initialized (medium)

`ST_CreateTree` never installs `tree->VTable = &iSuffixTree`; `count` is never
incremented for nodes and remains zero; Flags and counter are dead state. The
type is opaque publicly, but these common container-header fields imply
introspection/observer conventions and become misleading internal invariants.
Either maintain them accurately or simplify the private header; tests should
at least verify the vtable if it remains part of the representation.

### ST10 - recursive lifecycle/traversal can exhaust the C stack (medium)

Deletion, Print, and Apply recurse through both child depth and siblings. A
suffix tree can have linear depth for adversarial repetitive strings, making
these public lifecycle operations vulnerable to stack exhaustion even after
successful construction. Use iterative traversal with an explicit stack, or
set/document a checked maximum source length based on supported resources.

### ST11 - length narrowing and arithmetic are unchecked (high)

The public wrappers pass `strlen` (`size_t`) to an `int32_t` length; internal
indexes, phase arithmetic, edge ends, and path positions are all signed
32-bit. Values above `INT32_MAX-1` narrow and can make `length+1`, allocations,
or indexes wrap. The source allocation also performs unchecked addition.
Reject unsupported lengths before conversion and check every allocation-size
calculation.

### ST12 - Print mutates shared state and writes terminal NUL bytes (low/medium)

Print stores the borrowed stream in `tree->outstream`, making two concurrent
Print calls on one immutable tree race and potentially send recursive output
to the wrong stream. Leaf edge output includes the unique NUL terminator via
`%c`, producing embedded zero bytes in what appears to be text output. Pass the
stream through recursion instead of storing it, and specify whether the
terminal symbol should be rendered or suppressed/escaped.

### ST13 - search result and empty-key semantics are under-specified (medium)

Matches use one-based positions, unlike ordinary C offsets, and repeated
patterns do not promise which occurrence is returned. Empty-key behavior
depends on whether a terminator edge is reachable; the broken one-character
base currently returns NOTFOUND even though an empty string is conventionally
a substring. Document one-based versus zero-based return values, first-versus-
any occurrence, and empty-key behavior; preserve NOTFOUND as a negative value
distinct from every valid result.

### ST14 - strict warning builds fail (low/build gate)

`SEA` receives an allocator parameter that is never used. GCC
`-Wall -Wextra -Wpedantic` reports it, so a strict `-Werror` ownership build
fails. Remove the redundant argument after allocator flow remains sourced from
the tree.

## Historical test baseline and current coverage

The historical `tests/test.c` smoke function creates `"mississippi"`, prints
it, finds `"ssis"`, prints the returned position, and finalizes. It has no
assertions and was not a dedicated CTest suite. The current
`unittests/suffixtree_test.c` is registered as `test_suffixtree` and is the
`SuffixTree` owner in `unittests/coverage_manifest.json`; its current coverage
and sanitizer results are recorded below.

## Required ASan/UBSan and coverage test matrix

1. Boundary construction/search for empty, one-character, two-character,
   unique, all-repeated, periodic, and classic strings (`banana`,
   `mississippi`). Assert every prefix, suffix, full string, all substrings,
   edge-internal endings, absent strings, longer-than-source keys, and empty
   key. Validate any returned one-based occurrence against the source.
2. Differential randomized testing: generate many short non-NUL byte strings
   and keys, compare Find with a naive substring oracle, and verify every
   reported position. Include bytes 1, 127, 128, and 255 to characterize signed
   char handling.
3. A counting/failing allocator for header, source, root, initial leaf, every
   new-child allocation, and both allocations of edge splitting. Every failure
   must return NULL, preserve the source input, balance allocations, and avoid
   cross-allocator frees. Keep sanitizer regressions for root/split failures.
4. NULL tree/text/key/allocator/stream/callback cases for every public entry;
   zero-length and near-limit length validation without attempting huge
   allocation. Verify operation-specific error names/codes.
5. Clear empty/nonempty, Clear twice, Find/Print/Apply after Clear, Sizeof after
   Clear, and Finalize after each state. The chosen empty-state contract must
   be safe and metadata must reflect live ownership.
6. Apply preorder count/content using a defined public visit record, callback
   early termination, root inclusion policy, NULL callback, and deep trees.
   Print to `tmpfile`, verify deterministic structure/text and terminal-symbol
   policy, and perform independent/concurrent stream calls if thread safety is
   claimed.
7. Assert copied-source ownership by mutating/freeing caller storage after
   Create. Exercise two simultaneous trees with different custom allocators
   and interleaved queries/finalization; add nested/concurrent construction to
   regress global `suffixless` state.
8. Internal test-only invariant validation after each construction: traverse
   every child/sibling once, check reciprocal/father links, edge bounds and
   distinct child initials, count nodes/bytes, validate leaf virtual ends,
   suffix links, and confirm every suffix reaches a leaf.
9. Very deep/repetitive inputs for Print, Apply, Clear, and Finalize under ASan/
   UBSan to expose recursion overflow at a practical supported boundary.
10. Run the dedicated suite under ASan+UBSan and GCC coverage. New-son/split,
    rule 3, repeated-extension, root/internal suffix-link, skip/no-skip,
    edge-shorter/string-shorter/mismatch, and allocation/error branches are all
    needed for at least 80% lines and 70% branches. LeakSanitizer should run
    where the environment permits it; this workspace's ptrace policy may
    require reporting leak detection as unavailable rather than passed.

## Compatibility-preserving handoff

Fix boundary initialization and outer public validation first, then make every
node allocation failure-atomic and move `suffixless` into per-construction
state. Define Clear, Apply payload/termination, search-position, repeated-match,
and empty-key contracts before changing their observable results. Preserve the
immutable copied-source design, public interface shape, custom allocator
ownership, Ukkonen linear construction goal, and negative NOTFOUND result.

## Implementation and verification status (2026-08-08)

`src/SuffixTree.c` now initializes the virtual leaf end for all source lengths,
checks all public pointer/allocator/stream inputs before dereferencing them,
rejects lengths that cannot be represented by the internal signed indexes, and
propagates every node allocation failure. Split construction stages both node
allocations before changing reachable topology; a failed construction rolls
back all nodes, source storage, and the header through the selected allocator.
Pending suffix-link state is a field of each tree rather than file-global state.

`Clear` retains a valid empty root, releases all descendants iteratively, and
keeps the live heap statistic accurate; `Finalize`, `Print`, and `Apply` are
safe after repeated clears. `Apply` visits the synthetic root in preorder and
returns zero when a callback returns zero. `Print` no longer stores the borrowed
stream in the tree and suppresses the internal terminal NUL. Empty keys return
the conventional one-based position 1. Node deletion, printing, and Apply use
the existing parent/sibling links iteratively, avoiding recursion depth limits.

The dedicated `unittests/suffixtree_test.c` covers boundaries, copied-source
ownership, non-NUL binary bytes (including 128 and 255), randomized differential
searching, null/error paths, Clear/Apply/Print, independent custom allocators,
and every allocation-failure point. GCC coverage for the dedicated run reports
94.04% line coverage, 99.25% branch sites executed, and 81.72% branches taken.
ASan/UBSan passes with leak detection disabled. LeakSanitizer itself cannot run
in this workspace because its ptrace restriction reports `LeakSanitizer has
encountered a fatal error`; this is reported as unavailable rather than passed.

## Current local integration verification

The current unsanitized CTest run passes all 36 registered tests. The focused
ASan/UBSan run passes with leak detection disabled. LeakSanitizer cannot
initialize in this ptrace-restricted workspace, so no local LSan pass is claimed;
run the leak-enabled suite outside that restriction for final leak verification.
