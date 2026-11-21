#!/usr/bin/env bash
# Run the SDK's test executables (sdk/tests, built for win64 or win32) on
# Windows itself. Everywhere else they only ever run under wine, which differs
# from Windows in places: it can't create symbolic links, for one. Needs
# nothing but bash (Git Bash on a Windows runner) and the executables:
#
#   dino build win64 -C sdk/tests
#   tests/windows.sh sdk/tests/build/win64
#
# Words after the directory are put in front of each executable, which is how
# to try this script elsewhere: tests/windows.sh sdk/tests/build/win64 wine
set -uo pipefail

dir=$(cd "${1:?usage: windows.sh <directory of test_*.exe> [launcher...]}" && pwd)
shift
launcher=("$@")

# The tests create their files in the current directory.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

failed=0
ran=0
for exe in "$dir"/test_*.exe; do
    [[ -e $exe ]] || continue
    name=$(basename "$exe" .exe)
    ran=$((ran + 1))

    # What set_tests_properties() gives each test in sdk/tests/CMakeLists.txt.
    vars=()
    case $name in
    test_logging_env) vars=(DINO_LOG=WaRn) ;;
    esac

    # The limit `dino test` gives a test executable.
    timeout 120 env "${vars[@]}" "${launcher[@]}" "$exe" >"$work/log" 2>&1 </dev/null
    code=$?
    # utest reports a skipped case as passed. Nothing has a reason to skip on
    # Windows, so a skip here is a case that silently stopped testing anything.
    skipped=$(sed -n 's/^\[  SKIPPED \] \([^ ]*\) (.*/\1/p' "$work/log" | sort -u | tr '\n' ' ')
    if [[ $code == 0 && -z $skipped ]]; then
        printf '%-24s ok\n' "$name"
        continue
    fi
    failed=1
    if [[ $code == 124 ]]; then
        printf '%-24s FAILED (timed out)\n' "$name"
    elif [[ $code != 0 ]]; then
        printf '%-24s FAILED (exit %d)\n' "$name" "$code"
    else
        printf '%-24s FAILED (skipped: %s)\n' "$name" "${skipped% }"
    fi
    sed 's/^/    /' "$work/log"
done

if ((ran == 0)); then
    echo "windows: no test_*.exe in $dir" >&2
    exit 1
fi
exit $failed
