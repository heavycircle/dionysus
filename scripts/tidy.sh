#!/usr/bin/env bash
# Run clang-tidy (.clang-tidy, warnings as errors) on every SDK source under
# two compile databases, x86_64 and win64: shared files under both, each
# *_posix.c / *_win32.c under its own. Uses dino's rewritten
# compile_commands.json, so the real musl/MinGW headers are used.
#
#   scripts/tidy.sh                                         # clang-tidy from PATH
#   scripts/tidy.sh uvx --from clang-tidy==19.1.0 clang-tidy  # pinned, as CI runs it
#
# Needs zig, cmake and a dino built from this checkout (zig build).
set -euo pipefail

tidy=("$@")
((${#tidy[@]})) || tidy=(clang-tidy)
cd "$(dirname "$0")/.."
dino=${DINO:-$PWD/zig-out/bin/dino}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export DINO_SDK_PATH=$PWD/sdk DINO_HOME=$work/home

# Any project made with dino_executable() puts the SDK sources in its database.
"$dino" init "$work/proj" --no-configure >/dev/null

failed=0
for target in x86_64 win64; do
    # A failed compile still leaves the database behind; analysis is what matters here.
    "$dino" build "$target" -C "$work/proj" >/dev/null 2>&1 || true
    db=$work/db-$target
    mkdir -p "$db"
    cp "$work/proj/compile_commands.json" "$db/" || {
        echo "tidy: no compile database for $target" >&2
        exit 1
    }
    for src in sdk/src/*/*.c; do
        # Each platform half is only in its own target's database.
        case "$target:$src" in
        x86_64:*_win32.c | win64:*_posix.c) continue ;;
        esac
        if ! "${tidy[@]}" --quiet --warnings-as-errors="*" -p "$db" "$PWD/$src"; then
            echo "tidy: $src ($target) has findings" >&2
            failed=1
        fi
    done
done
exit $failed
