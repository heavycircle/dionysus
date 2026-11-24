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
  arm      arm-linux-musleabihf       qemu-arm       ARMv7-A 32-bit, hard float
  arm64    aarch64-linux-musl         qemu-aarch64   ARM 64-bit
  i386     x86-linux-musl             native         Intel x86 32-bit, i686
  ppc      powerpc-linux-musleabihf   qemu-ppc       PowerPC 32-bit, big endian
  win32    x86-windows-gnu            wine           Windows PE32 (x86)
  win64    x86_64-windows-gnu         wine           Windows PE32+ (x86_64)
* x86_64   x86_64-linux-musl          native         AMD64

* default target on this machine
```

- **With no target**, commands use the one marked `*`, which matches the host.
- **Names** are matched in any case, and `aarch64`, `amd64` and `i686` work for `arm64`, `x86_64` and `i386`.
- **`--all`** builds every named target. It can't be combined with explicit targets. `dino test --all` skips the tests of a target this host can never run (Linux targets on a Windows host, `arm` on an ARM64 machine without 32-bit support) and says so in the summary; a runner that is only missing (qemu, wine) fails up front, since installing it fixes that.
- **CPUs:** each target builds for zig's baseline CPU of its architecture, except `i386`, which builds for the i686 (Pentium Pro and later, no SSE2 needed) as Debian's and Ubuntu's i386 ports do. Older or newer CPUs: `CFLAGS=-mcpu=i486`, `-mcpu=native` and so on, which also reach the link.
- **Other Zig targets:** any Zig target triple works in place of a name, e.g.
  `dino build riscv64-linux-musl`. musl and MinGW triples (`*-linux-musl*`, `*-windows-gnu`) link statically, as the named targets do; any other C library (`x86_64-linux-gnu`, macOS, the BSDs) can't be linked statically, so those link dynamically. Only the seven named targets are tested, and the SDK is only written for Linux and Windows.
- **Failures:** a target that fails to build doesn't stop the others. The summary at the end lists
  each target, and the command exits non-zero if any of them failed.

## Output

Each target builds in `build/<target>/`, and `dino build` lists what it produced, with sizes:

```
==> arm64 (aarch64-linux-musl, release)
    build/arm64/hello (6.3 KiB)
