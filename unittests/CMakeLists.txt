cmake_minimum_required(VERSION 3.13)

set(CCL_UNIT_TEST_TARGETS)

function(ccl_add_unit_test unit suite_source)
    set(target "test_${unit}")
    if(TARGET "${target}")
        message(FATAL_ERROR "Duplicate CCL unit-test target: ${target}")
    endif()

    get_filename_component(suite_source_abs "${suite_source}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${suite_source_abs}")
        message(FATAL_ERROR "Unit-test suite does not exist: ${suite_source}")
    endif()

    add_executable("${target}"
        test_main.c
        test_support.c
        "${suite_source_abs}")
    target_include_directories("${target}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries("${target}" PRIVATE ccl)

    if(UNIX)
        target_link_libraries("${target}" PRIVATE m)
    endif()

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # Keep the test warning policy target-scoped.  The historical
        # ValArray suite has a few intentionally unused fixtures; those must
        # not alter the caller's C flags or the library warning policy.
        target_compile_options("${target}" PRIVATE -Wall -Wno-pointer-sign)
    endif()

    if(CCL_INSTRUMENT_COMPILE_OPTIONS)
        target_compile_options("${target}" PRIVATE
            ${CCL_INSTRUMENT_COMPILE_OPTIONS})
    endif()
    if(CCL_INSTRUMENT_LINK_OPTIONS)
        target_link_options("${target}" PRIVATE
            ${CCL_INSTRUMENT_LINK_OPTIONS})
    endif()

    set(work_directory "${PROJECT_BINARY_DIR}/unittests/work/${target}")
    file(MAKE_DIRECTORY "${work_directory}")
    add_test(NAME "${target}" COMMAND "${target}")
    set_tests_properties("${target}" PROPERTIES
        WORKING_DIRECTORY "${work_directory}")
    if(CCL_ENABLE_SANITIZERS)
        set_tests_properties("${target}" PROPERTIES
            ENVIRONMENT
                "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
    endif()

    set(CCL_UNIT_TEST_TARGETS "${CCL_UNIT_TEST_TARGETS};${target}" PARENT_SCOPE)
endfunction()

# Suites with the conventional <unit>_test.c name are picked up without a
# shared registry.  A legacy source name is listed explicitly during the
# migration; its executable still follows the same one-suite contract.
file(GLOB CCL_AUTOMATIC_SUITE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*_test.c")
foreach(suite_source IN LISTS CCL_AUTOMATIC_SUITE_SOURCES)
    get_filename_component(unit "${suite_source}" NAME_WE)
    string(REGEX REPLACE "_test$" "" unit "${unit}")
    ccl_add_unit_test("${unit}" "${suite_source}")
endforeach()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/valarrayinttest.c")
    ccl_add_unit_test(valarray valarrayinttest.c)
endif()

if(CCL_UNIT_TEST_TARGETS)
    add_custom_target(check
        DEPENDS ${CCL_UNIT_TEST_TARGETS}
        COMMAND "${CMAKE_CTEST_COMMAND}"
                --test-dir "${PROJECT_BINARY_DIR}"
                --output-on-failure
        USES_TERMINAL)
endif()

if(CCL_ENABLE_COVERAGE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CCL_COVERAGE_MANIFEST
        "${CMAKE_CURRENT_SOURCE_DIR}/coverage_manifest.json")
    set(CCL_COVERAGE_CHECKER
        "${CMAKE_CURRENT_SOURCE_DIR}/check_coverage.py")

    # Coverage counters are process-global files.  The checker intentionally
    # runs suites one at a time, so this target must not express one target
    # dependency per unit.
    foreach(unit_entry IN ITEMS
            dictionary dlistgen listgen strcollectiongen stringlistgen
            valarray vectorgen
            SuffixTree bitstrings bloom buffer deque dlist error fgetline
            generic hashtable heap iMask list malloc_debug memorymanager
            observer pool pooldebug priorityqueue qsort_r qsortex queue
            redblacktree scapegoat searchtree sequential smallpool vector)
        add_custom_target("coverage-${unit_entry}"
            COMMAND "${Python3_EXECUTABLE}" "${CCL_COVERAGE_CHECKER}"
                    --source-root "${PROJECT_SOURCE_DIR}"
                    --build-root "${PROJECT_BINARY_DIR}"
                    --manifest "${CCL_COVERAGE_MANIFEST}"
                    --gcov "${CCL_GCOV_EXECUTABLE}"
                    --unit "${unit_entry}"
            DEPENDS ${CCL_UNIT_TEST_TARGETS}
            USES_TERMINAL)
    endforeach()

    add_custom_target(coverage-check
        COMMAND "${Python3_EXECUTABLE}" "${CCL_COVERAGE_CHECKER}"
                --source-root "${PROJECT_SOURCE_DIR}"
                --build-root "${PROJECT_BINARY_DIR}"
                --manifest "${CCL_COVERAGE_MANIFEST}"
                --gcov "${CCL_GCOV_EXECUTABLE}"
                --all
        DEPENDS ${CCL_UNIT_TEST_TARGETS}
        USES_TERMINAL)
endif()
