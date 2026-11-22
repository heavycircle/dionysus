# Contributing to Dionysus

Dionysus lets you write C once and get tiny, fully static binaries for Linux (musl) and Windows
(MinGW) on arm, arm64, i386, ppc and x86_64. It has two parts:

- **The SDK** (`sdk/`): the `dino_*` portability layer (sockets, threads, mutexes, files, I/O
  multiplexing) and the CMake toolchain that turns `zig cc` into a cross compiler.
- **`dino`** (`cli/`): a single executable, written in Zig, that carries the SDK inside it and
  drives CMake. `dino build arm64 win64` is all a user has to type.

## Contents

- [Getting started](#getting-started)
- [Repository layout](#repository-layout)
- [Building and testing](#building-and-testing)
- [How the toolchain works](#how-the-toolchain-works)
- [Working on the SDK](#working-on-the-sdk)
- [Code style](#code-style)
- [Commit messages](#commit-messages)
- [Pull requests](#pull-requests)
- [Issues and labels](#issues-and-labels)
- [Versioning](#versioning)
- [Releases](#releases)

## Getting started

### Prerequisites

| Tool | Version | Needed for |
| --- | --- | --- |
| [Zig](https://ziglang.org/download/) | **0.17.1** exactly (`minimum_zig_version` in `build.zig.zon`) | building `dino`; it is also the C compiler and linker for every target |
| CMake | 3.20 or newer | building any C project |
| Ninja, or make | any | CMake generator; Ninja is required on Windows hosts |
| `qemu-user` | any | running arm, arm64 and ppc builds on an x86_64 host (optional) |
| `wine` (plus `wine32` for win32) | any | running Windows builds on Linux (optional) |
| `clang-format` | 19 or newer; CI pins 19.1.7 (see [CI](#ci)) | formatting C code. The repository's `.clang-format` uses `MainIncludeChar`, which older versions reject; the copy `dino init` writes leaves it out and works from 14 |

**Installing Zig.** Download the 0.17.1 archive for your platform from
<https://ziglang.org/download/>, extract it anywhere, and put the directory on your `PATH`:

```sh
tar -xJf zig-x86_64-linux-0.17.1.tar.xz -C ~/.local/opt
export PATH="$HOME/.local/opt/zig-x86_64-linux-0.17.1:$PATH"   # add to your shell profile
zig version                                                   # 0.17.1
```

Distribution packages are often a different Zig version, and Zig changes a lot between releases,
so prefer the official archive. Contributors who only touch C code still need Zig, because it is
the compiler, but they never have to write any.

**Running cross builds (Debian/Ubuntu).**

```sh
sudo dpkg --add-architecture i386      # only needed for win32 under wine
sudo apt update
sudo apt install cmake ninja-build qemu-user wine wine64 wine32:i386
```

`dino doctor` reports what is installed and which targets this machine can run.

**Editor setup.** [ZLS](https://github.com/zigtools/zls) is the Zig language server; build the
release that matches Zig 0.17.1. ZLS runs `zig build check` on save to report full compile errors.
For C, `dino init` and `dino build` write `compile_commands.json` to the project root for clangd.
`dino init` configures for the host, so the file exists before anything has compiled, and `dino build`
refreshes it right after configuring, even when compiling fails. The SDK's own sources resolve
`<dino/...>` through `sdk/compile_flags.txt` with no build at all.

That root copy is rewritten for clang tools (`cli/compdb.zig`): each `zig cc -target <t> ...` entry
becomes `clang --target=<triple> -nostdlibinc -isystem <zig's musl/MinGW headers> ...`, so clangd
and clang-tidy need no extra flags and see the same libc the build uses. The database follows the
first target of the last `dino build`: `dino build win64` gives your editor the Windows-only files
(`sdk/src/core/*_win32.c`, `<windows.h>`), and `dino build` switches back to the host, where the
`*_posix.c` files have real flags. Projects configured with
plain `cmake` and the toolchain file get CMake's unrewritten database, which clang tools can't read.

### Docker

The `Dockerfile` builds an image with everything above, plus a wine prefix that's already set up.
Use it when you don't want to install the cross runners locally, or to reproduce test results:

```sh
docker build -t dionysus .
docker run --rm -v "$PWD:/work" dionysus dino build --all -C examples/mutex_example
docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/examples.sh /repo/examples
```

The image has `dino` built from the source tree at image build time, so rebuild the image after
changing `cli/` or `sdk/`.

The container starts as root only to pick a user (`scripts/docker-entrypoint.sh`): the owner of the
working directory, so files written into a mounted project keep their owner. `dino` (UID 1000) is
remapped to that UID; a root-owned mount (rootless Docker, Docker Desktop) runs as root; `--user` is
respected. The image's `USER` is `root:dino-entrypoint`, so a start without `--user` is UID 0 in
that group, while `--user 0` starts in group 0 and is left alone. Remapping edits `/etc/passwd`
rather than calling `usermod -u`, which re-owns the whole home directory, and re-owns only files
under `/home/dino` that the image made: still owned by UID 1000 and on the image's own filesystem,
so mounts there (a zig cache, a read-only directory) are never touched. Wine is the hard part: it
refuses a prefix owned by another user, and re-owning the 1.4 GB one built into the image takes
minutes. So the real prefix lives in `/opt/wine-prefix`,
world-writable, and each user's `~/.wine` is a few KB of registry copies and links into it. The
entrypoint also writes the prefix's `wineserver` file (Debian's wine names its socket directory
there); without it, wine processes started together on a fresh container race to create it and
some fail (#71). `tests/docker.sh dionysus` checks the cases, including parallel wine starts,
mounts under the home directory and `--user 0`; CI runs it.

**Devcontainer.** `.devcontainer/devcontainer.json` uses the same image, for VS Code ("Reopen in
Container"), GitHub Codespaces, or the `devcontainer` CLI. It runs `zig build` on creation and puts
`zig-out/bin` first on `PATH`, so `dino` is the one built from your checkout. Run `zig build` again
after changing `cli/` or `sdk/`.

## Repository layout

```
build.zig, build.zig.zon   Zig build for dino; build.zig.zon holds the version
cli/                       dino source (main.zig, commands/, cmake.zig, libraries.zig, ...)
sdk/
  include/dino/core/       public headers: error, event, fs, io, mutex (+ condition variables, once), network,
                           pipe, process, runtime, thread, time
  include/dino/util/       extras built on core: logging
  src/core/                implementations: <module>.c (shared, if any), <module>_posix.c and
                           <module>_win32.c (one is built per platform)
  src/util/                implementations of the util headers
  cmake/                   toolchain.cmake, flags.cmake, Dino.cmake, version.h.in
  portability/             wrappers for system headers that warn about calls with a dino_*
                           replacement (common/, posix/, win32/)
  CMakeLists.txt           the dino_core and dino_util static libraries
  testing/                 utest.h (vendored) and the leak counter, for dino_test()
  tests/                   the SDK's own tests, one test_<module>.c per module
docs/                      user documentation: README.md (index), guide/ (using dino),
                           sdk/ (the dino_* modules)
examples/                  one standalone CMake project per SDK feature, plus org_library and
                           org_app (a DINO_LIBRARY_PATH library and a project that uses it)
tests/examples.sh          builds and runs every example on every target
tests/sdk.sh               runs sdk/tests with `dino test`
tests/windows.sh           runs the Windows builds of sdk/tests on Windows itself
tests/warnings.sh          checks the portability warnings against tests/fixtures/raw_calls
tests/docs.sh              checks docs links and builds and runs the C programs in docs/
tests/headers.sh           checks every public header compiles alone as C and C++ and stays namespaced
tests/cli.sh               checks the dino command line end to end
tests/cmake-minimum.sh     builds and tests with the oldest supported CMake
tests/docker.sh            checks the image's entrypoint (file ownership, wine)
tests/image.sh             smoke test of a built image, run before a release pushes it
scripts/release.sh         cuts a release (see Releases)
scripts/dist.sh            builds the release archives
scripts/lintcommit.sh      checks commit messages
scripts/tidy.sh            clang-tidy on the SDK for x86_64 and win64
.clang-format, .clang-tidy C style and static-analysis rules (also written by dino init)
cliff.toml                 release-notes config for git-cliff
.github/workflows/         CI, release and backport workflows
.github/actions/setup-zig/ installs the pinned Zig in workflows
Dockerfile                 build and test image
.devcontainer/             devcontainer using that image
```

Some history explains a few of the leftover pieces. The project started as a hand-written, no-libc
runtime with its own syscall stubs. It now builds against musl (Linux) and MinGW-w64 (Windows)
through `zig cc`, and `dino_*` only wraps the places where those two disagree. A bash `dino`
script was replaced by the Zig executable.

## Building and testing

### dino itself

```sh
zig build                      # debug build at zig-out/bin/dino
zig build -Drelease            # small optimized build
zig build test                 # unit tests (also checks build.zig.zon and the SDK agree on the version)
zig build run -- doctor        # build and run with arguments
zig fmt build.zig cli          # format
```

`dino` embeds `sdk/` when it is compiled and unpacks it to its cache (`~/.cache/dino`, or
`$DINO_HOME`) the first time it runs. **After editing `sdk/`, either rebuild `dino` or point it at
your working tree:**

```sh
DINO_SDK_PATH="$PWD/sdk" zig-out/bin/dino build -C examples/mutex_example
```

### C projects and examples

```sh
dino build                     # host target, Release
dino build arm64 win64         # several targets; output in build/<target>/
dino build --all --debug       # every target, Debug
dino run arm64 -- --flag       # build, then run under qemu/wine as needed
dino build x86_64 -- -DFOO=ON  # everything after -- goes to CMake
```

`dino build` is a thin layer over CMake. To see exactly what it does, or to work without it, these
are the equivalent commands:

```sh
SDK=$PWD/sdk
cmake -S examples/mutex_example -B build/arm64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$SDK/cmake/toolchain.cmake \
    -DCMAKE_PROJECT_INCLUDE=$SDK/cmake/Dino.cmake \
    -DDINO_SDK_PATH=$SDK \
    -DZIG_TARGET=aarch64-linux-musl \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/arm64
```

`dino` also passes `-DDINO_LIBRARY_PATH=<list>` (empty when the variable isn't set) and, for foreign
targets, `-DCMAKE_CROSSCOMPILING_EMULATOR`. `examples/org_app` only configures with
`DINO_LIBRARY_PATH` pointing at `examples/org_library`, as `tests/examples.sh` sets it.

### SDK tests

The SDK's own tests live in `sdk/tests/`, one `test_<module>.c` per module, written with
[utest.h](https://github.com/sheredom/utest.h) (vendored in `sdk/testing/`):

```c
#include <utest.h>

#include <dino/core/mutex.h>

UTEST(mutex, is_recursive)
{
    dino_mutex_t m = dino_mutex_create();
    ASSERT_NE(DINO_INVALID_MUTEX, m);
    EXPECT_EQ(0, dino_mutex_lock(m));
    EXPECT_EQ(0, dino_mutex_lock(m));
    ...
}

UTEST_MAIN()
```

Every file is one `dino_test()` executable and one CTest test. Run them with `dino test`:

```sh
DINO_SDK_PATH=$PWD/sdk dino test --all -C sdk/tests     # every target (needs qemu-user and wine)
DINO_SDK_PATH=$PWD/sdk dino test --memcheck -C sdk/tests # host, under valgrind
tests/sdk.sh --all                                     # same, from a scratch copy (what CI runs)
```

- **Leaks fail tests.** On the Linux targets, `dino_test()` links an allocation counter
  (`-Wl,--wrap=malloc,...`, `sdk/testing/leakcheck.c`); anything still allocated at exit fails the
  test. Release builds can optimize away an allocation that never escapes the test itself, so use
  `--debug` when testing the counter on test code; SDK allocations are always counted.
- **`--memcheck`** builds a dynamically linked x86_64 glibc variant (valgrind can't see the allocator
  in a static binary) and runs each test under valgrind: invalid reads and writes, uninitialized
  memory, use after free and lost memory.
- Every `UTEST` needs at least one `ASSERT_*` or `EXPECT_*`; one without leaves utest's result
  parameter unused, which `-Wextra -Werror` rejects.
- Tests run in parallel, so each file uses its own port range (`#define PORT 18100`, 18200, ...).
- **Skips fail.** utest counts a case that calls `UTEST_SKIP` as passed, so `tests/sdk.sh` fails on any skipped case that isn't on its list of known ones (today: the symbolic link cases, under a wine that can't create links). Add to that list only with the reason, and only when another job runs the case for real.
- A test file that runs longer than 120 seconds is stopped and fails as `Timeout`, and its output ends with the `[ RUN ]` line of the case that hung (`dino test --timeout <seconds>` changes the limit).

### Test matrix

`tests/examples.sh` copies `examples/` to a temporary directory, builds each one for all seven
targets, and runs the results natively, under qemu-user, or under wine. Run it in the Docker image,
which has every runner installed. Known failures are listed in the script with their issue number;
a known failure that starts passing fails the run, so remove its entry in the PR that fixes it.

`tests/examples.sh /repo/examples arm64 win64` limits the run to some targets. `CFLAGS` from the
environment reaches every compile; CI sets `-Wall -Wextra -Werror`, so warnings fail the build.
The SDK's own sources are the exception: they drop the project's warning flags for a fixed `-Wall
-Wextra` that never fails a project's build (the end of `sdk/CMakeLists.txt`). `sdk/tests` sets
`DINO_SDK_WERROR`, so `tests/sdk.sh` is the check that fails on a warning in the SDK.

`tests/windows.sh <directory>` runs the win64 or win32 builds of the SDK tests on Windows itself, with no emulator and no ctest; CI cross-compiles them on Linux and runs them on a Windows runner. Wine isn't Windows: some cases can't run or can't fail under it (symbolic links, `SO_REUSEADDR`), so this job is the one that checks them. No case may skip there. On Linux, `tests/windows.sh sdk/tests/build/win64 wine` tries the script itself.

`tests/docs.sh [targets...]` keeps the documentation honest: every relative link and `#anchor` in
the Markdown must resolve, and every ```` ```c ```` block in `docs/` that contains `int main(` (or
`UTEST_MAIN()`, built with `dino_test()`) is built for each target and run, and must exit 0. Write
examples in the docs as complete programs that take no arguments, so they're tested; fragments
without `main` aren't. A block that isn't C to compile belongs in a fence without the `c` tag.

`tests/warnings.sh [targets...]` checks the [portability warnings](docs/guide/portability-warnings.md):
it builds `tests/fixtures/raw_calls` under clang's `-verify`, so every flagged call must warn
exactly as its `expected-warning` comment says, in four language modes, and then builds it again
with `DINO_NO_PLATFORM_WARNINGS` set, where nothing may warn. `--memcheck` repeats the check
against glibc's headers.

`tests/cli.sh [cases...]` checks `dino` itself end to end: configure caching, `dino run`,
`dino init` and the command line. Each `case_<name>` function is one check on a fresh project for
the host target; name cases to run only those. Unit tests in `cli/` cover what doesn't need CMake.

`tests/cmake-minimum.sh` installs the oldest CMake dino supports (3.20.5, from PyPI) and builds a
new project and the SDK tests with it. Raising the minimum means changing `min_cmake` in
`cli/commands/doctor.zig`, the `cmake_minimum_required` lines (SDK, `dino init`'s template,
examples, docs) and the version this script installs, together.

### CI

`.github/workflows/ci.yml` runs on every PR and on pushes to `main` and `release-*`:

| Job | What it checks |
| --- | --- |
| `lint` | `zig fmt --check`, `clang-format --dry-run --Werror`, `shellcheck`, the public headers (`tests/headers.sh`), clang-tidy on the SDK (x86_64 and win64 views), and commit messages |
| `unit` | `zig build test`, and `tests/cmake-minimum.sh` |
| `targets (<target>)` | for one target with `-Wall -Wextra -Werror`, in the Docker image built from the PR (so it tests the PR's `dino`): `tests/examples.sh`, the SDK tests with `tests/sdk.sh`, `tests/warnings.sh` and `tests/docs.sh`; the x86_64 job also runs the SDK tests under valgrind and the warnings against glibc (`--memcheck`) |
| `cli` | `tests/cli.sh` in the same image; a job of its own because it is the slowest check and doesn't depend on the target |
| `docker` | `tests/docker.sh` on the same image: the entrypoint (file ownership, wine) |
| `windows (win32)`, `windows (win64)` | the SDK tests, cross-compiled by `windows-build` on Linux with `-Wall -Wextra -Werror`, run on Windows itself by `tests/windows.sh` |

Run the same checks locally before asking for review:

```sh
zig fmt --check build.zig cli && zig build test
tests/cmake-minimum.sh
git ls-files '*.c' '*.h' | xargs uvx --from clang-format==19.1.7 clang-format --dry-run --Werror
git ls-files '*.sh' | xargs uvx --from shellcheck-py==0.10.0.1 shellcheck
tests/headers.sh
scripts/tidy.sh uvx --from clang-tidy==19.1.0 clang-tidy
scripts/lintcommit.sh origin/main..HEAD
docker build -t dionysus . && docker run --rm -e CFLAGS="-Wall -Wextra -Werror" \
    -v "$PWD:/repo:ro" dionysus /repo/tests/examples.sh /repo/examples
docker run --rm -e CFLAGS="-Wall -Wextra -Werror" -v "$PWD:/repo:ro" dionysus /repo/tests/sdk.sh --all
docker run --rm -e CFLAGS="-Wall -Wextra -Werror" -v "$PWD:/repo:ro" dionysus /repo/tests/warnings.sh
docker run --rm -e CFLAGS="-Wall -Wextra -Werror" -v "$PWD:/repo:ro" dionysus /repo/tests/docs.sh
docker run --rm -v "$PWD:/repo:ro" dionysus /repo/tests/cli.sh
```

The linters are pinned through [uv](https://docs.astral.sh/uv/) so everyone formats with the same
version as CI. `uvx --from clang-format==19.1.7 clang-format -i <files>` fixes formatting.

## How the toolchain works

`sdk/cmake/toolchain.cmake` makes CMake cross-compile with Zig:

- `CMAKE_C_COMPILER` is `zig;cc;-target;<triple>`. The target lives in the compiler command, not in
  `CMAKE_C_FLAGS`, so user flags can't drop it.
- `ar` and `ranlib` are small generated wrappers around `zig ar` and `zig ranlib`, so no host
  binutils are needed.
- Everything links statically (`-static`). musl and MinGW are the C libraries because they can be
  linked statically; glibc can't be linked statically in a way that works reliably.
- `flags.cmake` (loaded via `CMAKE_USER_MAKE_RULES_OVERRIDE`) sets per-configuration flags.
  `Release` and `MinSizeRel` use `-Oz`, section garbage collection, stripping and no unwind tables.
  `Debug` uses `-O0 -g`. A project configured without a build type gets `Release`.

`Dino.cmake` is injected after every `project()` call through `CMAKE_PROJECT_INCLUDE`. It adds
the SDK as a subdirectory (unless the project already did), sets `DINO_SDK_VERSION`, and defines
`dino_executable(name sources...)`, which is `add_executable` plus `target_link_libraries(name
PRIVATE dino_util)`, and `dino_test()`. That is why a user's `CMakeLists.txt` doesn't need to know
where the SDK is. It then adds every directory in `DINO_LIBRARY_PATH` the same way, under
`_dino_libs/` and `EXCLUDE_FROM_ALL`; see [docs/guide/libraries.md](docs/guide/libraries.md#how-it-works).

| dino name | Zig triple | Output | Runs on an x86_64 Linux host via |
| --- | --- | --- | --- |
| `x86_64` | `x86_64-linux-musl` | static ELF | native |
| `i386` | `x86-linux-musl` | static ELF | native |
| `arm64` | `aarch64-linux-musl` | static ELF | `qemu-aarch64` |
| `arm` | `arm-linux-musleabihf` | static ELF | `qemu-arm` |
| `ppc` | `powerpc-linux-musleabihf` | static ELF | `qemu-ppc` |
| `win64` | `x86_64-windows-gnu` | PE `.exe` | `wine` |
| `win32` | `x86-windows-gnu` | PE `.exe` | `wine` (needs `wine32`) |

`dino targets` prints this table for the current host. Any other Zig triple can be passed to
`dino build` directly.

## Working on the SDK

### Design rules

- **Handles are opaque, and each type is its own C type**, so passing one where another is expected doesn't compile (`tests/headers.sh` checks it). A handle is a pointer to an undefined struct, `typedef struct dino_<thing> *dino_<thing>_t`, with `NULL` as its `DINO_INVALID_<THING>`. Where a platform has no struct behind it (a POSIX pipe's descriptor, a pid), the value is carried in the pointer type through `uintptr_t` and never dereferenced. The one exception is `dino_socket_t`, an integer holding the OS socket so that it can be passed to platform calls, with `DINO_INVALID_SOCKET` as all bits set.
- **Names:** `dino_<thing>_<verb>` for functions, where the thing is what the call acts on, usually but not always the module (`dino_socket_send`, `dino_cond_wait` in the mutex module, `dino_env_get` in the process module), and `dino_<thing>_t` for every type (`dino_process_spawn_opts_t`, `dino_thread_fn_t`). Adding something to a container reads `dino_<container>_add_<thing>` (`dino_event_add_socket`, `dino_event_add_timer`). `DINO_` for macros and constants. Nothing outside these prefixes; `tests/headers.sh` checks it.
- **Return values:** `0` for success and `-1` for failure, never a platform error code.
  Constructors return the handle, with the `DINO_INVALID_*` value on failure; functions that start
  something or produce several results take out-parameters and return `0` / `-1`. Waits return
  `DINO_TIMEOUT` when the time runs out. Document it in the header's `@return`.
- **Every failure records a reason** for `dino_error()` before it returns: `return dino__fail(DINO_EINVAL);` for an argument, `return dino__fail_os();` right after the system call that failed, before any cleanup can replace `errno` (`src/core/error_internal.h`). A path that returns `-1` without one makes `dino_error()` report some earlier failure. `DINO_TIMEOUT` is not a failure and records nothing.
- **Timeouts** are `int timeout_ms`: `-1` waits forever, `0` checks once.
- **Strings are UTF-8** on every platform; Windows code converts them for the `W` APIs.
- **Headers** are self-contained, in `extern "C"` guards, and documented with `/** */` comments
  (`tests/headers.sh` checks the first two).
- **No winpthreads.** Threads and mutexes use the Win32 API directly (`CreateThread`,
  `CRITICAL_SECTION`); winpthreads works but makes binaries much larger.
- **Handles aren't inherited.** Every socket, pipe or file the SDK opens is close-on-exec on
  POSIX and non-inheritable on Windows, so child processes only get what they're explicitly given.
- **Internal names use `dino__`.** Helpers private to one file are `static`. When an SDK
  internal must still be visible to the linker (for example `dino__runtime_auto`, which a link
  option names), it gets a double underscore. These aren't API: they're never declared in a
  public header or documented as something to call, and they can change or disappear without
  notice.
- **Windows libraries link `PRIVATE`** (`ws2_32` today) so they don't leak into users' link lines.
- **Size matters.** Check release binary sizes before and after a change (`dino build --all`
  prints them). An unexplained increase is a regression.

### Adding a module

Platform portability goes in `core` (`dino_core`). Optional extras that are built on it, such as
logging, go in `util` (`dino_util`, which links `dino_core`); use `util` and `DINO_UTIL_` in place
of `core` and `DINO_CORE_` below.

1. Add the header to `sdk/include/dino/core/<name>.h` with a `DINO_CORE_<NAME>_H` include guard and
   doc comments in the style of the existing headers.
2. Implement it. Code that differs by platform goes in `sdk/src/core/<name>_posix.c` and
   `<name>_win32.c`: a function that differs at all lives wholly in both files, with no
   `#ifdef _WIN32` in shared code. Code that is the same everywhere goes in `<name>.c`, and helpers
   the halves share in `<name>_internal.h` (internal symbols get the `dino__` prefix). A module with
   no platform code is just `<name>.c`. **Every function needs both halves.** A function defined
   only for POSIX links fine on Linux and fails on Windows.
3. In `sdk/CMakeLists.txt`, add `<name>.c` (if any) to `add_library(dino_core ...)` (or `dino_util`)
   and `<name>` to `DINO_PLATFORM_MODULES`, which builds the right half and fails the configure on
   any platform if either half is missing.
4. If it needs process-wide setup (like `WSAStartup`), make it lazy: an idempotent, thread-safe
   `dino_<name>_init()` that the module's entry points call first, returning their usual failure
   value if it fails (see `dino_net_init()` and `dino_socket_create()`), and call it from
   `dino_runtime_init()`. Only setup that must happen before `main()` and can't fail goes in
   `src/core/runtime_auto.c`, which is linked into every program on Linux.
5. Add `sdk/tests/test_<name>.c` covering every function, including the error paths
   (`DINO_INVALID_*` handles, bad arguments). `dino test --all` and CI pick it up automatically;
   make sure it passes on all seven targets and under `--memcheck`.
6. If the module replaces platform calls (a `pipe()`, a `CreateThread()`), make them warn. Add a
   redeclaration to the wrapper for the header that declares each one, in `sdk/portability/common`
   (headers on both platforms), `posix` or `win32`, creating the wrapper if there isn't one yet (copy
   an existing wrapper's shape). Then add the call with its `expected-warning` comment to
   `tests/fixtures/raw_calls`, and run `tests/warnings.sh`. Only flag calls the module really
   replaces; see [docs/guide/portability-warnings.md](docs/guide/portability-warnings.md#how-it-works).
7. Add `examples/<name>_example/` (a `CMakeLists.txt` with `dino_executable` and a `main.c` that
   exits nonzero on failure, so the test matrix notices) and a row in `examples/README.md`. The
   test matrix picks it up automatically.
8. Add `docs/sdk/<name>.md` in the shape of the existing pages (what it's for, one complete example
   program, a table of every function, pitfalls, links to the header and the example), and a row in
   the module tables of `docs/README.md` and `docs/sdk/README.md`. `tests/docs.sh` builds and runs
   the example on every target.

## Code style

- **C:** `clang-format` with the repository's `.clang-format` (WebKit base, Allman braces, 100
  columns). Use the pinned version from [CI](#ci) (`uvx --from clang-format==19.1.7 clang-format
  -i <files>`); other versions can format differently.
- **Includes** are sorted and grouped by clang-format, in this order: the file's own header first
  (`io_posix.c` and `io_win32.c` start with `<dino/core/io.h>`, which keeps headers
  self-contained), then system and standard headers, then other `<dino/...>` headers in their own
  block. On Windows, `<winsock2.h>` and `<ws2tcpip.h>` always come before `<windows.h>`; the config
  pins that order.
- **Static analysis:** `.clang-tidy` enables `clang-analyzer-*` and `bugprone-*`, minus a couple of
  noisy checks. The SDK must be clean under it; `scripts/tidy.sh` runs it with warnings as errors on
  every SDK source, once with the x86_64 and once with the win64 compile database, so the Windows
  halves are analyzed too. `dino init` copies the same `.clang-format` and `.clang-tidy` into new
  projects, where findings stay warnings.
- **Zig:** `zig fmt`. Its output is the style; there are no options to argue about.
- **CMake and shell:** match the surrounding file (4-space indents, lowercase commands).
- **Comments explain why, not what.** Code should be readable without narration; save comments for
  reasons, constraints and surprises ("64-bit Windows SEH requires unwind tables").

## Commit messages

We follow [Conventional Commits](https://www.conventionalcommits.org), as
[neovim does](https://github.com/neovim/neovim/blob/master/CONTRIBUTING.md#commit-messages):

```
type(scope): lowercase subject in the imperative, no period, at most 72 characters

Problem:
What is wrong or missing, and why it matters.

Solution:
What this commit changes, and anything a reviewer should know.
```

- **type:** `build`, `chore`, `ci`, `docs`, `feat`, `fix`, `perf`, `refactor`, `revert`, `test`.
  `chore` is for maintenance that fits nowhere else; it's listed under "Other changes" in the
  release notes.
  `release: vX.Y.Z` is reserved for `scripts/release.sh`.
- **scope** (optional) is an area label: `sdk`, `cli`, `build`, `examples`, `ci`, `docker`, or a
  module such as `network`.
- **Breaking changes** add `!` after the type or scope and a `BREAKING CHANGE:` footer explaining
  what users have to change.
- Small commits can skip the `Problem:`/`Solution:` body if the subject says it all.
- `scripts/lintcommit.sh origin/main..HEAD` checks subjects; CI runs it on every PR. Merge commits
  are skipped. Squash `fixup!` commits and reword `Revert "..."` to `revert: ...` before review.

```
feat(cli): add dino doctor
fix(sdk): define dino_fs_init and dino_fs_cleanup on Windows
feat(cli)!: write build output to out/ instead of build/
```

Commits before this document existed use `scope: subject` (`cli: dino init/...`); don't copy them.

## Pull requests

- **Target `main`.** Fixes are backported from there; see [Versioning](#versioning).
- **One change per PR.** A bug found along the way gets its own issue and PR. Small PRs get reviewed
  faster.
- **Keep your branch current by rebasing** (`git rebase main`), not by merging `main` into it.
  Squash fixup commits before merge, so every commit on `main` builds and has a proper message.
- **Don't rewrite a branch someone else has built on.** For stacked PRs, rebase the dependent
  branch with `git rebase --onto main <old-base>` after its base merges. Merging the base
  in instead leaves duplicate commits in history.
- Open a draft PR for work in progress.
- Include what you tested (targets built, examples run, size changes). Put `Closes #N` in the
  description.

## Issues and labels

Issues labeled `complexity:low` are open to anyone. Don't ask whether you can take one; comment
that you're working on it and open a (draft) PR.

| Group | Labels | Meaning |
| --- | --- | --- |
| Type | `bug`, `bug-crash`, `bug-regression`, `enhancement`, `refactor`, `performance`, `security`, `documentation`, `breaking-change` | what kind of change |
| Area | `sdk`, `cli`, `build`, `examples`, `ci`, `distribution`, `dependencies`, `project-management`, `code-standards` | which part of the repo |
| Topic | `network`, `filesystem`, `threads`, `event-loop`, `job-control`, `time`, `logging` | SDK subsystem |
| Platform | `platform:linux`, `platform:windows`, `platform:arm`, `platform:other` | where it happens |
| Needs | `needs:repro`, `needs:response`, `needs:decision`, `needs:design`, `needs:discussion`, `needs:backport` | what's blocking it |
| Has | `has:repro`, `has:bisected`, `has:workaround`, `has:plan` | what's already known |
| Complexity | `complexity:low`, `complexity:high` | risk of the change |
| Closed | `closed:invalid`, `closed:question`, `closed:wontfix` | why it was closed |

## Versioning

Dionysus uses [semantic versioning](https://semver.org) and
[neovim's release model](https://github.com/neovim/neovim/blob/master/MAINTAIN.md#release-policy).
`dino` and the SDK always share one version number.

**The version in the tree always names the next release.** It lives in `build.zig.zon`
(`.version`) and `sdk/CMakeLists.txt` (`project(DinoSDK VERSION ...)` plus
`DINO_VERSION_PRERELEASE`). `zig build test` fails if the two disagree. Only the release commit
itself drops the `-dev` suffix.

| Branch | Version in the tree | `dino version` reports |
| --- | --- | --- |
| `main` | `0.2.0-dev` | `0.2.0-dev.14+g1a2b3c4d5` (14 commits after the last tag) |
| `release-0.1` | `0.1.1-dev` | `0.1.1-dev.3+g6e7f8a9b0` |
| tag `v0.1.0` | `0.1.0` | `0.1.0` |
| no tag reachable yet | `0.1.0-dev` | `0.1.0-dev+g1a2b3c4d5` |

`build.zig` computes this from `git describe`. Builds without `.git` (source archives, the Docker
build) report the plain tree version; pass `-Dversion-string=X.Y.Z` to override it. C code can
include `<dino/version.h>` for `DINO_VERSION`, `DINO_VERSION_MAJOR`, `DINO_VERSION_MINOR`,
`DINO_VERSION_PATCH` and `DINO_VERSION_PRERELEASE`.

**Branches:**

- **`main` is the only development branch.** Everything lands here first: features, fixes,
  breaking changes.
- **`release-X.Y`** is created from the `vX.Y.0` tag and exists only to produce `vX.Y.Z` patch
  releases. It gets bug, security and documentation fixes, and never features or breaking changes.
- **Never commit directly to a release branch.** Merge the fix to `main` first, then add the
  `ci:backport release-X.Y` label to the PR (before or after merging). `.github/workflows/backport.yml`
  (adapted from neovim's) opens a `backport: <title>` PR that cherry-picks the commits with
  `git cherry-pick -x` onto `release-X.Y`. Review and merge it like any PR. If the cherry-pick
  conflicts, the workflow comments on the original PR; then backport by hand with
  `git cherry-pick -x` in a PR of your own. Use `needs:backport` on issues whose fix should be
  backported.
- **Only the newest `release-X.Y` is supported.** When `release-X.(Y+1)` is cut, the old branch is
  frozen and its `ci:backport release-X.Y` label is deleted, so nothing can be backported to it by
  accident. `scripts/release.sh` prints both label commands.

**Tags:** `vX.Y.Z` tags are annotated and never moved. Two tags move, both set by CI:
`stable` points at the newest release (a patch for an older line doesn't move it), and `nightly`
at the last `main` commit the nightly build published. Never push either by hand.

## Releases

Maintainers cut releases with `scripts/release.sh` from a clean checkout:

- **On `main`** (tree at `X.Y.0-dev`): commits `release: vX.Y.0`, tags `vX.Y.0`, creates
  `release-X.Y` from the tag and bumps it to `X.Y.1-dev`, then bumps `main` to `X.(Y+1).0-dev`.
- **On `release-X.Y`** (tree at `X.Y.Z-dev`): commits `release: vX.Y.Z`, tags it, bumps the
  branch to `X.Y.(Z+1)-dev`.

The release commit also sets every `ghcr.io/heavycircle/dionysus:X.Y.Z` in the Markdown to the new
version, so the docs at a tag name the image that tag publishes. Write image references in the docs
with a full version (any will do); the next release fixes it up.

Before it changes anything the script fetches and requires that the branch is exactly
`origin/<branch>`, that the tag and the release branch don't exist yet, and that the `ci` run for
that commit succeeded (it asks `gh`; `--no-ci-check` skips the lookup). It then runs the unit tests,
checks that the freshly built `dino` reports exactly the new version, and prints the
`git push --atomic` command instead of pushing. If anything fails part-way, it deletes the tag and
branch it made and resets to where it started. Review the commits, then push. For a major release,
bump `main` to `X+1.0.0-dev` by hand in a normal PR first.

Pushing the tag runs `.github/workflows/release.yml`. Nothing is published until every `ci.yml` job
has passed on the tagged commit and the image built from it has passed `tests/image.sh`: it holds
the `dino` the tag names, `dino doctor` finds every tool and runner, a new project builds, runs and
passes its tests on every target, and the entrypoint checks in `tests/docker.sh` pass. Then the
image is pushed, and last the GitHub Release is published:

- `dino-X.Y.Z-x86_64-linux.tar.xz`, `dino-X.Y.Z-aarch64-linux.tar.xz` and
  `dino-X.Y.Z-x86_64-windows.zip`, each holding `dino`, `LICENSE` and `README.md`, plus
  `SHA256SUMS`. `scripts/dist.sh X.Y.Z dist` builds the same archives locally. They are
  [reproducible](https://www.gnu.org/software/tar/manual/html_section/Reproducibility.html):
  timestamps come from `SOURCE_DATE_EPOCH` (default: the commit time), and owners, permissions
  and file order are normalized. The same commit, Zig version and tar/xz/zip versions give
  byte-identical archives.
- Release notes generated by [git-cliff](https://git-cliff.org) (`cliff.toml`) from the commits
  since the previous tag, grouped by commit type, which is why commit messages follow
  [Conventional Commits](#commit-messages). There is no `CHANGELOG.md`; the Releases page is the
  changelog. Preview the next release's notes with `git cliff --unreleased`, and edit the notes on
  GitHub afterwards if they need context.

- The Docker image `ghcr.io/heavycircle/dionysus`, tagged `:X.Y.Z` and `:X.Y`. If the tag is the
  newest version, `:stable` and `:latest` move to it too, and so does the `stable` git tag.

To redo a release (a failed run, a fixed workflow), run **release** from the Actions tab with the
tag as input, or `gh workflow run release -f tag=vX.Y.Z`. It replaces the assets, keeps the notes,
and republishes the image. The `ci` jobs run on the tag again first; for a tag older than today's
`ci.yml`, whose tree lacks the scripts it calls, add `-f ci=false`.

**Nightly.** `.github/workflows/nightly.yml` runs daily (and on demand) from `main`. When `main` has
moved since the last nightly and its `ci` run succeeded, it rebuilds the `nightly` prerelease:
archives named `dino-nightly-<platform>` (stable download URLs; the binaries report the exact
`X.Y.0-dev.N+g<sha>` version), notes for everything since the last release, and the `:nightly`
image. A `main` whose `ci` failed or is still running is skipped until the next run.