```

Test executables (from `dino_test()`) aren't part of it: `dino build` and `dino run` build only the project's own programs, and [`dino test`](testing.md) builds the tests. Their sources are still in `compile_commands.json`, so editors know them.

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
`MinSizeRel`) can be selected with `-- -DCMAKE_BUILD_TYPE=...`. Such a build type stays for later `dino build`, `dino run` and `dino test` commands on that target until `--debug` or `--release` is given:

```sh
dino build -- -DCMAKE_BUILD_TYPE=RelWithDebInfo
dino test                    # tests the RelWithDebInfo build
dino build --release         # back to Release
```

### What release optimizes for

Release compiles your code and the SDK for size (`-Oz`). The C library's memory functions and allocator are built for whatever the link asks for, and that differs by platform:

- **Linux targets link for speed** (`-O2`): `memcpy` and `memset` run at glibc's speed, for under a kilobyte more per program. Linked for size they were about 20 times slower (a 4 KiB `memcpy`: 1077 ns against 50 ns).
- **Windows targets link for size** (`-Oz`): linking for speed there adds about 14 KB of MinGW runtime to every program, a quarter of a hello world.

Choose for a project with `DINO_LINK_FOR`, `speed` or `size`, set before `project()` or passed as `dino build -- -DDINO_LINK_FOR=size`; a change takes effect on the next build.

Some of the C library is slow at any setting: `strlen`, `strcmp`, `memchr` and `strstr` in the static musl that dino links work a byte at a time, several times slower than glibc's on long strings. Code that spends its time there may want its own routines. System calls, file and network I/O, and starting a program (several times faster than a dynamically linked one) are not slower.

## Running

`dino run` builds one target, then runs the project's executable. Only the program writes to stdout: the build's output goes to stderr, so `dino run | jq .` or `dino run > out.json` gets the program's output alone.

```sh
dino run                     # this machine
dino run arm64               # under qemu-aarch64
dino run win64               # under wine
dino run -- --verbose input.txt   # arguments for the program go after --
```

- **Runners:** foreign Linux binaries run under qemu-user (`qemu-aarch64-static` is preferred if
  installed) and Windows binaries under wine. `dino doctor` shows which runners it found. An x86_64
  host runs i386 binaries directly; an aarch64 host runs arm binaries directly only if its CPU can
  run 32-bit ARM code (many newer ones, and Apple silicon, can't), and uses `qemu-arm` otherwise. wine runs only Windows binaries for the host's own architecture (x86 and x86_64 on an x86_64 host), and x32 binaries (`*-linux-gnux32`, `*-linux-muslx32`) can't be run at all. A target that can't be run, or whose runner isn't installed, fails before anything is built.
- **Exit code:** `dino run` exits with the program's exit code. On Windows that is the full 32-bit code, so a crash (`0xC0000005`) is passed on as it is.
- **Signals:** dino waits for the program to exit. Ctrl-C and Ctrl-\\ at the terminal, and the hangup of a closed terminal, reach the program directly, once. A signal sent to dino alone (`kill`, a supervisor's `SIGTERM`) is passed on to the program, and once the program has exited, to anything it left running, so an interrupted `dino build` doesn't leave make and the compilers working in the background. dino doesn't wait for those to finish. A signal that was ignored when dino started (`nohup dino build`, a background job in a script) stays ignored, by dino and by what it runs. On Windows, Ctrl-C and Ctrl-Break reach the program and dino ignores them. The same holds while `dino build` and `dino test` run CMake and CTest, and once interrupted they stop instead of going on to the next target.
- **wine output:** wine's own `fixme:` chatter is silenced unless you set `WINEDEBUG` yourself.
- **Several executables:** pick one with `--exe <name>`. Test executables (from `dino_test()`) are
  never picked automatically, only by name, and `dino run --exe` builds the tests too. Executables the build leaves out (`EXCLUDE_FROM_ALL`, as vendored dependencies often add them) are neither listed nor picked.
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
| `-j <n>`, `--jobs <n>` | build, run, test        | parallel jobs, 1 to 4096 (default: number of CPUs)              |
| `-v`, `--verbose`      | build, run, test        | show the CMake commands and the full compiler output            |
| `--all`                | build, test             | every named target                                              |
| `-- <args>`            | build                   | passed to CMake's configure step, e.g. `-- -DENABLE_LOGGING=ON` |
| `-- <args>`            | run                     | passed to the program                                           |
| `-- <args>`            | test                    | passed to `ctest`                                               |

## Exit codes

| Code    | Meaning                                                                                    |
| ------- | ------------------------------------------------------------------------------------------ |
| 0       | success                                                                                    |
| 1       | something failed: configuring, compiling, a test, a missing tool                           |
| 2       | a mistake on the command line: unknown command, option or target, a missing or empty value |
| 128 + n | interrupted by signal n while CMake or CTest ran: 130 for Ctrl-C, 143 for `SIGTERM`        |

Once the program has started, `dino run` exits with the program's own code instead, so there a 1
or 2 means whatever the program means by it.

## When things are rebuilt

- **Incremental:** `dino build` runs CMake's own incremental build, so only changed files are
  recompiled.
- **Reconfiguring:** a target is reconfigured when its settings change: the build mode, the SDK
  version, `DINO_LIBRARY_PATH`, the qemu or wine found on `PATH`, or arguments after `--` that differ from the last ones given. Settings given after `--` stay in the CMake cache, so a later command without them (`dino run`, `dino test`, a plain `dino build`) keeps them and doesn't reconfigure.
- **Starting over:** when the SDK changes (a new dino, another `DINO_SDK_PATH`, or an edit to its `cmake/toolchain.cmake` or `cmake/flags.cmake`), zig changes, the project was moved or copied to another directory, or the last configure failed, the target's CMake cache is deleted and it is configured from scratch, since CMake reads the toolchain only once and refuses a cache made in another directory. Settings from earlier `--` arguments are dropped with it, except a build type.
- **`CFLAGS`:** CMake reads it from the environment only the first time a target is configured.
  After changing it, run `dino clean` first.

```sh
dino clean            # remove every build/<target>/ dino created
dino clean arm win32  # just these
```

`dino clean` leaves alone any directory under `build/` that dino hasn't built in.

## The first build

The first build of each target, in each build mode, takes noticeably longer than later ones: zig builds the C library (musl or MinGW) and its runtime for that target and caches them in its own cache directory (`~/.cache/zig`), which every later project reuses. On a typical machine that is several seconds for a Linux target and longer for Windows; `dino` says so when it starts configuring a target for the first time.

The C library is built again for each new combination of flags that reach the link, since zig caches it per set of flags: switching to `--debug` once, or adding a flag such as `-flto` or `-mcpu=...`, each costs one more of these builds. In Docker, the published images come with Release builds' libraries already in their cache, and a mounted cache keeps the rest between containers (see [Docker](docker.md#the-zig-cache)).

## Building the SDK once

Each project compiles the SDK into its own build directory, once per target and build mode, and again after `dino clean`: about half a second for a Linux target and a few seconds for Windows. Set `DINO_SDK_PREBUILT=1` to build it once instead, outside any project, and have every project link that copy:

```sh
export DINO_SDK_PREBUILT=1
dino build --all
```

- **What is shared:** one copy per zig version, target and build mode, under the unpacked SDK in `$DINO_HOME` (`dino doctor` shows where). The first project to need one builds it, saying so; every later project, and the same project after `dino clean`, only links it.
- **Your flags don't reach it.** The shared copy is built with dino's own flags, so a project whose flags could change the SDK's code keeps compiling the SDK itself, with those flags. Warnings (`-W...`, but not `-Wa,` `-Wp,` `-Wl,`), `-w`, `-pedantic`, `-std=...` and how diagnostics look are known not to; anything else in `CFLAGS` or `CMAKE_C_FLAGS`, changed `CMAKE_C_FLAGS_RELEASE`/`_DEBUG`, a build type other than Debug or Release, or `add_compile_options()`, `add_compile_definitions()` or `include_directories()` before `project()` keeps the source build. `dino build -v` says when that happens and why.
- **Not used** with `DINO_SDK_PATH`, which points at a working tree whose sources change without a new version, or for `dino test --memcheck`. If the copy can't be built or written, `dino` warns and compiles the SDK in the project.
- **The SDK options** (`DINO_NO_AUTO_RUNTIME`, `DINO_NO_PLATFORM_WARNINGS`, `DINO_NO_LEAK_CHECK`) work the same either way: they change what a project links, not how the SDK is compiled.
- **Your editor** no longer sees the SDK's sources in `compile_commands.json`, only your own; the SDK's headers are found as before.

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

`dino` also passes these, which a plain build adds as it needs them:

- `-DCMAKE_CROSSCOMPILING_EMULATOR=qemu-aarch64` (or `wine` for Windows), so `ctest` can run the tests of a foreign target. Without it they fail to start.
- `-DDINO_LIBRARY_PATH=/abs/a;/abs/b` for [organization libraries](libraries.md). Use absolute paths: a relative entry is resolved against the project's source directory, not the current one.
- `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, for `compile_commands.json`.

`dino build -v` prints the commands it runs, without shell quoting: an argument with spaces needs quotes added before it can be pasted into a shell. `dino` won't build in a directory configured this way: it stops rather than replace the cache and its settings, so use another directory for one or the other.
