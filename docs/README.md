# Dionysus documentation

How to build your own projects with `dino` and the `dino_*` SDK. To work on Dionysus itself, see
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Guide

New to dino? Read the first two pages in order.

| Page                                                  | What it covers                                                                            |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| [Installing](guide/install.md)                        | The release binary, Zig and CMake, or the Docker image; reading `dino doctor`             |
| [Your first project](guide/first-project.md)          | `dino init`, the generated files, building, running and testing                           |
| [Building](guide/building.md)                         | Targets, output, debug and release, running foreign binaries, options, rebuilding         |
| [Projects](guide/projects.md)                         | The `CMakeLists.txt`: sources, include paths, several executables, libraries, SDK options |
| [Testing](guide/testing.md)                           | `dino_test()` and utest.h, the leak check, `--memcheck`, tests in CI                      |
| [Docker](guide/docker.md)                             | The image, tags, file ownership, CI workflows, derived images                             |
| [Editors](guide/editors.md)                           | `compile_commands.json`, clangd, clang-tidy and clang-format, debugging                   |
| [Organization libraries](guide/libraries.md)          | Making your own libraries available to every project with `DINO_LIBRARY_PATH`             |
| [Portability warnings](guide/portability-warnings.md) | The warnings for platform calls that have a `dino_*` replacement, and how to opt out      |
| [Troubleshooting](guide/troubleshooting.md)           | Common error messages and what to do about them                                           |

## SDK

The `dino_*` API, one page per module, each with a complete example that CI builds and runs on every
target. Start with the [overview](sdk/README.md), which explains the conventions every module shares.

| Page                          | What it covers                                                                       |
| ----------------------------- | ------------------------------------------------------------------------------------ |
| [Overview](sdk/README.md)     | The modules, the two libraries, handles, return values, timeouts, ownership, threads |
| [Runtime](sdk/runtime.md)     | Automatic setup, failing early, opting out                                           |
| [Errors](sdk/error.md)        | Why a call failed: `dino_error()` and its codes; non-blocking connects               |
| [Network](sdk/network.md)     | TCP and UDP sockets over IPv4 and IPv6                                               |
| [Poll and select](sdk/io.md)  | Waiting on a few sockets                                                             |
| [Event loop](sdk/event.md)    | Waiting on many sockets, pipes, processes and timers                                 |
| [Threads](sdk/thread.md)      | pthreads on every target, plus monotonic timed waits and thread IDs                  |
| [Pipes](sdk/pipe.md)          | Byte pipes between threads and processes                                             |
| [Processes](sdk/process.md)   | Running programs, their stdio and exit codes; the environment                        |
| [Files](sdk/fs.md)            | File sizes over 2 GB; stdio for programs the kernel starts                           |
| [Time](sdk/time.md)           | Sleeping, the monotonic clock, the wall clock                                        |
| [Logging](sdk/logging.md)     | Levelled log lines                                                                   |
| [dino vs libc](sdk/libc.md)   | What the C library already does portably, and what needs `dino_*`                    |
| [Gotchas](sdk/gotchas.md)     | Platform differences the API can't hide                                              |
| [Upgrading](sdk/upgrading.md) | What changed between SDK versions, with before-and-after code                        |

The headers in [`sdk/include/dino/`](../sdk/include/dino) are the reference for every function. A
generated API reference is planned in [#98](https://github.com/heavycircle/dionysus/issues/98).
