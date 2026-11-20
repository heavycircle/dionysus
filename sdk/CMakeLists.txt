cmake_minimum_required(VERSION 3.20)

# Projects that still add_subdirectory() the SDK would otherwise redefine the
# target that Dino.cmake already created.
if(TARGET dino_core)
    return()
endif()

project(DinoSDK VERSION 0.6.2 LANGUAGES C)
# Dropped only on the release commit; must match build.zig.zon (CONTRIBUTING.md#versioning).
set(DINO_VERSION_PRERELEASE "-dev")

add_library(dino_core STATIC
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

# The POSIX halves need more than ISO C declares (clock_gettime, pthread mutex
# types, pipe2, accept4, gettid, ...), also when the project builds with
# -std=c11 or CMAKE_C_EXTENSIONS OFF.
target_compile_definitions(dino_core PRIVATE _GNU_SOURCE)

configure_file(cmake/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/dino/version.h" @ONLY)
target_include_directories(dino_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/include")

if(WIN32)
    target_link_libraries(dino_core PRIVATE ws2_32)
endif()

# Wrappers for the system headers that declare calls with a dino_* replacement
# (sdk/portability/common/dino_portability.h). INTERFACE only: everything that
# links the SDK gets the warnings, the SDK's own sources never do. The POSIX-only
# headers stay off Windows include paths so __has_include(<poll.h>) still tells
# the truth there, and the reverse for the Windows ones.
target_include_directories(dino_core BEFORE INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/portability/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/portability/${DINO_PLATFORM}")
option(DINO_NO_PLATFORM_WARNINGS "Don't warn about platform calls that have a dino_* replacement" OFF)
if(DINO_NO_PLATFORM_WARNINGS)
    target_compile_definitions(dino_core INTERFACE DINO_ALLOW_PLATFORM_CALLS)
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
target_compile_definitions(dino_util PRIVATE _GNU_SOURCE)

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
