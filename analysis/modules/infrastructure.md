# Build, test, sanitizer, and coverage infrastructure audit

## Scope and baseline

This report is an implementation handoff for the infrastructure bootstrap. It
was produced from `develop` at commit
`3a8e480ca5ab7a34308dfa3998556cba8935ea2f` using CMake 4.2.3, GCC/gcov
15.2.0, and Clang 21.1.8. It does not change production code, CMake, tests, or
the shared module index.

The current build has one static target, `ccl`, and one test executable,
`runtests`. The executable embeds the suite registry in `unittests/runtests.c`,
so every new suite would have to edit that shared file. The only suite is the
2,167-line `unittests/valarrayinttest.c` suite. It contains 82 tests. The
legacy programs under `tests/` are not built by CMake and are not unit suites.

There is no CTest integration. `ctest --test-dir <build> --output-on-failure`
returns success while printing `No tests were found!!!`. This is a false-green
result and must be fixed before relying on CTest in the campaign.

## Reproduced baseline failures

### GCC build is red and caller flags are overwritten

Configuring with

```text
cmake -S . -B <build> -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS=-DCCL_CALLER_FLAG=1
```

leaves the requested value in `CMakeCache.txt`, but the generated
`CMakeFiles/ccl.dir/flags.make` contains only:

```text
-Wall -Wno-pointer-sign -Werror -g -fvisibility=hidden
```

The top-level `set(CMAKE_C_FLAGS ...)` therefore discards caller-provided
flags. In particular, an attempted GCC sanitizer or coverage configuration can
silently lose its compile instrumentation. The condition is also wrong: it
tests the C++ variable `CMAKE_COMPILER_IS_GNUCXX` even though the flags affect C
sources.

With the flags forced by the project, GCC fails in every ValArray
instantiation:

```text
src/valarraygen.c:1645:16: error: variable 'siz' set but not used
[-Werror=unused-but-set-variable]
```

Once that production warning is allowed through for measurement, the existing
test source also emits five warning classes that become errors under the
global `-Werror`: one unused local in `test_mismatch`, two unused locals in
`test_front_back_readonly`, one unused local in `test_apply_with_slice`, and
two unused static print helpers.

### Sanitizer run finds a production overflow

A target-instrumented equivalent Clang build with
`-fsanitize=address,undefined -fno-omit-frame-pointer` builds. Running
`runtests` with leak detection and halt-on-error reaches test 35 of 82, then
aborts:

```text
AddressSanitizer: heap-buffer-overflow
READ of size 1
src/valarraygen.c:1634:31 in CompareEqualScalar
unittests/valarrayinttest.c:806 in test_compare_equal_scalar
```

The allocation is one byte at `src/valarraygen.c:1627`; the invalid read is 15
bytes past it. This is the first regression the ValArray coding agent must keep
under the new harness.

### Unsanitized tests are green but coverage is below the gate

After locally working around the warning policy solely for measurement, all 82
existing tests pass without sanitizers. GCC JSON coverage for the instantiated
`src/valarraygen.c` implementation is:

```text
Lines executed:          63.85% of 1195
Branches executed:       78.90% of 616
Branches taken >= once:  51.95% of 616
```

The last value is the branch coverage metric required by the plan. The current
suite is therefore below both the 80% line and 70% branch gates. Plain execution
also fails to reveal the heap overflow, which demonstrates why sanitizer runs
must remain separate and mandatory.

## Required top-level CMake changes

1. Remove the assignment to `CMAKE_C_FLAGS`. Never mutate cached caller flags.
   Apply the existing GNU warning policy to `ccl` with
   `target_compile_options`; check `CMAKE_C_COMPILER_ID`, not a C++ variable.
   Do not broaden the warning set as part of this bootstrap. Apply warnings to
   test targets deliberately rather than through directory-global flags. Test
   sources should be warning-clean, but warning-policy expansion can be a later
   change.

2. Replace directory-global include paths with
   `target_include_directories(ccl PUBLIC ...)`, using build and install
   interfaces. Keep platform definitions target-scoped as well (`UNIX` on
   `ccl`; the CRT definitions on targets that require them).

3. Add cache options, both defaulting to `OFF`:

   ```cmake
   option(CCL_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
   option(CCL_ENABLE_COVERAGE "Build with GCC/gcov coverage instrumentation" OFF)
   ```

   Reject a configuration that enables both. Sanitizer mode is a failure on
   unsupported compilers; do not silently create an uninstrumented build.
   Coverage mode must require GNU C and a `gcov` matching the compiler major
   version.

