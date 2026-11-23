#!/usr/bin/env bash
# End-to-end checks of the dino CLI: configure caching, `dino run`, `dino init`
# and the command line itself. Intended for the Docker image:
#
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/cli.sh
#
# Pass case names to run only those.

# The case_ functions are called by name below.
# shellcheck disable=SC2317
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export DINO_SDK_PATH=$repo/sdk DINO_HOME=${DINO_HOME:-$work/home}

# A fresh project in $work/<name>, without configuring it.
project() {
    rm -rf "${work:?}/$1"
    dino init --no-configure "$work/$1" >/dev/null
    cd "$work/$1" || exit 1
}

# A build made by another zig is started over, as for a new SDK: CMake keeps
# the compiler it found and the objects, so a reconfigure alone wouldn't do.
case_zig_version_starts_over() {
    project zig_version
    dino build >/dev/null 2>&1 || return 1
    mkdir -p "$work/zig_version_bin"
    # shellcheck disable=SC2016 # $1 and $@ are for the wrapper, not this shell
    printf '#!/bin/sh\nif [ "$1" = version ]; then echo 0.17.99; exit 0; fi\nexec %s "$@"\n' \
        "$(command -v zig)" >"$work/zig_version_bin/zig"
    chmod +x "$work/zig_version_bin/zig"
    PATH=$work/zig_version_bin:$PATH dino build -v >out 2>&1 || { cat out; return 1; }
    grep -q 'mutex_posix\.c' out || { echo "SDK not rebuilt"; cat out; return 1; }
    # And with the same zig again, nothing is redone.
    PATH=$work/zig_version_bin:$PATH dino build -v >out 2>&1 || { cat out; return 1; }
    ! grep -q 'mutex_posix\.c' out
}

