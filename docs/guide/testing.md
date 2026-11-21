# Testing

```sh
dino test                  # build and run the tests for this machine
dino test arm64 win64      # under qemu and wine
dino test --all            # every target
dino test -R network       # only tests whose name matches a regex
dino test --memcheck       # under valgrind
```

Tests are executables defined with `dino_test()` and run with CTest. Each one links the SDK and
[utest.h](https://github.com/sheredom/utest.h), a single-header test framework that the SDK
provides. On Linux targets, every test also fails if it leaks memory.

## Writing tests

The `dino init` template turns every `tests/test_*.c` into a test executable:

```cmake
file(GLOB TESTS CONFIGURE_DEPENDS tests/test_*.c)
foreach(test ${TESTS})
    get_filename_component(test_name ${test} NAME_WE)
    dino_test(${test_name} ${test})
    target_link_libraries(${test_name} PRIVATE core)   # the code under test, if it's a library
endforeach()
```

A test file is a set of `UTEST` cases followed by `UTEST_MAIN()`:

```c
#include <string.h>
#include <utest.h>

static int add(int a, int b)
{
    return a + b;
}

UTEST(math, adds)
{
    EXPECT_EQ(5, add(2, 3));
    EXPECT_NE(0, add(-1, 2));
}

UTEST(strings, compares)
{
    const char *name = "dino";
    ASSERT_TRUE(name != NULL); // ASSERT_* stops the case, EXPECT_* carries on
    EXPECT_STREQ("dino", name);
    EXPECT_EQ(4u, strlen(name));
}

UTEST_MAIN()
```

- `EXPECT_*` records a failure and continues the case. `ASSERT_*` stops it.

- The checks are `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`, `TRUE`, `FALSE`, `NEAR` (floats within a
  tolerance), `STREQ`, `STRNE`, `STRNEQ`/`STRNNE` (the first *n* characters) and `MEMEQ`, each as
  `EXPECT_` and `ASSERT_`, and each with a `_MSG` form that adds a message.

- Fixtures share setup and teardown across cases:

  ```
  struct server { dino_socket_t s; };
  UTEST_F_SETUP(server) { utest_fixture->s = dino_socket_create(...); }
  UTEST_F_TEARDOWN(server) { dino_socket_close(utest_fixture->s); }
  UTEST_F(server, accepts) { ... utest_fixture->s ... }
  ```

- A test executable passes when every case passes. CTest shows one line per executable, and its
  output lists each case.

Keep the program's logic in a library that both the program and the tests link, as
[`examples/kv_app`](../../examples/kv_app) does, so the tests exercise exactly the code that ships.

## Running tests

- **Building:** `dino test` builds first (the same as `dino build` for those targets), then runs
  CTest in each target's build directory.
- **Foreign targets** run under qemu-user or wine, exactly like `dino run`.
- **In parallel:** test executables run in parallel (`-j`, default: the number of CPUs). Tests that
  open sockets should each use their own ports.
- **Filtering:** `-R <regex>` runs only the test executables whose names match.
- **ctest arguments:** anything after `--` goes to `ctest`, e.g.
  `dino test -- --repeat until-fail:20` to hunt a flaky test, or `dino test -- --output-on-failure`.
- **Exit code:** non-zero if any test failed on any target, with a summary per target at the end.

## The leak check

On the Linux targets, every `dino_test()` executable is linked with an allocation counter. If
anything is still allocated when the test exits, it fails:

```
dino leak check: 1 allocation(s) still live at exit
```

It counts every `malloc`, `calloc`, `realloc`, `aligned_alloc`, `posix_memalign` and `free`, the C
library's own included (`strdup`, `fopen`, ...), so a missing `free` or `fclose` is caught.

- **When it checks:** at the very end of the process, after `atexit` handlers and the destructors of
  C++ globals and function-local statics. Memory a global frees in its destructor doesn't count.
- **What it ignores:** the environment. libc keeps the strings and the `environ` array that
  `setenv`, `putenv`, `unsetenv` and `clearenv` allocate for the life of the process, so they aren't
  counted.
- **C++:** C++ tests get the same check, including in a project that enables only `CXX`. `new` and
  `delete` go through `malloc` and `free`, so they're counted too.
- **Windows targets** have no leak check: the C runtime allocates inside a DLL there.
- **Turning it off:** `DINO_LEAK_CHECK=0` in the environment disables it for one run.
  `DINO_NO_LEAK_CHECK` (set before `project()`) leaves it out of the build.

To find *where* the leak comes from, use `--memcheck`.

## `--memcheck`: valgrind

```sh
dino test --memcheck
```

It builds the tests again in `build/memcheck/`, for x86_64 Linux against the dynamic glibc (valgrind
can't see the allocator inside a static binary), and runs each test under valgrind. As well as
leaks, with the stack that allocated them, it catches reads and writes out of bounds, use after free
and use of uninitialized memory:

```
2 bytes in 1 blocks are definitely lost
   at malloc
   by kv_set (src/kv.c:59)
   by utest_run_kv_delete (tests/test_kv.c:42)
```

It needs an x86_64 Linux host with valgrind installed. The Docker image has it. `dino clean memcheck`
removes `build/memcheck/`.

## In CI

Run the tests for every target in the Docker image, which has all the runners. Add warnings as
errors through `CFLAGS`:

```sh
docker run --rm -e CFLAGS="-Wall -Wextra -Werror" -v "$PWD:/work" \
    ghcr.io/heavycircle/dionysus:stable dino test --all
```

See [Docker](docker.md#ci) for a GitHub Actions workflow.
