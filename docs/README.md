# Dionysus documentation

How to build your own projects with `dino` and the `dino_*` SDK. To work on Dionysus itself, see
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Guide

New to dino? Read the first two pages in order.

| Page | What it covers |
| --- | --- |
| [Installing](guide/install.md) | The release binary, Zig and CMake, or the Docker image; reading `dino doctor` |
| [Your first project](guide/first-project.md) | `dino init`, the generated files, building, running and testing |
| [Building](guide/building.md) | Targets, output, debug and release, running foreign binaries, options, rebuilding |
| [Projects](guide/projects.md) | The `CMakeLists.txt`: sources, include paths, several executables, libraries, SDK options |
| [Testing](guide/testing.md) | `dino_test()` and utest.h, the leak check, `--memcheck`, tests in CI |
| [Docker](guide/docker.md) | The image, tags, file ownership, CI workflows, derived images |
| [Editors](guide/editors.md) | `compile_commands.json`, clangd, clang-tidy and clang-format, debugging |
| [Organization libraries](guide/libraries.md) | Making your own libraries available to every project with `DINO_LIBRARY_PATH` |
| [Portability warnings](guide/portability-warnings.md) | The warnings for platform calls that have a `dino_*` replacement, and how to opt out |
| [Troubleshooting](guide/troubleshooting.md) | Common error messages and what to do about them |