# Configuring against another SDK starts from a fresh CMake cache, so the old
# toolchain file is no longer used.
case_sdk_switch() {
    project sdk_switch
    dino build >/dev/null || return 1
    cp -r "$repo/sdk" "$work/sdk2"
    DINO_SDK_PATH=$work/sdk2 dino build >/dev/null || return 1
    ! grep -rq "$repo/sdk/" build/x86_64/CMakeFiles/*/CMakeSystem.cmake
}

# A failed configure leaves no stamp that skips the next one: the release
# build reconfigures after a failed debug configure wrote Debug to the cache.
case_failed_configure() {
    project failed_configure
    echo 'if(CMAKE_BUILD_TYPE STREQUAL Debug)
    message(FATAL_ERROR "no debug builds")
endif()' >>CMakeLists.txt
    dino build >/dev/null || return 1
    dino build --debug >/dev/null 2>&1 && return 1
    dino build >/dev/null || return 1
    grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' build/x86_64/CMakeCache.txt
}

# A build type given with `--` stays for run and test, without reconfiguring,
# until --debug or --release.
case_build_type_kept() {
    project build_type
    dino build -- -DCMAKE_BUILD_TYPE=RelWithDebInfo >out 2>&1 || { cat out; return 1; }
    grep -q 'x86_64 (.*, RelWithDebInfo)' out || { cat out; return 1; }
    local cmd
    for cmd in 'run -v' 'test -v' 'build -v'; do
        eval "dino $cmd" >out 2>&1 || { cat out; return 1; }
        ! grep -q -- '-S .* -B ' out || { echo "dino $cmd reconfigured"; cat out; return 1; }
    done
    grep -q '^CMAKE_BUILD_TYPE:STRING=RelWithDebInfo$' build/x86_64/CMakeCache.txt || return 1
    dino build --release >/dev/null || return 1
    grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' build/x86_64/CMakeCache.txt
}

# `--` arguments reconfigure only when they change, and a command without
# them keeps what they set.
case_extra_args_reconfigure_once() {
    project extra_args
    # shellcheck disable=SC2016 # for CMake to expand
    echo 'message(WARNING "FLAVOR=${FLAVOR}")' >>CMakeLists.txt
    dino build -- -DFLAVOR=sweet >out 2>&1 || { cat out; return 1; }
    grep -q 'FLAVOR=sweet' out || { cat out; return 1; }
    local cmd
    for cmd in 'build -v' 'run -v' 'build -v -- -DFLAVOR=sweet'; do
        eval "dino $cmd" >out 2>&1 || { cat out; return 1; }
        ! grep -q -- '-S .* -B ' out || { echo "dino $cmd reconfigured"; cat out; return 1; }
    done
    dino build -- -DFLAVOR=sour >out 2>&1 || { cat out; return 1; }
    grep -q 'FLAVOR=sour' out || { cat out; return 1; }
}

# A moved or copied project builds on the first try: CMake refuses a cache
# made for other directories, so dino starts over.
case_moved_project() {
    project moved
    dino build >/dev/null || return 1
    cp -r "$work/moved" "$work/moved_copy"
    mv "$work/moved" "$work/moved_away"
    cd "$work/moved_away" && dino run | grep -qx 'Hello from moved!' || return 1
    cd "$work/moved_copy" && dino run | grep -qx 'Hello from moved!'
}

# A build dir configured with plain CMake isn't taken over: its cache stays.
case_hand_configured_refused() {
    project hand_configured
    cmake -S . -B build/x86_64 -DCMAKE_TOOLCHAIN_FILE="$DINO_SDK_PATH/cmake/toolchain.cmake" \
        -DCMAKE_PROJECT_INCLUDE="$DINO_SDK_PATH/cmake/Dino.cmake" -DZIG_TARGET=x86_64-linux-musl \
        -DMINE=kept >/dev/null || return 1
    dino build >out 2>&1 && { cat out; return 1; }
    grep -q 'build/x86_64 was configured without dino' out || { cat out; return 1; }
    grep -q '^MINE:UNINITIALIZED=kept$' build/x86_64/CMakeCache.txt || return 1
    [[ ! -e build/x86_64/.cmake/api/v1/query ]]
}

# What CMake generated, deleted by hand, comes back with the next build.
case_generated_files_deleted() {
    project generated_deleted
    dino build >/dev/null || return 1
    rm -r build/x86_64/.cmake/api/v1/reply
    dino run | grep -qx 'Hello from generated_deleted!' || return 1
    rm build/x86_64/build.ninja build/x86_64/Makefile 2>/dev/null
    dino run | grep -qx 'Hello from generated_deleted!'
}

# Editing an SDK checkout's toolchain files starts the build over: CMake reads
# them only on a first configure.
case_sdk_checkout_edit() {
    project sdk_edit
    cp -r "$repo/sdk" "$work/sdk_edit_sdk"
    export DINO_SDK_PATH=$work/sdk_edit_sdk
    dino build >/dev/null || return 1
    echo 'string(APPEND CMAKE_C_FLAGS_RELEASE_INIT " -DEDITED_FLAGS")' >>"$DINO_SDK_PATH/cmake/flags.cmake"
    dino build -v >out 2>&1 || { cat out; return 1; }
    grep -q -- '-DEDITED_FLAGS.*mutex_posix\.c' out || { echo "SDK not rebuilt with the new flags"; cat out; return 1; }
    dino build -v >out 2>&1 || { cat out; return 1; }
    ! grep -q 'mutex_posix\.c' out
}

# A library path CMake would split in two is refused by name.
case_library_path_semicolon() {
    project library_semicolon
    mkdir -p "$work/lib;semi/semi" && echo 'add_library(semi INTERFACE)' >"$work/lib;semi/semi/CMakeLists.txt"
    DINO_LIBRARY_PATH="$work/lib;semi/semi" dino build >out 2>&1 && { cat out; return 1; }
    grep -q "has a ';' in its path" out || { cat out; return 1; }
}

# `dino clean` removes a build dir whose first configure failed.
case_clean_failed() {
    project clean_failed
    echo 'message(FATAL_ERROR "broken")' >>CMakeLists.txt
    dino build >/dev/null 2>&1 && return 1
    [[ -d build/x86_64 ]] || return 1
    dino clean >/dev/null || return 1
    [[ ! -e build ]]
}

# `dino clean` prints paths as the other commands do, and leaves an empty
# build/ it didn't empty alone.
case_clean_paths() {
    project clean_paths
    mkdir build
    dino clean >out 2>&1 || { cat out; return 1; }
    [[ -d build ]] || { echo "removed a build/ that wasn't dino's"; return 1; }
    dino build >/dev/null 2>&1 || return 1
    dino clean >out 2>&1 || { cat out; return 1; }
    grep -qx '    removed  build/x86_64' out || { cat out; return 1; }
    [[ ! -e build ]]
}

# `-v` prints commands that can be pasted, quoting a path with a space.
case_verbose_quotes() {
    mkdir -p "$work/my dir" && rm -rf "$work/my dir/quoted"
    dino init --no-configure "$work/my dir/quoted" >/dev/null || return 1
    cd "$work/my dir/quoted" || return 1
    dino build -v >out 2>&1 || { cat out; return 1; }
    grep -qF "\$ cmake -S '$work/my dir/quoted' -B build/x86_64" out || { grep -- '-S ' out; return 1; }
}

# `dino run` finds an executable built outside the build directory.
case_run_output_outside_build() {
    project run_outside
    # shellcheck disable=SC2016 # for CMake to expand
    sed -i '/^dino_executable/i set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)' CMakeLists.txt
    dino run | grep -qx 'Hello from run_outside!'
}

# `dino run`'s stdout is the program's alone; the build goes to stderr.
case_run_stdout_is_the_programs() {
    project run_stdout
    [[ $(dino run 2>/dev/null) == 'Hello from run_stdout!' ]] || { dino run; return 1; }
    [[ $(dino run -v 2>/dev/null) == 'Hello from run_stdout!' ]]
}

# A program left out of the build (EXCLUDE_FROM_ALL) isn't listed or picked,
# and asking for it by name says why it isn't there.
case_excluded_executable() {
    project excluded
    mkdir -p extra && echo 'int main(void) { return 0; }' >extra/extra.c
    echo 'add_executable(extra EXCLUDE_FROM_ALL extra/extra.c)' >>CMakeLists.txt
    dino build >out 2>&1 || { cat out; return 1; }
    ! grep -q 'extra' out || { cat out; return 1; }
    dino run 2>/dev/null | grep -qx 'Hello from excluded!' || return 1
    dino run --exe extra >out 2>&1 && { cat out; return 1; }
    grep -q "'extra' is left out of the build (EXCLUDE_FROM_ALL)" out || { cat out; return 1; }
}

# `dino run --no-build` after `dino init`, which configures but doesn't build.
case_no_build_before_first_build() {
    rm -rf "$work/unbuilt"
    dino init "$work/unbuilt" >/dev/null 2>&1 || return 1
    cd "$work/unbuilt" || return 1
    dino run --no-build >out 2>&1 && { cat out; return 1; }
    grep -q 'x86_64 has not been built yet; run .dino build x86_64.' out || { cat out; return 1; }
}

# A target this host can't run is refused before anything is built.
case_run_checks_runner_first() {
    project runner_first
    local t
    for t in aarch64-macos x86_64-linux-muslx32; do
        dino run "$t" >out 2>&1 && { cat out; return 1; }
        grep -q "$t binaries cannot be run on this host" out || { cat out; return 1; }
        ! grep -q '^==>' out || { echo "built before checking"; cat out; return 1; }
    done
}

# Targets go by other common spellings too, in any case.
case_target_aliases() {
    project aliases
    dino build AArch64 >out 2>&1 || { cat out; return 1; }
    grep -q '^==> arm64 ' out || { cat out; return 1; }
    [[ -d build/arm64 ]]
}

# A library's executables aren't the project's, even when the library puts
# its binaries outside _dino_libs.
case_library_tool_not_listed() {
    library libs/tooled tooled ''
    # shellcheck disable=SC2016 # for CMake to expand
    printf '%s\n' 'set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)' \
        'add_executable(tool src/lib.c)' >>"$work/libs/tooled/CMakeLists.txt"
    export DINO_LIBRARY_PATH=$work/libs/tooled
    project tool_user
    dino build >out 2>&1 || { cat out; return 1; }
    ! grep -q 'bin/tool' out || { cat out; return 1; }
    dino run 2>/dev/null | grep -qx 'Hello from tool_user!'
}

# `dino run --no-build --debug` doesn't run a release build in its place.
case_no_build_checks_mode() {
    project no_build_mode
    dino build >/dev/null || return 1
    dino run --no-build --debug && return 1
    dino run --no-build --release >/dev/null || return 1
    dino run --no-build >/dev/null
}

# Command-line mistakes exit 2, other failures 1.
case_usage_errors() {
    mkdir -p "$work/empty" && cd "$work/empty" || return 1
    local cmd code
    for cmd in 'frob' 'help frob' 'build --bogus' 'build -C ""' 'build -C=' 'build nosuchtarget' \
        'build --all x86_64' 'build -j 0' 'run x86_64 arm' 'test --memcheck arm' 'clean -C ""' \
        'init a b' 'init ""' 'targets x' 'doctor x' 'run --exe' 'test --timeout 0' 'test --timeout soon' \
        'build -j 1_0' 'build -j +3' 'build -j 99999' 'build -C --debug' 'run --exe --' 'test --memcheck --release'; do
        eval "dino $cmd" >/dev/null 2>&1
        code=$?
        [[ $code == 2 ]] || { echo "dino $cmd: exit $code"; return 1; }
    done
    dino build >/dev/null 2>&1
    code=$?
    [[ $code == 1 ]] || { echo "dino build without a project: exit $code"; return 1; }
}

# Every command has help, help and version included.
case_help() {
    local c
    for c in init build run test targets doctor clean help version; do
        dino help "$c" | grep -q "^Usage: dino $c" || { echo "help $c"; return 1; }
    done
}

# doctor checks what it calls ok: a zig of another version, or one that fails
# or can't be started, is a problem, and the rest of the report still comes.
case_doctor_checks_zig() {
    mkdir -p "$work/doctor/bin" && cd "$work/doctor" || return 1
    dino doctor >out 2>&1 || { cat out; return 1; }
    grep -q '\[ ok \] zig ' out || { cat out; return 1; }
    grep -q ' valgrind ' out || { cat out; return 1; }

    local script
    for script in '#!/bin/sh\necho 0.13.0\n' '#!/bin/sh\nexit 3\n' '#!/no/such/interpreter\n'; do
        # shellcheck disable=SC2059
        printf "$script" >bin/zig
        chmod +x bin/zig
        PATH=$work/doctor/bin:$PATH dino doctor >out 2>&1 && { cat out; return 1; }
        grep -q '\[MISS\] zig ' out || { cat out; return 1; }
        grep -q '\] cmake ' out || { cat out; return 1; }
        grep -q '^1 problem(s) found' out || { cat out; return 1; }
    done
    printf '#!/bin/sh\necho 0.13.0\n' >bin/zig
    PATH=$work/doctor/bin:$PATH dino doctor >out 2>&1
    grep -q 'zig .*0\.13\.0.*dino needs Zig' out
}

# dino build and dino run leave the test executables out, and editors still
# see their sources; dino test builds them, and switching back and forth
# never reconfigures. A test asked for by name with --exe is built too.
case_tests_left_out_of_build() {
    project no_tests
    dino build >out 2>&1 || { cat out; return 1; }
    [[ ! -e build/x86_64/test_main ]] || { echo "dino build built the test"; return 1; }
    ! grep -q test_main out || { cat out; return 1; }
    grep -q 'tests/test_main.c' compile_commands.json || { echo "test source missing from compile_commands.json"; return 1; }
    dino test >out 2>&1 || { cat out; return 1; }
    [[ -x build/x86_64/test_main ]] || return 1
    dino build -v >out 2>&1 || { cat out; return 1; }
    ! grep -q -- '-S .* -B ' out || { echo "switching back reconfigured"; cat out; return 1; }
    dino clean >/dev/null
    dino run --exe test_main >out 2>&1 || { cat out; return 1; }
}

# A target named twice, even by its triple, is built once.
case_duplicate_targets() {
    project duplicates
    [[ $(dino build x86_64 x86_64-linux-musl x86_64 | grep -c '^==> x86_64 ') == 1 ]]
}

# `dino clean memcheck` removes the build/memcheck of `dino test --memcheck`.
case_clean_memcheck() {
    project clean_memcheck
    dino test --memcheck >/dev/null || return 1
    dino clean memcheck >/dev/null || return 1
    [[ ! -e build/memcheck ]]
}

# `dino test --memcheck` leaves the editor's compile_commands.json on the
# normal build, not the glibc one valgrind needs.
case_memcheck_keeps_compdb() {
    project memcheck_compdb
    dino build >/dev/null 2>&1 || return 1
    grep -q 'x86_64-linux-musl' compile_commands.json || return 1
    dino test --memcheck >out 2>&1 || { cat out; return 1; }
    grep -q 'x86_64-linux-musl' compile_commands.json || { echo "repointed at the memcheck build"; return 1; }
}

# C++ entries in compile_commands.json name libc++'s headers.
case_compdb_cxx() {
    project compdb_cxx
    sed -i 's/^project(compdb_cxx C)$/project(compdb_cxx C CXX)/' CMakeLists.txt
    echo 'dino_executable(cxx_app src/app.cpp)' >>CMakeLists.txt
    printf '#include <vector>\nint main() { return std::vector<int>{0}[0]; }\n' >src/app.cpp
    dino build >/dev/null || return 1
    grep -q 'libcxx/include' compile_commands.json || return 1
    [[ $(grep -c 'libcxx/include' compile_commands.json) == 1 ]]
}

# C++ sources get the same per-configuration flags as C.
case_cxx_config_flags() {
    project cxx_flags
    sed -i 's/^project(cxx_flags C)$/project(cxx_flags C CXX)/' CMakeLists.txt
    echo 'dino_executable(cxx_app src/app.cpp)' >>CMakeLists.txt
    echo 'int main() { return 0; }' >src/app.cpp
    dino build -v >out 2>&1 || { cat out; return 1; }
    grep -q -- '-Oz -DNDEBUG -ffunction-sections .*app\.cpp' out || return 1
    dino build -v --debug >out 2>&1 || { cat out; return 1; }
    grep -q -- '-O0 -g .*app\.cpp' out
}

# CMAKE_SYSTEM_PROCESSOR names the target's architecture as uname -m would.
case_system_processor() {
    project processor
    # shellcheck disable=SC2016 # for CMake to expand
    echo 'message(STATUS "processor: ${CMAKE_SYSTEM_PROCESSOR}")' >>CMakeLists.txt
    local t want
    for t in x86_64:x86_64 i386:i686 arm:armv7l arm64:aarch64 ppc:ppc win32:x86 win64:AMD64; do
        want=${t#*:}
        t=${t%%:*}
        dino build -v "$t" >out 2>&1 || { cat out; return 1; }
        grep -qx -- "-- processor: $want" out || { echo "$t: $(grep 'processor:' out)"; return 1; }
    done
}

# The compile database comes from the first target only, even when that one
# fails to configure.
case_compdb_first_target() {
    project compdb_first
    echo 'if(ZIG_TARGET MATCHES powerpc)
    message(FATAL_ERROR "no ppc")
endif()' >>CMakeLists.txt
    dino build ppc x86_64 >/dev/null 2>&1 && return 1
    [[ ! -e compile_commands.json ]]
}

# dinos started together unpack the embedded SDK into an empty cache without
# getting in each other's way.
case_parallel_sdk_unpack() {
    local i pids=() failed=0
    for i in 1 2 3 4 5 6; do
        project "unpack$i"
    done
    for i in 1 2 3 4 5 6; do
        (cd "$work/unpack$i" && DINO_SDK_PATH='' DINO_HOME=$work/unpack_home dino build >log 2>&1) &
        pids+=($!)
    done
    for i in "${pids[@]}"; do wait "$i" || failed=1; done
    ((failed == 0)) || { cat "$work"/unpack*/log; return 1; }
    [[ $(ls "$work/unpack_home/sdk") != *.tmp-* ]]
}

