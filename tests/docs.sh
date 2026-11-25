#!/usr/bin/env bash
# Keep the documentation honest:
#   - every relative link in the Markdown (docs/, the READMEs, CONTRIBUTING.md)
#     points at a file that exists, and every #anchor at a heading in it
#   - every ```c block in docs/ that contains `int main(` or `UTEST_MAIN()` is a
#     complete program (the latter a utest.h test, built with dino_test): it's
#     built for each target and run (natively, under qemu-user or under wine),
#     and must exit 0
# Fragments without either aren't built. Intended for the Docker image, which
# has every runner:
#
#   docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/docs.sh arm64 win64
#
# With no targets, every target is tested. CFLAGS from the environment reaches
# every compile (CI sets -Wall -Wextra -Werror).
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
targets=("$@")
((${#targets[@]})) || targets=(arm arm64 i386 ppc win32 win64 x86_64)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export DINO_SDK_PATH=$repo/sdk DINO_HOME=${DINO_HOME:-$work/home}
failed=0

# GitHub's heading anchors: lowercase, punctuation other than - and _ dropped,
# spaces turned into -.
anchors() {
    awk '/^```/ { fence = !fence } !fence && /^#+ / {
        sub(/^#+ +/, ""); s = tolower($0)
        gsub(/[^a-z0-9 _-]/, "", s); gsub(/ /, "-", s); print s
    }' "$1"
}

check_links() {
    local file=$1 dir link path anchor
    dir=$(dirname "$file")
    # Inline links outside code blocks and code spans; images too.
    while IFS= read -r link; do
        case $link in
        http://* | https://* | mailto:*) continue ;;
        esac
        path=${link%%#*}
        anchor=
        [[ $link == *"#"* ]] && anchor=${link#*#}
        if [[ -z $path ]]; then
            path=$file
        else
            path=$dir/$path
        fi
        if [[ ! -e $path ]]; then
            echo "  ${file#"$repo"/}: broken link ($link)"
            failed=1
        elif [[ -n $anchor && $path == *.md ]] && ! anchors "$path" | grep -qxF "$anchor"; then
            echo "  ${file#"$repo"/}: no heading for #$anchor ($link)"
            failed=1
        fi
    done < <(
        text=$(awk '/^```/ { fence = !fence; next } !fence { gsub(/`[^`]*`/, ""); print }' "$file")
        grep -oE '\]\([^) ]+\)' <<<"$text" | sed -E 's/^\]\(//; s/\)$//'
        # HTML links and images too, as README.md's header uses.
        grep -oE '(href|src|srcset)="[^"]+"' <<<"$text" | sed -E 's/^[a-z]+="//; s/"$//'
    )
}

echo "links:"
mapfile -t pages < <(find "$repo/docs" -name '*.md' | sort)
pages+=("$repo/README.md" "$repo/CONTRIBUTING.md" "$repo/examples/README.md" "$repo/sdk/README.md")
before=$failed
for page in "${pages[@]}"; do
    check_links "$page"
done
((failed == before)) && echo "  all ${#pages[@]} pages ok"

# One project, one executable per program: <page path>_<block number>.
proj=$work/programs
mkdir -p "$proj"
programs=()
while IFS= read -r page; do
    rel=${page#"$repo"/docs/}
    base=$(tr '/.-' '___' <<<"${rel%.md}")
    awk -v out="$proj/$base" '
        /^```c[[:space:]]*$/ { inblock = 1; text = ""; next }
        inblock && /^```/ {
            inblock = 0
            if (text ~ /int main\(|UTEST_MAIN\(\)/) { n++; f = out "_" n ".c"; printf "%s", text > f; close(f) }
            next
        }
        inblock { text = text $0 "\n" }
    ' "$page"
done < <(find "$repo/docs" -name '*.md' | sort)
for src in "$proj"/*.c; do
    [[ -e $src ]] || break
    programs+=("$(basename "$src" .c)")
done

# None at all means the pages or their fences moved, not that there's nothing to test.
if ((${#programs[@]} == 0)); then
    echo "programs: none found in docs/"
    exit 1
fi
{
    echo 'cmake_minimum_required(VERSION 3.15)'
    echo 'project(DocPrograms C)'
    for p in "${programs[@]}"; do
        if grep -q 'UTEST_MAIN()' "$proj/$p.c"; then
            echo "dino_test($p $p.c)"
        else
            echo "dino_executable($p $p.c)"
        fi
    done
} >"$proj/CMakeLists.txt"

echo "programs (${#programs[@]}):"
for target in "${targets[@]}"; do
    # dino build leaves dino_test() programs out; dino_tests builds them.
    if ! { dino build "$target" -C "$proj" && cmake --build "$proj/build/$target" --target dino_tests; } \
        >"$work/build.log" 2>&1; then
        echo "  $target: build failed"
        grep -E "error|warning:" "$work/build.log" | sed "s#$proj/##; s/^/    /" | head -n 30
        failed=1
        continue
    fi
    for p in "${programs[@]}"; do
        if timeout 120 dino run "$target" --no-build --exe "$p" -C "$proj" >"$work/run.log" 2>&1 </dev/null; then
            printf '  %-8s %-34s ok\n' "$target" "$p"
        else
            printf '  %-8s %-34s FAILED\n' "$target" "$p"
            sed 's/^/      /' "$work/run.log" | tail -n 15
            failed=1
        fi
    done
done
exit $failed
