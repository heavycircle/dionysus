# Troubleshooting

Start with `dino doctor`: it checks the tools `dino` uses and says what's missing or the wrong version. Below are the
messages you're most likely to meet, what they mean, and what to do.

## Setup

**``zig was not found on PATH (run `dino doctor`)``**, or the same for `cmake`

Install it ([Installing](install.md)) and make sure the directory is on `PATH` in the shell that
runs `dino`. Zig is an archive, not an installer: add its directory to `PATH` yourself.

**`neither ninja nor make was found on PATH`**

CMake needs a build tool. Install `ninja-build` (faster) or `make`. On Windows, Ninja is required.

**`cmake version 3.x.y; 3.20 or newer is required`** (from `dino doctor`)

Install a newer CMake. Ubuntu 20.04's 3.16 is too old. Kitware's apt repository has current
versions, as does `uv tool install cmake`.

**`cannot locate a cache directory; set DINO_HOME`**

Neither `XDG_CACHE_HOME` nor `HOME` is set (common in minimal containers). Set `DINO_HOME` to any
writable directory.

**`DINO_SDK_PATH '...' is not a dino SDK (missing cmake/toolchain.cmake)`**

`DINO_SDK_PATH` must point at the `sdk/` directory of a Dionysus checkout, not at the checkout
itself. Unset it to use the SDK built into `dino`.

**`dino doctor`: `.../sdk/<version> differs from this dino's SDK (...)`**

`dino` unpacks its SDK into the cache directory once and makes the files read-only (except on Windows), so that an edit made after an editor's go-to-definition jumped into it can't change every project's build. A file there changed anyway. Remove the directory named in the message; the next command unpacks it again.

## Building

**``no CMakeLists.txt in '...' (create one with `dino init`)``**

Run `dino` in the project directory, or pass `-C <dir>`.

**``unknown target 'x' (see `dino targets`)``**

Use a name from `dino targets` (`arm`, `arm64`, `i386`, `ppc`, `win32`, `win64`, `x86_64`, in any case, or `aarch64`, `amd64`, `i686`) or a
full Zig target triple.

**A Windows build fails with `undefined reference to ...`**

The function lives in a Windows library that isn't linked. Link it for Windows only:
`if(WIN32) target_link_libraries(app PRIVATE <lib>) endif()`. The Windows documentation names the
library for each function (for example `advapi32` for the registry functions). Sockets need
nothing: `dino_core` links `ws2_32`.

**`undefined reference to 'dino_...'`**

The target doesn't link the SDK. Use `dino_executable()`, or add
`target_link_libraries(app PRIVATE dino_core)` (or `dino_util` for logging).

**A change to `CFLAGS` has no effect**

CMake reads `CFLAGS` only when a target is first configured. Run `dino clean`, then build again.

**The build behaves as if old settings were still there**

Options given with `-- -DNAME=VALUE` stay in CMake's cache until changed. Run `dino clean <target>`
to start that target from scratch.

**`warning: 'pipe' is deprecated: pipe() is POSIX-only; use dino_pipe_create() ...`**

The [portability warnings](portability-warnings.md): the call has a portable `dino_*` replacement.
Switch to it, or opt out for that call, file, target or project.

## Running

**`qemu-aarch64 is needed to run arm64 binaries; install qemu-user (or use the dino Docker image)`**

Install `qemu-user` (or `qemu-user-static`), or run in the [Docker image](docker.md).

**`wine is needed to run win64 binaries on Linux; install wine`**

Install wine. For `win32`, 32-bit wine too (`dpkg --add-architecture i386` then `wine32:i386` on
Debian and Ubuntu).

**`exec format error`** when running a foreign binary directly

The kernel can't run another architecture's binary by itself. Use `dino run <target>`, which goes
through qemu, or register qemu with binfmt_misc (`qemu-user-binfmt` on Debian/Ubuntu) so the
kernel hands such binaries to qemu.

**wine: `... is not owned by you`, or wine hangs on first start**

wine refuses a prefix (`~/.wine`) that belongs to another user, which happens after running wine
with `sudo` or in a container as another user. Remove the prefix or fix its owner. The first wine
start in a new prefix takes a while as it builds one. The Docker image avoids both.

**`the project defines several executables; choose one with --exe (a, b)`**

`dino run --exe a`.

**``arm64 has not been built yet; run `dino build arm64` ``**

`--no-build` needs an existing build for that target.

**`build/x86_64 holds a Release build, not Debug; run without --no-build to build it`**

`build/<target>/` holds one build mode at a time, whichever was built last. Drop `--no-build`, or
the `--debug`/`--release` that asks for the other mode.

**`'test' can't be a project name: CMake or dino already has a target by that name`**

`dino init` names the project after its directory, and CMake and dino already define targets named
`all`, `clean`, `help`, `install`, `package`, `preinstall`, `test`, `test_main` and `dino_*`; `CMakeFiles`, `Testing`, `_dino` and `_dino_libs` are directories in `build/<target>/`, where the program would have to go. Use a directory with
another name.

**A program that works on Linux fails under wine, or the other way round**

Look for platform calls that only work on one side: the [portability warnings](portability-warnings.md)
point most of them out, and [SDK gotchas](../sdk/gotchas.md) lists the subtler differences (file
sizes over 2 GB, `SOCKET` handles, `printf` formats for 64-bit integers).

## Testing

**`dino leak check: N allocation(s) still live at exit`**

The test leaked memory (Linux targets only). Run `dino test --memcheck` to see where it was
allocated. See [Testing](testing.md#the-leak-check).

**`--memcheck needs valgrind; install it (or use the dino Docker image)`**

Install valgrind. `--memcheck` also needs an x86_64 Linux host, and takes no targets.

**`No tests were found!!!`**

The project defines no `dino_test()` targets, or `-R` matched none of them.

**A test passes alone but fails under `dino test`**

Tests run in parallel. Two tests using the same port, file or directory interfere. Give each its
own, or run with `-j 1` to confirm.

## Editors

**clangd can't find `<dino/core/...>` or reports errors the build doesn't**

`compile_commands.json` is missing or stale. Run `dino build` once, then restart clangd. Make
sure no other `compile_commands.json` (for example inside `build/`) is configured in the editor in
its place.

**clang-tidy or clangd ignores `.clang-tidy`**

A `.clang-tidy` from an older `dino init` uses a format clang-tidy 14 can't read. Copy the
`CheckOptions` list form from a freshly generated one.

## Still stuck

Run the command again with `-v`, which prints every CMake command and the full compiler output, and
[open an issue](https://github.com/heavycircle/dionysus/issues) with that output, `dino version` and
`dino doctor`.
