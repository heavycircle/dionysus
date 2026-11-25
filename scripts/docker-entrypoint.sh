#!/bin/sh
# Entrypoint of the dino Docker image. Runs the command as the owner of the
# working directory (normally the mounted project), so files dino writes there
# belong to the host user rather than to the image's `dino` user (UID 1000):
#
#   --user was given (--user 0 too)   run as that user
#   owner is UID and GID 1000         run as dino
#   owner is root                     run as root (rootless Docker and Docker
#                                     Desktop map the host user to root inside
#                                     the container)
#   any other owner                   give `dino` that UID and GID, run as it;
#                                     with a read-only root filesystem, where
#                                     dino can't be changed, run as that UID
#                                     and GID directly (HOME in /tmp, so
#                                     --read-only needs --tmpfs /tmp)
#
# The image starts as root in the group dino-entrypoint (the Dockerfile's
# USER); --user 0 starts in group 0, which is how it's told apart.
#
# `dino-entrypoint --own-wine-prefix` only sets up the caller's wine prefix; the
# Dockerfile uses it to give `dino` one at build time.
set -eu

# Wine refuses a prefix (default $HOME/.wine) owned by another user, and
# re-owning the shared 1.4 GB one takes minutes. A per-user prefix is only
# copies of the registry files wine rewrites plus links to the shared,
# world-writable drive_c in /opt/wine-prefix; wine only checks who owns the
# prefix directory itself.
own_wine_prefix() {
    prefix=$HOME/.wine
    if [ ! -O "$prefix" ]; then
        mkdir -p "$prefix"
        cp /opt/wine-prefix/*.reg /opt/wine-prefix/.update-timestamp "$prefix"/
        ln -sfn /opt/wine-prefix/drive_c "$prefix"/drive_c
        ln -sfn /opt/wine-prefix/dosdevices "$prefix"/dosdevices
    fi

    # Debian's wine names wineserver's socket directory in $prefix/wineserver
    # (mode 0400), creating it on first start. Wine processes started together
    # on a prefix without one race to create it, and the losers exit with
    # "error opening file descriptor for wineserver tmpdir file" (#71). Name it
    # up front; wineserver creates the directory and checks that it's private.
    if [ ! -s "$prefix/wineserver" ]; then
        tmpdir=$(mktemp -u "${TMPDIR:-/tmp}/wine-XXXXXX")
        (umask 377 && printf %s "${tmpdir##*/}" >"$prefix/wineserver")
    fi
}

# The image's zig cache in /opt, warmed in the published images, unless the
# user chose one (ZIG_GLOBAL_CACHE_DIR, or a cache mounted at zig's default
# place in the home directory) or it can't be written (--read-only), where zig
# keeps its default in $HOME.
use_image_zig_cache() {
    if [ -z "${ZIG_GLOBAL_CACHE_DIR-}" ] && [ ! -e "$HOME/.cache/zig" ] && [ -w /opt/zig-cache ]; then
        export ZIG_GLOBAL_CACHE_DIR=/opt/zig-cache
    fi
}

if [ "${1-}" = --own-wine-prefix ]; then
    own_wine_prefix
    exit 0
fi

if [ "$(id -u)" != 0 ]; then
    # --user with an arbitrary UID: the image's HOME may not be writable.
    if [ ! -w "${HOME:-/nonexistent}" ]; then
        HOME="/tmp/home-$(id -u)"
        export HOME
        mkdir -p "$HOME"
    fi
    own_wine_prefix
    use_image_zig_cache
    exec "$@"
fi

# By number: --user 0:<gid> may name a group with no name.
if [ "$(id -g)" != "$(getent group dino-entrypoint | cut -d: -f3)" ]; then
    export HOME=/root
    own_wine_prefix
    use_image_zig_cache
    exec "$@"
fi

uid=$(stat -c %u .)
gid=$(stat -c %g .)

if [ "$uid" = 0 ]; then
    export HOME=/root
    own_wine_prefix
    use_image_zig_cache
    exec setpriv --reuid=0 --regid=0 --init-groups "$@"
fi

image_uid=$(id -u dino)
image_gid=$(id -g dino)
if [ "$uid" != "$image_uid" ] || [ "$gid" != "$image_gid" ]; then
    if [ ! -w /etc/passwd ]; then
        # --read-only: no account to change. The non-root path above sets up
        # HOME, the wine prefix and the zig cache for a UID without one.
        exec setpriv --reuid="$uid" --regid="$gid" --clear-groups dino-entrypoint "$@"
    fi
    # -o: the host UID/GID may already belong to an account in the image.
    groupmod -o -g "$gid" dino
    # Not usermod -u: it re-owns everything under the home directory, mounts
    # in it (a zig cache, say) included, and fails on read-only ones.
    sed -i "s/^dino:\([^:]*\):[0-9]*:[0-9]*:/dino:\1:$uid:$gid:/" /etc/passwd
    # Re-own only what the image made: files still owned by its UID, on the
    # image's own filesystem. -xdev keeps find out of mounts, but it still
    # lists each mount point, so those are told apart by device: the root
    # filesystem's, not /home/dino's, which is a mount's own when something
    # is mounted at /home/dino itself.
    image_dev=$(stat -c %d /)
    find /home/dino -xdev -user "$image_uid" -printf '%D %p\n' | while read -r dev path; do
        if [ "$dev" = "$image_dev" ]; then chown -h "$uid:$gid" "$path"; fi
    done
fi

# Again as dino, to set up its wine prefix like any other non-root user's.
export HOME=/home/dino USER=dino LOGNAME=dino
exec setpriv --reuid=dino --regid=dino --init-groups dino-entrypoint "$@"
