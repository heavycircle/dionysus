#!/usr/bin/env bash
# Check the portability warnings (sdk/portability): build tests/fixtures/raw_calls,
# where clang's -verify fails the build unless every flagged call warns exactly
# as its `expected-warning` comment says and nothing else does, in four
# language modes. Then build a copy with DINO_NO_PLATFORM_WARNINGS ON, where
# nothing may warn. --memcheck does the same against glibc's headers (the
# `dino test --memcheck` build). Intended for the Docker image:
#
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/warnings.sh arm64 win64
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/warnings.sh --memcheck
#
# With no arguments, every target is checked.
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export DINO_SDK_PATH=$repo/sdk DINO_HOME=${DINO_HOME:-$work/home}

targets=("$@")
((${#targets[@]})) || targets=(arm arm64 i386 ppc win32 win64 x86_64)

cp -r "$repo/tests/fixtures/raw_calls" "$work/warn"
cp -r "$repo/tests/fixtures/raw_calls" "$work/quiet"
# The project-wide opt-out as a project sets it: before project(), where dino
# adds the SDK.
sed -i 's/^project(/set(DINO_NO_PLATFORM_WARNINGS ON)\nproject(/' "$work/quiet/CMakeLists.txt"

failed=0
for target in "${targets[@]}"; do
    for mode in warn quiet; do
        if [[ $target == --memcheck ]]; then
            cmd=(dino test --memcheck -C "$work/$mode")
        else
            cmd=(dino build "$target" -C "$work/$mode")
        fi
        if "${cmd[@]}" >"$work/log" 2>&1; then
            printf '%-12s %-6s ok\n' "${target#--}" "$mode"
        else
            printf '%-12s %-6s FAILED\n' "${target#--}" "$mode"
            grep -E "error|File |warning:" "$work/log" | sort -u | sed 's/^/    /' | head -n 40
            failed=1
        fi
    done
done
exit $failed
