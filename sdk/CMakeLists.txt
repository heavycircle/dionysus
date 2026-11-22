cmake_minimum_required(VERSION 3.20)

# Projects that still add_subdirectory() the SDK would otherwise redefine the
# target that Dino.cmake already created.
if(TARGET dino_core)
    return()
endif()

project(DinoSDK VERSION 0.8.0 LANGUAGES C)
# Dropped only on the release commit; must match build.zig.zon (CONTRIBUTING.md#versioning).
set(DINO_VERSION_PRERELEASE "-dev")

add_library(dino_core STATIC
    src/core/error.c
    src/core/event.c
    src/core/process.c
    src/core/runtime.c
    src/core/runtime_auto.c
    src/core/time.c
)

# Modules that differ by platform: <module>_win32.c or <module>_posix.c is built,
# next to the shared <module>.c if it has one. Both halves must exist, checked
# here on every platform, so a module missing its Windows half fails a Linux
# configure too instead of only the Windows build.
set(DINO_PLATFORM_MODULES
    error
    event
    fs
    io
    mutex
    network
    pipe
    process
    thread
    time
)
if(WIN32)
    set(DINO_PLATFORM win32)
else()
    set(DINO_PLATFORM posix)
endif()
foreach(module ${DINO_PLATFORM_MODULES})
    foreach(platform win32 posix)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/${module}_${platform}.c")
            message(FATAL_ERROR "src/core/${module}_${platform}.c is missing: every platform module needs both halves")
        endif()
    endforeach()
    target_sources(dino_core PRIVATE "src/core/${module}_${DINO_PLATFORM}.c")
endforeach()

configure_file(cmake/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/dino/version.h" @ONLY)
target_include_directories(dino_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/include")

if(WIN32)
    target_link_libraries(dino_core PRIVATE ws2_32)
endif()

# Optional extras built on dino_core (logging, ...), kept out of the core library.
add_library(dino_util STATIC
    src/util/logging.c
)
target_link_libraries(dino_util PUBLIC dino_core)

# Used by dino_test() (cmake/Dino.cmake). testing/utest.h is sheredom/utest.h at
# fc4715da3b3e3bfa936cf69850f2cfe969ed85b7, unmodified (public domain).
add_library(dino_testing INTERFACE)
# Object libraries rather than INTERFACE sources, so they build here with the
# SDK's warnings instead of with each test's.
add_library(dino_test_output OBJECT testing/unbuffered.c)
# --wrap needs the static libc of the Linux targets: zig cc doesn't pass it to
# lld-link, and the Windows CRT allocates inside a DLL anyway. A dynamic libc
# (DINO_DYNAMIC) allocates inside a shared object too: what it allocates isn't
# counted and what the program then frees is.
option(DINO_NO_LEAK_CHECK "Don't link the allocation counter into dino_test() executables" OFF)
set(DINO__LEAK_CHECK_OBJECTS "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DINO_DYNAMIC AND NOT DINO_NO_LEAK_CHECK)
    add_library(dino_leakcheck OBJECT testing/leakcheck.c)
    set(DINO__LEAK_CHECK_OBJECTS $<TARGET_OBJECTS:dino_leakcheck>)
endif()

set(DINO__UTF8_OBJECTS "")
if(WIN32)
    add_library(dino_utf8_console OBJECT src/windows/utf8_console_win32.c)
    set(DINO__UTF8_OBJECTS $<TARGET_OBJECTS:dino_utf8_console>)
endif()

set(DINO__SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(DINO__TEST_OBJECTS $<TARGET_OBJECTS:dino_test_output>)
include(cmake/interface.cmake)

# Unless prebuilt (DINO_SDK_PREBUILT), the SDK's own sources are compiled
# inside the project, so the project's flags reach them (CFLAGS, and
# CMAKE_C_STANDARD or add_compile_options() set before project()). Projects
# aren't promised that (docs/guide/projects.md), but the sources must not rely
# on those flags either way:
# - _GNU_SOURCE: the POSIX halves need more than ISO C declares (clock_gettime,
#   pthread mutex types, pipe2, accept4, gettid, ...), also under -std=c11.
# - -Wno-everything drops the warnings the project enabled, so its
#   -Wconversion -Werror never fails on SDK code, and the SDK keeps its own
#   -Wall -Wextra, never fatal unless DINO_SDK_WERROR is set (sdk/tests sets it,
#   so CI fails on them).
option(DINO_SDK_WERROR "Make warnings in the SDK's own sources errors (for SDK development)" OFF)
set(dino_sdk_warnings -Wno-everything -Wall -Wextra)
if(DINO_SDK_WERROR)
    list(APPEND dino_sdk_warnings -Werror)
else()
    list(APPEND dino_sdk_warnings -Wno-error)
endif()
foreach(target dino_core dino_util dino_test_output dino_leakcheck dino_utf8_console)
    if(TARGET ${target})
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
        target_compile_options(${target} PRIVATE ${dino_sdk_warnings})
    endif()
endforeach()

# With DINO_SDK_PREBUILT=1, `dino build` builds the SDK on its own, once per zig
# version, target and build type, setting this to where the result goes
# (cli/prebuilt.zig); prebuilt.cmake imports it from there. dino-prebuilt.cmake
# is copied last and marks a complete set.
if(DINO_SDK_PREBUILD)
    set(dino_prebuilt_info "${CMAKE_CURRENT_BINARY_DIR}/dino-prebuilt.cmake")
    set(dino_leak_check OFF)
    if(TARGET dino_leakcheck)
        set(dino_leak_check ON)
    endif()
    set(dino_utf8_console OFF)
    if(TARGET dino_utf8_console)
        set(dino_utf8_console ON)
    endif()
    file(WRITE "${dino_prebuilt_info}"
        "set(DINO__PREBUILT_VERSION \"${PROJECT_VERSION}\")\n"
        "set(DINO__PREBUILT_TARGET \"${ZIG_TARGET}\")\n"
        "set(DINO__PREBUILT_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\")\n"
        "set(DINO__PREBUILT_LEAK_CHECK ${dino_leak_check})\n"
        "set(DINO__PREBUILT_UTF8_CONSOLE ${dino_utf8_console})\n")
    set(out "${DINO_SDK_PREBUILD}")
    set(copy "${CMAKE_COMMAND}" -E copy)
    set(steps
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${out}/include/dino"
        COMMAND ${copy} "${CMAKE_CURRENT_BINARY_DIR}/include/dino/version.h" "${out}/include/dino/"
        COMMAND ${copy} $<TARGET_FILE:dino_core> "${out}/libdino_core.a"
        COMMAND ${copy} $<TARGET_FILE:dino_util> "${out}/libdino_util.a"
        COMMAND ${copy} $<TARGET_OBJECTS:dino_test_output> "${out}/unbuffered.o")
    set(dino_prebuilt_parts dino_core dino_util dino_test_output)
    if(dino_leak_check)
        list(APPEND steps COMMAND ${copy} $<TARGET_OBJECTS:dino_leakcheck> "${out}/leakcheck.o")
        list(APPEND dino_prebuilt_parts dino_leakcheck)
    endif()
    if(dino_utf8_console)
        list(APPEND steps COMMAND ${copy} $<TARGET_OBJECTS:dino_utf8_console> "${out}/utf8_console.o")
        list(APPEND dino_prebuilt_parts dino_utf8_console)
    endif()
    list(APPEND steps COMMAND ${copy} "${dino_prebuilt_info}" "${out}/")
    add_custom_target(dino_prebuilt ALL ${steps} VERBATIM)
    add_dependencies(dino_prebuilt ${dino_prebuilt_parts})
endif()
