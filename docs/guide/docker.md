# Docker

The `ghcr.io/heavycircle/dionysus` image has everything `dino` needs and can run:

- `dino`, with the SDK built in
- Zig, CMake and Ninja, to build
- qemu-user and wine (64 and 32-bit), to run the `arm`, `arm64`, `ppc`, `win32` and `win64` builds
- valgrind, for `dino test --memcheck`

With it, the only thing a machine or a CI runner needs is Docker. The image is built for x86_64 (`linux/amd64`) only for now; on an arm64 host (Apple silicon, Graviton) it runs under emulation, if at all ([#163](https://github.com/heavycircle/dionysus/issues/163)).

```sh
docker run --rm -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino build --all
docker run --rm -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino test --all
docker run --rm -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino run win32
```

Mount the project at `/work`, the image's working directory, and put the command after the image
name. With no command, the image runs `dino doctor`.

A shell alias makes it feel local:

```sh
alias dino='docker run --rm -it -v "$PWD:/work" ghcr.io/heavycircle/dionysus:stable dino'
```

The alias always works on the current directory, so `-C <dir>` must name a path inside it. `-t` needs a terminal: in scripts, git hooks and editor tasks, Docker stops with "the input device is not a TTY", so leave `-t` out there.

## Tags

| Tag                  | What it is                                                            |
| -------------------- | --------------------------------------------------------------------- |
| `:stable`, `:latest` | the newest release                                                    |
| `:X.Y.Z`             | one release, unchanging. Pin it where builds must never change.       |
| `:X.Y`               | the newest patch release of a minor version, as the examples here use |
| `:nightly`           | the latest `main`                                                     |

## File ownership

The image starts as root only to choose a user, then runs your command as the owner of `/work`. So
`build/` and `compile_commands.json` belong to you on the host, with no `-u` flag. This also works
under rootless Docker and Docker Desktop, where your files appear to be owned by root inside the
container.

Passing `--user` yourself also works; the image then runs as that user. That includes `--user 0`
(or `--user root`), to run as root whoever owns `/work`.

Mounts under the image's home directory are left as they are, read-only ones included. To share a zig cache between runs, mount it anywhere and name it in `ZIG_GLOBAL_CACHE_DIR`, which works whichever user the command runs as (a cache mounted at `/home/dino/.cache/zig` is used only when it runs as `dino`, not as root under rootless Docker and Docker Desktop, or with `--user`):

```sh
docker run --rm -v "$PWD:/work" -v "$HOME/.cache/zig:/zig-cache" -e ZIG_GLOBAL_CACHE_DIR=/zig-cache \
    ghcr.io/heavycircle/dionysus:stable dino build --all
```

`compile_commands.json` written in the container names the container's paths (`/work`, the image's zig), so an editor on the host can't use it. Run the editor's language server in the container instead, through a [dev container](#dev-containers).

## The zig cache

zig builds the C library and runtime for each target the first time a build needs them, about a minute for all seven targets, and a new container starts with whatever its image holds. The published images (`:stable`, the version tags and `:nightly`) are built with that cache already filled for Release builds of every target, in `/opt/zig-cache`, so a new project's first `dino build --all` takes about half the time (78 s to 32 s on a CI-sized machine); a `--debug` build, or one with flags of its own, still builds its own library once per container. An image you build yourself starts cold unless you pass `--build-arg WARM_ZIG_CACHE=1`, which adds about 240 MB.

zig uses that cache unless you choose another: set `ZIG_GLOBAL_CACHE_DIR`, or mount one as shown above to keep a cache across containers.

## Wine

Every container user gets a wine prefix that links to a shared, prebuilt one in the image, so the
first Windows run doesn't spend about 40 seconds building a new prefix. There's nothing to set
up. `WINEDEBUG=-all` is set, so wine's own diagnostics stay out of your output.

## Environment

Variables reach `dino` with `-e`:

```sh
docker run --rm -v "$PWD:/work" -e CFLAGS="-Wall -Wextra -Werror" \
    ghcr.io/heavycircle/dionysus:stable dino build --all
```

Paths in variables such as `DINO_LIBRARY_PATH` are container paths. Mount what they point to; see
[Organization libraries](libraries.md#docker-and-ci).

`LANG` is `C.UTF-8`, so file names and arguments outside ASCII reach Windows programs under wine intact, as UTF-8 (see [UTF-8 on Windows](../sdk/fs.md#utf-8-on-windows)). Without a UTF-8 locale wine can't create such a file at all.

## CI

A GitHub Actions workflow that builds and tests every target on each push and PR:

```yaml
name: ci
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        target: [arm, arm64, i386, ppc, win32, win64, x86_64]
    steps:
      - uses: actions/checkout@v5
      - name: Build and test ${{ matrix.target }}
        run: |
          docker run --rm -e CFLAGS="-Wall -Wextra -Werror" -v "$PWD:/work" \
            ghcr.io/heavycircle/dionysus:0.9 dino test ${{ matrix.target }}
      - uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.target }}
          path: build/${{ matrix.target }}/
```

- **`docker run` in a step**, not a job-level `container:`. GitHub Actions overrides the image's
  entrypoint for container jobs, and the entrypoint is what makes the files belong to the runner's
  user.
- **`dino test` builds before testing**, so one step covers both. Use `dino build` for targets
  whose binaries you only ship.
- **One job per target** runs the targets in parallel, and a failure names its target.

The same `docker run` works in any CI system that can run Docker. Systems that run the whole job inside the image, such as GitLab CI's `image:`, check the code out somewhere other than `/work`, so the entrypoint can't tell whose files they are. Set `entrypoint: [""]` there, and the job runs as the image's root user, which can write the checkout.

`0.9` is the newest 0.9.x release; each patch release moves it. Pin a full version (`0.9.0`) if every build must use the same image.

## Your own image

To give every developer and CI job the same extras (organization libraries, more tools), build on
the image:

```dockerfile
FROM ghcr.io/heavycircle/dionysus:0.9
COPY company-libs /opt/company
ENV DINO_LIBRARY_PATH=/opt/company/logging:/opt/company/proto
```

## Dev containers

The repository's [`.devcontainer`](../../.devcontainer) builds the image for working on Dionysus
itself. For your own project, a `.devcontainer/devcontainer.json` can use the published image:

```json
{
  "name": "my-project",
  "image": "ghcr.io/heavycircle/dionysus:stable",
  "remoteUser": "dino",
  "customizations": {
    "vscode": { "extensions": ["llvm-vs-code-extensions.vscode-clangd"] }
  }
}
```