4. Instrument the library and every test translation unit. For GNU/Clang
   sanitizer builds use the compile flags
   `-fsanitize=address,undefined -fno-omit-frame-pointer` and ensure every test
   executable receives `-fsanitize=address,undefined` at link time. For GCC
   coverage builds use `--coverage -O0 -g -fprofile-abs-path` at compile time
   and `--coverage` at link time. `-fprofile-abs-path` is important because the
   JSON checker must map template generator files back to canonical manifest
   paths.

   A static archive has no sanitizer runtime link step. Either put the link
   flags on `ccl` as `INTERFACE` usage requirements or apply them explicitly in
   the unit-test helper. Merely adding sanitizer compile flags to `ccl` is not
   sufficient.

5. Use `include(CTest)` at the top level and honor `BUILD_TESTING`. Keep the
   existing project-is-top-level guard so embedding CCL does not unconditionally
   add its tests. Add `unittests` only when both conditions are true.

6. Add an aggregate `check` target that depends on all unit-test executables
   and runs `${CMAKE_CTEST_COMMAND} --output-on-failure`. Do not call a target
   `test`, because CMake generators reserve that name for CTest.

If `target_link_options` and modern `FindPython3` are used, raise the minimum
CMake version from 3.10 to at least 3.13. Alternatively, retain 3.10 and pass
driver link flags through target link libraries, but the modern target-link
form is clearer and less error-prone.

## One-suite-per-executable convention

Create a single `ccl_add_unit_test(unit source)` helper in
`unittests/CMakeLists.txt`. For a unit named `valarray`, it must:

- build `test_valarray` from the common runner/support plus exactly one suite
  source;
- link `ccl`, the math library where required, and active instrumentation;
- register exactly one CTest named `test_valarray`;
- give the CTest a private working directory such as
  `<binary>/unittests/work/test_valarray`;
- apply the sanitizer environment when sanitizer mode is enabled; and
- append the executable to the list used by `check`.

Every suite source exports the same getter because suites live in separate
executables:

```c
const TestSuite *ccl_get_test_suite(void);
```

The common `test_main.c` calls that getter and passes the result to
`ccl_run_suite`. This removes the central registry and prevents parallel agents
from editing a shared runner. Test counts should use `size_t`, and suite/test
arrays should be `const`.

Initially migrate `valarrayinttest.c` to the `valarray` ownership-unit target.
Do not register the legacy `tests/test.c` program as a unit test; it aborts on
failure, contains unrelated containers in one process, and writes fixed files.
Its useful cases should be moved into their owning suites later.

Recommended file split:

```text
unittests/test_main.c          generic main, one suite per executable
unittests/test_support.h       types, assertions, declarations, case macro
unittests/test_support.c       runner, diagnostics, temporary-file helpers
unittests/<unit>_test.c        one ownership-unit suite
```

## Test cleanup and temporary files

The current `TEST_ASSERT` immediately returns from a test. If an assertion
fails after an allocation or `fopen`, the container/file is leaked and a leak
sanitizer can obscure the original assertion. New and migrated tests must use a
single cleanup path:

```c
static int test_example(void)
{
    int rc = -1;
    SomeContainer *value = NULL;
    FILE *stream = NULL;

    value = create_value();
    TEST_REQUIRE(value != NULL); /* records location and jumps to cleanup */
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    /* assertions */
    rc = 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (value != NULL) finalize_value(value);
    return rc;
}
```

`TEST_REQUIRE` should report the suite case, expression/description, file, and
line, then jump to the mandatory `cleanup` label. Avoid assertions whose
condition has side effects.

Use `tmpfile()` whenever the API accepts `FILE *`. All six fixed-name files in
the current ValArray suite (`test_valarray.bin`, `test_load_valarray.bin`,
`test_roundtrip.bin`, `test_empty.bin`, `test_single.bin`, and
`test_fprintf.txt`) can be eliminated this way by flushing/seeking/rewinding the
same stream. For an API that genuinely requires a pathname, add a shared helper
implemented with `mkstemp` on POSIX and `GetTempPath`/`GetTempFileName` on
Windows, and register/unlink it on the cleanup path. The per-CTest working
directory is a second layer of isolation, not a replacement for unique files.

## GCC JSON coverage checker

