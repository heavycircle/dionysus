# Injected by `dino build` via CMAKE_PROJECT_INCLUDE, so it runs after every project() call.
# This is what lets a plain CMakeLists.txt link `dino_core` and `dino_util` without vendoring the SDK.
include_guard(GLOBAL)

if(NOT DINO_SDK_PATH)
    get_filename_component(DINO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

if(NOT TARGET dino_core)
    add_subdirectory("${DINO_SDK_PATH}" "${CMAKE_BINARY_DIR}/_dino" EXCLUDE_FROM_ALL)
endif()
# For libraries that need a minimum SDK: if(DINO_SDK_VERSION VERSION_LESS 0.6)
get_directory_property(DINO_SDK_VERSION DIRECTORY "${DINO_SDK_PATH}" DEFINITION DinoSDK_VERSION)

function(dino_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dino_util)
endfunction()

# The first project() is the top-level one, so `ctest` and `dino test` find the
# tests at the root of the build directory.
enable_testing()
# Test targets get FOLDER dino_tests, which the CMake file API reports, so
# `dino run` can tell them apart from the program itself.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# A test executable: dino_util and dino_core plus utest.h (`#include <utest.h>`), and on Linux
# targets a leak check that fails the test if anything is still allocated at
# exit. Registered with CTest, which runs foreign targets under qemu or wine.
function(dino_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dino_testing)
    set_target_properties(${name} PROPERTIES FOLDER dino_tests)
    # A library's tests belong to the library: they run when it's the project
    # being tested, not in every project that has it on DINO_LIBRARY_PATH. The
    # target still exists so the library's own target_link_libraries() works.
    if(NOT DINO__LIBRARY_DEPENDENCY)
        add_test(NAME ${name} COMMAND ${name})
    endif()
endfunction()

# Organization libraries from DINO_LIBRARY_PATH (a CMake list; `dino` passes
# the environment variable of the same name). EXCLUDE_FROM_ALL builds one only
# when something links it. A library that is itself the top-level project,
# because it's being built or tested on its own, isn't added a second time.
get_filename_component(_dino_top "${CMAKE_SOURCE_DIR}" REALPATH)
set(DINO__LIBRARY_DEPENDENCY ON)
foreach(_dino_library IN LISTS DINO_LIBRARY_PATH)
    get_filename_component(_dino_library "${_dino_library}" REALPATH)
    if(NOT _dino_library STREQUAL _dino_top)
        get_filename_component(_dino_name "${_dino_library}" NAME)
        add_subdirectory("${_dino_library}" "${CMAKE_BINARY_DIR}/_dino_libs/${_dino_name}" EXCLUDE_FROM_ALL)
    endif()
endforeach()
unset(DINO__LIBRARY_DEPENDENCY)
unset(_dino_top)
unset(_dino_library)
unset(_dino_name)
