# Docker

The `ghcr.io/heavycircle/dionysus` image has everything `dino` needs and can run:

- `dino`, with the SDK built in
- Zig, CMake and Ninja, to build
- qemu-user and wine (64 and 32-bit), to run the `arm`, `arm64`, `ppc`, `win32` and `win64` builds
- valgrind, for `dino test --memcheck`

With it, the only thing a machine or a CI runner needs is Docker.

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

The alias always works on the current directory, so `-C <dir>` must name a path inside it.

## Tags

| Tag                  | What it is                                                                 |
| -------------------- | -------------------------------------------------------------------------- |
| `:stable`, `:latest` | the newest release                                                         |
| `:X.Y.Z`             | one release, unchanging. Use this in CI, so builds don't change under you. |
| `:X.Y`               | the newest patch release of a minor version                                |
| `:nightly`           | the latest `main`                                                          |

## File ownership

The image starts as root only to choose a user, then runs your command as the owner of `/work`. So
`build/` and `compile_commands.json` belong to you on the host, with no `-u` flag. This also works
under rootless Docker and Docker Desktop, where your files appear to be owned by root inside the
container.

Passing `--user` yourself also works; the image then runs as that user. That includes `--user 0`
(or `--user root`), to run as root whoever owns `/work`.

Mounts under the image's home directory, such as a zig cache shared between runs, are left as they
are, read-only ones included:

```sh
docker run --rm -v "$PWD:/work" -v "$HOME/.cache/zig:/home/dino/.cache/zig" \
    ghcr.io/heavycircle/dionysus:stable dino build --all
```

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
            ghcr.io/heavycircle/dionysus:0.7.0 dino test ${{ matrix.target }}
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

The same command works in any CI system that can run a container.

## Your own image

To give every developer and CI job the same extras (organization libraries, more tools), build on
the image:

```dockerfile
FROM ghcr.io/heavycircle/dionysus:0.7.0
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
