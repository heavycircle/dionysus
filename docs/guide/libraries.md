# Organization libraries

Every dino project can link `dino_core` and `dino_util` without vendoring the SDK. Organization
libraries extend that to your own code: list a library's directory in `DINO_LIBRARY_PATH` once, per
user, machine or CI image, and every dino project can link it with one `target_link_libraries`
line. It's built for each target with the same toolchain and flags as the SDK.

Typical uses are an organization's shared code (logging conventions, configuration loading,
internal protocols) and third-party libraries you want every project to use at one pinned version.

- [Quick start](#quick-start)
- [Registering libraries](#registering-libraries)
- [Writing a library](#writing-a-library)
- [Testing a library](#testing-a-library)
- [Example: an organization's libraries](#example-an-organizations-libraries)
- [Example: wrapping a third-party library](#example-wrapping-a-third-party-library)
- [Docker and CI](#docker-and-ci)
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)

## Quick start

The repository has a working pair: [`examples/org_library`](../../examples/org_library) (a small
`key = value` config reader with tests) and [`examples/org_app`](../../examples/org_app), which uses it.

```sh
export DINO_LIBRARY_PATH=$PWD/examples/org_library

dino doctor                          # lists org_library under "Libraries"
dino run arm64 -C examples/org_app  # builds org::config for arm64, links it, runs under qemu
dino test --all -C examples/org_library
```

All `org_app` needs is a single line in its `CMakeLists.txt`:

```cmake
dino_executable(org_app main.c)
target_link_libraries(org_app PRIVATE org::config)
```

## Registering libraries

`DINO_LIBRARY_PATH` is a list of library directories, separated like `PATH`: `:` on Linux, `;` on
Windows.

```sh
export DINO_LIBRARY_PATH=/opt/acme/acme_log:/opt/acme/acme_proto
```

- Each entry is a directory with a `CMakeLists.txt`. Relative entries are resolved against the
  directory you run `dino` from, and symlinks are followed.

- Directory names must be unique, because each library builds in `build/<target>/_dino_libs/<name>`.
  An entry listed twice is used once.

- Empty entries are ignored. A path containing `;` is refused, since CMake would split it in two.

- `dino build`, `run` and `test` check every entry and stop with an error naming the first bad one.
  `dino doctor` reports them all:

  ```
  Libraries (DINO_LIBRARY_PATH):
    [ ok ] acme_log   /opt/acme/acme_log
    [MISS] acme_proto '/opt/acme/acme_proto' has no CMakeLists.txt
  ```

- Changing or unsetting `DINO_LIBRARY_PATH` reconfigures the project on the next build.

Only local directories are supported. Getting the libraries onto a machine (a git clone, a package,
a volume mount, a derived Docker image) is up to you; see [Docker and CI](#docker-and-ci).

Without `dino`, pass the list to CMake yourself, as a CMake list (`;`-separated), next to the
settings shown in [CONTRIBUTING.md](../../CONTRIBUTING.md#c-projects-and-examples):

```sh
cmake ... -DCMAKE_PROJECT_INCLUDE=$SDK/cmake/Dino.cmake "-DDINO_LIBRARY_PATH=/opt/acme/acme_log;/opt/acme/acme_proto"
```

## Writing a library

A library is a directory with a `CMakeLists.txt` that defines one or more static library targets.
It needs no manifest and no install step. A good layout:

```
acme_log/
  CMakeLists.txt
  include/acme/log.h      public headers, under a directory named after the organization
  src/log.c
  tests/test_log.c        run by `dino test -C acme_log`, never by the projects using it
```

```cmake
cmake_minimum_required(VERSION 3.20)
project(AcmeLog C)

if(DINO_SDK_VERSION VERSION_LESS 0.6)
    message(FATAL_ERROR "acme_log needs the dino SDK 0.6 or newer (found ${DINO_SDK_VERSION})")
endif()

add_library(acme_log STATIC src/log.c)
add_library(acme::log ALIAS acme_log)
target_include_directories(acme_log PUBLIC include)
target_link_libraries(acme_log PUBLIC dino_util)

dino_test(test_log tests/test_log.c LIBRARIES acme::log)
```

Rules and recommendations:

- **Static libraries.** Every dino executable is fully static, so define `STATIC` (or `INTERFACE`,
  for header-only) libraries.
- **Link the SDK like any project.** `dino_core`, `dino_util` and the `dino_*` CMake functions are
  available in a library's `CMakeLists.txt`. Link them `PUBLIC` if your headers include SDK headers,
  otherwise `PRIVATE`.
- **Name targets with a prefix, and add a namespaced alias.** All libraries share one target
  namespace with the SDK and the project. Defining a name that already exists (`dino_core`, or a
  target another library defined) stops the configure with CMake's "another target with the same
  name already exists" error. Projects linking `acme::log` rather than `acme_log` also get a clear
  error when the library isn't registered, because CMake treats names containing `::` as targets
  that must exist.
- **Put public headers under a prefix directory** (`include/acme/log.h`, included as
  `<acme/log.h>`) so they can't collide with a project's own headers or another library's.
- **Keep settings on targets.** Use `target_compile_options` and `target_compile_definitions`, not
  `CMAKE_C_FLAGS` or `add_compile_options`. Optimization, stripping and the target triple come from
  dino's toolchain already, the same as for the SDK.
- **Check the SDK version** with `DINO_SDK_VERSION` (for example `0.6.0`) if you rely on newer APIs.
- **Depend on other registered libraries by name.** `target_link_libraries(acme_proto PUBLIC acme::log)` works whatever order the entries are in, as long as both are registered.
- **Give `dino_test()` the libraries to link.** `dino_test(test_log tests/test_log.c LIBRARIES acme::log)` defines the test only when the library is the project being tested; in a project that uses the library it creates nothing. Without `LIBRARIES` the test target is created there too, so that your own `target_link_libraries(test_log ...)` has something to work on, and its name is then taken in every project: two libraries, or a library and a project, can't both have a `test_main`.
- **Guard everything else that is only for the library itself.** Calls that need the test to exist, such as `set_tests_properties(test_log PROPERTIES TIMEOUT 300)`, and tools or examples you don't want in other projects, go inside `if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)` (or `if(PROJECT_IS_TOP_LEVEL)`, from CMake 3.21).
- **Tests and tools are yours alone.** Nothing in a library builds unless something links it, and executables defined in a library never show up in a consuming project's `dino run`. Tests registered with plain `add_test()`, as third-party code does, are disabled in the projects that use the library: `dino test` lists them as `Disabled` and doesn't run them.

## Testing a library

A library is also an ordinary dino project, so test it the same way as any other:

```sh
dino test -C /opt/acme/acme_log            # host
dino test --all -C /opt/acme/acme_log      # all 7 targets, under qemu-user and wine
dino test --memcheck -C /opt/acme/acme_log # under valgrind
```

On Linux targets every `dino_test()` executable also fails if anything is still allocated at exit,
exactly as in the SDK's own tests. This works with the library still on `DINO_LIBRARY_PATH`, because
a library that is the project being built isn't added a second time.

The same holds for another copy of it: when you test a checkout while the installed copy is on `DINO_LIBRARY_PATH` (a CI image with the libraries baked in, say), the entry whose `project()` name matches the project being built is left out, and `dino test -v` says so. The other libraries on the path are used as usual.

## Example: an organization's libraries

Acme keeps two libraries in one repository, which every developer clones to `~/src/acme-dino` and
the CI image copies to `/opt/acme`:

```
acme-dino/
  acme_log/                logging conventions on top of dino_util's logger
    CMakeLists.txt
    include/acme/log.h
    src/log.c
    tests/test_log.c
  acme_proto/              Acme's wire protocol; uses acme_log and dino_core sockets
    CMakeLists.txt
    include/acme/proto.h
    src/frame.c
    src/client.c
    tests/test_frame.c
```

`acme_proto/CMakeLists.txt` depends on the other library by name:

```cmake
cmake_minimum_required(VERSION 3.20)
project(AcmeProto C)

add_library(acme_proto STATIC src/frame.c src/client.c)
add_library(acme::proto ALIAS acme_proto)
target_include_directories(acme_proto PUBLIC include)
target_link_libraries(acme_proto PUBLIC acme::log dino_core)

file(GLOB TESTS CONFIGURE_DEPENDS tests/test_*.c)
foreach(test ${TESTS})
    get_filename_component(test_name ${test} NAME_WE)
    dino_test(${test_name} ${test} LIBRARIES acme::proto)
endforeach()
```

Every developer registers both once, in their shell profile:

```sh
export DINO_LIBRARY_PATH=$HOME/src/acme-dino/acme_log:$HOME/src/acme-dino/acme_proto
```

and any Acme service, freshly made with `dino init`, links what it uses:

```cmake
dino_executable(billing ${SOURCES})
target_link_libraries(billing PRIVATE acme::proto)   # brings acme::log and dino_core with it
```

`dino build --all` then builds `acme_log` and `acme_proto` for every target alongside the service.
Projects that don't link them don't build them.

## Example: wrapping a third-party library

The same mechanism pins one copy of a third-party library for every project. For
[cJSON](https://github.com/DaveGamble/cJSON) (MIT), keep its two source files next to a small
`CMakeLists.txt`:

```
cjson/
  CMakeLists.txt
  LICENSE                 cJSON's license, kept with its code
  cjson/cJSON.h           upstream, unmodified, at a recorded commit
  cJSON.c
```

```cmake
cmake_minimum_required(VERSION 3.20)
project(cJSON C)

add_library(cjson STATIC cJSON.c)
add_library(cjson::cjson ALIAS cjson)
# The headers sit under cjson/, so projects write #include <cjson/cJSON.h>.
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Third-party code: upstream warnings shouldn't fail builds that use CFLAGS=-Werror.
target_compile_options(cjson PRIVATE -w)
```

```sh
export DINO_LIBRARY_PATH=/opt/acme/acme_log:/opt/acme/acme_proto:/opt/third_party/cjson
```

A third-party library doesn't need to know about dino at all; it only needs a `CMakeLists.txt` that
defines a static target. If upstream already ships a suitable one, the directory can be the upstream
checkout itself. Its own tests don't run in your projects: `dino test` lists them as `Disabled`.

## Docker and CI

Inside the [Docker image](../../README.md#getting-dino), paths in `DINO_LIBRARY_PATH` are container
paths. Either mount the libraries:

```sh
docker run --rm -v "$PWD:/work" -v "$HOME/src/acme-dino:/opt/acme:ro" \
    -e DINO_LIBRARY_PATH=/opt/acme/acme_log:/opt/acme/acme_proto \
    ghcr.io/heavycircle/dionysus:stable dino build --all
```

or build a derived image that has them baked in, so CI and developers share exactly the same
libraries:

```dockerfile
FROM ghcr.io/heavycircle/dionysus:0.9.0
COPY acme-dino /opt/acme
ENV DINO_LIBRARY_PATH=/opt/acme/acme_log:/opt/acme/acme_proto
```

Mount libraries read-only if you like: their build output goes to the project's `build/` directory,
never to the library directory.

To test the libraries themselves in CI, run `dino test --all -C <library>` for each one, in the
same image.

## How it works

`dino` checks `DINO_LIBRARY_PATH` and passes it to CMake as `-DDINO_LIBRARY_PATH=<list>`.
`cmake/Dino.cmake`, which every dino build injects through `CMAKE_PROJECT_INCLUDE`, adds the SDK
and then each library:

```cmake
add_subdirectory(<library> ${CMAKE_BINARY_DIR}/_dino_libs/<name> EXCLUDE_FROM_ALL)
```

`EXCLUDE_FROM_ALL` is what keeps an unused library from being built. A library that is itself the
top-level project is skipped, and so is another copy of it (an entry with the same `project()` name).

While libraries are being added, `dino_test()` with `LIBRARIES` does nothing. Without it, it creates the test executable but doesn't register it with CTest, so it's built only if something asks for it, and never run. `add_test()` is overridden for the same stretch: the test is registered, so the library's own `set_tests_properties()` calls still work, and then marked `DISABLED`.

## Troubleshooting

| Message                                                                                           | Cause                                                                                                                                                      |
| ------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DINO_LIBRARY_PATH entry '...' does not exist`                                                    | A typo, or a host path used inside Docker.                                                                                                                 |
| `DINO_LIBRARY_PATH entry '...' has no CMakeLists.txt`                                             | The entry points at the repository, not at the library directory inside it.                                                                                |
| `... has the same directory name as '...'`                                                        | Two libraries named alike, e.g. `a/log` and `b/log` (or `a/Log` and `b/log` on Windows and macOS). Rename one directory.                                   |
| `Target "app" links to: acme::log but the target was not found`                                   | The library isn't on `DINO_LIBRARY_PATH` (check `dino doctor`), or its alias is spelled differently.                                                       |
| `add_library cannot create target "..." because another target with the same name already exists` | Two libraries, or a library and the SDK or the project, define the same target name.                                                                       |
| `dino_test(test_main): the library in ... already has a test target with this name`               | A library creates its test targets in every project. Give its `dino_test()` calls `LIBRARIES`, as described under [Writing a library](#writing-a-library). |
| `set_tests_properties Can not find test to add properties to: ...`                                | A library sets properties on its own test outside a guard; see [Writing a library](#writing-a-library).                                                    |
| `... needs the dino SDK 0.6 or newer`                                                             | The library's own version check: update `dino`.                                                                                                            |