Add a standard-library-only script, for example
`unittests/check_coverage.py`, and a coordinator-owned
`unittests/coverage_manifest.json`. The manifest is authoritative for ownership
and must represent every physical `src/*.c` file exactly once. Use a dual list
for generator families because wrapper translation units contain macros and an
include but often produce no executable gcov lines:

```json
{
  "units": {
    "valarray": {
      "physical_sources": [
        "src/valarraygen.c",
        "src/valarrayint.c",
        "src/valarraydouble.c"
      ],
      "coverage_sources": ["src/valarraygen.c"]
    },
    "vector": {
      "physical_sources": ["src/vector.c"],
      "coverage_sources": ["src/vector.c"]
    }
  }
}
```

The real ValArray entry must contain all nine wrappers, not just the abbreviated
example. Equivalent generator-family entries are required for dictionary,
dlist, list, strcollection, stringlist, and vectorgen. Standalone units use the
same file in both lists. The checker must validate that all listed paths exist,
that physical ownership has no duplicates, and that requested units exist.

For one unit, the checker must perform this sequence:

1. Resolve and validate the source/build roots and require a CMake cache in the
   build root.
2. Delete only `*.gcda` below that build root, so a prior suite cannot inflate
   this unit's numbers.
3. Run exact CTest `^test_<unit>$` and stop on failure.
4. Discover all `*.gcno` files below the build root. Invoke matching `gcov`
   separately for each with
   `--json-format --branch-probabilities --branch-counts` in a fresh temporary
   output directory, then parse its `.gcov.json.gz` immediately. Separate
   directories avoid output-name collisions.
5. Canonicalize each JSON `files[].file` path and retain only this manifest
   unit's `coverage_sources`.
6. Union executable lines by `(source path, line_number)` and mark a line
   covered if the sum of its execution counts across instantiations is greater
   than zero.
7. Union branches by `(source path, line_number, source_block_id,
   destination_block_id, fallthrough, throw)` and mark a branch covered if its
   summed count is greater than zero. Generator sources occur in several
   objects; treating duplicate records as new branches would distort the
   denominator.
8. Fail if a required coverage source has no gcov data. Compute line and branch
   percentages, write a machine-readable report under
   `<build>/coverage/<unit>.json`, print a concise summary, and return nonzero
   below 80% lines or 70% branches. If the union contains zero branches, report
   branch coverage as `N/A` and pass that gate.

Do not compute branch coverage from gcov's `Branches executed` value. The gate
is branches with count greater than zero, corresponding to gcov's `Taken at
least once` value.

Expose two kinds of coverage targets only when `CCL_ENABLE_COVERAGE=ON`:

- `coverage-<unit>` invokes the checker for one unit;
- `coverage-check` invokes it in all-units mode.

All-units mode must process units sequentially: clear counters, run one exact
CTest, report it, then move to the next. CMake dependency fan-out is unsuitable
because parallel coverage targets would share and corrupt `ccl`'s `.gcda`
files. Independent coding agents should continue using distinct `/tmp` build
directories as planned.

## Sanitizer execution contract

Register these CTest environment values in sanitizer mode:

```text
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

The exact per-unit loop is:

```text
cmake -S . -B <unique-build> -DCMAKE_BUILD_TYPE=Debug \
  -DCCL_ENABLE_SANITIZERS=ON
cmake --build <unique-build> --target test_<unit>
ctest --test-dir <unique-build> -R '^test_<unit>$' --output-on-failure
```

After every unit is clean, build all tests and run full CTest in a fresh
sanitizer directory. Coverage uses a separate fresh build with
`CCL_ENABLE_COVERAGE=ON`; the mutually exclusive configuration prevents mixed
runtime and counter failures.

## Bootstrap acceptance checklist

- A caller-provided `CMAKE_C_FLAGS` marker remains on `ccl` compile commands.
- Default GCC and Clang builds complete after the known source/test warnings are
  fixed by their owning agents.
- `ctest -N` lists one test for every registered ownership unit; it never
  reports a false-green zero-test run in a normal top-level build.
- `test_valarray` reproduces the `CompareEqualScalar` overflow before its source
  fix and passes cleanly afterward under ASan/UBSan/leak detection.
- Parallel ordinary CTest execution cannot collide on fixed filenames.
- `coverage-valarray` reports the current baseline below the gate, then passes
  only after meaningful tests raise lines to at least 80% and taken branches to
  at least 70%.
- `coverage-check` isolates units sequentially and emits one JSON report per
  manifest unit.
- Configuring sanitizer and coverage together, coverage with Clang, or either
  mode on an unsupported toolchain fails loudly at configure time.