# An unpacked SDK without its marker (from an older dino, or a cleanup cut
# short) is replaced, and the temporaries of a killed unpack are removed once
# they are old enough not to be a running dino's.
case_sdk_leftover_replaced() {
    project leftover
    export DINO_SDK_PATH='' DINO_HOME=$work/leftover_home
    dino doctor >/dev/null 2>&1
    local sdk
    sdk=$(echo "$DINO_HOME"/sdk/*)
    [[ -f $sdk/.dino-sdk ]] || return 1
    rm "$sdk/.dino-sdk"
    mkdir -p "$sdk.tmp-dead/include" "$sdk.tmp-alive"
    touch -d '-1 hour' "$sdk.tmp-dead"
    dino build >out 2>&1 || { cat out; return 1; }
    [[ -f $sdk/.dino-sdk ]] || return 1
    [[ ! -e $sdk.tmp-dead && -d $sdk.tmp-alive ]] || { ls "$DINO_HOME/sdk"; return 1; }
    [[ $(ls "$DINO_HOME/sdk") != *.stale-* ]]
}

# The unpacked SDK is read-only, and doctor finds a file that changed anyway.
case_sdk_read_only_and_verified() {
    mkdir -p "$work/verify" && cd "$work/verify" || return 1
    export DINO_SDK_PATH='' DINO_HOME=$work/verify_home
    dino doctor >out 2>&1 || { cat out; return 1; }
    local header
    header=$(echo "$DINO_HOME"/sdk/*/include/dino/core/fs.h)
    [[ -f $header && ! -w $header ]] || { ls -l "$header"; return 1; }
    chmod u+w "$header" && echo '// edited' >>"$header"
    dino doctor >out 2>&1 && { cat out; return 1; }
    grep -q 'differs from this dino.s SDK (include/dino/core/fs.h)' out || { cat out; return 1; }
}

# The probe doesn't need a project source to exist: the first one may be
# generated by the build that comes after the database is written.
case_compdb_generated_source() {
    project compdb_generated
    cat >CMakeLists.txt <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(compdb_generated C)
add_custom_command(OUTPUT gen.c
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c gen.c
    DEPENDS src/main.c)
add_executable(gen ${CMAKE_CURRENT_BINARY_DIR}/gen.c)
dino_executable(compdb_generated src/main.c)
CMAKE
    dino build >out 2>&1 || { cat out; return 1; }
    ! grep -q 'not rewritten' out || { cat out; return 1; }
    grep -q '"clang"' compile_commands.json
}

# With DINO_SDK_PREBUILT=1 the SDK is built once per zig version, target and
# build type, outside any project, and imported. These cases share one
# DINO_HOME, so only the first to need a set builds it.
prebuilt() {
    DINO_SDK_PATH='' DINO_HOME=$work/prebuilt_home DINO_SDK_PREBUILT=1 dino "$@"
}

