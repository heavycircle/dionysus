# Your first project

```sh
dino init hello
cd hello
dino run
```

```
Hello from hello!
```

That's a complete project: `dino run` configured it, built it for this machine, and ran it. Its progress (`==> x86_64 ...`, the build, the binary's size) goes to stderr, so stdout carries only the program's output.

## What `dino init` creates

```
hello/
  CMakeLists.txt          builds every .c file under src/, plus one test per tests/test_*.c
  src/main.c              the program
  tests/test_main.c       a first test, using utest.h
  .gitignore              ignores build/ and compile_commands.json
  .clang-format           the SDK's formatting rules
  .clang-tidy             the SDK's clang-tidy checks, including the portability warnings
  compile_commands.json   for clangd and other editors
```

`dino init` never overwrites a file that already exists, so it's safe to run in a directory that
already has some of these. In a directory that already has a `CMakeLists.txt` it treats the project as yours: it adds whichever of `.gitignore`, `.clang-format` and `.clang-tidy` are missing and leaves `src/` and `tests/` alone, so a `main.c` you renamed or a sample test you deleted doesn't come back. It also configures the project once, so `compile_commands.json` exists
before the first build and your editor works straight away. `--no-configure` skips that step.

The project, and its program, are named after the directory. A name that CMake or dino already
uses for a target is refused before anything is written: `all`, `clean`, `help`, `install`,
`package`, `preinstall`, `test`, `test_main`, CMake's other target names (`package_source`, `edit_cache`, `rebuild_cache`, `ALL_BUILD`, ...), anything starting with `dino_`, and the names of directories in `build/<target>/` (`CMakeFiles`, `Testing`, `_dino`, `_dino_libs`). An existing project is never refused for its name.

The generated `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(hello C)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)

# dino_executable() is add_executable() plus linking the dino SDK (dino_core, dino_util).
dino_executable(hello ${SOURCES})
target_include_directories(hello PRIVATE src)
# Libraries listed in DINO_LIBRARY_PATH are linked by target name, e.g.
# target_link_libraries(hello PRIVATE org::config)

# Every tests/test_*.c is a test executable (utest.h); `dino test` runs them.
file(GLOB TESTS CONFIGURE_DEPENDS tests/test_*.c)
foreach(test ${TESTS})
    get_filename_component(test_name ${test} NAME_WE)
    dino_test(${test_name} ${test})
endforeach()
```

It's plain CMake. `dino_executable()` and `dino_test()` are the only dino additions, and there's no
path to the SDK anywhere: `dino` provides the SDK to every project it builds. New `.c` files under
`src/` are picked up on the next build. [Projects](projects.md) covers growing it further.

## Using the SDK

`src/main.c` starts as plain C. Calls that differ between Linux and Windows go through the `dino_*`
API instead. Here the program sleeps and measures how long it slept, the same way on every target:

```c
#include <stdio.h>

#include <dino/core/time.h>

int main(void)
{
    uint64_t start = dino_time_monotonic_ns();
    dino_sleep_ms(50);
    uint64_t elapsed_ms = (dino_time_monotonic_ns() - start) / 1000000;

    printf("slept for about %llu ms\n", (unsigned long long)elapsed_ms);
    return elapsed_ms >= 50 ? 0 : 1;
}
```

The [SDK overview](../sdk/README.md) lists every module. If you call a platform function that has
a `dino_*` replacement, such as `fork()` or `usleep()`, the build warns and names the
replacement (see [portability warnings](portability-warnings.md)).

## Building for every target

```sh
dino build --all
```

```
==> arm (arm-linux-musleabihf, release)
    build/arm/hello (7.5 KiB)
    build/arm/test_main (37.2 KiB)
==> arm64 (aarch64-linux-musl, release)
    build/arm64/hello (7.5 KiB)
...
==> win64 (x86_64-windows-gnu, release)
    build/win64/hello.exe (57.5 KiB)
    build/win64/test_main.exe (63.0 KiB)
==> x86_64 (x86_64-linux-musl, release)
    build/x86_64/hello (7.3 KiB)
    build/x86_64/test_main (36.2 KiB)

==> summary: 7 succeeded, 0 failed
    ok       arm
    ok       arm64
...
    ok       x86_64
```

Each target gets its own `build/<target>/` directory. The Linux binaries are fully static, so they
run on any Linux kernel for their architecture with nothing installed. The Windows ones are
ordinary `.exe` files that only need the DLLs every Windows installation has.

To run one of them on this machine, through qemu or wine as needed:

```sh
dino run arm64
dino run win64
```

## Running the tests

```sh
dino test          # this machine
dino test --all    # every target, under qemu and wine
```

`tests/test_main.c` uses [utest.h](https://github.com/sheredom/utest.h), which the SDK provides. On
Linux targets every test also fails if it leaks memory. [Testing](testing.md) has the details.

## Next

- [Building](building.md): targets, debug builds, running foreign binaries, passing arguments
- [Projects](projects.md): more sources, directories, libraries and executables
- [Editors](editors.md): clangd and clang-tidy
