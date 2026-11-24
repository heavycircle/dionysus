# Injected by `dino build` via CMAKE_PROJECT_INCLUDE, so it runs after every project() call.
# This is what lets a plain CMakeLists.txt link `dino_core` and `dino_util` without vendoring the SDK.
include_guard(GLOBAL)

if(NOT DINO_SDK_PATH)
    get_filename_component(DINO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(DINO__PREBUILT_USED OFF)
if(NOT TARGET dino_core AND DINO_SDK_PREBUILT_DIR)
    include("${CMAKE_CURRENT_LIST_DIR}/prebuilt.cmake")
endif()
# For libraries that need a minimum SDK: if(DINO_SDK_VERSION VERSION_LESS 0.6)
if(DINO__PREBUILT_USED)
    set(DINO_SDK_VERSION "${DINO__PREBUILT_VERSION}")
else()
    if(NOT TARGET dino_core)
        add_subdirectory("${DINO_SDK_PATH}" "${CMAKE_BINARY_DIR}/_dino" EXCLUDE_FROM_ALL)
    endif()
    get_directory_property(DINO_SDK_VERSION DIRECTORY "${DINO_SDK_PATH}" DEFINITION DinoSDK_VERSION)
endif()

function(dino_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dino_util dino__utf8)
endfunction()

# What a Release build's link optimizes for. zig builds the C library's own
# memcpy, memset and allocator for the link command's -O level, and the
# compile flags say -Oz: linked that way, memcpy is some 20 times slower.
# "speed" links at -O2, for under a kilobyte on the Linux targets; "size"
# keeps -Oz, which saves about 14 KB of MinGW runtime on Windows. A generator
# expression, not an _INIT flag, so a change takes effect on the next build.
if(ZIG_TARGET MATCHES "windows")
    set(_dino_link_default size)
else()
    set(_dino_link_default speed)
endif()
set(DINO_LINK_FOR "${_dino_link_default}" CACHE STRING
    "What Release builds link for: speed (-O2) or size (-Oz)")
set_property(CACHE DINO_LINK_FOR PROPERTY STRINGS speed size)
unset(_dino_link_default)
if(DINO_LINK_FOR STREQUAL "speed")
    add_link_options("$<$<CONFIG:Release>:-O2>")
elseif(NOT DINO_LINK_FOR STREQUAL "size")
    message(FATAL_ERROR "DINO_LINK_FOR is '${DINO_LINK_FOR}'; use speed or size")
endif()

# The SDK reads its options here, at the first project(): a project that sets
# one later (or changes it) changes nothing, silently. Note what was used, and
# warn at the end of the top-level CMakeLists.txt about any that differ now.
set(DINO__OPTIONS DINO_NO_PLATFORM_WARNINGS DINO_NO_AUTO_RUNTIME DINO_NO_LEAK_CHECK
    DINO_NO_UTF8_MANIFEST DINO_NO_LIBC_SHIMS DINO_SDK_WERROR DINO_LINK_FOR)
foreach(_dino_option IN LISTS DINO__OPTIONS)
    set(DINO__USED_${_dino_option} "${${_dino_option}}")
endforeach()
unset(_dino_option)
function(_dino_check_late_options)
    foreach(option IN LISTS DINO__OPTIONS)
        set(used "${DINO__USED_${option}}")
        set(now "${${option}}")
        if(NOT option STREQUAL "DINO_LINK_FOR")
            # Compared as booleans: ON, TRUE and 1 are the same setting.
            foreach(var used now)
                if(${var})
                    set(${var} ON)
                else()
                    set(${var} OFF)
                endif()
            endforeach()
        endif()
        if(NOT used STREQUAL now)
            message(WARNING "${option} is ${now} here, but the SDK read it as ${used} at project() "
                "and still uses that. Set it before project(), or pass -D${option}=${now} to "
                "`dino build --`.")
        endif()
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _dino_check_late_options)

# The first project() is the top-level one, so `ctest` and `dino test` find the
# tests at the root of the build directory.
enable_testing()
# Test targets get FOLDER dino_tests, which the CMake file API reports, so
# `dino run` can tell them apart from the program itself.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# Test executables are left out of `all`, so `dino build` and `dino run` don't
# compile them; `dino test` builds this target as well. One build directory
# serves both, so switching commands never reconfigures.
add_custom_target(dino_tests)

