cmake_minimum_required(VERSION 3.15)

# Projects that still add_subdirectory() the SDK would otherwise redefine the
# target that Dino.cmake already created.
if(TARGET dino_core)
    return()
endif()

project(DinoSDK VERSION 0.5.0 LANGUAGES C)
# Dropped only on the release commit; must match build.zig.zon (CONTRIBUTING.md#versioning).
set(DINO_VERSION_PRERELEASE "-dev")

add_library(dino_core STATIC
    src/core/io.c
    src/core/mutex.c
    src/core/network.c
    src/core/pipe.c
    src/core/process.c
    src/core/runtime.c
    src/core/runtime_auto.c
    src/core/thread.c
    src/core/time.c
)

# Modules that differ by platform: <module>_win32.c or <module>_posix.c is built,
# next to the shared <module>.c if it has one. Both halves must exist, checked
# here on every platform, so a module missing its Windows half fails a Linux
# configure too instead of only the Windows build.
set(DINO_PLATFORM_MODULES
    fs
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

# Links runtime_auto.c's constructor into every program, which wouldn't otherwise
# pull that object from the archive. Its only job (closed stdio) is Linux-only.
option(DINO_NO_AUTO_RUNTIME "Don't run the SDK's setup before main() (see dino/core/runtime.h)" OFF)
if(NOT WIN32 AND NOT DINO_NO_AUTO_RUNTIME)
    target_link_options(dino_core INTERFACE "LINKER:-u,dino__runtime_auto")
endif()

# Optional extras built on dino_core (logging, ...), kept out of the core library.
add_library(dino_util STATIC
    src/util/logging.c
)
target_link_libraries(dino_util PUBLIC dino_core)

# Used by dino_test() (cmake/Dino.cmake). testing/utest.h is sheredom/utest.h at
# fc4715da3b3e3bfa936cf69850f2cfe969ed85b7, unmodified (public domain).
add_library(dino_testing INTERFACE)
# SYSTEM: utest.h's own warnings and clang-tidy findings stay out of test code.
target_include_directories(dino_testing SYSTEM INTERFACE testing)
target_link_libraries(dino_testing INTERFACE dino_util)
# --wrap needs the static libc of the Linux targets: zig cc doesn't pass it to
# lld-link, and the Windows CRT allocates inside a DLL anyway.
option(DINO_NO_LEAK_CHECK "Don't link the allocation counter into dino_test() executables" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DINO_NO_LEAK_CHECK)
    target_sources(dino_testing INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/testing/leakcheck.c")
    target_link_options(dino_testing INTERFACE
        "LINKER:--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"
        "LINKER:--wrap=aligned_alloc,--wrap=posix_memalign")
endif()
