# Dionysus

Write C once and get tiny, fully static executables for Linux and Windows on five architectures,
from any machine, with one command:

```sh
dino init hello && cd hello
dino build --all
```

```
build/arm/hello (5.2 KiB)
build/arm64/hello (5.5 KiB)
build/i386/hello (4.1 KiB)
build/ppc/hello (6.4 KiB)
build/win32/hello.exe (67.0 KiB)
build/win64/hello.exe (57.0 KiB)
build/x86_64/hello (4.7 KiB)
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
on all seven targets. Timing, process spawning, an epoll/IOCP event loop and `dino test` are on the
[v1.0 roadmap](https://github.com/heavycircle/dionysus/issues/25).

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
[Zig 0.17.1](https://ziglang.org/download/) and CMake 3.15+ on your `PATH`. `qemu-user` and
`wine` are only needed to *run* foreign targets.

To build `dino` from source instead:

```sh
git clone https://github.com/heavycircle/dionysus && cd dionysus
zig build -Drelease --prefix ~/.local     # installs ~/.local/bin/dino
```

Alternatively, the Docker image has everything:

```sh
docker build -t dionysus .
docker run --rm -v "$PWD:/work" dionysus dino build --all
```

## Using it

```sh
dino build                    # host target, Release, output in build/<target>/
dino build arm64 win64        # specific targets
dino build --debug            # unoptimized, with symbols
dino run arm64 -- --port 80   # build, then run under qemu-aarch64
dino targets                  # what this machine can build and run
dino help <command>
```

An existing CMake project only needs `target_link_libraries(app PRIVATE dino_core)`, or
`dino_executable(app main.c)` as a shortcut. `dino` makes the SDK available, so the project never
vendors it. See [`examples/`](examples) for one project per SDK feature.

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). It covers setting up Zig and the cross runners, the
Docker and devcontainer workflows, how the toolchain works, SDK design rules, commit and PR
conventions, and how versions and releases are cut. Issues labeled
[`complexity:low`](https://github.com/heavycircle/dionysus/labels/complexity%3Alow) are a good
place to start.
