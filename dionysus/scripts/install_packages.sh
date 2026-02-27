#!/usr/bin/env bash

install="sudo apt-get install -y --no-install-recommends"

install-linux() {
    sudo dpkg --add-architecture i386
    sudo apt-get update -y

    $install gcc-multilib libc6-dev-i386
}
