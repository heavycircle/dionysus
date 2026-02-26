#!/usr/bin/env bash

BUILD_DIR="build"

main() {
    rm -rf "$BUILD_DIR"

    cmake -S . -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR"
}

main "$@"
