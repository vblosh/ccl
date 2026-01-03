cmake_minimum_required(VERSION 3.10)

#GTEST
include(GoogleTest)

set(SOURCE_FILES intListTests.cpp)
set(UNIT_TESTS unittests)

# Use the output directory set in the main project
message(STATUS "Library output directory in subproject: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

add_executable(${UNIT_TESTS} ${SOURCE_FILES})
# target_include_directories(${UNIT_TESTS} PRIVATE )
target_link_libraries(${UNIT_TESTS} GTest::GTest GTest::Main ccl)

