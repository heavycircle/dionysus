#!/usr/bin/env bash
# Check every public SDK header (sdk/include/dino/**.h):
#   - it compiles on its own, as C11 and as C++11, warning-free with
#     -Wpedantic and -Wconversion (projects' warning flags apply to the
#     headers, unlike the SDK's sources), for 64- and 32-bit Linux and Windows
#   - it has extern "C" guards, so C++ code links against the C functions
#   - everything it adds is in the SDK's namespace: macros start with DINO_,
#     and functions, types and enum constants with dino_ or DINO_
# and that handles of different types can't be mixed up: passing one where
# another is expected doesn't compile; and that an argv of string literals
# builds without a warning in C and C++.
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

# Each handle type is its own C type. Every call below passes the wrong kind of
# handle and must fail to compile; the same calls with the right handles must
# compile, so a failure isn't something else.
distinct_handles() {
    local wrong right
    local -a calls=(
        'dino_socket_close(pipe)|dino_socket_close(sock)'
        'dino_pipe_close(mutex)|dino_pipe_close(pipe)'
        'dino_mutex_lock(cond)|dino_mutex_lock(mutex)'
        'dino_thread_join(process, NULL)|dino_thread_join(thread, NULL)'
        'dino_process_kill(thread)|dino_process_kill(process)'
        'dino_event_add_socket(sock, loop, 0, NULL)|dino_event_add_socket(loop, sock, 0, NULL)'
        'dino_event_del_timer(loop, set)|dino_event_del_timer(loop, timer)'
        'dino_fd_set_add(timer, sock)|dino_fd_set_add(set, sock)'
    )
    local ok=1 lang compiler
    for call in "${calls[@]}"; do
        wrong=${call%%|*} right=${call#*|}
        for lang in c c++; do
            compiler=cc
            [[ $lang == c++ ]] && compiler=c++
            for which in "$wrong" "$right"; do
                cat >"$work/mix.c" <<C
#include <stddef.h>

#include <dino/core/event.h>
#include <dino/core/io.h>
#include <dino/core/mutex.h>
#include <dino/core/network.h>
#include <dino/core/pipe.h>
#include <dino/core/process.h>
#include <dino/core/thread.h>

void mix(dino_socket_t sock, dino_pipe_t pipe, dino_mutex_t mutex, dino_cond_t cond,
    dino_thread_t thread, dino_process_t process, dino_event_loop_t loop,
    dino_event_timer_t timer, dino_fd_set_t set);
void mix(dino_socket_t sock, dino_pipe_t pipe, dino_mutex_t mutex, dino_cond_t cond,
    dino_thread_t thread, dino_process_t process, dino_event_loop_t loop,
    dino_event_timer_t timer, dino_fd_set_t set)
{
    (void)sock, (void)pipe, (void)mutex, (void)cond, (void)thread, (void)process;
    (void)loop, (void)timer, (void)set;
    (void)$which;
}
C
                if zig $compiler -target x86_64-linux-musl -Wall -Wextra -Werror -c -o "$work/mix.o" \
                    -I "$inc" -x "$lang" "$work/mix.c" 2>"$work/err"; then
                    [[ $which == "$wrong" ]] && { echo "  $lang: '$wrong' compiles; the handle types can be mixed up"; ok=0; }
                else
                    [[ $which == "$right" ]] && { echo "  $lang: '$right' doesn't compile:"; sed 's/^/      /' "$work/err" | head -n 5; ok=0; }
                fi
            done
        done
    done
    ((ok))
}

# The documented way to run a program, a list of string literals, must build
# without a warning in C++ and in C under -Wwrite-strings.
literal_argv() {
    cat >"$work/argv.c" <<'C'
#include <stddef.h>
#include <string.h>

#include <dino/core/process.h>

int run_it(void);
int run_it(void)
{
    const char *const       argv[] = { "/bin/sh", "-c", "echo hi", NULL };
    const char *const       env[]  = { "LANG=C", NULL };
    dino_process_run_opts_t opts;
    dino_process_result_t   result;
    memset(&opts, 0, sizeof(opts));
    opts.env = env;
    int rc   = dino_process_run(argv, &opts, &result);
    dino_process_result_free(&result);
    return rc;
}
C
    local ok=1
    zig cc -target x86_64-linux-musl -std=c11 -Wall -Wextra -Wwrite-strings -Werror -c \
        -o "$work/argv.o" -I "$inc" -x c "$work/argv.c" 2>"$work/err" ||
        { echo "  literal argv warns in C:"; sed 's/^/      /' "$work/err" | head -n 5; ok=0; }
    zig c++ -target x86_64-linux-musl -std=c++11 -Wall -Wextra -Werror -c \
        -o "$work/argv.o" -I "$inc" -x c++ "$work/argv.c" 2>"$work/err" ||
        { echo "  literal argv warns in C++:"; sed 's/^/      /' "$work/err" | head -n 5; ok=0; }
    ((ok))
}

if (($# == 0)); then
    mapfile -t headers < <(cd "$inc" && find dino -name '*.h' | sort)
    printf '%s\n' "${headers[@]}" | xargs -P "$(nproc)" -n 1 "$0" || exit 1
    distinct_handles || exit 1
    literal_argv || exit 1
    echo "all ${#headers[@]} headers ok: self-contained as C and C++, guarded, namespaced; handle types distinct; literal argv builds"
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
