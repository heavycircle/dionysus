# Logging

`#include <dino/util/logging.h>` · `dino_util`

Levelled, timestamped log lines on stderr, safe from any thread. Part of `dino_util`, which
`dino_executable()` links. With a plain `target_link_libraries`, link `dino_util` rather than
`dino_core`.

## Example

```c
#include <stdio.h>

#include <dino/util/logging.h>

int main(void)
{
    DINO_LOG_INFO("starting, pid file at %s", "/run/app.pid");
    DINO_LOG_DEBUG("compiled out of release builds, the default, so not below");
    DINO_LOG_WARN("disk %d%% full", 91);

    dino_log_set_level(DINO_LOG_LEVEL_ERROR);
    DINO_LOG_WARN("not shown: below the level");
    DINO_LOG_ERROR("shown: %s", "connection lost");

    return dino_log_get_level() == DINO_LOG_LEVEL_ERROR ? 0 : 1;
}
```

```
14:03:22.418 [I] main.c:7: starting, pid file at /run/app.pid
14:03:22.418 [W] main.c:9: disk 91% full
14:03:22.418 [E] main.c:13: shown: connection lost
```

Each line is `HH:MM:SS.mmm [L] file:line: message`: the time in UTC with milliseconds, the level's
initial, and the source file without its directory. A newline is added if the message doesn't end in
one.

## Levels

From lowest to highest: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`. Lines below the current level
aren't written.

- **Default level:** `DEBUG`, or `INFO` in builds that define `NDEBUG`. A release build, which `dino build` makes by default, defines it, so there `DINO_LOG=debug` shows nothing more: the debug lines aren't compiled in (see below).
- **`DINO_LOG`:** the environment variable overrides the default: `trace`, `debug`, `info`, `warn`
  (or `warning`), `error` or `off`, in any case, e.g. `DINO_LOG=warn ./app`. It's read the first time anything is
  logged. Any other value is reported once on stderr and ignored.
- **`dino_log_set_level(level)`** overrides both, from any thread. `DINO_LOG_LEVEL_OFF` silences
  everything.
- **Release builds:** `DINO_LOG_TRACE` and `DINO_LOG_DEBUG` compile to nothing under `NDEBUG`. Their
  arguments are still type-checked, so release builds report the same format errors.

## Functions and macros

|                                                           | Does                                                                                                                                                                                                                               |
| --------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DINO_LOG_TRACE(fmt, ...)` ... `DINO_LOG_ERROR(fmt, ...)` | Log a line at that level, `printf`-style. The format must be a string literal.                                                                                                                                                     |
| `dino_log_set_level(level)` / `dino_log_get_level()`      | Set or read the lowest level written.                                                                                                                                                                                              |
| `dino_log_set_output(FILE *stream)`                       | Write lines to `stream` instead of stderr; `NULL` goes back to stderr. Once it returns, no thread writes to the old stream, so it may be closed.                                                                                   |
| `dino_log_set_callback(fn, userdata)`                     | Hand each message to `fn(level, file, line, message, userdata)` instead of writing a line to a stream: for syslog, the Windows event log or a log of your own, which add their own time and level. `NULL` goes back to the stream. |
| `dino_log_write(level, file, line, fmt, ...)`             | What the macros call, for wrappers of your own.                                                                                                                                                                                    |

- **One write per line:** each line is written with a single call, so lines from different threads
  never interleave.
- **Flushed every line**, so nothing is lost if the program crashes: each line costs what an `fprintf` plus `fflush` does, a system call. Below the current level a line costs only the level check. For many lines a second, raise the level or give `dino_log_set_output` a stream of your own.
- **Length:** lines over 1 KB are truncated.
- **`errno`** is left as it was, so logging inside error handling doesn't disturb it.
- **A callback** gets the parts of a line separately: the level, the source file without its directory, the line number, and the formatted message without a trailing newline. It runs in the thread that logged, one message at a time under the lock that keeps lines whole: it must not log itself, and should be quick. Since 1.0 (`#if DINO_VERSION_AT_LEAST(1, 0, 0)`).

```c
#include <syslog.h>

#include <dino/util/logging.h>

static void to_syslog(
    dino_log_level_t level, const char *file, int line, const char *message, void *userdata)
{
    (void)userdata;
    static const int priority[] = { LOG_DEBUG, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR };
    // syslog adds the time; the level becomes the priority.
    syslog(priority[level], "%s:%d: %s", file, line, message);
}

// At startup: dino_log_set_callback(to_syslog, NULL);
```

See also: [`examples/logging_example`](../../examples/logging_example/main.c).
