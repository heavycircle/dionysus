#!/usr/bin/env bash
# Build every example for every target with dino, then run each result
# (natively, under qemu-user, or under wine), and run `dino test` for examples
# that have a tests/ directory. org_library is on DINO_LIBRARY_PATH for every
# example: org_app links it, and the rest show an unused library changes
# nothing. Intended for the Docker image,
# which has all runners installed:
#
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/examples.sh /repo/examples [targets...]
#
# With no targets, every target is tested. CFLAGS from the environment reaches
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

# Billions of loop iterations are fine natively but take minutes under emulation.
# Libraries have nothing to run; their tests still run.
declare -A build_only=([thread_example]=1 [org_library]=1)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export DINO_HOME=${DINO_HOME:-$work/home}
cp -r "$src"/*/ "$work"/
export DINO_LIBRARY_PATH=$work/org_library

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
