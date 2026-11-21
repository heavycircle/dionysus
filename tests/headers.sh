#!/usr/bin/env bash
# Check every public SDK header (sdk/include/dino/**.h):
#   - it compiles on its own, as C11 and as C++11, warning-free with
#     -Wpedantic and -Wconversion (projects' warning flags apply to the
#     headers, unlike the SDK's sources), for 64- and 32-bit Linux and Windows
#   - it has extern "C" guards, so C++ code links against the C functions
#   - everything it adds is in the SDK's namespace: macros start with DINO_,
#     and functions, types and enum constants with dino_ or DINO_
#
# Needs zig and python3. Runs in CI's lint job:
#
#   tests/headers.sh                     # every header, in parallel
#   tests/headers.sh dino/core/pipe.h    # one header
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
inc=$repo/sdk/include
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

targets=(x86_64-linux-musl x86-linux-musl x86_64-windows-gnu x86-windows-gnu)
# A real compile: zig's driver can't cache the missing object of -fsyntax-only.
cflags=(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wcast-qual -Wstrict-prototypes -Werror -c -o "$work/tu.o" -I "$inc")
failed=0

fail() {
    echo "  $1"
    failed=1
}

# Top-level declarations made in the header itself (not in what it includes).
declared_names() {
    python3 -I -c '
import json, sys
header = sys.argv[1]
ast = json.load(sys.stdin)
current = None
for node in ast.get("inner", []):
    loc = node.get("loc", {})
    # clang names the file only when it changes from the previous node.
    current = loc.get("file", loc.get("expansionLoc", {}).get("file", current))
    if current != header or node.get("isImplicit"):
        continue
    if node.get("name"):
        print(node["name"])
    if node.get("kind") == "EnumDecl":
        for c in node.get("inner", []):
            if c.get("kind") == "EnumConstantDecl":
                print(c["name"])
' "$1"
}

if (($# == 0)); then
    mapfile -t headers < <(cd "$inc" && find dino -name '*.h' | sort)
    printf '%s\n' "${headers[@]}" | xargs -P "$(nproc)" -n 1 "$0" || exit 1
    echo "all ${#headers[@]} headers ok: self-contained as C and C++, guarded, namespaced"
    exit 0
fi

# The macro baseline: every system header any public header includes, since a
# header also brings in the system headers of the dino headers it includes.
cat "$inc"/dino/*/*.h | grep -E '^#include <' | grep -v '<dino/' | sort -u >"$work/sys.c"
zig cc -target x86_64-linux-musl -E -dM -x c "$work/sys.c" | awk '{ print $2 }' | sed 's/(.*//' |
    sort -u >"$work/before"

for header in "$@"; do
    path=$inc/$header
    printf '#include <%s>\n' "$header" >"$work/tu.c"
    for target in "${targets[@]}"; do
        if ! zig cc -target "$target" -std=c11 "${cflags[@]}" -x c "$work/tu.c" 2>"$work/err"; then
            fail "$header: doesn't compile alone as C for $target"
            sed 's/^/      /' "$work/err" | head -n 10
        fi
        if ! zig c++ -target "$target" -std=c++11 "${cflags[@]}" -x c++ "$work/tu.c" 2>"$work/err"; then
            fail "$header: doesn't compile alone as C++ for $target"
            sed 's/^/      /' "$work/err" | head -n 10
        fi
    done

    if grep -qE '^[a-z].*\(' "$path" && ! grep -q 'extern "C"' "$path"; then
        fail "$header: declares functions without extern \"C\" guards"
    fi

    # Macros: what the header defines beyond the system headers.
    zig cc -target x86_64-linux-musl -E -dM -I "$inc" -x c "$work/tu.c" | awk '{ print $2 }' |
        sed 's/(.*//' | sort -u >"$work/after"
    while IFS= read -r macro; do
        [[ $macro == DINO_* ]] || fail "$header: defines $macro, outside the DINO_ namespace"
    done < <(comm -13 "$work/before" "$work/after")

    # Functions, types and enum constants, from clang's AST.
    while IFS= read -r name; do
        [[ $name == dino_* || $name == DINO_* ]] ||
            fail "$header: declares $name, outside the dino_ / DINO_ namespace"
    done < <(zig cc -target x86_64-linux-musl -fsyntax-only -Wno-unused-command-line-argument \
        -Xclang -ast-dump=json -I "$inc" -x c "$work/tu.c" 2>/dev/null | declared_names "$path")
done
exit $failed
