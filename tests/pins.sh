#!/usr/bin/env bash
# Versions pinned in more than one place must agree:
#   - Zig: build.zig.zon's minimum_zig_version, the Dockerfile and the CI's
#     setup-zig action, with the same checksum in the last two
#   - CMake: the oldest that doctor accepts (min_cmake in
#     cli/commands/doctor.zig) is what the SDK, dino init's template and the
#     examples ask for
set -uo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo" || exit 1
failed=0

same() {
    local what=$1 expected=$2 where=$3 got=$4
    if [[ $got != "$expected" ]]; then
        echo "FAILED: $what is ${got:-missing} in $where, $expected elsewhere"
        failed=1
    fi
}

zon=$(sed -n 's/^ *\.minimum_zig_version = "\(.*\)",$/\1/p' build.zig.zon)
docker_zig=$(sed -n 's/^ARG ZIG_VERSION=//p' Dockerfile)
docker_sha=$(sed -n 's/^ARG ZIG_SHA256=//p' Dockerfile)
action=.github/actions/setup-zig/action.yml
action_zig=$(sed -n 's/^ *ZIG_VERSION: //p' "$action")
action_sha=$(sed -n 's/^ *ZIG_SHA256: //p' "$action")
same "Zig" "$zon" Dockerfile "$docker_zig"
same "Zig" "$zon" "$action" "$action_zig"
same "Zig's checksum" "$docker_sha" "$action" "$action_sha"
[[ -n $zon ]] || { echo "FAILED: no minimum_zig_version in build.zig.zon"; failed=1; }

cmake=$(sed -n 's/^const min_cmake.*major = \([0-9]*\), \.minor = \([0-9]*\),.*/\1.\2/p' cli/commands/doctor.zig)
[[ -n $cmake ]] || { echo "FAILED: no min_cmake in cli/commands/doctor.zig"; failed=1; }
for file in sdk/CMakeLists.txt cli/commands/init.zig examples/*/CMakeLists.txt; do
    got=$(sed -n 's/.*cmake_minimum_required(VERSION \([0-9.]*\)).*/\1/p' "$file" | head -n 1)
    same "the CMake minimum" "$cmake" "$file" "$got"
done

((failed)) || echo "ok      Zig $zon, CMake $cmake"
exit $failed
