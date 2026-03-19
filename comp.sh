#!/usr/bin/env bash

debug() { echo -e "\n\e[1;32m$" "$*" "\e[0m"; }

main() {
    local arch="$1"
    local type="$2"

    local target runtime ext=""
    case "$arch" in
    i386)
        target="x86-linux-musl"
        runtime=""
        ;;
    x86_64)
        target="x86_64-linux-musl"
        runtime=""
        ;;
    arm)
        target="arm-linux-musleabihf"
        runtime="qemu-arm-static"
        ;;
    ppc)
        target="powerpc-linux-musleabihf"
        runtime="qemu-ppc-static"
        ;;
    win32)
        target="x86-windows-gnu"
        runtime="wine"
        ext=".exe"
        ;;
    esac

    debug "make clean"
    make clean

    debug "make target"
    make BUILD="$type" ZIG_TARGET="$target"

    debug "stat main"
    file "./main$ext"
    ls -lh "./main$ext"

    debug "./main$ext"
    $runtime "./main$ext"
}

main "$@"
