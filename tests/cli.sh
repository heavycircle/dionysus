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
