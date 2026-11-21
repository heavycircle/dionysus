#!/usr/bin/env bash
# Smoke test of a built image, run by release.yml before it pushes one: the
# image holds the dino the tag names, with every tool and runner, and a new
# project builds, runs and passes its tests on every target using only what is
# inside the image. Then the entrypoint checks (tests/docker.sh). Runs on the
# host:
#
#   docker build -t dionysus . && tests/image.sh dionysus [VERSION]
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

"$(dirname "$0")/docker.sh" "$image" || failed=1

exit $failed
