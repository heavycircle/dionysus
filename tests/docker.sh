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
# The arm64 image has no wine (see the Dockerfile).
if [[ $(docker image inspect -f '{{.Architecture}}' "$image") == arm64 ]]; then
    parallel_wine=true
fi

# Usage: check NAME UID[:GID] [docker run args...] (GID defaults to UID)
check() {
    local name=$1 owner=${2%%:*} group=${2#*:}
    shift 2
    local dir=$work/$name
    mkdir -p "$dir"
    docker run --rm -v "$dir:/p" --entrypoint chown "$image" "$owner:$group" /p

    if ! docker run --rm "$@" -v "$dir:/work" "$image" sh -c \
        "dino init app >/dev/null && dino build -C app >/dev/null && $parallel_wine" \
        >"$work/$name.log" 2>&1; then
        echo "FAILED: $name"
        sed 's/^/        /' "$work/$name.log" | tail -n 20
        failed=1
        return
    fi
    local foreign
    foreign=$(docker run --rm -v "$dir:/p" --entrypoint find "$image" /p \( ! -uid "$owner" -o ! -gid "$group" \) -printf '%U:%G %p\n') || {
        echo "FAILED: $name: couldn't list the project's files"
        failed=1
        return
    }
    if [[ -n $foreign ]]; then
        echo "FAILED: $name: files not owned by $owner:$group:"
        echo "$foreign" | head -n 5 | sed 's/^/        /'
        failed=1
        return
    fi
    echo "ok      $name: project files owned by $owner:$group, parallel wine starts"
}

check uid-1000 1000
check other-uid 4321
check root-owned 0
check explicit-user 4321:4321 --user 4321:4321
# The image's UID with another group, as on hosts whose users share group 100.
check uid-1000-gid-100 1000:100

# A read-only root filesystem, where dino can't be remapped: the command runs
# as the project's owner directly, with HOME in the tmpfs. No wine: its shared
# prefix in /opt can't be written then.
mkdir -p "$work/read-only"
docker run --rm -v "$work/read-only:/p" --entrypoint chown "$image" 4321:4321 /p
if docker run --rm --read-only --tmpfs /tmp -v "$work/read-only:/work" "$image" sh -c \
    'dino init app >/dev/null && dino build -C app >/dev/null && echo "$(id -u):$(id -g)"' \
    >"$work/read-only.log" 2>&1 && [[ $(tail -n 1 "$work/read-only.log") == 4321:4321 ]]; then
    echo "ok      read-only: runs as the project's owner"
else
    echo "FAILED: read-only"
    sed 's/^/        /' "$work/read-only.log" | tail -n 20
    failed=1
fi

# A mount under /home/dino (a zig cache, say) isn't re-owned when dino is
# remapped, and a read-only one doesn't stop the run. One owned by the image's
# UID 1000 is the case a plain `find -xdev` would still touch: it visits the
# mount point itself.
# Usage: check_home_mount NAME MOUNT_POINT MOUNT_OWNER
check_home_mount() {
    local name=$1 target=$2 mount_owner=$3 dir=$work/$1
    mkdir -p "$dir/project" "$dir/mount"
    docker run --rm -v "$dir:/p" --entrypoint sh "$image" -c \
        "chown 4321:4321 /p/project && touch /p/mount/host-file && chown -R $mount_owner:$mount_owner /p/mount && chmod 1777 /p/mount"
    if ! docker run --rm -v "$dir/project:/work" -v "$dir/mount:$target" "$image" \
        sh -c 'dino init app >/dev/null && dino build -C app >/dev/null' >"$work/$name.log" 2>&1; then
        echo "FAILED: $name"
        sed 's/^/        /' "$work/$name.log" | tail -n 20
        failed=1
        return
    fi
    local owner
    owner=$(docker run --rm -v "$dir:/p" --entrypoint stat "$image" -c %u /p/mount /p/mount/host-file | sort -u)
    if [[ $owner != "$mount_owner" ]]; then
        echo "FAILED: $name: the mount under /home/dino was re-owned (now UID $owner)"
        failed=1
        return
    fi
    echo "ok      $name: a mount under /home/dino keeps its owner"
}

check_home_mount home-mount /home/dino/.cache/zig 5555
check_home_mount home-mount-ro /home/dino/reference:ro 1000
# Mounted at /home/dino itself: its files are on the mount's device, not the
# image's, so they're left alone too.
check_home_mount home-itself /home/dino 1000

# --user 0 runs as root, even though /work belongs to another UID.
mkdir -p "$work/user-root"
docker run --rm -v "$work/user-root:/p" --entrypoint chown "$image" 4321:4321 /p
for spec in 0 root 0:0; do
    ids=$(docker run --rm --user "$spec" -v "$work/user-root:/work" "$image" sh -c 'echo "$(id -u):$(id -g)"')
    if [[ $ids == 0:0 ]]; then
        echo "ok      --user $spec: runs as root"
    else
        echo "FAILED: --user $spec: runs as $ids, expected 0:0"
        failed=1
    fi
done

# Nothing mounted: /work belongs to dino, so the image behaves as before.
user=$(docker run --rm "$image" id -un)
if [[ $user == dino ]]; then
    echo "ok      no mount: runs as dino"
else
    echo "FAILED: no mount: runs as $user, expected dino"
    failed=1
fi

exit $failed
