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

# utest reports a skipped case as passed and ctest shows its executable green,
# so a case that stops testing anything would go unnoticed. Only the skips
# with a known reason are allowed; tests/windows.sh runs those cases for real.
unexpected=0
for log in "$work"/tests/build/*/Testing/Temporary/LastTest.log; do
    [[ -e $log ]] || continue
    target=$(basename "${log%/Testing/Temporary/LastTest.log}")
    while read -r name; do
        case "$target:$name" in
        win32:file_size.symlink_to_* | win64:file_size.symlink_to_*)
            echo "sdk: $target: skipped $name (wine can't create symbolic links)"
            ;;
        *)
            echo "sdk: $target: unexpected skip: $name" >&2
            unexpected=1
            ;;
        esac
    done < <(sed -n 's/^\[  SKIPPED \] \([^ ]*\) (.*/\1/p' "$log" | sort -u)
done
exit $unexpected
