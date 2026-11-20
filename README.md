# Dionysus

Write C once and get tiny, fully static executables for Linux and Windows on five architectures,
from any machine, with one command:

```sh
dino init hello && cd hello
dino build --all
```

```
build/arm/hello (6.0 KiB)
build/arm64/hello (6.3 KiB)
build/i386/hello (4.9 KiB)
build/ppc/hello (7.7 KiB)
build/win32/hello.exe (66.5 KiB)
build/win64/hello.exe (57.0 KiB)
build/x86_64/hello (5.8 KiB)
```

Linux builds are static ELF executables linked against musl; Windows builds are PE executables
linked against MinGW-w64. (The Windows sizes are a known regression, #26.)

Dionysus is two things:

- **The `dino_*` SDK**, a small C portability layer over the places where Linux and Windows
  disagree: sockets, `poll`/`select`, threads, mutexes, large files and process startup. Code
  written against it builds unchanged for every target.
- **`dino`**, a single executable that carries the SDK and a CMake toolchain built on `zig cc`.
  Projects are ordinary CMake projects; `dino` cross-compiles them, and runs the results through
  qemu-user or wine when the target isn't the host.

| Target | Triple | | Target | Triple |
| --- | --- | --- | --- | --- |
| `x86_64` | `x86_64-linux-musl` | | `ppc` | `powerpc-linux-musleabihf` |
| `i386` | `x86-linux-musl` | | `win64` | `x86_64-windows-gnu` |
| `arm64` | `aarch64-linux-musl` | | `win32` | `x86-windows-gnu` |
| `arm` | `arm-linux-musleabihf` | | | |

## Status

Pre-1.0 and under active development. The toolchain, the `dino` tool and the core SDK modules work
on all seven targets, including timing, processes, pipes and an epoll/IOCP event loop. What's left
for 1.0 is on the [v1.0 roadmap](https://github.com/heavycircle/dionysus/issues/25).

## Getting dino

Download the archive for your platform from the
[latest release](https://github.com/heavycircle/dionysus/releases/latest) (Linux x86_64 and
aarch64 as `.tar.xz`, Windows x86_64 as `.zip`; Windows hosts are not yet tested), and put `dino`
on your `PATH`:

```sh
tar -xJf dino-<version>-x86_64-linux.tar.xz
install -m755 dino-<version>-x86_64-linux/dino ~/.local/bin/
dino doctor                               # checks for zig, cmake, a generator, qemu and wine
```

`dino` is a single static executable with the SDK inside it. Building projects with it requires
[Zig 0.17.1](https://ziglang.org/download/) and CMake 3.20+ on your `PATH`. `qemu-user` and
`wine` are only needed to *run* foreign targets.

To build `dino` from source instead:

```sh
git clone https://github.com/heavycircle/dionysus && cd dionysus
zig build -Drelease --prefix ~/.local     # installs ~/.local/bin/dino
```

Alternatively, the Docker image has everything (dino, Zig, CMake, qemu-user and wine):

```sh
docker run --rm -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino build --all
docker run --rm -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino run arm64
```

Files dino writes into your project (`build/`, `compile_commands.json`) belong to you: the image
runs as the owner of the mounted directory, with no `-u` flag needed, including under rootless
Docker and Docker Desktop.

Tags: `:stable` (newest release), `:X.Y.Z`, and `:nightly` (latest `main`). The
[nightly prerelease](https://github.com/heavycircle/dionysus/releases/tag/nightly) has `main`'s
dino as downloads too.

## Using it

```sh
dino build                    # host target, Release, output in build/<target>/
dino build arm64 win64        # specific targets
dino build --debug            # unoptimized, with symbols
dino run arm64 -- --port 80   # build, then run under qemu-aarch64
dino test arm64 win64          # build and run tests/ (utest.h) under qemu/wine
dino targets                  # what this machine can build and run
dino help <command>
```

An existing CMake project only needs `target_link_libraries(app PRIVATE dino_core)` (plus `dino_util`
for extras such as logging), or `dino_executable(app main.c)`, which links both. `dino` makes the SDK available, so the project never
vendors it. See [`examples/`](examples) for one project per SDK feature.

### Portability warnings

Code that links the SDK is warned, in `dino build` and in your editor, when it calls a platform API
that has a portable `dino_*` replacement:

```
src/main.c:7:9: warning: 'pipe' is deprecated: pipe() is POSIX-only; use dino_pipe_create() from <dino/core/pipe.h> [-Wdeprecated-declarations]
```

It needs no particular clangd or clang-tidy version. [docs/guide/portability-warnings.md](docs/guide/portability-warnings.md)
lists what's flagged and how to opt out for a project, a file or a single call.

### Organization libraries

Your own shared libraries can be just as available as the SDK. List their directories in
`DINO_LIBRARY_PATH` once (per user, machine or Docker image) and every project links them by name,
built for each target with dino's toolchain:

```sh
export DINO_LIBRARY_PATH=/opt/acme/acme_log:/opt/acme/acme_proto
```

```cmake
target_link_libraries(app PRIVATE acme::proto)
```

[docs/guide/libraries.md](docs/guide/libraries.md) covers the layout a library needs, testing one with
`dino test`, wrapping third-party code, and Docker and CI.

## Documentation

[docs/](docs/README.md) has the user guide (installing, building and testing projects, Docker,
editors, organization libraries, troubleshooting) and a page for every SDK module. New here? Start
with [Installing](docs/guide/install.md) and [your first project](docs/guide/first-project.md).

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). It covers setting up Zig and the cross runners, the
Docker and devcontainer workflows, how the toolchain works, SDK design rules, commit and PR
conventions, and how versions and releases are cut. Issues labeled
[`complexity:low`](https://github.com/heavycircle/dionysus/labels/complexity%3Alow) are a good
place to start.
