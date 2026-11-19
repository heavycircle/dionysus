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
    DINO_LOG_DEBUG("debug lines show unless the build defines NDEBUG");
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

- **Default level:** `DEBUG`, or `INFO` in builds that define `NDEBUG`.
- **`DINO_LOG`:** the environment variable overrides the default: `trace`, `debug`, `info`, `warn`,
  `error` or `off`, in any case, e.g. `DINO_LOG=warn ./app`. It's read the first time anything is
  logged.
- **`dino_log_set_level(level)`** overrides both, from any thread. `DINO_LOG_LEVEL_OFF` silences
  everything.
- **Release builds:** `DINO_LOG_TRACE` and `DINO_LOG_DEBUG` compile to nothing under `NDEBUG`. Their
  arguments are still type-checked, so release builds report the same format errors.

## Functions and macros

| | Does |
| --- | --- |
| `DINO_LOG_TRACE(fmt, ...)` ... `DINO_LOG_ERROR(fmt, ...)` | Log a line at that level, `printf`-style. The format must be a string literal. |
| `dino_log_set_level(level)` / `dino_log_get_level()` | Set or read the lowest level written. |
| `dino_log_set_output(FILE *stream)` | Write lines to `stream` instead of stderr; `NULL` goes back to stderr. Set `NULL` before closing the stream. |
| `dino_log_write(level, file, line, fmt, ...)` | What the macros call, for wrappers of your own. |

- **One write per line:** each line is written with a single call, so lines from different threads
  never interleave.
- **Length:** lines over 1 KB are truncated.
- **`errno`** is left as it was, so logging inside error handling doesn't disturb it.

See also: [`examples/logging_example`](../../examples/logging_example/main.c).
