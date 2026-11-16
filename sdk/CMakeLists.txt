cmake_minimum_required(VERSION 3.15)

# Projects that still add_subdirectory() the SDK would otherwise redefine the
# target that Dino.cmake already created.
if(TARGET dino_core)
    return()
endif()

project(DinoSDK VERSION 0.2.1 LANGUAGES C)
# Dropped only on the release commit; must match build.zig.zon (CONTRIBUTING.md#versioning).
set(DINO_VERSION_PRERELEASE "-dev")

add_library(dino_core STATIC
    src/core/fs.c
    src/core/io.c
    src/core/mutex.c
    src/core/network.c
    src/core/runtime.c
    src/core/thread.c
)

configure_file(cmake/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/dino/version.h" @ONLY)
target_include_directories(dino_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/include")

if(WIN32)
    target_link_libraries(dino_core PRIVATE ws2_32)
endif()

# Used by dino_test() (cmake/Dino.cmake). testing/utest.h is sheredom/utest.h at
# fc4715da3b3e3bfa936cf69850f2cfe969ed85b7, unmodified (public domain).
add_library(dino_testing INTERFACE)
# SYSTEM: utest.h's own warnings and clang-tidy findings stay out of test code.
target_include_directories(dino_testing SYSTEM INTERFACE testing)
target_link_libraries(dino_testing INTERFACE dino_core)
# --wrap needs the static libc of the Linux targets: zig cc doesn't pass it to
# lld-link, and the Windows CRT allocates inside a DLL anyway.
option(DINO_NO_LEAK_CHECK "Don't link the allocation counter into dino_test() executables" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DINO_NO_LEAK_CHECK)
    target_sources(dino_testing INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/testing/leakcheck.c")
    target_link_options(dino_testing INTERFACE
        "LINKER:--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"
        "LINKER:--wrap=aligned_alloc,--wrap=posix_memalign")
endif()
