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
