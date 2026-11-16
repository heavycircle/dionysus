# Injected by `dino build` via CMAKE_PROJECT_INCLUDE, so it runs after every project() call.
# This is what lets a plain CMakeLists.txt link `dino_core` without vendoring the SDK.
include_guard(GLOBAL)

if(NOT DINO_SDK_PATH)
    get_filename_component(DINO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

if(NOT TARGET dino_core)
    add_subdirectory("${DINO_SDK_PATH}" "${CMAKE_BINARY_DIR}/_dino" EXCLUDE_FROM_ALL)
endif()

function(dino_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dino_core)
endfunction()

# The first project() is the top-level one, so `ctest` and `dino test` find the
# tests at the root of the build directory.
enable_testing()
# Test targets get FOLDER dino_tests, which the CMake file API reports, so
# `dino run` can tell them apart from the program itself.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# A test executable: dino_core plus utest.h (`#include <utest.h>`), and on Linux
# targets a leak check that fails the test if anything is still allocated at
# exit. Registered with CTest, which runs foreign targets under qemu or wine.
function(dino_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dino_testing)
    set_target_properties(${name} PROPERTIES FOLDER dino_tests)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