# Whether the build log in $1 compiled the SDK in the project.
sdk_compiled() {
    grep -q 'mutex_posix\.c' "$1"
}

# A second project compiles no SDK source and its program runs; without the
# variable the SDK is compiled in the project as before.
case_prebuilt_shared() {
    project prebuilt_a
    prebuilt build >out 2>&1 || { cat out; return 1; }
    project prebuilt_b
    prebuilt build -v >out 2>&1 || { cat out; return 1; }
    sdk_compiled out && { echo "the SDK was compiled again"; return 1; }
    build/x86_64/prebuilt_b | grep -q 'Hello' || return 1
    rm -rf build
    DINO_SDK_PATH='' DINO_HOME=$work/prebuilt_home dino build -v >out 2>&1 || { cat out; return 1; }
    sdk_compiled out
}

# Debug and release are separate sets, built from their own flags: NDEBUG
# sets the default log level (logging.c). Switching back reuses the first.
case_prebuilt_modes() {
    project prebuilt_modes
    cat >src/main.c <<'C'
#include <stdio.h>
#include <dino/util/logging.h>

int main(void)
{
    printf("level %d\n", (int)dino_log_get_level());
    return 0;
}
C
    prebuilt build >/dev/null 2>&1 || return 1
    local release debug
    release=$(build/x86_64/prebuilt_modes)
    prebuilt build --debug >/dev/null 2>&1 || return 1
    debug=$(build/x86_64/prebuilt_modes)
    [[ $release != "$debug" ]] || { echo "both builds print '$release'"; return 1; }
    prebuilt build -v >out 2>&1 || { cat out; return 1; }
    ! sdk_compiled out && [[ $(build/x86_64/prebuilt_modes) == "$release" ]] &&
        [[ -d $(echo "$work"/prebuilt_home/sdk/*/lib/zig-*/x86_64-linux-musl-debug) ]]
}

# Flags that can't change the SDK's code, such as CI's, still use the set.
case_prebuilt_neutral_flags() {
    project prebuilt_neutral
    CFLAGS='-Wall -Wextra -Werror -std=c11 -fcolor-diagnostics' prebuilt build -v >out 2>&1 || { cat out; return 1; }
    ! sdk_compiled out
}

# Anything that could change the SDK's code keeps the source build, and with
# the flag: the set was built without it.
case_prebuilt_flag_fallback() {
    project prebuilt_fallback
    CFLAGS=-DDINO_TEST_X prebuilt build -v >out 2>&1 || { cat out; return 1; }
    grep 'mutex_posix\.c' out | grep -q -- -DDINO_TEST_X || { echo "CFLAGS"; cat out; return 1; }
    rm -rf build
    prebuilt build -v -- -DCMAKE_C_FLAGS=-fshort-enums >out 2>&1 || { cat out; return 1; }
    grep 'mutex_posix\.c' out | grep -q -- -fshort-enums || { echo "CMAKE_C_FLAGS"; cat out; return 1; }
    rm -rf build
    prebuilt build -v -- "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" >out 2>&1 || { cat out; return 1; }
    grep 'mutex_posix\.c' out | grep -q -- '-O2 -DNDEBUG' || { echo "CMAKE_C_FLAGS_RELEASE"; cat out; return 1; }
    rm -rf build
    sed -i 's/^project(/add_compile_definitions(DINO_TEST_Y)\nproject(/' CMakeLists.txt
    prebuilt build -v >out 2>&1 || { cat out; return 1; }
    grep 'mutex_posix\.c' out | grep -q -- -DDINO_TEST_Y || { echo "add_compile_definitions"; cat out; return 1; }
}

