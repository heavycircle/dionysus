#!/bin/sh
# Entrypoint of the dino Docker image. Runs the command as the owner of the
# working directory (normally the mounted project), so files dino writes there
# belong to the host user rather than to the image's `dino` user (UID 1000):
#
#   owner is UID 1000, or --user was given   run as is
#   owner is root                            run as root (rootless Docker and
#                                            Docker Desktop map the host user to
#                                            root inside the container)
#   any other owner                          give `dino` that UID and GID, run as it
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
    exec "$@"
fi

uid=$(stat -c %u .)
gid=$(stat -c %g .)

if [ "$uid" = 0 ]; then
    export HOME=/root
    own_wine_prefix
    exec "$@"
fi

if [ "$uid" != "$(id -u dino)" ]; then
    # -o: the host UID/GID may already belong to an account in the image.
    # /home/dino is a few KB: its wine prefix links into /opt, and chown -R
    # doesn't follow links.
    groupmod -o -g "$gid" dino
    usermod -o -u "$uid" -g "$gid" dino
    chown -R dino:dino /home/dino
fi

export HOME=/home/dino USER=dino LOGNAME=dino
exec setpriv --reuid=dino --regid=dino --init-groups "$@"
