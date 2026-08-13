# ValArray family implementation and verification

## Scope

The ValArray implementation is a macro-instantiated generator family.  The
physical production sources are:

| File | Role | Specialization |
| --- | --- | --- |
| `src/valarraygen.c` | shared implementation body | selected by wrapper macros |
| `src/valarrayint.c` | wrapper | `int` |
| `src/valarraydouble.c` | wrapper | `double` |
| `src/valarrayfloat.c` | wrapper | `float` |
| `src/valarraylongdouble.c` | wrapper | `long double` |
| `src/valarraylonglong.c` | wrapper | `long long` |
| `src/valarrayshort.c` | wrapper | `short` |
| `src/valarraysize_t.c` | wrapper | `size_t` |
| `src/valarrayuint.c` | wrapper | `unsigned` |
| `src/valarrayulonglong.c` | wrapper | `unsigned long long` |

The public macro declarations are in `include/valarraygen.h` and
`include/valarray.h`; the concrete interfaces are declared through the same
wrapper names (`iValArrayInt`, `iValArrayDouble`, and so on).  The ownership
unit is `unittests/valarrayinttest.c`; its suite also links and smoke-tests all
nine concrete wrappers.

## Generator boundaries

Every wrapper defines `_ValArray`, `ValArray`, `ElementType`, the interface
type, allocator interface, GUID, and magic number before including
`valarraygen.c`.  Integer wrappers additionally define `__IS_INTEGER__` and
unsigned wrappers define `__IS_UNSIGNED__`.  Consequently the generated API
has three specialization boundaries:

* integer-only modulo operations;
* unsigned-only bitwise and shift operations;
* signed/floating absolute value, or floating tolerance comparison and
  inverse operations.

The suite exercises creation, insertion, indexing, slices, serialization,
comparison, arithmetic, iterator paths, observer callbacks, and these
specialization-only operations.  It also creates an array through each of the
nine wrapper interfaces, adds a typed value, reads it back, and finalizes it.

## Confirmed defect fixes

`CompareEqualScalar` previously allocated only `1 + len / CHAR_BIT` bytes and
then treated that allocation as a `Mask`.  A `Mask` has a header followed by
one byte per logical element, so the first `data` access read and wrote beyond
the allocation.  The function now uses `iMask.Create`, reuses an existing
mask only when its capacity is sufficient, clears the data region, writes one
boolean byte per element, and sets the returned logical length.  The
regression checks mask length, all four match/non-match values, and the
reuse path under sanitizers.

The unused `siz` assignment in `Compare` was removed.  This keeps all nine
generator instantiations clean under GCC `-Wall -Werror` without changing the
public interface or comparison result.

The pre-existing ValArray analysis documents remain in `analysis/` and are
preserved.  This report records only the generator-family inventory and the
defect/test evidence for this ownership pass.

## Verification

The direct GCC unit build uses all nine wrappers and the generator dependencies
with `-Wall -Wno-pointer-sign -Werror`; all 84 tests pass.  The equivalent
AddressSanitizer + UndefinedBehaviorSanitizer build passes all 84 tests with
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; the original
`CompareEqualScalar` overflow is not reproduced.

Coverage was measured after running the suite once with GCC coverage
instrumentation and aggregating the nine wrapper instantiations for the
canonical generator source.  The result is:

* lines: 1,079 / 1,312 (82.24%);
* branches taken: 485 / 686 (70.70%).

Both values clear the ownership thresholds of 80% lines and 70% branches.

LeakSanitizer cannot complete in this execution environment: with leak
detection enabled, the process aborts with `LeakSanitizer has encountered a
fatal error` and reports that LeakSanitizer does not work under ptrace.  This
is an environment limitation rather than a passing leak report; the
non-leak sanitizer checks pass as stated above.

## Final integration verification

The final untraced integration run passed with ASan and UBSan after correcting
the test-owned mask cleanup. LeakSanitizer is unavailable in the current
ptrace-restricted environment, so no leak-enabled pass is claimed here.
