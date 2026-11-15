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
| CMake | 3.15 or newer | building any C project |
| Ninja, or make | any | CMake generator; Ninja is required on Windows hosts |
| `qemu-user` | any | running arm, arm64 and ppc builds on an x86_64 host (optional) |
| `wine` (plus `wine32` for win32) | any | running Windows builds on Linux (optional) |
| `clang-format` | any recent | formatting C code |

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
For C, `dino build` writes `compile_commands.json` to the project root for clangd.

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

**Devcontainer.** `.devcontainer/devcontainer.json` uses the same image, for VS Code ("Reopen in
Container"), GitHub Codespaces, or the `devcontainer` CLI. It runs `zig build` on creation and puts
`zig-out/bin` first on `PATH`, so `dino` is the one built from your checkout. Run `zig build` again
after changing `cli/` or `sdk/`.

## Repository layout

```
build.zig, build.zig.zon   Zig build for dino; build.zig.zon holds the version
cli/                       dino source (main.zig, commands/, cmake.zig, targets.zig, ...)
sdk/
  include/dino/core/       public headers: fs, io, kernel, mutex, network, runtime, thread
  include/dino/util/       logging
  src/core/                implementations, one file per header, POSIX and Win32 in each
  cmake/                   toolchain.cmake, flags.cmake, Dino.cmake, version.h.in
  CMakeLists.txt           the dino_core static library
examples/                  one standalone CMake project per SDK feature
tests/examples.sh          builds and runs every example on every target
scripts/release.sh         cuts a release (see Releases)
scripts/dist.sh            builds the release archives
scripts/lintcommit.sh      checks commit messages
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

### Test matrix

`tests/examples.sh` copies `examples/` to a temporary directory, builds each one for all seven
targets, and runs the results natively, under qemu-user, or under wine. Run it in the Docker image,
which has every runner installed. Known failures are listed in the script with their issue number;
a known failure that starts passing fails the run, so remove its entry in the PR that fixes it.

`tests/examples.sh /repo/examples arm64 win64` limits the run to some targets. `CFLAGS` from the
environment reaches every compile; CI sets `-Wall -Wextra -Werror`, so warnings fail the build.

### CI

`.github/workflows/ci.yml` runs on every PR and on pushes to `main` and `release-*`:

| Job | What it checks |
| --- | --- |
| `lint` | `zig fmt --check`, `clang-format --dry-run --Werror`, `shellcheck`, and commit messages |
| `unit` | `zig build test` |
| `examples (<target>)` | `tests/examples.sh` for one target with `-Wall -Wextra -Werror`, in the Docker image built from the PR, so it tests the PR's `dino` |

Run the same checks locally before asking for review:

```sh
zig fmt --check build.zig cli && zig build test
git ls-files '*.c' '*.h' | xargs uvx --from clang-format==19.1.7 clang-format --dry-run --Werror
git ls-files '*.sh' | xargs uvx --from shellcheck-py==0.10.0.1 shellcheck
scripts/lintcommit.sh origin/main..HEAD
docker build -t dionysus . && docker run --rm -e CFLAGS="-Wall -Wextra -Werror" \
    -v "$PWD:/repo:ro" dionysus /repo/tests/examples.sh /repo/examples
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
the SDK as a subdirectory (unless the project already did) and defines
`dino_executable(name sources...)`, which is `add_executable` plus `target_link_libraries(name
PRIVATE dino_core)`. That is why a user's `CMakeLists.txt` doesn't need to know where the SDK is.

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

- **Handles are opaque.** Each resource type is a `typedef uintptr_t dino_<thing>_t` so the same
  header works on both platforms, with a `DINO_INVALID_<THING>` sentinel (for example
  `DINO_INVALID_SOCKET`, `DINO_INVALID_MUTEX`).
- **Return values:** `0` for success and nonzero (usually `-1`) for failure, unless the function
  returns a handle, in which case failure is the `DINO_INVALID_*` value. Document it in the
  header's `@return`.
- **No winpthreads.** Threads and mutexes use the Win32 API directly (`CreateThread`,
  `CRITICAL_SECTION`); winpthreads works but makes binaries much larger.
- **Windows libraries link `PRIVATE`** (`ws2_32` today) so they don't leak into users' link lines.
- **Size matters.** Check release binary sizes before and after a change (`dino build --all`
  prints them). An unexplained increase is a regression.

### Adding a module

1. Add the header to `sdk/include/dino/core/<name>.h` with a `DINO_CORE_<NAME>_H` include guard and
   doc comments in the style of the existing headers.
2. Implement it in `sdk/src/core/<name>.c`, split with `#ifdef _WIN32`. **Every function needs both
   halves.** A function defined only for POSIX links fine on Linux and fails on Windows.
3. Add the source file to `add_library(dino_core ...)` in `sdk/CMakeLists.txt`.
4. If it needs process-wide setup (like `WSAStartup`), call it from `dino_runtime_init()` and
   `dino_runtime_cleanup()` in `src/core/runtime.c`.
5. Add `examples/<name>_example/` (a `CMakeLists.txt` with `dino_executable` and a `main.c` that
   exits nonzero on failure, so the test matrix notices) and a row in `examples/README.md`. The test matrix picks it up
   automatically.

## Code style

- **C:** `clang-format` with the repository's `.clang-format` (WebKit base, Allman braces, 100
  columns). Use the pinned version from [CI](#ci) (`uvx --from clang-format==19.1.7 clang-format
  -i <files>`); other versions can format differently.
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
  frozen.

**Tags:** `vX.Y.Z` tags are annotated and never moved. `stable` (newest release) and `nightly`
(latest `main` build) are planned as moving tags, along with a published Docker image (#21).

## Releases

Maintainers cut releases with `scripts/release.sh` from a clean checkout:

- **On `main`** (tree at `X.Y.0-dev`): commits `release: vX.Y.0`, tags `vX.Y.0`, creates
  `release-X.Y` from the tag and bumps it to `X.Y.1-dev`, then bumps `main` to `X.(Y+1).0-dev`.
- **On `release-X.Y`** (tree at `X.Y.Z-dev`): commits `release: vX.Y.Z`, tags it, bumps the
  branch to `X.Y.(Z+1)-dev`.

The script runs the unit tests, checks that the freshly built `dino` reports exactly the new
version, and prints the `git push` command instead of pushing. Review the commits, then push. For
a major release, bump `main` to `X+1.0.0-dev` by hand in a normal PR first.

Pushing the tag runs `.github/workflows/release.yml`, which publishes the GitHub Release:

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

Re-running the workflow for a tag that already has a release replaces its assets and keeps the
notes.
