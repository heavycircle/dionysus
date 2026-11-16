#!/usr/bin/env bash
# Checks the image's entrypoint (scripts/docker-entrypoint.sh): files dino
# writes into a mounted project belong to the project's owner, whatever its UID,
# and wine starts for that user, including several processes at once on a fresh
# container the way `dino test` starts them (#71). Runs on the host, against a
# built image:
#
#   docker build -t dionysus . && tests/docker.sh dionysus
set -uo pipefail

image=${1:?usage: tests/docker.sh IMAGE}
work=$(mktemp -d)
# The files belong to other UIDs; remove them from inside a container.
trap 'docker run --rm -v "$work:/w" --entrypoint sh "$image" -c "rm -rf /w/*"; rm -rf "$work"' EXIT

failed=0

# Expanded by the container's sh. A bare `wait` always succeeds, so wait for
# each process by PID.
# shellcheck disable=SC2016
parallel_wine='pids=; for i in 1 2 3 4 5 6; do wine cmd /c exit 0 & pids="$pids $!"; done
    for pid in $pids; do wait "$pid" || exit 1; done'

# Usage: check NAME OWNER_UID [docker run args...]
check() {
    local name=$1 owner=$2
    shift 2
    local dir=$work/$name
    mkdir -p "$dir"
    docker run --rm -v "$dir:/p" --entrypoint chown "$image" "$owner:$owner" /p

    if ! docker run --rm "$@" -v "$dir:/work" "$image" sh -c \
        "dino init app >/dev/null && dino build -C app >/dev/null && $parallel_wine" \
        >"$work/$name.log" 2>&1; then
        echo "FAILED: $name"
        sed 's/^/        /' "$work/$name.log" | tail -n 20
        failed=1
        return
    fi
    local foreign
    foreign=$(docker run --rm -v "$dir:/p" --entrypoint find "$image" /p ! -uid "$owner" -printf '%u %p\n')
    if [[ -n $foreign ]]; then
        echo "FAILED: $name: files not owned by UID $owner:"
        echo "$foreign" | head -n 5 | sed 's/^/        /'
        failed=1
        return
    fi
    echo "ok      $name: project files owned by UID $owner, parallel wine starts"
}

check uid-1000 1000
check other-uid 4321
check root-owned 0
check explicit-user 4321 --user 4321

# Nothing mounted: /work belongs to dino, so the image behaves as before.
user=$(docker run --rm "$image" id -un)
if [[ $user == dino ]]; then
    echo "ok      no mount: runs as dino"
else
    echo "FAILED: no mount: runs as $user, expected dino"
    failed=1
fi

exit $failed
