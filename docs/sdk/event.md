# Event loop

`#include <dino/core/event.h>` · `dino_core`

One thread waits on any number of sockets, pipes, child processes and timers, and handles whichever
are ready. It's built on epoll on Linux and on an I/O completion port on Windows, so a wait costs the
same with ten sockets or ten thousand, unlike [`poll`](io.md).

## How it works

1. Create a loop with `dino_event_loop_create()`.
1. Add what to watch, each with a `userdata` pointer of your choosing: sockets (for reading and/or
   writing), pipe read ends, processes (for their exit), timers.
1. Call `dino_event_wait()`. It blocks until something is ready, then fills an array of
   `dino_event_t`, each with the `events` that are ready and the item's `userdata`.
1. Handle each one, and wait again.

Readiness is **level-triggered**: a socket that still has unread data is reported again by the next
wait, so you never lose an event by not reading everything at once.

## Example: a server, a client and a timer in one loop

```c
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/event.h>
#include <dino/core/network.h>

#define PORT 19301

enum { LISTENER = 1, CONNECTION, CLIENT, DEADLINE };

int main(void)
{
    dino_event_loop_t loop = dino_event_loop_create();
    if (loop == DINO_INVALID_EVENT_LOOP)
        return 1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    dino_socket_t listener = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    dino_socket_set_reuseaddr(listener, 1);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 8) != 0)
        return 1;
    dino_event_add_socket(loop, listener, DINO_EVENT_READ, (void *)(intptr_t)LISTENER);

    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    if (dino_socket_connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        return 1;
    dino_socket_send(client, "hello", 5, 0);
    dino_event_add_socket(loop, client, DINO_EVENT_READ, (void *)(intptr_t)CLIENT);

    // Fail rather than hang if something goes wrong.
    dino_event_timer_t deadline = dino_event_add_timer(loop, 5000, 0, (void *)(intptr_t)DEADLINE);

    dino_socket_t conn = DINO_INVALID_SOCKET;
    char          reply[16] = { 0 };
    int           done = 0, ok = 0;
    while (!done)
    {
        dino_event_t events[8];
        int          n = dino_event_wait(loop, events, 8, -1);
        if (n < 0)
            break;
        for (int i = 0; i < n; i++)
        {
            switch ((int)(intptr_t)events[i].userdata)
            {
            case LISTENER: // a connection to accept
                conn = dino_socket_accept(listener, NULL, NULL);
                dino_event_add_socket(loop, conn, DINO_EVENT_READ, (void *)(intptr_t)CONNECTION);
                break;
            case CONNECTION: // the server's side has data: echo it back
            {
                char buf[16];
                int  len = (int)recv(conn, buf, sizeof(buf), 0);
                if (len > 0)
                    dino_socket_send(conn, buf, (size_t)len, 0);
                break;
            }
            case CLIENT: // the echo came back
                recv(client, reply, sizeof(reply) - 1, 0);
                ok   = strcmp(reply, "hello") == 0;
                done = 1;
                break;
            case DEADLINE:
                printf("timed out\n");
                done = 1;
                break;
            }
        }
    }
    printf("reply: %s\n", reply);

    dino_event_del_timer(loop, deadline);
    dino_event_del_socket(loop, client);
    dino_event_del_socket(loop, conn);
    dino_event_del_socket(loop, listener);
    dino_socket_close(client);
    dino_socket_close(conn);
    dino_socket_close(listener);
    dino_event_loop_destroy(loop);
    return ok ? 0 : 1;
}
```

[`examples/event_example`](../../examples/event_example/main.c) is a larger version: a server thread
and a client each with their own loop, holding 1000 connections at once.

## Events

| Flag                 | Means                                                         | `userdata`    |
| -------------------- | ------------------------------------------------------------- | ------------- |
| `DINO_EVENT_READ`    | data to read, a connection to accept, or end of stream        | the item's    |
| `DINO_EVENT_WRITE`   | a send won't block, or a non-blocking `connect` finished      | the item's    |
| `DINO_EVENT_ERROR`   | an error on the socket; always reported                       | the item's    |
| `DINO_EVENT_HANGUP`  | the peer closed or shut down its side: a read returns 0       | the item's    |
| `DINO_EVENT_TIMER`   | a timer fired                                                 | the timer's   |
| `DINO_EVENT_EXIT`    | a watched process ended                                       | the process's |
| `DINO_EVENT_WAKE`    | another thread called `dino_event_wake`                       | `NULL`        |
| `DINO_EVENT_ONESHOT` | (interest only) report once, then disarm until the next `mod` |               |

`READ` and `WRITE` are reported only for a socket watched for them. `ERROR` comes whenever it happens. `HANGUP` comes with `READ` when the socket is watched for it, so a handler that only looks at `READ` still finds the end of the stream with a `recv` that returns `0`. A socket watched for neither `READ` nor `WRITE` reports a reset (`ERROR | HANGUP`) but not a peer that only closed or shut down its side: that lasts, and would end every wait at once. Two cases still differ by platform:

- A refused non-blocking `connect` reports `WRITE | ERROR`, and on Linux `HANGUP` too.
- A socket that is neither connected, connecting nor listening reports `HANGUP` at once on Linux
  (with `WRITE`, if watched for it) and nothing on Windows. Add a socket after `connect` or `listen`.

## Functions

