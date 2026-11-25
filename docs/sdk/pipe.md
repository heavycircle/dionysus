# Pipes

`#include <dino/core/pipe.h>` · `dino_core`

A one-way byte stream: bytes written to one end come out of the other, in order. Pipes connect
threads, or a parent and a child [process](process.md) (its stdin, stdout and stderr are pipes when
you ask for them).

Windows' anonymous pipes can't be waited on or used with an event loop, so the SDK builds its pipes
from named pipes there. Both platforms then behave the same: normally 64 KB of buffer, waits with a timeout,
and support in the [event loop](event.md).

## Example: a worker thread writes, the main thread reads

```c
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <dino/core/pipe.h>

static void *writer(void *arg)
{
    dino_pipe_t out = (dino_pipe_t)arg;
    for (int i = 1; i <= 3; i++)
    {
        char line[32];
        int  len = snprintf(line, sizeof(line), "line %d\n", i);
        dino_pipe_write(out, line, len);
    }
    dino_pipe_close(out); // the reader sees end of file
    return NULL;
}

int main(void)
{
    dino_pipe_t read_end, write_end;
    if (dino_pipe_create(&read_end, &write_end) != 0)
        return 1;

    pthread_t t;
    if (pthread_create(&t, NULL, writer, (void *)write_end) != 0)
        return 1;

    char text[128] = { 0 };
    int  total     = 0;
    for (;;)
    {
        if (dino_pipe_wait(read_end, 5000) != 0) // readable, or give up
            return 1;
        int n = dino_pipe_read(read_end, text + total, (int)sizeof(text) - 1 - total);
        if (n <= 0)
            break; // 0: every write end is closed
        total += n;
    }
    pthread_join(t, NULL);
    dino_pipe_close(read_end);

    printf("%s", text);
    return strcmp(text, "line 1\nline 2\nline 3\n") == 0 ? 0 : 1;
}
```

## Functions

| Function                                            | Does                                                                                                                                                                                                                                                             | Returns                                           |
| --------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------- |
| `dino_pipe_create(&read_end, &write_end)`           | A new pipe. Neither end is inherited by child processes; a child's stdio pipes come from [`dino_process_spawn`](process.md).                                                                                                                                     | `0`, `-1`                                         |
| `dino_pipe_create_ex(&read_end, &write_end, flags)` | A pipe with an end, or both, for a child's stdio (`DINO_PIPE_CHILD_READS`, `DINO_PIPE_CHILD_WRITES`), to pass with `DINO_STDIO_PIPE_END` ([processes](process.md)). A marked end reads and writes but can't go to `dino_pipe_wait` or an event loop. Since 0.10. | `0`, `-1`                                         |
| `dino_pipe_read(p, buf, len)`                       | Read up to `len` bytes, waiting until at least one is there.                                                                                                                                                                                                     | bytes read; `0` at end of file (or `len` 0); `-1` |
| `dino_pipe_write(p, buf, len)`                      | Write up to `len` bytes, waiting while the pipe is full.                                                                                                                                                                                                         | bytes written (maybe fewer than `len`), `-1`      |
| `dino_pipe_wait(p, timeout_ms)`                     | Wait until a read won't block: data, or end of file.                                                                                                                                                                                                             | `0`, `DINO_TIMEOUT`, `-1`                         |
| `dino_pipe_close(p)`                                | Close one end.                                                                                                                                                                                                                                                   | `0`, `-1`                                         |

## Rules

- **End of file:** reads return `0` once every write end is closed and the data is drained. Close
  write ends you don't use, or the reader never sees the end.
- **Short writes:** `dino_pipe_write` may write fewer bytes than asked. Write the rest with another
  call.
- **No `SIGPIPE`:** writing after every read end has closed returns `-1`, and the process survives,
  without the SDK touching its signal handling.
- **Waiting:** pipes aren't sockets, so [`poll`](io.md) and `select` don't take them on Windows. Wait
  on one pipe with `dino_pipe_wait`, or on many alongside sockets with
  [`dino_event_add_pipe`](event.md#pipes-and-processes).
- **A full pipe blocks:** a writer waits once 64 KB are unread. A thread that writes to a pipe and
  later reads from it can deadlock unless someone else drains it. On Linux the 64 KB are asked for, not guaranteed: a user over the kernel's pipe-buffer limit (`/proc/sys/fs/pipe-user-pages-soft`) gets 4 KB pipes, so don't size a write by the 64 KB to be sure it can't block.

See also: [`examples/pipe_example`](../../examples/pipe_example/main.c).
