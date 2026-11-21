cmake_minimum_required(VERSION 3.20)
project(RawCalls C)

# Calls every platform function sdk/portability flags, under clang's -verify:
# each call's `expected-warning` comment must be matched by exactly that
# warning, and nothing else may warn. tests/warnings.sh builds this per target.
# Each variant is a language mode that changes which declarations the libc
# headers expose, so a wrapper that redeclares something the headers left out
# fails to compile.

if(WIN32)
    set(platform win32)
else()
    set(platform posix)
endif()

if(DINO_NO_PLATFORM_WARNINGS)
    # The project-wide opt-out: nothing may warn at all.
    set(check -Werror=deprecated-declarations)
else()
    # The note pointing at the attribute in the wrapper isn't part of the
    # contract, and CI's -Werror would turn the expected warnings into errors.
    set(check "SHELL:-Xclang -verify" "SHELL:-Xclang -verify-ignore-unexpected=note"
        -Wno-error=deprecated-declarations)
endif()

function(variant name standard extensions)
    add_library(raw_${name} STATIC raw_common.c raw_${platform}.c)
    target_link_libraries(raw_${name} PRIVATE dino_core)
    target_compile_options(raw_${name} PRIVATE ${check})
    target_include_directories(raw_${name} SYSTEM PRIVATE system)
    target_compile_definitions(raw_${name} PRIVATE ${ARGN})
    set_target_properties(raw_${name} PROPERTIES C_STANDARD ${standard} C_EXTENSIONS ${extensions})
endfunction()

variant(default 17 ON)
variant(strict 11 OFF)
variant(posix2008 11 OFF _POSIX_C_SOURCE=200809L)
variant(gnu 17 ON _GNU_SOURCE)

# Per-target opt-out, as a project would set it.
add_library(raw_allowed STATIC raw_common.c raw_${platform}.c)
target_link_libraries(raw_allowed PRIVATE dino_core)
target_include_directories(raw_allowed SYSTEM PRIVATE system)
target_compile_definitions(raw_allowed PRIVATE DINO_ALLOW_PLATFORM_CALLS)
target_compile_options(raw_allowed PRIVATE -Werror=deprecated-declarations)

# Per-file opt-out.
add_library(raw_allowed_file STATIC allowed_file.c)
target_link_libraries(raw_allowed_file PRIVATE dino_core)
target_compile_options(raw_allowed_file PRIVATE -Werror=deprecated-declarations)

dino_test(builds builds.c)
