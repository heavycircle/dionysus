#!/usr/bin/env bash
# Run the SDK's own tests (sdk/tests) with `dino test`, from a scratch copy so
# the source tree is never written. Intended for the Docker image, which has
# qemu-user, wine and valgrind:
#
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/sdk.sh arm64 win64
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/sdk.sh --memcheck
#
# Arguments go to `dino test` (targets, --all, --memcheck, -R, ...).
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cp -r "$repo/sdk/tests" "$work/tests"
export DINO_SDK_PATH=$repo/sdk DINO_HOME=${DINO_HOME:-$work/home}
dino test "$@" -C "$work/tests"