| Function                                                     | Does                                                                                     |
| ------------------------------------------------------------ | ---------------------------------------------------------------------------------------- |
| `dino_event_loop_create()` / `dino_event_loop_destroy(loop)` | Create a loop, or destroy it. Destroying drops what's registered without closing it.     |
| `dino_event_add_socket(loop, sock, events, ud)`              | Watch a socket for `DINO_EVENT_READ` and/or `DINO_EVENT_WRITE`.                          |
| `dino_event_mod_socket(loop, sock, events, ud)`              | Change what it's watched for and its userdata. Rearms a fired `ONESHOT`.                 |
| `dino_event_del_socket(loop, sock)`                          | Stop watching it. Do this before closing the socket.                                     |
| `dino_event_add_pipe`, `_mod_pipe`, `_del_pipe`              | The same for the read end of a [pipe](pipe.md), `READ` only.                             |
| `dino_event_add_process(loop, process, ud)`                  | Report `DINO_EVENT_EXIT` once when a [process](process.md) ends.                         |
| `dino_event_del_process(loop, process)`                      | Stop watching it. Must come before `dino_process_wait`.                                  |
| `dino_event_add_timer(loop, delay_ms, interval_ms, ud)`      | A timer firing after `delay_ms`, then every `interval_ms` (`0`: once). Returns a handle. |
| `dino_event_del_timer(loop, timer)`                          | Delete a timer, fired or not. Every timer must be deleted (or the loop destroyed).       |
| `dino_event_wait(loop, out, max, timeout_ms)`                | Wait. Returns how many items it wrote to `out`, `0` on timeout, `-1` on error.           |
| `dino_event_wake(loop)`                                      | Make a wait return a `DINO_EVENT_WAKE` item, from any thread.                            |

Everything returns `0` on success and `-1` on error (adding an item twice, or deleting one that isn't
there, is an error), except the create functions, which return a handle or its invalid value.

## Threads

- **Calls from other threads:** every function except `dino_event_loop_destroy` may be called from
  any thread, even while another thread is blocked in `dino_event_wait`. Adding a socket from one
  thread wakes a wait in another to include it.
- **Stopping or handing over work:** `dino_event_wake` makes a waiting thread return a
  `DINO_EVENT_WAKE` item. Use it to stop a loop or to tell it there's work queued for it. Wakes not
  yet reported merge into one.
- **Several threads on one loop:** add sockets with `DINO_EVENT_ONESHOT`, so one event goes to one
  thread, and rearm each with `dino_event_mod_socket` once that thread has handled it. Without
  `ONESHOT`, one ready socket wakes every waiting thread on Linux but only one on Windows.
  On Windows a socket or pipe that was reported is watched again only when a thread next calls `dino_event_wait`; a thread already blocked in a wait doesn't pick it up. If the thread that got the event never waits again, nobody is told about that item again.
- **Deleting while another thread waits:** after a `del` (or `dino_event_del_timer`) returns, no wait
  that starts later reports the item. A wait already running in another thread may still return
  one last event for it, so a thread that frees an item's userdata must make sure no other thread
  is mid-wait, for example by deleting from the thread that waits.

## Timers

- **Never early:** a timer never fires before its delay. A repeating timer that falls behind (nobody
  waited for a while) fires once and skips the periods it missed.
- **Freeing:** a fired one-shot timer stays allocated until `dino_event_del_timer` or the loop is
  destroyed.
- **Delete each timer once, through its own loop.** `dino_event_del_timer` with another loop returns `-1` and leaves the timer alone. Deleting a timer twice is undefined: the first delete frees it.
- **Busy loops:** when timers and sockets are both ready and don't all fit in `out`, waits take
  turns at filling it first. A socket that stays readable delays a timer by one wait at most, and a
  timer that's due at every wait still leaves room for I/O, even with `max` of 1.
- **Timeouts per wait:** a timer is how a loop gets a deadline for one operation. For a single wait,
  `timeout_ms` is simpler.

## Pipes and processes

- **Pipes:** only read ends can be watched, and only for `DINO_EVENT_READ`. Windows has no
  "room to write" signal for pipes. A pipe reports `READ` while data is waiting, then `READ` with
  `HANGUP` once every write end has closed and what was written before is read, so a handler that stops at `HANGUP` loses nothing.
- **One loop per pipe end:** on Windows a pipe end belongs to the first loop it joins for as long as
  it exists. Keep each pipe end in one loop.
- **Processes:** `dino_event_add_process` reports the exit once. Then `dino_event_del_process` it and
  `dino_process_wait` for the exit code, which returns at once. On Linux this needs kernel 5.3 or
  later (pidfds).

## Pitfalls

- **Delete before closing.** Remove a socket or pipe from the loop (`del`) before closing it. A socket closed while still in a loop is dropped from it without an event, on both platforms, and a later `del` for it usually returns `-1`. On Windows the loop only notices the close during a wait, and never for a `ONESHOT` socket that already fired: until then a new socket that happens to get the same handle value can't be added. A pipe closed while in a loop may still be reported on Windows, as an error and with its old userdata, so never free the userdata of something you haven't deleted.
- **A failed `mod` changes nothing.** The item keeps the events and userdata it had.
- **Non-blocking sockets.** Make sockets non-blocking (`dino_socket_set_nonblocking`) when one event
  may not cover everything you'll read or accept: keep calling until it fails with [`dino_error()`](error.md) of `DINO_EAGAIN`, as `examples/event_example` does for `accept`. Any other code is a real failure. `DINO_EMFILE` from `accept` means the process is out of descriptors while the connection is still waiting: the listener stays ready, so stop watching it for a moment instead of spinning.
- **`max` is a limit, not a requirement.** Items that don't fit in `out` stay ready for the next
  wait.
