# Projects

A dino project is an ordinary CMake project. `dino` adds the SDK and a few functions to every
project it builds, through CMake's `CMAKE_PROJECT_INCLUDE` hook, so a `CMakeLists.txt` never needs
to know where the SDK is:

| Provided | What it is |
| --- | --- |
| `dino_executable(name sources...)` | `add_executable()` plus linking `dino_util` (and with it `dino_core`) |
| `dino_test(name sources...)` | a test executable with utest.h and the leak check, registered with CTest (see [Testing](testing.md)) |
| `dino_core` | the portability layer: sockets, threads, processes, pipes, the event loop, ... |
| `dino_util` | extras built on it, such as logging |
| `DINO_SDK_VERSION` | the SDK's version, for example `0.6.0` |

Everything else is plain CMake, so any CMake guide applies. This page covers what most projects
need.

## Sources

The `dino init` template builds every `.c` file under `src/`:

```cmake
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)
dino_executable(hello ${SOURCES})
target_include_directories(hello PRIVATE src)
```

`CONFIGURE_DEPENDS` makes the next build notice files you add or remove. To list sources explicitly
instead:

```cmake
dino_executable(hello src/main.c src/config.c src/net/server.c)
```

## Include paths and definitions

```cmake
target_include_directories(hello PRIVATE src include)
target_compile_definitions(hello PRIVATE APP_VERSION="1.2.0" ENABLE_METRICS)
target_compile_options(hello PRIVATE -Wall -Wextra)
```

Set flags on targets rather than through `CMAKE_C_FLAGS`. The target triple, optimization and
stripping already come from dino's toolchain.

## Several executables

Call `dino_executable()` once per program. Code they share belongs in a static library that each of
them links, so it's compiled once:

```cmake
add_library(core STATIC src/core/store.c src/core/protocol.c)
target_include_directories(core PUBLIC src/core)
target_link_libraries(core PUBLIC dino_core)

dino_executable(server src/server_main.c)
target_link_libraries(server PRIVATE core)

dino_executable(client src/client_main.c)
target_link_libraries(client PRIVATE core)
```

`dino build` builds both. `dino run` asks which one to run (`dino run --exe server`).
[`examples/kv_app`](../../examples/kv_app) is a complete project laid out this way, with tests that
link the same library.

## Subdirectories

A large project can give each part its own `CMakeLists.txt`:

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(app C)
add_subdirectory(lib/storage)
dino_executable(app src/main.c)
target_link_libraries(app PRIVATE storage)
```

```cmake
# lib/storage/CMakeLists.txt
add_library(storage STATIC storage.c)
target_include_directories(storage PUBLIC include)
target_link_libraries(storage PUBLIC dino_core)
```

## Libraries

Everything links statically, so libraries need to be static (`STATIC`), or header-only (`INTERFACE`).

- **Your own code or vendored third-party code** in the repository: `add_library()` or
  `add_subdirectory()`, as above. Silence warnings in code you don't own with
  `target_compile_options(vendored PRIVATE -w)`.
- **Libraries shared across many projects:** register them once in `DINO_LIBRARY_PATH`, and every
  project links them by name. See [Organization libraries](libraries.md).
- **System libraries** come from the target's C library, not the host's:
  - On Linux, musl's libc already contains the math and thread functions, so `-lm` and `-pthread`
    aren't needed (and are harmless).
  - On Windows, MinGW-w64 provides the usual import libraries; `dino_core` already links `ws2_32`.

  ```cmake
  if(WIN32)
      target_link_libraries(app PRIVATE user32 advapi32)
  endif()
  ```

- **Host packages** (`find_package()` for something installed by apt) won't work for foreign targets:
  they're built for the host and usually not static. Build such libraries from source as part of the
  project, or register them as organization libraries.

## Platform-specific code

Prefer the `dino_*` API, which hides the difference. When a project really needs its own platform
code, split it by file rather than by `#ifdef` inside a function:

```cmake
if(WIN32)
    target_sources(app PRIVATE src/platform_win32.c)
else()
    target_sources(app PRIVATE src/platform_posix.c)
endif()
```

In C, `_WIN32` is defined for both Windows targets and `__linux__` for the Linux ones.

## SDK options

These change how the SDK is built, so they must be set **before `project()`**, which is where dino
adds the SDK. They can also be passed as `dino build -- -D<OPTION>=ON`.

```cmake
cmake_minimum_required(VERSION 3.15)
set(DINO_NO_PLATFORM_WARNINGS ON)
project(app C)
```

| Option | Effect |
| --- | --- |
| `DINO_NO_AUTO_RUNTIME` | don't run the SDK's setup before `main()` (see [runtime](../sdk/runtime.md)) |
| `DINO_NO_LEAK_CHECK` | don't link the leak check into `dino_test()` executables |
| `DINO_NO_PLATFORM_WARNINGS` | don't warn about platform calls that have a `dino_*` replacement |

## Using the SDK from an existing project

A project that already has its own `CMakeLists.txt` builds with `dino build` unchanged. To use the
SDK, link it:

```cmake
target_link_libraries(existing_app PRIVATE dino_core)   # or dino_util for logging too
```

Don't copy the SDK into the project or `add_subdirectory()` it: `dino` already provides it, at the
version that matches the `dino` binary.

## Building with plain CMake

`dino` adds these CMake settings, and nothing else. They're listed under
[Building without dino](building.md#without-dino).
