cmake_minimum_required(VERSION 3.10)

# Test executable for ValArray tests
set(UNIT_TESTS runtests)
set(SOURCE_FILES 
    runtests.c 
    valarrayinttest.c
)

message(STATUS "Building unit tests: ${UNIT_TESTS}")

add_executable(${UNIT_TESTS} ${SOURCE_FILES})
target_link_libraries(${UNIT_TESTS} ccl)

# Link math library on Unix systems
if (UNIX)
    target_link_libraries(${UNIT_TESTS} m)
endif()