# A test executable: dino_util and dino_core plus utest.h (`#include <utest.h>`), and on Linux
# targets a leak check that fails the test if anything is still allocated at
# exit. Registered with CTest, which runs foreign targets under qemu or wine.
#
#   dino_test(<name> <sources>... [LIBRARIES <targets>...])
#
# A library's tests belong to the library: they run when it's the project being
# tested, not in every project that has it on DINO_LIBRARY_PATH. With LIBRARIES
# nothing at all is created there. Without it the caller links the target
# itself, so the target has to exist, and target names are global: two
# libraries, or a library and the project, can then only have a test_main each
# by luck.
function(dino_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LIBRARIES")
    if(DINO__LIBRARY_DEPENDENCY AND "LIBRARIES" IN_LIST ARGN)
        return()
    endif()
    if(TARGET ${name})
        get_target_property(library ${name} DINO__LIBRARY_TEST)
        if(library)
            message(FATAL_ERROR
                "dino_test(${name}): the library in ${library} (from DINO_LIBRARY_PATH) "
                "already has a test target with this name, and target names are shared by "
                "the project and every library.\n"
                "Fix the library: give its dino_test() calls the libraries to link, "
                "dino_test(${name} <sources> LIBRARIES <targets>), so that its tests exist "
                "only when the library itself is the project being tested.")
        endif()
    endif()
    add_executable(${name} EXCLUDE_FROM_ALL ${arg_UNPARSED_ARGUMENTS})
    target_link_libraries(${name} PRIVATE dino_testing dino__utf8 ${arg_LIBRARIES})
    set_target_properties(${name} PROPERTIES FOLDER dino_tests)
    if(DINO__LIBRARY_DEPENDENCY)
        set_target_properties(${name} PROPERTIES DINO__LIBRARY_TEST "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        add_test(NAME ${name} COMMAND ${name})
        add_dependencies(dino_tests ${name})
    endif()
endfunction()

# A library can also register tests with add_test() itself, as most third-party
# CMakeLists.txt do. Their executables are never built in a project that only
# uses the library, so there they are disabled: still there for the library's
# own set_tests_properties() calls, but not run. Overriding a command keeps the
# original as _add_test(); the arguments are passed on in brackets so that empty
# ones and ones with semicolons arrive unchanged.
function(add_test)
    set(arguments "")
    if(ARGC GREATER 0)
        math(EXPR last "${ARGC} - 1")
        foreach(i RANGE ${last})
            string(APPEND arguments " [==[${ARGV${i}}]==]")
        endforeach()
    endif()
    cmake_language(EVAL CODE "_add_test(${arguments})")
    if(DINO__LIBRARY_DEPENDENCY)
        if(ARGV0 STREQUAL "NAME")
            set_tests_properties("${ARGV1}" PROPERTIES DISABLED TRUE)
        else()
            set_tests_properties("${ARGV0}" PROPERTIES DISABLED TRUE)
        endif()
    endif()
endfunction()

# Organization libraries from DINO_LIBRARY_PATH (a CMake list; `dino` passes
# the environment variable of the same name). EXCLUDE_FROM_ALL builds one only
# when something links it. A library that is itself the top-level project,
# because it's being built or tested on its own, isn't added a second time:
# neither the same directory nor another copy of it, such as the one a CI image
# has on DINO_LIBRARY_PATH while a checkout of the library is tested.
get_filename_component(_dino_top "${CMAKE_SOURCE_DIR}" REALPATH)
set(DINO__LIBRARY_DEPENDENCY ON)
foreach(_dino_library IN LISTS DINO_LIBRARY_PATH)
    get_filename_component(_dino_library "${_dino_library}" REALPATH)
    if(_dino_library STREQUAL _dino_top)
        continue()
    endif()
    file(STRINGS "${_dino_library}/CMakeLists.txt" _dino_project
        REGEX "^[ \t]*[Pp][Rr][Oo][Jj][Ee][Cc][Tt][ \t]*\\(" LIMIT_COUNT 1)
    if(_dino_project MATCHES "\\([ \t]*\"?([A-Za-z0-9_.+-]+)" AND CMAKE_MATCH_1 STREQUAL PROJECT_NAME)
        message(NOTICE "dino: not using ${_dino_library} from DINO_LIBRARY_PATH: "
            "it is another copy of this project (${PROJECT_NAME})")
        continue()
    endif()
    get_filename_component(_dino_name "${_dino_library}" NAME)
    add_subdirectory("${_dino_library}" "${CMAKE_BINARY_DIR}/_dino_libs/${_dino_name}" EXCLUDE_FROM_ALL)
endforeach()
unset(DINO__LIBRARY_DEPENDENCY)
unset(_dino_top)
unset(_dino_library)
unset(_dino_name)
unset(_dino_project)
