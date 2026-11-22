#!/usr/bin/env bash
# Smoke test of a built image, run by release.yml before it pushes one: the
# image holds the dino the tag names, with every tool and runner, and a new
# project builds, runs and passes its tests on every target using only what is
# inside the image. Then the entrypoint checks (tests/docker.sh). Runs on the
# host:
#
#   docker build -t dionysus . && tests/image.sh dionysus [VERSION]
#
# With DINO_IMAGE_WARM=1 (release.yml), the image must also have been built
# with WARM_ZIG_CACHE=1: its zig cache already holds every target's C library.
set -uo pipefail

image=${1:?usage: tests/image.sh IMAGE [VERSION]}
version=${2:-}
failed=0

fail() {
    echo "FAILED: $1"
    failed=1
}

reported=$(docker run --rm "$image" dino version)
if [[ -z $reported ]]; then
    fail "dino version printed nothing"
elif [[ -n $version && $reported != "dino $version" ]]; then
    fail "the image holds '$reported', expected 'dino $version'"
else
    echo "ok      $reported"
fi

# doctor exits 0 when only a runner is missing; the image must have them all.
if ! doctor=$(docker run --rm "$image" dino doctor 2>&1) || grep -q '\[MISS\]' <<<"$doctor"; then
    fail "dino doctor reports something missing"
    echo "        ${doctor//$'\n'/$'\n'        }"
else
    echo "ok      dino doctor finds every tool and runner"
fi

# Expanded by the container's sh.
# shellcheck disable=SC2016
project='set -e
    dino init app >/dev/null
    cd app
    dino build --all
    for target in $(dino targets | sed -n "s/^[* ] \([a-z0-9]*\)  .*/\1/p"); do
        dino run "$target" --no-build
    done
    dino test --all'
if log=$(docker run --rm "$image" sh -c "$project" 2>&1); then
    echo "ok      a new project builds, runs and passes its tests on every target"
else
    fail "a new project doesn't build, run and pass its tests on every target"
    log=$(tail -n 40 <<<"$log")
    echo "        ${log//$'\n'/$'\n'        }"
fi

# zig uses the image's cache unless the user chose one, by variable or by a
# mount at its default place under the home directory.
for case in "-|/opt/zig-cache" \
    "--tmpfs /home/dino/.cache/zig:uid=1000|" \
    "-e ZIG_GLOBAL_CACHE_DIR=/tmp/mine|/tmp/mine"; do
    args=${case%|*} want=${case#*|}
    [[ $args == - ]] && args=
    # shellcheck disable=SC2086,SC2016
    got=$(docker run --rm $args "$image" sh -c 'printf %s "${ZIG_GLOBAL_CACHE_DIR-}"')
    if [[ $got == "$want" ]]; then
        echo "ok      zig cache with '${args:-no options}': ${want:-the default}"
    else
        fail "zig cache with '${args:-no options}' is '$got', expected '$want'"
    fi
done

# A warm cache is one a Release build of a new project barely adds to: building
# a C library adds thousands of files, the project's own objects a few dozen.
# As an arbitrary user, so the cache must be writable by whoever the entrypoint
# picks, not only by the user that warmed it.
if [[ ${DINO_IMAGE_WARM:-0} == 1 ]]; then
    # shellcheck disable=SC2016
    warm='set -e
        before=$(find "$ZIG_GLOBAL_CACHE_DIR" -type f | wc -l)
        cd /tmp && dino init --no-configure app >/dev/null && dino build --all -C app >/dev/null
        after=$(find "$ZIG_GLOBAL_CACHE_DIR" -type f | wc -l)
        echo "$before $after"'
    if counts=$(docker run --rm --user 4321:4321 "$image" sh -c "$warm" 2>&1) &&
        read -r before after <<<"$counts" && ((before > 1000 && after - before < 500)); then
        echo "ok      the zig cache is warm for every target and writable by any user"
    else
        fail "the zig cache isn't warm, or another user can't use it (files before and after: $counts)"
    fi
fi

"$(dirname "$0")/docker.sh" "$image" || failed=1

exit $failed
