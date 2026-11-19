#!/usr/bin/env bash
# Check that CMake configure checks (check_function_exists, check_type_size,
# ...) test the target being built: configure tests/fixtures/configure_checks,
# which fails when a check gives the wrong answer for the target. Intended for
# the Docker image:
#
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/configure.sh arm64 win64
#
# With no arguments, every target is checked.
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export DINO_SDK_PATH=$repo/sdk DINO_HOME=${DINO_HOME:-$work/home}

targets=("$@")
((${#targets[@]})) || targets=(arm arm64 i386 ppc win32 win64 x86_64)

cp -r "$repo/tests/fixtures/configure_checks" "$work/project"
failed=0
for target in "${targets[@]}"; do
    if dino build "$target" -C "$work/project" >"$work/log" 2>&1; then
        printf '%-8s ok\n' "$target"
    else
        printf '%-8s FAILED\n' "$target"
        sed -n '/configure checks for/,/Configuring incomplete/p' "$work/log" |
            grep -v -e '^ *$' -e 'Configuring incomplete' | sed 's/^ */    /'
        grep -q 'configure checks for' "$work/log" || tail -n 20 "$work/log" | sed 's/^/    /'
        failed=1
    fi
done
exit $failed
