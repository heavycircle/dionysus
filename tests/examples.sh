#!/usr/bin/env bash
# Build every example for every target with dino, then run each result
# (natively, under qemu-user, or under wine), and run `dino test` for examples
# that have a tests/ directory. org_library is on DINO_LIBRARY_PATH for every
# example: org_app links it, and the rest check that an unused library is
# never built. Intended for the Docker image,
# which has all runners installed:
#
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/examples.sh /repo/examples [targets...]
#
# With no targets, every target is tested. The SDK comes from this checkout
# (DINO_SDK_PATH overrides it; set but empty, dino's built-in SDK is used, as
# DINO_SDK_PREBUILT=1 needs). CFLAGS from the environment reaches
# every compile (CI sets -Wall -Wextra -Werror). Examples are copied to a
# scratch directory, so the source tree is never written.
set -uo pipefail

src=${1:-examples}
shift || true
targets=("$@")
((${#targets[@]})) || targets=(arm arm64 i386 ppc win32 win64 x86_64)

# Failures tracked in issues. Remove an entry once it's fixed; an unexpected
# pass fails the run so this list can't go stale.
declare -A known_failures=()

# Libraries have nothing to run; their tests still run.
declare -A build_only=([org_library]=1)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export DINO_HOME=${DINO_HOME:-$work/home}
# The checkout's SDK, not the one built into dino, which may be older.
DINO_SDK_PATH=${DINO_SDK_PATH-$(cd "$(dirname "$0")/../sdk" && pwd)}
export DINO_SDK_PATH
cp -r "$src"/*/ "$work"/
export DINO_LIBRARY_PATH=$work/org_library

# docs/guide/libraries.md promises a library on DINO_LIBRARY_PATH is built only
# for the projects that link it. Every example has org_library on the path and
# only org_app links it; org_library itself builds as its own project.
library_built_only_if_linked() {
    local name=$1 target=$2 built
    [[ $name == org_library ]] && return 0
    built=$(find "$work/$name/build/$target/_dino_libs" -type f \
        \( -name '*.o' -o -name '*.obj' -o -name '*.a' -o -name '*.lib' \) 2>/dev/null)
    if [[ $name == org_app ]]; then
        [[ -n $built ]]
    else
        [[ -z $built ]]
    fi
}

unexpected=0
printf '%-18s %-8s %s\n' EXAMPLE TARGET RESULT
for dir in "$src"/*/; do
    name=$(basename "$dir")
    for target in "${targets[@]}"; do
        key="$name/$target"
        log="$work/$name.$target.log"
        result=ok
        if ! dino build "$target" -C "$work/$name" >"$log" 2>&1; then
            result="build failed"
        elif ! grep -q "^==> $target " "$log"; then
            # dino's own lines and CMake's share the file: none may overwrite another.
            result="build output missing from the log"
        elif ! library_built_only_if_linked "$name" "$target"; then
            result="org_library built for a project that doesn't link it, or not for org_app"
        elif [[ -z ${build_only[$name]:-} ]] &&
            ! timeout 120 dino run "$target" --no-build -C "$work/$name" >>"$log" 2>&1 </dev/null; then
            result="run failed"
        elif [[ -d $dir/tests ]] && ! timeout 300 dino test "$target" -C "$work/$name" >>"$log" 2>&1 </dev/null; then
            result="tests failed"
        elif [[ -d $dir/tests ]]; then
            result="ok (+ tests)"
        fi

        if [[ -n ${known_failures[$key]:-} ]]; then
            if [[ $result == ok* ]]; then
                result="UNEXPECTED PASS (remove from known_failures, ${known_failures[$key]})"
                unexpected=$((unexpected + 1))
            else
                result="expected failure (${known_failures[$key]})"
            fi
        elif [[ $result != ok* ]]; then
            unexpected=$((unexpected + 1))
            result="FAILED: $result"
            sed 's/^/        /' "$log" | tail -n 20
        elif [[ -n ${build_only[$name]:-} && -d $dir/tests ]]; then
            result="ok (not run, + tests)"
        elif [[ -n ${build_only[$name]:-} ]]; then
            result="ok (build only)"
        fi
        printf '%-18s %-8s %s\n' "$name" "$target" "$result"
    done
done

echo
if ((unexpected)); then
    echo "$unexpected unexpected result(s)"
    exit 1
fi
echo "all examples behaved as expected"
