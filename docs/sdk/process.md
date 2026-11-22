# Processes

`#include <dino/core/process.h>` · `dino_core`

Run other programs, talk to them through their stdin, stdout and stderr, and collect their exit
codes. On Linux this is `posix_spawn` and `waitpid`; on Windows, `CreateProcess`. The differences
that matter are handled for you:

- **Arguments** reach the child exactly as given. Windows has no argument array, only one command
  line, so the SDK quotes each argument by the rules the child's C runtime uses to split it again. The exception is a Windows program that reads its command line itself; `cmd.exe` is one, as described under the function table below.
- **How it ended** comes back the same way, as a `dino_process_status_t`: `signal` is `0` when the program exited and `code` is its exit code. A signal on Linux, or a crash on Windows, sets `signal` to the matching `<signal.h>` number (`SIGSEGV` for an access violation, ...), so `if (status.signal)` is a portable crash test. After `dino_process_kill`, both platforms report `signal` `9` and `code` `DINO_EXIT_KILLED` (137).
- **Inheritance:** the child gets only its stdin, stdout and stderr, never the parent's other files,
  sockets or pipes. That includes descriptors you opened yourself without close-on-exec: on Linux
  the SDK closes them in the child by listing `/proc/self/fd`. Two gaps remain there: one another
  thread opens during the spawn can still get through, and without `/proc` they're inherited.

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
    const char *const       argv[] = SHELL("echo out&& echo err 1>&2&& exit 3");
    dino_process_run_opts_t opts   = { 0 };
    dino_process_result_t   result;
    opts.timeout_ms = 10000; // killed if it takes longer

    int rc = dino_process_run(argv, &opts, &result);
    printf("rc %d, exit code %d\n", rc, result.status.code);
    printf("stdout: %s", result.out);
    printf("stderr: %s", result.err);

    int ok = rc == 0 && result.status.code == 3 && strncmp(result.out, "out", 3) == 0
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
  `timeout_ms` (`0`: no limit; otherwise it gives up after that long, killing the child if it's
  still running).
- **Results:** `dino_process_result_t` has `out`/`out_len`, `err`/`err_len` (both NUL-terminated) and
  `status`, how the child ended.
- **Return value:** `0` when the child ran to the end, `DINO_TIMEOUT` when it was killed for taking
  too long (`result` still holds what it wrote first), or `-1` if it couldn't be started. Call
  `dino_process_result_free` in every case.
- **Background processes:** a process the child started that keeps stdout, stderr or stdin open
  keeps `dino_process_run` waiting after the child exits. With a timeout it stops at the deadline
  and returns `DINO_TIMEOUT`, with what was read so far and the child's own `status`. Without
  one it waits as long as they stay open, as a shell's `$(...)` does.

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

dino_process_status_t status;
dino_process_wait(child, &status);
```

| Function                                            | Does                                                                                                   | Returns                             |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ----------------------------------- |
| `dino_process_spawn(&p, argv, opts, &pipes)`        | Start `argv[0]` with arguments. `opts` and `pipes` may be `NULL`.                                      | `0`, `-1` if it couldn't be started |
| `dino_process_wait(p, &status)`                     | Wait for it to end, store how it ended, release it.                                                    | `0`, `-1`                           |
| `dino_process_wait_timeout(p, timeout_ms, &status)` | The same, giving up after `timeout_ms`. On `DINO_TIMEOUT` it keeps running and the handle stays valid. | `0`, `DINO_TIMEOUT`, `-1`           |
| `dino_process_kill(p)`                              | End it now (`SIGKILL` / `TerminateProcess`); `0` if it already exited. Still wait for it afterwards.   | `0`, `-1`                           |
| `dino_process_id(p)`                                | Its OS process ID, for logs.                                                                           | the ID                              |

- **`argv[0]`:** a path, or a bare name looked up on `PATH`. Windows also adds `.exe` and looks in a
  few system directories first. A relative path such as `./tool` or `bin/tool` is found from `cwd`
  when that is set, on both platforms (Windows adds `.exe` to it too).
- **Batch files:** a `.bat` or `.cmd` file runs under `cmd.exe`, which reads `&`, `|`, `<`, `>` and
  `^` in its command line as commands of its own. The SDK quotes every argument so they stay literal,
  and refuses (returns `-1` for) an argument containing `"`, `%` or a line break, which quoting can't
  protect. The script gets each argument with its quotes: `%1` is `"a b"`, `%~1` is `a b`. Passing
  untrusted input to a batch file is still risky, since the script itself may expand `%~1` unquoted.
- **Stdio modes:** each of `stdin_mode`, `stdout_mode` and `stderr_mode` is `DINO_STDIO_INHERIT` (the
  parent's own, the default), `DINO_STDIO_NULL` (`/dev/null` or `NUL`) or `DINO_STDIO_PIPE` (a new
  [pipe](pipe.md), whose parent end appears in `pipes.in`, `.out` or `.err`).
- **Wait for every process exactly once.** On Linux an unwaited child stays a zombie.
- **Leave `SIGCHLD` alone, and let nothing else wait for the child** (Linux). If the program ignores `SIGCHLD` (`SIG_IGN` or `SA_NOCLDWAIT`), or other code in it calls `wait()` or `waitpid(-1, ...)`, the exit status goes there. Every wait then returns `-1` although the child ran, `dino_process_run` included, and the exit code is lost. The process ID may also be reused by then, so don't `dino_process_kill` that handle afterwards.
- **Killing ends one process, not what it started.** `dino_process_kill` and the timeout of `dino_process_run` end the process you spawned. Programs it started keep running: whatever a `sh -c` command launched without `exec`, and everything a `.bat` or `.cmd` file started, because the process you hold there is the `cmd.exe` that runs the script.
- **`cmd.exe` and quotes:** `cmd.exe` parses its command line itself and doesn't understand the `\"` that the SDK's quoting produces. `{ "cmd.exe", "/d", "/c", command, NULL }` works as long as `command` contains no `"`; a command with a quoted path in it (`dir "C:\Program Files"`) arrives mangled. Put such a command in a `.bat` file and run that.
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
