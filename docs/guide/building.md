# Building

```sh
dino build                    # this machine's target, release
dino build arm64 win64        # some targets
dino build --all              # every target
dino build --debug            # unoptimized, with symbols
dino run arm64 -- --port 80   # build, then run under qemu with arguments
dino clean                    # remove build/
```

`build`, `run`, `test` and `clean` work on the project in the current directory, or in the
directory given with `-C <dir>`. `dino init` takes the directory as an argument, `dino init myapp`,
and with `-C <dir>` it works from `<dir>`: `dino init -C ~/src myapp` creates `~/src/myapp`.

## Targets

```sh
dino targets
```

```
  NAME     ZIG TARGET                 RUNS VIA       DESCRIPTION
  arm      arm-linux-musleabihf       qemu-arm       ARM 32-bit, hard float
  arm64    aarch64-linux-musl         qemu-aarch64   ARM 64-bit
  i386     x86-linux-musl             native         Intel x86 32-bit
  ppc      powerpc-linux-musleabihf   qemu-ppc       PowerPC 32-bit, big endian
  win32    x86-windows-gnu            wine           Windows PE32 (x86)
  win64    x86_64-windows-gnu         wine           Windows PE32+ (x86_64)
* x86_64   x86_64-linux-musl          native         AMD64

* default target on this machine
```

- **With no target**, commands use the one marked `*`, which matches the host.
- **`--all`** builds every named target. It can't be combined with explicit targets.
- **Other Zig targets:** any Zig target triple works in place of a name, e.g.
  `dino build riscv64-linux-musl`. Only the seven named targets are tested, and the SDK is only
  written for Linux and Windows.
- **Failures:** a target that fails to build doesn't stop the others. The summary at the end lists
  each target, and the command exits non-zero if any of them failed.

## Output

Each target builds in `build/<target>/`, and `dino build` lists what it produced, with sizes:

```
==> arm64 (aarch64-linux-musl, release)
    build/arm64/hello (6.3 KiB)
    build/arm64/test_main (40.2 KiB)
```

The Linux binaries are static ELF files linked against musl. They run on any Linux kernel for their
architecture, with nothing installed. The Windows binaries are PE files linked against MinGW-w64
that need only the DLLs every Windows installation has.

Release builds are optimized for size (`-Oz`, unused sections dropped, symbols stripped), so a hello
world is around 5 to 8 KiB on Linux and about 60 KiB on Windows.

## Debug and release

Release is the default. `--debug` builds with `-O0 -g` instead, for a debugger:

```sh
dino build --debug
dino run --debug
```

A target has a single build directory shared by both modes, so switching between `--debug` and
release reconfigures and rebuilds that target. CMake's other build types (`RelWithDebInfo`,
`MinSizeRel`) can be selected with `-- -DCMAKE_BUILD_TYPE=...`.

## Running

`dino run` builds one target, then runs the project's executable:

```sh
dino run                     # this machine
dino run arm64               # under qemu-aarch64
dino run win64               # under wine
dino run -- --verbose input.txt   # arguments for the program go after --
```

- **Runners:** foreign Linux binaries run under qemu-user (`qemu-aarch64-static` is preferred if
  installed) and Windows binaries under wine. `dino doctor` shows which runners it found. An x86_64
  host runs i386 binaries directly; an aarch64 host runs arm binaries directly only if its CPU can
  run 32-bit ARM code (many newer ones, and Apple silicon, can't), and uses `qemu-arm` otherwise.
- **Exit code:** `dino run` exits with the program's exit code.
- **Signals:** dino waits for the program to exit. Ctrl-C and Ctrl-\\ at the terminal reach the
  program directly, once; `SIGTERM`, `SIGHUP` and a `SIGINT` sent to dino with `kill` are passed
  on to it. On Windows, Ctrl-C and Ctrl-Break reach the program and dino ignores them. The same
  holds while `dino build` and `dino test` run CMake and CTest.
- **wine output:** wine's own `fixme:` chatter is silenced unless you set `WINEDEBUG` yourself.
- **Several executables:** pick one with `--exe <name>`. Test executables (from `dino_test()`) are
  never picked automatically, only by name.
- **Skipping the build:** `--no-build` runs what's already in `build/<target>/`, whichever mode
  was built last. With `--debug` or `--release` as well, it fails if that isn't the mode there.

The binaries in `build/` are ordinary files, so you can also copy them to the machine they're for
and run them there.

## Options

| Option                 | Commands                | Effect                                                          |
| ---------------------- | ----------------------- | --------------------------------------------------------------- |
| `-C <dir>`             | build, run, test, clean | use `<dir>` as the project directory                            |
| `-C <dir>`             | init                    | work from `<dir>`; the directory argument is relative to it     |
| `--debug`, `--release` | build, run, test        | choose the build mode (release is the default)                  |
| `-j <n>`, `--jobs <n>` | build, run, test        | parallel jobs (default: number of CPUs)                         |
| `-v`, `--verbose`      | build, run, test        | show the CMake commands and the full compiler output            |
| `--all`                | build, test             | every named target                                              |
| `-- <args>`            | build                   | passed to CMake's configure step, e.g. `-- -DENABLE_LOGGING=ON` |
| `-- <args>`            | run                     | passed to the program                                           |
| `-- <args>`            | test                    | passed to `ctest`                                               |

## Exit codes

| Code | Meaning                                                                                    |
| ---- | ------------------------------------------------------------------------------------------ |
| 0    | success                                                                                    |
| 1    | something failed: configuring, compiling, a test, a missing tool                           |
| 2    | a mistake on the command line: unknown command, option or target, a missing or empty value |

Once the program has started, `dino run` exits with the program's own code instead, so there a 1
or 2 means whatever the program means by it.

## When things are rebuilt

- **Incremental:** `dino build` runs CMake's own incremental build, so only changed files are
  recompiled.
- **Reconfiguring:** a target is reconfigured when its settings change: the build mode, the SDK
  version, `DINO_LIBRARY_PATH`, or arguments after `--`. Settings given after `--` stay in the
  CMake cache for later builds.
- **Starting over:** when the SDK changes (a new dino, or another `DINO_SDK_PATH`), or the last
  configure failed, the target's CMake cache is deleted and it is configured from scratch, since
  CMake reads the toolchain only once. Settings from earlier `--` arguments are dropped with it.
- **`CFLAGS`:** CMake reads it from the environment only the first time a target is configured.
  After changing it, run `dino clean` first.

```sh
dino clean            # remove every build/<target>/ dino created
dino clean arm win32  # just these
```

`dino clean` leaves alone any directory under `build/` that it didn't create.

## Without dino

`dino build` is a thin layer over CMake, and a project needs nothing from it at build time beyond
these settings. To build without it, for example in an existing CMake setup:

```sh
SDK=/path/to/dionysus/sdk
cmake -S . -B build/arm64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$SDK/cmake/toolchain.cmake \
    -DCMAKE_PROJECT_INCLUDE=$SDK/cmake/Dino.cmake \
    -DDINO_SDK_PATH=$SDK \
    -DZIG_TARGET=aarch64-linux-musl \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/arm64
```

`dino build -v` prints the exact commands it runs.
