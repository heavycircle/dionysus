#!/usr/bin/env bash
# Builds a new project and runs the SDK's tests with the oldest CMake dino
# supports (min_cmake in cli/commands/doctor.zig), installed from PyPI. Needs
# uv and the dino from `zig build`; runs natively, so only x86_64 is tested
# and the other targets are just built:
#
#   zig build && tests/cmake-minimum.sh [cmake version]
set -euo pipefail

version=${1:-3.20.5}
repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

uv venv -q "$work/cmake"
uv pip install -q -p "$work/cmake" "cmake==$version"
# This checkout's dino, never another one on PATH.
[[ -x $repo/zig-out/bin/dino ]] || {
    echo "tests/cmake-minimum.sh: no zig-out/bin/dino; run \`zig build\` first" >&2
    exit 1
}
export PATH=$work/cmake/bin:$repo/zig-out/bin:$PATH
export DINO_SDK_PATH=$repo/sdk DINO_HOME=$work/home

cmake --version | head -n 1
dino doctor | grep -q '\[ ok \] cmake' || { dino doctor; exit 1; }
dino init "$work/app"
dino test -C "$work/app" x86_64
dino build -C "$work/app" arm64 win64
"$repo/tests/sdk.sh" x86_64