# Another zig gets its own set, and the project starts over with it.
case_prebuilt_zig_version() {
    project prebuilt_zig
    prebuilt build >/dev/null 2>&1 || return 1
    mkdir -p "$work/prebuilt_zig_bin"
    # shellcheck disable=SC2016 # $1 and $@ are for the wrapper, not this shell
    printf '#!/bin/sh\nif [ "$1" = version ]; then echo 0.17.98; exit 0; fi\nexec %s "$@"\n' \
        "$(command -v zig)" >"$work/prebuilt_zig_bin/zig"
    chmod +x "$work/prebuilt_zig_bin/zig"
    PATH=$work/prebuilt_zig_bin:$PATH prebuilt build >out 2>&1 || { cat out; return 1; }
    grep -q 'building the SDK' out && [[ -d $(echo "$work"/prebuilt_home/sdk/*/lib/zig-0.17.98) ]]
}

# Projects started together on an empty DINO_HOME all succeed, whichever
# builds the set first, and leave nothing temporary behind.
case_prebuilt_parallel() {
    local i pids=() failed=0 lib
    for i in 1 2 3 4; do
        project "prebuilt_par$i"
    done
    for i in 1 2 3 4; do
        (cd "$work/prebuilt_par$i" &&
            DINO_SDK_PATH='' DINO_HOME=$work/prebuilt_par_home DINO_SDK_PREBUILT=1 dino build >log 2>&1) &
        pids+=($!)
    done
    for i in "${pids[@]}"; do wait "$i" || failed=1; done
    ((failed == 0)) || { cat "$work"/prebuilt_par*/log; return 1; }
    lib=$(echo "$work"/prebuilt_par_home/sdk/*/lib/zig-*)
    [[ $(ls "$lib") == x86_64-linux-musl-release ]] || { ls "$lib"; return 1; }
}

# A set without its marker is reported and never linked; nor is it deleted,
# since it may be another dino's, just renamed into place.
case_prebuilt_incomplete() {
    project prebuilt_incomplete
    export DINO_SDK_PATH='' DINO_HOME=$work/prebuilt_incomplete_home DINO_SDK_PREBUILT=1
    dino build >/dev/null 2>&1 || return 1
    local set
    set=$(echo "$DINO_HOME"/sdk/*/lib/zig-*/x86_64-linux-musl-release)
    rm "$set/dino-prebuilt.cmake"
    rm -rf build
    dino build -v >out 2>&1 || { cat out; return 1; }
    grep -q 'not a complete prebuilt SDK' out && sdk_compiled out && [[ -f $set/libdino_core.a ]]
}

# A set that can't be written is a warning, and the SDK is compiled in the
# project.
case_prebuilt_readonly() {
    project prebuilt_readonly
    export DINO_SDK_PATH='' DINO_HOME=$work/prebuilt_ro_home
    dino doctor >/dev/null 2>&1
    local sdk
    sdk=$(echo "$DINO_HOME"/sdk/*)
    chmod a-w "$sdk"
    DINO_SDK_PREBUILT=1 dino build -v >out 2>&1
    local rc=$?
    chmod u+w "$sdk"
    ((rc == 0)) || { cat out; return 1; }
    grep -q 'compiling the SDK in the project instead' out && sdk_compiled out
}

# The SDK's options are applied to the imported set as to the source build.
case_prebuilt_options() {
    project prebuilt_options
    prebuilt build -v >out 2>&1 || { cat out; return 1; }
    grep -q 'dino__runtime_auto' out || { echo "no -u dino__runtime_auto"; return 1; }
    prebuilt build -v -- -DDINO_NO_AUTO_RUNTIME=ON -DDINO_NO_PLATFORM_WARNINGS=ON >out 2>&1 || { cat out; return 1; }
    ! grep -q 'dino__runtime_auto' out || { echo "DINO_NO_AUTO_RUNTIME ignored"; return 1; }
    grep -q 'DINO_ALLOW_PLATFORM_CALLS' out || { echo "DINO_NO_PLATFORM_WARNINGS ignored"; return 1; }
    prebuilt test -v >out 2>&1 || { cat out; return 1; }
    grep -q 'wrap=malloc' out || { echo "no leak check"; return 1; }
    prebuilt test -v -- -DDINO_NO_LEAK_CHECK=ON >out 2>&1 || { cat out; return 1; }
    ! grep -q 'wrap=malloc' out && ! sdk_compiled out
}

# The leak check works with the imported objects, a C++-only project included.
case_prebuilt_leak_check() {
    export DINO_SDK_PATH='' DINO_HOME=$work/prebuilt_home DINO_SDK_PREBUILT=1
    case_leak_check_allocators && case_leak_check_cxx_only
}

# The i386 target is built for an i686, without SSE2, rather than zig's
# default Pentium 4. qemu-user doesn't enforce a CPU model's features, so the
# compiler's own macros tell which CPU it built for.
case_i386_builds_for_i686() {
    project i386_cpu
    cat >src/main.c <<'C'
#include <stdio.h>

int main(void)
{
#if defined(__SSE2__)
    puts("sse2");
#elif defined(__i686__)
    puts("i686");
#else
    puts("other");
#endif
    return 0;
}
C
    dino build i386 >out 2>&1 || { cat out; return 1; }
    [[ $(dino run i386 --no-build 2>&1) == i686 ]] || { dino run i386 --no-build; return 1; }
    # win32 keeps zig's default: Windows itself needs SSE2.
    dino build win32 >out 2>&1 || { cat out; return 1; }
    [[ $(dino run win32 --no-build 2>/dev/null | tr -d '\r') == sse2 ]]
}

# A triple whose C library can't be linked statically (glibc here) builds
# dynamically instead of failing CMake's compiler check.
case_glibc_triple_links_dynamically() {
    project glibc
    dino build x86_64-linux-gnu >out 2>&1 || { cat out; return 1; }
    file build/x86_64-linux-gnu/glibc | grep -q 'dynamically linked' || { file build/x86_64-linux-gnu/glibc; return 1; }
    [[ $(build/x86_64-linux-gnu/glibc) == *Hello* ]]
}

# find_path and find_package don't find the host's headers and libraries,
# which are for another C library; find_program still finds host tools.
case_find_skips_host_paths() {
    project find_host
    cat >>CMakeLists.txt <<'CMAKE'
find_path(DINO_TEST_STDIO stdio.h)
find_package(ZLIB QUIET)
find_program(DINO_TEST_SH sh)
message(STATUS "find: ${DINO_TEST_STDIO} | ${ZLIB_FOUND} | ${DINO_TEST_SH}")
CMAKE
    dino build -v >out 2>&1 || { cat out; return 1; }
    local line
    line=$(grep -- '-- find:' out)
    if [[ $line != "-- find: DINO_TEST_STDIO-NOTFOUND | "*" | /"*sh || $line == *"| TRUE |"* ]]; then
        echo "$line"
        return 1
    fi
}

# A project may pick C89 (CMAKE_C_STANDARD or CFLAGS); the SDK's sources are
# C11 either way. Its tests can't: utest.h needs C99.
case_c89_project() {
    project c89
    sed -i 's/^project(/set(CMAKE_C_STANDARD 90)\nset(CMAKE_C_EXTENSIONS OFF)\nproject(/' CMakeLists.txt
    dino build >out 2>&1 || { cat out; return 1; }
    rm -rf build
    CFLAGS=-std=c89 dino build >out 2>&1 || { cat out; return 1; }
}

# The wrappers don't redeclare anything in -std=c89 -pedantic code: the
# pragma comes first, before any // comment.
case_wrappers_quiet_under_c89_pedantic() {
    project c89_pedantic
    cat >src/main.c <<'C'
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

int main(void)
{
    puts("ok");
    return 0;
}
C
    rm tests/test_main.c
    CFLAGS='-std=c89 -pedantic-errors' dino build >out 2>&1 || { cat out; return 1; }
}

# A project's own header with a wrapped name (spawn.h here, process.h on
# Windows) is found through the wrapper without breaking the build. The
# project's own include directories come before the wrappers'; a linked
# library's come after them, so its header is the one #include_next reaches.
case_own_header_with_a_wrapped_name() {
    project own_header
    cat >>CMakeLists.txt <<'CMAKE'
add_library(own_headers INTERFACE)
target_include_directories(own_headers INTERFACE include)
target_link_libraries(own_header PRIVATE own_headers)
CMAKE
    mkdir -p include
    printf '#define OWN_SPAWN 1\n' >include/spawn.h
    printf '#define OWN_PROCESS 2\n' >include/process.h
    mkdir -p src/net
    cat >src/net/use.c <<'C'
#include "spawn.h"
#include "process.h"

int own_headers(void)
{
    return OWN_SPAWN + OWN_PROCESS;
}
C
    dino build x86_64 win64 >out 2>&1 || { cat out; return 1; }
}

# LTO (IPO) works for the SDK and the project's own static libraries: CMake
# archives LTO objects with the compiler's archiver, which is zig's.
case_lto_static_libraries() {
    project lto
    sed -i 's/^project(/set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)\nproject(/' CMakeLists.txt
    cat >>CMakeLists.txt <<'CMAKE'
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo OUTPUT ipo_output)
message(STATUS "ipo: ${ipo} ${ipo_output}")
add_library(lto_lib STATIC lib/lib.c)
target_link_libraries(lto PRIVATE lto_lib)
CMAKE
    mkdir -p lib && printf 'int lto_lib(void) { return 1; }\n' >lib/lib.c
    dino build -v >out 2>&1 || { cat out; return 1; }
    grep -q -- '-- ipo: YES' out || { grep -- '-- ipo:' out; return 1; }
    grep -q -- '-flto' out
}

# A .rc file builds for Windows targets with zig's resource compiler, without a
# host windres.
case_windows_resource_file() {
    project rc
    cat >src/app.rc <<'RC'
1 VERSIONINFO
FILEVERSION 1,2,3,4
BEGIN
END
RC
    cat >>CMakeLists.txt <<'CMAKE'
if(WIN32)
    target_sources(rc PRIVATE src/app.rc)
endif()
CMAKE
    dino build win64 -v >out 2>&1 || { cat out; return 1; }
    grep -q 'dino-tools/rc' out || { echo "not built with zig rc"; grep -i 'app.rc' out; return 1; }
    # Its entry in compile_commands.json is kept as is, and the rest rewritten.
    ! grep -q 'not rewritten' out || { cat out; return 1; }
}

# An SDK option set after project() is too late, and the configure says so.
case_late_sdk_option_warns() {
    project late_option
    echo 'set(DINO_NO_LEAK_CHECK ON)' >>CMakeLists.txt
    dino build -v >out 2>&1 || { cat out; return 1; }
    grep -q 'DINO_NO_LEAK_CHECK is ON here, but the SDK read it as OFF' out || { cat out; return 1; }
    sed -i '$d' CMakeLists.txt
    sed -i 's/^project(/set(DINO_NO_LEAK_CHECK ON)\nproject(/' CMakeLists.txt
    rm -rf build
    dino build -v >out 2>&1 || { cat out; return 1; }
    ! grep -q 'DINO_NO_LEAK_CHECK is' out
}

# The build doesn't depend on finding zig on PATH again: the compiler and the
# ar wrapper name the zig found at configure time.
case_build_without_zig_on_path() {
    project zig_path
    cat >>CMakeLists.txt <<'CMAKE'
add_library(zig_path_lib STATIC lib/lib.c)
target_link_libraries(zig_path PRIVATE zig_path_lib)
CMAKE
    mkdir -p lib && printf 'int zig_path_lib(void) { return 1; }\n' >lib/lib.c
    dino build >out 2>&1 || { cat out; return 1; }
    printf 'int zig_path_lib(void) { return 2; }\n' >lib/lib.c
    local cmake_dir
    cmake_dir=$(dirname "$(command -v cmake)")
    PATH=$cmake_dir:/usr/bin:/bin cmake --build build/x86_64 >out 2>&1 || { cat out; return 1; }
}

# Names CMake or dino already use are refused before anything is written.
case_init_reserved_names() {
    local name
    for name in test all help install clean package preinstall dino_core dino_anything test_main \
        CMakeFiles Testing _dino _dino_libs; do
        mkdir -p "$work/reserved/$name"
        (cd "$work/reserved/$name" && dino init --no-configure)
        [[ $? == 2 ]] || { echo "$name: not refused"; return 1; }
        [[ -z $(ls -A "$work/reserved/$name") ]] || { echo "$name: files written"; return 1; }
    done
    dino init --no-configure "$work/reserved/new/help"
    [[ $? == 2 && ! -e $work/reserved/new ]] || { echo "new dir created"; return 1; }
    mkdir -p "$work/reserved/testing" && cd "$work/reserved/testing" && dino init --no-configure >/dev/null || return 1
    # A project that already exists keeps its name: init only adds to it.
    mkdir -p "$work/reserved/existing/test" && cd "$work/reserved/existing/test" || return 1
    echo 'project(mine C)' >CMakeLists.txt
    dino init --no-configure >/dev/null || { echo "existing project refused"; return 1; }
    [[ -f .clang-tidy ]]
}

# In a project that already exists, init adds the tooling files and nothing
# that its CMakeLists.txt would compile: no second main(), no sample test.
case_init_existing_project() {
    project init_existing
    mv src/main.c src/app.c
    rm tests/test_main.c .clang-tidy
    dino init --no-configure >out || { cat out; return 1; }
    cat out
    [[ ! -e src/main.c && ! -e tests/test_main.c && -f .clang-tidy ]] || return 1
    [[ $(grep -c 'skipped' out) == 2 ]] || return 1
    dino build >/dev/null 2>&1
}

# What goes wrong is named, and nothing is left that looks like a project.
case_init_failures() {
    mkdir -p "$work/init_fail/blocked" && cd "$work/init_fail" || return 1
    touch blocked/src
    dino init --no-configure blocked >out 2>&1 && { cat out; return 1; }
    grep -q 'creating blocked/src' out || { cat out; return 1; }
    [[ ! -e blocked/CMakeLists.txt ]] || { echo "CMakeLists.txt was written"; return 1; }

    # A link to nothing counts as there: the other files are still written.
    mkdir dangling && ln -s missing dangling/.gitignore
    dino init --no-configure dangling >out 2>&1 || { cat out; return 1; }
    [[ -f dangling/.clang-tidy && -L dangling/.gitignore ]] || { cat out; return 1; }

    # Configuring is a second step: when it can't run, the project is there
    # and init says so.
    DINO_LIBRARY_PATH=$work/init_fail/no_such_library dino init unconfigured >out 2>&1
    local code=$?
    cat out
    [[ $code == 1 && -f unconfigured/src/main.c ]] || return 1
    grep -q 'files were created, but configuring failed' out && grep -q '^Next:' out
}

# init -C <dir> works from <dir>, as -C does elsewhere.
case_init_dash_c() {
    mkdir -p "$work/c" && cd "$work/c" || return 1
    dino init --no-configure -C base app >/dev/null || return 1
    [[ -f base/app/CMakeLists.txt ]] || return 1
    grep -q 'project(app C)' base/app/CMakeLists.txt || return 1
    dino init --no-configure -C here >out || return 1
    grep -q 'project(here C)' here/CMakeLists.txt || return 1
    ! grep -q 'here/\./' out || { cat out; return 1; }
    # An absolute directory is not moved under -C.
    dino init --no-configure -C base "$work/c/absolute" >/dev/null || return 1
    [[ -f $work/c/absolute/CMakeLists.txt && ! -e base/${work#/} ]]
}

# The SDK and the test harness build under strict ISO C, whether it's asked
# for in CFLAGS or with CMAKE_C_STANDARD before project().
case_strict_c() {
    project strict_c
    CFLAGS=-std=c11 dino test >/dev/null || return 1
    project strict_c_standard
    sed -i '/^project/i set(CMAKE_C_STANDARD 11)\nset(CMAKE_C_EXTENSIONS OFF)' CMakeLists.txt
    dino test -v >out 2>&1 || { cat out; return 1; }
    grep -q 'std=c11 .*mutex_posix\.c' out
}

# The project's warning flags and -Werror don't reach the SDK's sources, which
# have their own set; DINO_SDK_WERROR makes that set fatal.
case_sdk_warnings() {
    project sdk_warnings
    CFLAGS='-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wcast-qual -Werror' \
        dino test -v >out 2>&1 || { cat out; return 1; }
    ! grep -q 'warning:' out || { grep 'warning:' out; return 1; }
    dino clean >/dev/null
    dino build -v -- -DDINO_SDK_WERROR=ON >out 2>&1 || { cat out; return 1; }
    grep -q -- '-Wno-everything -Wall -Wextra -Werror .*mutex_posix\.c' out
}

# A C++ project whose tests are tests/test_*.cpp.
# A test that outlives --timeout fails as a timeout and its output names the
# case that was running; the test's own TIMEOUT property takes precedence.
case_test_timeout() {
    project test_timeout
    cat >tests/test_main.c <<'C'
#include <utest.h>

#include <dino/core/time.h>

UTEST(slow, passes_first)
{
    EXPECT_EQ(1, 1);
}

UTEST(slow, sleeps)
{
    dino_sleep_ms(3000);
    EXPECT_EQ(1, 1);
}

UTEST_MAIN()
C
    dino test --timeout 1 >out 2>&1 && { cat out; return 1; }
    grep -q 'test_main .*Timeout' out || { cat out; return 1; }
    grep -q 'OK \] slow.passes_first' out || { cat out; return 1; }
    [[ $(grep 'slow\.' out | tail -n 1) == *'RUN      ] slow.sleeps' ]] || { cat out; return 1; }

    echo 'set_tests_properties(test_main PROPERTIES TIMEOUT 60)' >>CMakeLists.txt
    dino test --timeout 1 >out 2>&1 || { cat out; return 1; }
}

# Started with stdin, stdout and stderr closed, a program's first files must
# not become descriptors 0-2, not even the ones its own constructors open: the
# SDK's setup runs before them. Without the setup they do.
case_closed_stdio() {
    project closed_stdio
    cat >src/main.c <<'C'
#include <fcntl.h>
#include <stdio.h>

static int g_early = -1;

__attribute__((constructor)) static void open_early(void)
{
    g_early = open("early.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

int main(void)
{
    FILE *result = fopen("result.txt", "w");
    if (!result)
        return 1;
    fprintf(result, "%d %d\n", g_early, fileno(result));
    return fclose(result) == 0 ? 0 : 1;
}
C
    dino build >out 2>&1 || { cat out; return 1; }
    build/x86_64/closed_stdio 0<&- 1>&- 2>&- || return 1
    [[ $(cat result.txt) == '3 4' ]] || { echo "with the setup: $(cat result.txt)"; return 1; }

    dino build -- -DDINO_NO_AUTO_RUNTIME=ON >out 2>&1 || { cat out; return 1; }
    build/x86_64/closed_stdio 0<&- 1>&- 2>&- || return 1
    [[ $(cat result.txt) == '0 1' ]] || { echo "without the setup: $(cat result.txt)"; return 1; }
}

# Memory from every allocator entry point is counted: a block that is freed
# leaves the count at zero, and one that isn't is reported.
case_leak_check_allocators() {
    project leak_allocators
    cat >tests/test_main.c <<'C'
#define _GNU_SOURCE
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <utest.h>

static void *volatile kept;

UTEST(leak, allocator)
{
    const char *which = getenv("LEAK");
    void *p = NULL;
    if (!which || strcmp(which, "memalign") == 0)
        p = memalign(64, 100);
    else if (strcmp(which, "valloc") == 0)
        p = valloc(100);
    else if (strcmp(which, "reallocarray") == 0)
        p = reallocarray(NULL, 4, 8);
    else if (strcmp(which, "aligned_alloc") == 0)
        p = aligned_alloc(64, 128);
    ASSERT_TRUE(p != NULL);
    if (which)
        kept = p;
    else
        free(p);
}

UTEST_MAIN()
C
    dino test >out 2>&1 || { cat out; return 1; }
    local which
    for which in memalign valloc reallocarray aligned_alloc; do
        LEAK=$which dino test >out 2>&1 && { echo "$which"; cat out; return 1; }
        grep -q 'dino leak check: 1 allocation(s) still live at exit' out || { echo "$which"; cat out; return 1; }
    done
}

# What libc keeps for itself isn't reported, but a real leak next to it is.
case_leak_check_libc_caches() {
    project leak_caches
    cat >tests/test_main.c <<'C'
#include <netdb.h>
#include <pwd.h>
#include <stdlib.h>
#include <time.h>
#include <utest.h>

static void *volatile kept;

UTEST(leak, libc_keeps_these)
{
    EXPECT_TRUE(getpwuid(0) != NULL);
    gethostbyname("localhost");
    ASSERT_EQ(0, setenv("TZ", "/usr/share/zoneinfo/America/Argentina/ComodRivadavia", 1));
    time_t now = time(NULL);
    EXPECT_TRUE(localtime(&now) != NULL);
    if (getenv("LEAK"))
        kept = malloc(3);
}

UTEST_MAIN()
C
    dino test >out 2>&1 || { cat out; return 1; }
    LEAK=1 dino test >out 2>&1 && { cat out; return 1; }
    grep -q 'dino leak check: 1 allocation(s) still live at exit' out || { cat out; return 1; }
}

# More objects with destructors than fit in libc's first block of exit
# handlers, and a locale, which libc++ and libc keep loaded.
case_leak_check_cxx_statics() {
    cxx_project leak_cxx_many
    cat >tests/test_main.cpp <<'CPP'
#include <locale>
#include <string>
#include <utest.h>

struct Named
{
    std::string name{"long enough to need a heap allocation, not SSO"};
    ~Named() {}
};
static Named g_a[1], g_b[1], g_c[1], g_d[1], g_e[1], g_f[1], g_g[1], g_h[1], g_i[1], g_j[1];
#define FOUR(n) static Named g_##n##0, g_##n##1, g_##n##2, g_##n##3;
FOUR(k) FOUR(l) FOUR(m) FOUR(n) FOUR(o) FOUR(p) FOUR(q) FOUR(r) FOUR(s) FOUR(t)

UTEST(cxx, many_globals)
{
    std::locale named("");
    EXPECT_EQ(1u, sizeof(g_a) / sizeof(g_a[0]));
}

UTEST_MAIN()
CPP
    LANG=en_US.UTF-8 dino test >out 2>&1 || { cat out; return 1; }
}

# A library in $work/<directory> for DINO_LIBRARY_PATH: target <name>, alias
# <name>::<name>, and a test_main, the name every `dino init` project uses.
# <tests> is the CMake that defines the test.
library() {
    local dir=$work/$1 name=$2 tests=$3
    rm -rf "$dir"
    mkdir -p "$dir/src" "$dir/tests"
    echo "int ${name}_value(void) { return 7; }" >"$dir/src/lib.c"
    cat >"$dir/tests/test_main.c" <<C
#include <utest.h>

int ${name}_value(void);

UTEST($name, value)
{
    EXPECT_EQ(7, ${name}_value());
}

UTEST_MAIN()
C
    cat >"$dir/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.20)
project($name C)
add_library($name STATIC src/lib.c)
add_library($name::$name ALIAS $name)
$tests
CMAKE
}

# Libraries whose tests have the same names as the project's, and as each
# other's, are no problem when dino_test() gets LIBRARIES: only the tests of
# the project being tested exist.
case_library_tests_stay_out() {
    library libs/one one 'dino_test(test_main tests/test_main.c LIBRARIES one::one)'
    library libs/two two 'dino_test(test_main tests/test_main.c LIBRARIES two::two)'
    export DINO_LIBRARY_PATH=$work/libs/one:$work/libs/two
    project library_app
    dino test >out 2>&1 || { cat out; return 1; }
    grep -q '100% tests passed, 0 tests failed out of 1$' out || { cat out; return 1; }
    dino test -C "$work/libs/one" >out 2>&1 || { cat out; return 1; }
    grep -q '100% tests passed, 0 tests failed out of 1$' out || { cat out; return 1; }
}

# Without LIBRARIES the library's test target exists in every project, and the
# error says which library to fix. On its own the library still works.
case_library_test_name_clash() {
    library libs/old old 'dino_test(test_main tests/test_main.c)
target_link_libraries(test_main PRIVATE old::old)'
    export DINO_LIBRARY_PATH=$work/libs/old
    project clash_app
    dino test >out 2>&1 && { cat out; return 1; }
    grep -q 'dino_test(test_main): the library in' out || { cat out; return 1; }
    grep -q "$work/libs/old" out || { cat out; return 1; }
    dino test -C "$work/libs/old" >out 2>&1 || { cat out; return 1; }
}

# Testing a checkout of a library while another copy of it is registered.
case_library_checkout() {
    library libs/same same 'dino_test(test_main tests/test_main.c LIBRARIES same::same)'
    library checkout/same same 'dino_test(test_main tests/test_main.c LIBRARIES same::same)'
    export DINO_LIBRARY_PATH=$work/libs/same
    cd "$work/checkout/same" || return 1
    dino test -v >out 2>&1 || { cat out; return 1; }
    grep -q 'another copy of this project (same)' out || { cat out; return 1; }
}

# Tests a library registers with add_test() run when the library is tested,
# and are disabled, not run and not failed, in a project that uses it.
case_library_add_test() {
    library libs/third third 'enable_testing()
add_executable(third_selftest tests/selftest.c)
add_test(NAME third_selftest COMMAND third_selftest "" "a;b")
set_tests_properties(third_selftest PROPERTIES TIMEOUT 60)
add_test(third_old third_selftest)'
    echo 'int main(int argc, char **argv) { return argc == 3 && !argv[1][0] ? 0 : 1; }' >"$work/libs/third/tests/selftest.c"
    export DINO_LIBRARY_PATH=$work/libs/third
    project add_test_app
    dino test >out 2>&1 || { cat out; return 1; }
    grep -q '100% tests passed, 0 tests failed out of 1$' out || { cat out; return 1; }
    grep -q 'third_selftest (Disabled)' out || { cat out; return 1; }
    grep -q 'third_old (Disabled)' out || { cat out; return 1; }
    # third_old passes no arguments, so only third_selftest can pass: the
    # empty argument and the one with a semicolon arrived as written.
    dino test -C "$work/libs/third" >out 2>&1 && { cat out; return 1; }
    grep -q 'third_selftest \.* *Passed' out || { cat out; return 1; }
}

cxx_project() {
    project "$1"
    sed -i "s/^project($1 C)\$/project($1 C CXX)/; s/test_\\*\\.c)/test_*.cpp)/" CMakeLists.txt
    rm tests/test_main.c
}

# Memory a C++ global frees in its destructor isn't reported, since the
# report runs after static destructors; a real leak still is.
case_leak_check_cxx_global() {
    cxx_project leak_cxx
    cat >tests/test_main.cpp <<'CPP'
#include <cstdlib>
#include <string>
#include <vector>
#include <utest.h>

static std::vector<std::string> g_names{"long enough to need a heap allocation, not SSO"};

std::string *volatile kept;

UTEST(cxx, global)
{
    ASSERT_EQ(g_names.size(), 1u);
    if (std::getenv("LEAK"))
        kept = new std::string(g_names[0]);
}

UTEST_MAIN()
CPP
    dino test >out 2>&1 || { cat out; return 1; }
    LEAK=1 dino test >out 2>&1 && return 1
    grep -q 'dino leak check: .* still live at exit' out
}

# libc keeps what setenv and friends allocate for the environment; that isn't
# reported, a real leak in the same test is.
case_leak_check_setenv() {
    project leak_setenv
    cat >tests/test_main.c <<'C'
#include <stdlib.h>
#include <utest.h>

static char entry[] = "DINO_PUT=1";

UTEST(env, kept)
{
    ASSERT_EQ(setenv("DINO_A", "1", 1), 0);
    ASSERT_EQ(setenv("DINO_A", "a longer value", 1), 0);
    ASSERT_EQ(setenv("DINO_B", "2", 0), 0);
    ASSERT_EQ(putenv(entry), 0);
    ASSERT_EQ(unsetenv("DINO_B"), 0);
    if (getenv("LEAK"))
    {
        void *volatile p = malloc(16);
        (void)p;
    }
}

UTEST_MAIN()
C
    dino test >out 2>&1 || { cat out; return 1; }
    ! grep -q 'more frees' out || { cat out; return 1; }
    LEAK=1 dino test >out 2>&1 && return 1
    grep -q 'dino leak check: 1 allocation(s) still live at exit' out
}

# A project with only CXX enabled gets dino_test() and its leak check: the
# check is C, built in the SDK's directory, where C is enabled.
case_leak_check_cxx_only() {
    project leak_cxx_only
    sed -i 's/^project(leak_cxx_only C)$/project(leak_cxx_only CXX)/; s/\*\.c)/*.cpp)/' CMakeLists.txt
    mv src/main.c src/main.cpp
    cat >tests/test_main.cpp <<'CPP'
#include <cstdlib>
#include <utest.h>

int *volatile kept;

UTEST(cxx, only)
{
    if (std::getenv("LEAK"))
        kept = new int(1);
}

UTEST_MAIN()
CPP
    rm tests/test_main.c
    dino test >out 2>&1 || { cat out; return 1; }
    LEAK=1 dino test >out 2>&1 && return 1
    grep -q 'dino leak check: 1 allocation(s) still live at exit' out
}

# A program that exits 7 after its first SIGINT or SIGTERM, and reports how
# many arrived.
signal_program() {
    cat >src/main.c <<'C'
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t got;

static void on_signal(int sig)
{
    (void)sig;
    got++;
}

int main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    puts("ready");
    fflush(stdout);
    while (!got)
        pause();
    usleep(300000); // a second delivery would arrive meanwhile
    printf("got %d\n", (int)got);
    return 7;
}
C
    dino build >/dev/null
}

wait_for_ready() {
    for _ in $(seq 100); do
        grep -q ready "$1" 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

# SIGTERM sent to `dino run` reaches the program, and dino waits for it and
# exits with its status.
case_run_forwards_sigterm() {
    project run_sigterm
    signal_program || return 1
    dino run --no-build >out &
    local pid=$!
    wait_for_ready out || return 1
    kill -TERM "$pid"
    timeout 10 tail --pid="$pid" -f /dev/null || { pkill run_sigterm; return 1; }
    wait "$pid"
    local code=$?
    cat out
    [[ $code == 7 ]] && grep -qx 'got 1' out
}

# A signal that was ignored when dino started stays ignored in what it runs:
# nohup and background jobs of a script depend on it. Bits 0 and 1 of SigIgn
# are SIGHUP and SIGINT.
case_ignored_signals_stay_ignored() {
    project ignored_signals
    cat >src/main.c <<'C'
#include <stdio.h>
#include <string.h>

int main(void)
{
    char  line[256];
    FILE *status = fopen("/proc/self/status", "r");
    if (!status)
        return 1;
    while (fgets(line, sizeof(line), status))
        if (strncmp(line, "SigIgn:", 7) == 0)
            fputs(line, stdout);
    fclose(status);
    return 0;
}
C
    dino build >/dev/null || return 1
    (trap '' HUP INT; dino run --no-build) >out || { cat out; return 1; }
    cat out
    grep -Eq '^SigIgn:.*[37bf]$' out
}

# A signal sent to dino alone also ends what the program started. The program
# dies of it and would otherwise leave its child running.
case_signal_reaches_grandchildren() {
    project grandchildren
    cat >src/main.c <<'C'
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    pid_t child = fork();
    if (child == 0)
    {
        sleep(60);
        return 0;
    }
    printf("%d\nready\n", (int)child);
    fflush(stdout);
    pause();
    return 0;
}
C
    dino build >/dev/null 2>&1 || return 1
    dino run --no-build >out &
    local pid=$!
    wait_for_ready out || return 1
    local grandchild
    grandchild=$(head -n 1 out)
    kill -TERM "$pid"
    wait "$pid"
    local code=$?
    [[ $code == 143 ]] || { echo "dino exited $code"; kill "$grandchild"; return 1; }
    for _ in $(seq 50); do
        kill -0 "$grandchild" 2>/dev/null || return 0
        sleep 0.1
    done
    echo "the program's child $grandchild is still running"
    kill "$grandchild"
    return 1
}

# Interrupted while building one target, dino stops instead of going on to
# the next, and exits 128 + the signal.
case_interrupted_build_stops() {
    project interrupted
    echo 'if(ZIG_TARGET MATCHES x86_64)
    message(STATUS "configuring slowly")
    execute_process(COMMAND sleep 5)
endif()' >>CMakeLists.txt
    dino build -v x86_64 i386 >out 2>&1 &
    local pid=$!
    for _ in $(seq 100); do
        grep -q 'configuring slowly' out && break
        sleep 0.1
    done
    kill -TERM "$pid"
    wait "$pid"
    local code=$?
    cat out
    [[ $code == 143 ]] && ! grep -q '^==> i386' out
}

# Ctrl-C at a terminal reaches the program once (from the terminal, not again
# from dino), and dino exits with the program's status.
# script runs the command through $SHELL; bash execs it, dash would stay
# in between and die of the Ctrl-C itself.
case_run_ctrl_c() {
    project run_ctrl_c
    signal_program || return 1
    # Ctrl-C is typed once the program has written to out.
    # shellcheck disable=SC2094
    { wait_for_ready out && printf '\003' && sleep 2; } |
        SHELL=/bin/bash timeout 20 script -qefc 'dino run --no-build' /dev/null >out
    local code=$?
    cat out
    [[ $code == 7 ]] && grep -q 'got 1' out
}

cases=("$@")
if ((${#cases[@]} == 0)); then
    while read -r c; do cases+=("$c"); done < <(declare -F | sed -n 's/^declare -f case_//p')
fi
failed=0
for c in "${cases[@]}"; do
    if (case_"$c") >"$work/log" 2>&1; then
        printf '%-24s ok\n' "$c"
    else
        printf '%-24s FAILED\n' "$c"
        tail -n 20 "$work/log" | sed 's/^/    /'
        failed=1
    fi
done
exit $failed
