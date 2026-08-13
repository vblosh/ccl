# Build, test, sanitizer, and coverage infrastructure audit

## Current scope (2026-08-13)

This report describes the current `feature/ranges` worktree. The infrastructure
tracks 58 physical `src/*.c` files as 36 ownership units. Generator
implementations and their typed wrapper translation units share one ownership
unit so coverage is charged to the implementation that actually produces gcov
records.

The top-level build creates the static `ccl` library. When CCL is the top-level
project and `BUILD_TESTING` is enabled, `unittests/CMakeLists.txt` discovers the
`*_test.c` suites and creates one executable and one CTest per suite. The legacy
`valarrayinttest.c` source is registered explicitly as `test_valarray` during
its filename migration. The current result is 36 independent executables;
`test_valarray` contains 84 cases.

Legacy programs under `tests/` are not CTest suites. The authoritative current
test list is the CTest registry generated from `unittests/CMakeLists.txt`.

## Historical baseline

The original infrastructure audit was produced from `develop` commit
`3a8e480ca5ab7a34308dfa3998556cba8935ea2f`. At that point the build overwrote
caller flags, had one centrally registered `runtests` executable, exposed no
CTest tests, and the 82-case ValArray suite was below the coverage gates. A
sanitizer probe also found the then-current `CompareEqualScalar` overflow.

Those observations are retained as remediation history, not current-state
claims. Target-scoped flags, CTest registration, one-suite executables,
sanitizer/coverage options, isolated work directories, and the JSON coverage
checker are now implemented.

## Current CMake and CTest architecture

The top-level CMake file:

- uses target-scoped warnings, includes, platform definitions, and
  instrumentation rather than replacing `CMAKE_C_FLAGS`;
- exposes `CCL_ENABLE_SANITIZERS` and `CCL_ENABLE_COVERAGE`, both defaulting to
  `OFF`, and rejects enabling them together;
- accepts GNU, Clang, or AppleClang for AddressSanitizer/UBSan and requires GNU
  C plus a matching `gcov` major version for coverage;
- propagates sanitizer or coverage link options from the static `ccl` target
  to consumers;
- honors `BUILD_TESTING` and the top-level-project guard; and
- installs the public headers and static library without installing tests.

`ccl_add_unit_test` builds each suite from `test_main.c`, `test_support.c`, and
one suite source. Each CTest receives a private working directory below
`<build>/unittests/work`, and the aggregate `check` target builds all suites
before invoking CTest. Automatic discovery is convenient, but a new suite must
still have a matching ownership entry in `coverage_manifest.json` before the
coverage audit is complete.

## Sanitizer contract and current result

Sanitizer builds compile and link the library and all tests with:

```text
-fsanitize=address,undefined -fno-omit-frame-pointer
```

CTest configures `halt_on_error=1` for both sanitizers and requests
`ASAN_OPTIONS=detect_leaks=1`. In the current execution environment,
LeakSanitizer aborts during startup because the process is ptrace-restricted.
That is an environment limitation rather than evidence that every test has a
leak.

A fresh GCC 15.2 sanitizer build was therefore also run with
`ASAN_OPTIONS=detect_leaks=0`. All 36 executables passed under AddressSanitizer
and UndefinedBehaviorSanitizer. This report does not claim a current
LeakSanitizer pass. A non-ptraced CI or local environment should retain
`detect_leaks=1` as the final leak gate.

## Coverage ownership and checker

`unittests/coverage_manifest.json` is authoritative for coverage ownership. It
must satisfy all of these invariants:

1. Every physical `src/*.c` file appears exactly once in `physical_sources`.
2. Every listed path exists and no unit name is duplicated.
3. Each `coverage_sources` entry produces gcov data in the requested unit run.
4. Generator families list all wrappers as physical sources but charge
   executable coverage to the textually included generator source.
5. Each unit selects exactly one CTest, using an explicit `test` override when
   its ownership name and suite name differ.

For each unit, `check_coverage.py` removes only `.gcda` counters below the
selected build tree, runs the exact owning CTest, invokes the matching gcov in
JSON/branch mode, canonicalizes source paths, and unions duplicate records
from generator instantiations. Lines count as covered when their summed count
is positive. Branch coverage means branches taken at least once, not gcov's
less stringent “branches executed” metric.

The checker writes one JSON report below `<build>/coverage`, prints a concise
summary, and fails a unit below 80% lines or 70% taken branches. All-units mode
runs sequentially because test executables share the instrumented library's
counter files.

## Current verification results

A fresh default GCC 15.2 build passes all 36 CTests. The coverage run completes
all units and reports 33 passes plus three branch-gate failures:

| Unit | Lines | Taken branches | Result |
| --- | ---: | ---: | --- |
| `dlist` | 82.00% | 69.94% | branch gate fails |
| `hashtable` | 85.41% | 69.88% | branch gate fails |
| `vector` | 80.96% | 69.35% | branch gate fails |

`range`, the new ownership unit, passes at 87.46% lines and 76.03% branches.
The complete current table is maintained in `analysis/modules/README.md` and
is reproducible from the manifest. `coverage-check` intentionally returns
nonzero while the three deficits remain, even though it processes and reports
all 36 units.

## Reproduction commands

Default build and test:

```text
cmake -S . -B <build> -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build <build>
ctest --test-dir <build> --output-on-failure
```

Sanitizers:

```text
cmake -S . -B <asan-build> -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DCCL_ENABLE_SANITIZERS=ON
cmake --build <asan-build>
ctest --test-dir <asan-build> --output-on-failure
```

If LeakSanitizer cannot initialize because of ptrace, rerun the executables
with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` to separate ASan/UBSan
results from the unavailable leak check. Do not present that fallback as an
LSan pass.

Coverage:

```text
cmake -S . -B <coverage-build> -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DCCL_ENABLE_COVERAGE=ON
cmake --build <coverage-build>
cmake --build <coverage-build> --target coverage-check
```

## Maintenance checklist

- A new `src/*.c` file has a manifest owner, focused suite, coverage target,
  module report, and index row.
- `ctest -N` count, manifest unit count, ownership-report count (excluding this
  infrastructure report), and index count agree.
- Test work directories and temporary files remain isolated.
- Sanitizer and coverage configurations remain mutually exclusive and fail
  loudly on unsupported toolchains.
- Coverage percentages are refreshed after production or test changes; the
  index must not retain a historical all-green claim after a gate regression.
- ASan/UBSan, LeakSanitizer, and coverage outcomes are reported separately so
  an environmental LSan failure cannot be mistaken for a library result.
