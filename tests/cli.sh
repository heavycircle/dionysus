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

# `dino clean` removes a build dir whose first configure failed.
case_clean_failed() {
    project clean_failed
    echo 'message(FATAL_ERROR "broken")' >>CMakeLists.txt
    dino build >/dev/null 2>&1 && return 1
    [[ -d build/x86_64 ]] || return 1
    dino clean >/dev/null || return 1
    [[ ! -e build ]]
}

# `dino run` finds an executable built outside the build directory.
case_run_output_outside_build() {
    project run_outside
    # shellcheck disable=SC2016 # for CMake to expand
    sed -i '/^dino_executable/i set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)' CMakeLists.txt
    dino run | grep -qx 'Hello from run_outside!'
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
        'init a b' 'targets x' 'doctor x' 'run --exe'; do
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

# An unpacked SDK without its marker isn't deleted (it may be another dino's,
# just renamed into place); dino says what to remove instead.
case_sdk_leftover_reported() {
    project leftover
    export DINO_SDK_PATH='' DINO_HOME=$work/leftover_home
    dino doctor >/dev/null 2>&1
    local sdk
    sdk=$(echo "$DINO_HOME"/sdk/*)
    [[ -f $sdk/.dino-sdk ]] || return 1
    rm "$sdk/.dino-sdk"
    dino build 2>err && return 1
    cat err
    grep -q 'is not a complete SDK' err && [[ -d $sdk/cmake ]]
}

# Names CMake or dino already use are refused before anything is written.
case_init_reserved_names() {
    local name
    for name in test all help install clean package dino_core dino_anything test_main; do
        mkdir -p "$work/reserved/$name"
        (cd "$work/reserved/$name" && dino init --no-configure)
        [[ $? == 2 ]] || { echo "$name: not refused"; return 1; }
        [[ -z $(ls -A "$work/reserved/$name") ]] || { echo "$name: files written"; return 1; }
    done
    dino init --no-configure "$work/reserved/new/help"
    [[ $? == 2 && ! -e $work/reserved/new ]] || { echo "new dir created"; return 1; }
    mkdir -p "$work/reserved/testing" && cd "$work/reserved/testing" && dino init --no-configure >/dev/null
}

# init -C <dir> works from <dir>, as -C does elsewhere.
case_init_dash_c() {
    mkdir -p "$work/c" && cd "$work/c" || return 1
    dino init --no-configure -C base app >/dev/null || return 1
    [[ -f base/app/CMakeLists.txt ]] || return 1
    grep -q 'project(app C)' base/app/CMakeLists.txt || return 1
    dino init --no-configure -C here >/dev/null || return 1
    grep -q 'project(here C)' here/CMakeLists.txt
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
