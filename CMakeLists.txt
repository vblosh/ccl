cmake_minimum_required(VERSION 3.13)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_modules")

project(ccl LANGUAGES C)
set(CMAKE_ECLIPSE_VERSION 4.33)

include(CTest)

option(CCL_ENABLE_SANITIZERS
       "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(CCL_ENABLE_COVERAGE
       "Build with GCC/gcov coverage instrumentation" OFF)

if(CCL_ENABLE_SANITIZERS AND CCL_ENABLE_COVERAGE)
    message(FATAL_ERROR
            "CCL_ENABLE_SANITIZERS and CCL_ENABLE_COVERAGE are mutually exclusive")
endif()

if(CCL_ENABLE_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR
                "CCL_ENABLE_SANITIZERS requires a GNU or Clang C compiler")
    endif()
    set(CCL_INSTRUMENT_COMPILE_OPTIONS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer)
    set(CCL_INSTRUMENT_LINK_OPTIONS
        -fsanitize=address,undefined)
endif()

if(CCL_ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "CCL_ENABLE_COVERAGE requires GNU C and gcov")
    endif()

    find_program(CCL_GCOV_EXECUTABLE NAMES gcov REQUIRED)
    execute_process(
        COMMAND "${CCL_GCOV_EXECUTABLE}" --version
        OUTPUT_VARIABLE CCL_GCOV_VERSION_OUTPUT
        ERROR_VARIABLE CCL_GCOV_VERSION_ERROR
        RESULT_VARIABLE CCL_GCOV_VERSION_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT CCL_GCOV_VERSION_RESULT EQUAL 0)
        message(FATAL_ERROR "Unable to execute gcov: ${CCL_GCOV_VERSION_ERROR}")
    endif()
    string(REGEX MATCH "([0-9]+)" CCL_GCOV_VERSION_MAJOR
           "${CCL_GCOV_VERSION_OUTPUT}")
    string(REGEX MATCH "^[0-9]+" CCL_GCC_VERSION_MAJOR
           "${CMAKE_C_COMPILER_VERSION}")
    if(NOT CCL_GCOV_VERSION_MAJOR OR
       NOT CCL_GCC_VERSION_MAJOR OR
       NOT CCL_GCOV_VERSION_MAJOR STREQUAL CCL_GCC_VERSION_MAJOR)
        message(FATAL_ERROR
                "gcov (${CCL_GCOV_VERSION_OUTPUT}) must match GCC "
                "${CMAKE_C_COMPILER_VERSION}")
    endif()
    set(CCL_INSTRUMENT_COMPILE_OPTIONS
        --coverage
        -O0
        -g
        -fprofile-abs-path)
    set(CCL_INSTRUMENT_LINK_OPTIONS --coverage)
endif()

# Export no symbols by default (if the compiler supports it).
# This makes e.g. GCC's "visibility behavior" consistent with MSVC's.
# On Windows/MSVC this is a noop.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# The version number
set(project_name_VERSION_MAJOR 0)
set(project_name_VERSION_MINOR 1)

# output directories
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

set(CCL_SOURCES
    src/bitstrings.c
    src/bloom.c
    src/buffer.c
    src/deque.c
    src/dictionary.c
    src/dlist.c
    src/doubledlist.c
    src/doublelist.c
    src/error.c
    src/fgetline.c
    src/generic.c
    src/hashtable.c
    src/heap.c
    src/iMask.c
    src/intdlist.c
    src/intlist.c
    src/list.c
    src/longlongdlist.c
    src/longlonglist.c
    src/malloc_debug.c
    src/memorymanager.c
    src/observer.c
    src/pool.c
    src/pooldebug.c
    src/priorityqueue.c
    src/qsortex.c
    src/qsort_r.c
    src/queue.c
    src/redblacktree.c
    src/scapegoat.c
    src/searchtree.c
    src/sequential.c
    src/smallpool.c
    src/strcollection.c
    src/stringlist.c
    src/SuffixTree.c
    src/valarraydouble.c
    src/valarrayfloat.c
    src/valarrayint.c
    src/valarraylongdouble.c
    src/valarraylonglong.c
    src/valarrayshort.c
    src/valarraysize_t.c
    src/valarrayuint.c
    src/valarrayulonglong.c
    src/vector.c
    src/vectorsize_t.c
    src/wdictionary.c
    src/wstrcollection.c
    src/wstringlist.c
    )

add_library(ccl STATIC ${CCL_SOURCES})

target_include_directories(ccl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include/ccl>)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ccl PRIVATE -Wall -Wno-pointer-sign -Werror)
endif()

if(UNIX)
    target_compile_definitions(ccl PRIVATE UNIX)
endif()

if(MSVC)
    target_compile_definitions(ccl PRIVATE
        _CRT_SILENCE_NONCONFORMING_TGMATH_H
        _CRT_SECURE_NO_WARNINGS)
endif()

if(CCL_INSTRUMENT_COMPILE_OPTIONS)
    target_compile_options(ccl PRIVATE ${CCL_INSTRUMENT_COMPILE_OPTIONS})
endif()
if(CCL_INSTRUMENT_LINK_OPTIONS)
    # A static archive contains instrumented objects but performs no link of
    # its own. Propagate the required sanitizer/coverage runtime flags to
    # every consumer, including projects that embed ccl via add_subdirectory.
    target_link_options(ccl PUBLIC ${CCL_INSTRUMENT_LINK_OPTIONS})
endif()

set_target_properties(ccl PROPERTIES SOVERSION ${VERSION_MAJOR} VERSION ${VERSION_STRING})

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)

    # Let's nicely support folders in IDEs
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Add unittests subdirectory
    add_subdirectory(unittests)
endif()

install(DIRECTORY include/ DESTINATION include/ccl
        FILES_MATCHING PATTERN "*.h")
install(TARGETS ccl DESTINATION lib)
