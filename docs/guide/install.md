# Installing dino

There are two ways to get a working setup. Pick one:

- [**A release binary**](#a-release-binary) plus Zig and CMake on your machine. Builds run natively,
  and the editor integration works out of the box.
- [**The Docker image**](#the-docker-image), which has everything, including the runners for foreign
  targets. Nothing to install but Docker.

Either way, finish with [`dino doctor`](#checking-the-setup-with-dino-doctor).

## A release binary

`dino` is a single static executable with the SDK inside it. Download the archive for your platform
from the [latest release](https://github.com/heavycircle/dionysus/releases/latest) and put `dino` on
your `PATH`:

```sh
sha256sum --check --ignore-missing SHA256SUMS   # SHA256SUMS is attached to the same release
tar -xJf dino-<version>-x86_64-linux.tar.xz
install -m755 dino-<version>-x86_64-linux/dino ~/.local/bin/
```

To check that an archive was built by this repository's release workflow from the tagged commit, not only that it downloaded whole, verify its provenance attestation with the [GitHub CLI](https://cli.github.com/): `gh attestation verify dino-<version>-x86_64-linux.tar.xz -R heavycircle/dionysus`. Releases from 1.0 on have one (see [SECURITY.md](../../SECURITY.md)).

Archives are published for Linux x86_64 and aarch64 (`.tar.xz`) and for Windows x86_64 (`.zip`).
Windows hosts aren't tested yet; Linux is the supported host.

`dino` drives two tools that must be on your `PATH`:

| Tool                                 | Version       | Why                                                                                     |
| ------------------------------------ | ------------- | --------------------------------------------------------------------------------------- |
| [Zig](https://ziglang.org/download/) | 0.17.1        | its bundled clang, musl and MinGW-w64 are the compiler and C libraries for every target |
| [CMake](https://cmake.org/download/) | 3.20 or newer | projects are ordinary CMake projects                                                    |
| [Ninja](https://ninja-build.org/)    | any           | optional on Linux (`make` works too, more slowly); required on Windows                  |

Zig ships as an archive: unpack it anywhere and add the directory to `PATH`. Most distributions
package CMake and Ninja (`apt install cmake ninja-build`).

To run binaries for other targets, `dino run` and `dino test` also need:

| Runner                                 | For                                     | Package (Debian/Ubuntu)                                                     |
| -------------------------------------- | --------------------------------------- | --------------------------------------------------------------------------- |
| `qemu-arm`, `qemu-aarch64`, `qemu-ppc` | the `arm`, `arm64` and `ppc` targets    | `qemu-user` (or `qemu-user-static`)                                         |
| `wine`                                 | `win64`; `win32` also needs 32-bit wine | `wine`, plus `wine32:i386` for win32 (after `dpkg --add-architecture i386`) |
| `valgrind`                             | `dino test --memcheck`                  | `valgrind`                                                                  |

None of these are needed just to _build_ for those targets.

### From source

```sh
git clone https://github.com/heavycircle/dionysus && cd dionysus
zig build -Drelease --prefix ~/.local     # installs ~/.local/bin/dino
```

Nightly builds of `main` are on the [nightly release](https://github.com/heavycircle/dionysus/releases/tag/nightly).

## The Docker image

The image has `dino`, Zig, CMake, Ninja, qemu-user, wine (32 and 64-bit) and valgrind. Mount your
project at `/work` and put the `dino` command after the image name:

```sh
docker run --rm -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino build --all
docker run --rm -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino run arm64
```

Files it writes into your project belong to you, with no `-u` flag needed. [Docker](docker.md) covers
tags, file ownership, CI and derived images.

## Checking the setup with `dino doctor`

```sh
dino doctor
```

```
Required:
  [ ok ] zig        0.17.1 (/opt/zig/zig)
  [ ok ] cmake      cmake version 3.31.6 (/usr/bin/cmake)
  [ ok ] generator  ninja (/usr/bin/ninja)
  [ ok ] sdk        /home/you/.cache/dino/sdk/0.6.0-...

Libraries (DINO_LIBRARY_PATH):
  none

Runners for `dino run` and `dino test` (optional):
  [ ok ] arm        /usr/bin/qemu-arm
  [ ok ] arm64      /usr/bin/qemu-aarch64
  [ ok ] i386       native
  [ ok ] ppc        /usr/bin/qemu-ppc
  [ ok ] win32      /usr/bin/wine (needs its 32-bit support, which isn't checked)
  [ ok ] win64      /usr/bin/wine
  [ ok ] x86_64     native

For `dino test --memcheck` (optional):
  [ ok ] valgrind   /usr/bin/valgrind

All required tools found.
```

- **Required** must all be `ok`, or `dino doctor` exits non-zero. A missing generator means
  neither `ninja` nor `make` is on `PATH`, and CMake older than 3.20 is reported as a problem.
  So is a Zig that isn't 0.17 (any 0.17.x passes; dino is tested with 0.17.1), and a `zig` or `cmake` that is on `PATH` but fails to run.
- **sdk** is where `dino` unpacked its built-in SDK, once per SDK content (`sdk/<version>-<hash>`), under `$DINO_HOME` (by
  default `~/.cache/dino`). Its files are read-only (except on Windows), and doctor checks that they still match the SDK inside `dino`. Set `DINO_SDK_PATH` to build against another SDK checkout instead. What `dino` keeps under `$DINO_HOME` and how it's laid out is internal and may change between versions; don't script against it.
- **Libraries** lists your [organization libraries](libraries.md), and any broken entry counts as a
  problem.
- **Runners** are optional. A `MISS` only means `dino run` and `dino test` can't run that target
  here; `dino build` still builds it. `native` means the host runs it directly: an x86_64 Linux
  machine runs `i386` binaries without qemu. For `win32`, doctor only finds `wine`; whether that wine can run 32-bit programs (a `wine32` package, or a WoW64 build) is not something it can tell.
- **valgrind** is only needed for `dino test --memcheck`, which runs on x86_64 and aarch64 Linux hosts.

## Environment variables

| Variable            | Effect                                                                                                                                        |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| `DINO_HOME`         | Where `dino` keeps its unpacked SDK. Default: `$XDG_CACHE_HOME/dino` or `~/.cache/dino` (`%LOCALAPPDATA%\dino` on Windows).                   |
| `DINO_SDK_PATH`     | Build against this SDK directory instead of the built-in one. Used when working on the SDK itself.                                            |
| `DINO_SDK_PREBUILT` | `1`: build the SDK once per zig version, target and build mode and share it between projects ([Building](building.md#building-the-sdk-once)). |
| `DINO_LIBRARY_PATH` | [Organization libraries](libraries.md) every project can link.                                                                                |
| `CFLAGS`            | Read by CMake on a project's first configure and added to every compile, e.g. `CFLAGS="-Wall -Wextra"`.                                       |

Next: [your first project](first-project.md).
