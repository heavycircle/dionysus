# Processes

`#include <dino/core/process.h>` · `dino_core`

Run other programs, talk to them through their stdin, stdout and stderr, and collect their exit
codes. On Linux this is `posix_spawn` and `waitpid`; on Windows, `CreateProcess`. The differences
that matter are handled for you:

- **Arguments** reach the child exactly as given. Windows has no argument array, only one command
  line, so the SDK quotes each argument by the rules the child's C runtime uses to split it again.
- **Exit codes** come back the same way: the code the program exited with, `128 + signal` if a signal
  ended it (Linux), and `DINO_EXIT_KILLED` (137) after `dino_process_kill` on both platforms.
- **Inheritance:** the child gets only its stdin, stdout and stderr, never the parent's other files,
  sockets or pipes.

The program you run is still platform-specific. There's no `/bin/sh` on Windows, so the examples
pick the shell per platform.

## Example: run a command and collect its output

```c
#include <stdio.h>
#include <string.h>

#include <dino/core/process.h>

#ifdef _WIN32
#define SHELL(cmd) { "cmd.exe", "/d", "/c", cmd, NULL }
#else
#define SHELL(cmd) { "/bin/sh", "-c", cmd, NULL }
#endif

int main(void)
{
    char *const       argv[] = SHELL("echo out&& echo err 1>&2&& exit 3");
    dino_process_run_opts_t   opts   = { 0 };
    dino_process_result_t result;
    opts.timeout_ms = 10000; // killed if it takes longer

    int rc = dino_process_run(argv, &opts, &result);
    printf("rc %d, exit code %d\n", rc, result.exit_code);
    printf("stdout: %s", result.out);
    printf("stderr: %s", result.err);

    int ok = rc == 0 && result.exit_code == 3 && strncmp(result.out, "out", 3) == 0
        && strncmp(result.err, "err", 3) == 0;
    dino_process_result_free(&result);
    return ok ? 0 : 1;
}
```

## Running to completion: `dino_process_run`

`dino_process_run(argv, opts, &result)` starts the program, feeds it `opts->input`, collects
everything it writes to stdout and stderr, and waits for it to exit.

- **No deadlocks:** it reads both outputs and writes the input at the same time, so a child that
  fills one pipe while the parent is busy with another can't deadlock.
- **Options:** `dino_process_run_opts_t` has `cwd`, `env`, `input`/`input_len` (`NULL`: stdin is empty) and
  `timeout_ms` (`0`: no limit; otherwise the child is killed after that long).
- **Results:** `dino_process_result_t` has `out`/`out_len`, `err`/`err_len` (both NUL-terminated) and
  `exit_code`.
- **Return value:** `0` when the child ran to the end, `DINO_TIMEOUT` when it was killed for taking
  too long (`result` still holds what it wrote first), or `-1` if it couldn't be started. Call
  `dino_process_result_free` in every case.

## Talking to a running program: `dino_process_spawn`

For a long-running child, or to stream its output, spawn it and use the pipes:

```
dino_process_spawn_opts_t    opts = { 0 };
dino_process_pipes_t pipes;
opts.stdout_mode = DINO_STDIO_PIPE;      // pipes.out: read the child's stdout
opts.stdin_mode  = DINO_STDIO_NULL;      // reads see end of file

dino_process_t child;
if (dino_process_spawn(&child, argv, &opts, &pipes) != 0) { ... }

char buf[256];
int  n;
while ((n = dino_pipe_read(pipes.out, buf, sizeof(buf))) > 0)
    fwrite(buf, 1, n, stdout);
dino_pipe_close(pipes.out);

int code;
dino_process_wait(child, &code);
```

| Function                                          | Does                                                                                                   | Returns                             |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ----------------------------------- |
| `dino_process_spawn(&p, argv, opts, &pipes)`      | Start `argv[0]` with arguments. `opts` and `pipes` may be `NULL`.                                      | `0`, `-1` if it couldn't be started |
| `dino_process_wait(p, &code)`                     | Wait for it to end, store how it ended, release it.                                                    | `0`, `-1`                           |
| `dino_process_wait_timeout(p, timeout_ms, &code)` | The same, giving up after `timeout_ms`. On `DINO_TIMEOUT` it keeps running and the handle stays valid. | `0`, `DINO_TIMEOUT`, `-1`           |
| `dino_process_kill(p)`                            | End it now (`SIGKILL` / `TerminateProcess`). Still wait for it afterwards.                             | `0`, `-1`                           |
| `dino_process_id(p)`                              | Its OS process ID, for logs.                                                                           | the ID                              |

- **`argv[0]`:** a path, or a bare name looked up on `PATH`. Windows also adds `.exe` and looks in a
  few system directories first.
- **Stdio modes:** each of `stdin_mode`, `stdout_mode` and `stderr_mode` is `DINO_STDIO_INHERIT` (the
  parent's own, the default), `DINO_STDIO_NULL` (`/dev/null` or `NUL`) or `DINO_STDIO_PIPE` (a new
  [pipe](pipe.md), whose parent end appears in `pipes.in`, `.out` or `.err`).
- **Wait for every process exactly once.** On Linux an unwaited child stays a zombie.
- **Read every pipe you asked for.** A child that fills a 64 KB stderr pipe nobody reads blocks
  forever, while the parent waits on stdout. Read both (or use `dino_process_run`), or send the one
  you don't need to `DINO_STDIO_NULL`.
- **`opts.env`:** a complete environment for the child as `"NAME=value"` strings ending with `NULL`;
  `NULL` copies the parent's. Many Windows programs need `SYSTEMROOT` in it.
- **Waiting in an event loop:** to wait for a process along with sockets and timers, use
  [`dino_event_add_process`](event.md#pipes-and-processes).

## This process and the environment

| Function                        | Does                                                                                                                                                           | Returns                   |
| ------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------- |
| `dino_process_current_id()`     | This process's ID.                                                                                                                                             | the ID                    |
| `dino_env_get(name, buf, size)` | Read a variable, truncated to fit `buf` like `snprintf`. A return of `size` or more means it was cut; `buf` may be `NULL` with `size` 0 to ask for the length. | its length, `-1` if unset |
| `dino_env_set(name, value)`     | Set a variable. Children spawned afterwards with a `NULL` env see it.                                                                                          | `0`, `-1`                 |
| `dino_env_unset(name)`          | Remove a variable (removing one that isn't set succeeds).                                                                                                      | `0`, `-1`                 |

All strings are UTF-8, including on Windows, where the SDK converts them for the wide-character
APIs. Changing the environment isn't safe while other threads read or change it.

See also: [`examples/process_example`](../../examples/process_example/main.c).
