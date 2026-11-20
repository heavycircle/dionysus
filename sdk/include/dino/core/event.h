#ifndef DINO_CORE_EVENT_H
#define DINO_CORE_EVENT_H

#include <stdint.h>

#include <dino/core/network.h>
#include <dino/core/pipe.h>
#include <dino/core/process.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Event loop: register sockets, pipes and processes with the events you care
 * about, then wait for any of them to become ready. Built on epoll on Linux and on an I/O
 * completion port on Windows, so a wait costs the same with ten sockets or ten thousand, unlike
 * dino_poll and dino_select.
 *
 * Readiness is level-triggered: a socket that is still readable is reported
 * again by the next wait. DINO_EVENT_ONESHOT reports it once and then disarms
 * it until dino_event_mod_socket, so that one event goes to one thread when
 * several threads wait on the same loop. Without it, one ready socket wakes
 * every waiting thread on Linux and one of them on Windows.
 *
 * Every function may be called from any thread, including while another thread
 * is blocked in dino_event_wait, except dino_event_loop_destroy.
 */
typedef uintptr_t dino_event_loop_t;

#define DINO_INVALID_EVENT_LOOP ((dino_event_loop_t)0)

/** A timer in a loop, from dino_event_timer_add. */
typedef uintptr_t dino_event_timer_t;

#define DINO_INVALID_EVENT_TIMER ((dino_event_timer_t)0)

/* ---- Event flags ---- */
#define DINO_EVENT_READ    0x0001 /**< Data to read, a connection to accept, or end of stream. */
#define DINO_EVENT_WRITE   0x0004 /**< A send won't block, or a connect finished. */
#define DINO_EVENT_ERROR   0x0008 /**< Always reported. */
#define DINO_EVENT_HANGUP  0x0010 /**< Peer closed or shut down; always reported. */
#define DINO_EVENT_WAKE    0x0100 /**< From dino_event_wake; userdata is NULL. */
#define DINO_EVENT_TIMER   0x0200 /**< A timer fired; userdata is the timer's. */
#define DINO_EVENT_EXIT    0x0400 /**< A process ended; userdata is the process's. */
#define DINO_EVENT_ONESHOT 0x4000 /**< Interest only: disarm after one report. */

/**
 * One ready item from dino_event_wait.
 */
typedef struct
{
    uint32_t events;   /**< DINO_EVENT_* that are ready. */
    void    *userdata; /**< As given when the item was added. */
} dino_event_t;

/**
 * Create an event loop. Its handles are never inherited by child processes.
 *
 * @return Loop handle, or DINO_INVALID_EVENT_LOOP on failure.
 */
dino_event_loop_t dino_event_loop_create(void);

/**
 * Destroy a loop. Registered sockets, pipes and processes are dropped from it
 * but not closed or waited for, and its timers are deleted. No other thread may be using the loop.
 *
 * @param  loop  Loop to destroy.
 */
void dino_event_loop_destroy(dino_event_loop_t loop);

/**
 * Start watching a socket. A socket can be in a loop once; remove it with
 * dino_event_del_socket before closing it.
 *
 * READ and WRITE are reported only when asked for. Add a socket once it is
 * connected, connecting or listening: before that Linux reports HANGUP at
 * once and Windows reports nothing.
 *
 * @param  loop      Loop to add to.
 * @param  sock      Socket to watch.
 * @param  events    DINO_EVENT_READ and/or DINO_EVENT_WRITE, optionally with
 *                   DINO_EVENT_ONESHOT.
 * @param  userdata  Returned with every event for this socket.
 * @return 0 on success, -1 on error (including when sock is already added).
 */
int dino_event_add_socket(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events, void *userdata);

/**
 * Change what a socket is watched for, and its userdata. Rearms a
 * DINO_EVENT_ONESHOT socket that has fired.
 *
 * @return 0 on success, -1 on error (including when sock isn't added).
 */
int dino_event_mod_socket(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events, void *userdata);

/**
 * Stop watching a socket. No wait that starts after this returns reports it;
 * a wait already running in another thread may still return one event for it.
 *
 * @return 0 on success, -1 on error (including when sock isn't added).
 */
int dino_event_del_socket(dino_event_loop_t loop, dino_socket_t sock);

/**
 * Start watching the read end of a pipe. READ is reported while data is
 * waiting, and READ with HANGUP once every write end has closed (a read then
 * returns 0). A pipe can be in a loop once; remove it with dino_event_del_pipe
 * before closing it.
 *
 * Only read ends can be watched, and only for DINO_EVENT_READ: Windows has no
 * signal for a pipe having room to write, so write ends are refused on every
 * platform alike.
 *
 * On Windows a pipe end is tied to the first loop it joins for as long as it
 * exists: it can leave and rejoin that loop, but adding it to another loop
 * fails. Portable code keeps each pipe end in one loop.
 *
 * @param  loop      Loop to add to.
 * @param  pipe      Read end from dino_pipe_create or dino_process_spawn.
 * @param  events    DINO_EVENT_READ, optionally with DINO_EVENT_ONESHOT.
 * @param  userdata  Returned with every event for this pipe.
 * @return 0 on success, -1 on error (including a write end, WRITE interest,
 *         or a pipe already added).
 */
int dino_event_add_pipe(dino_event_loop_t loop, dino_pipe_t pipe, uint32_t events, void *userdata);

/**
 * Change a pipe's interest and userdata, as dino_event_mod_socket does.
 *
 * @return 0 on success, -1 on error (including when pipe isn't added).
 */
int dino_event_mod_pipe(dino_event_loop_t loop, dino_pipe_t pipe, uint32_t events, void *userdata);

/**
 * Stop watching a pipe. No wait that starts after this returns reports it, as
 * for dino_event_del_socket.
 *
 * @return 0 on success, -1 on error (including when pipe isn't added).
 */
int dino_event_del_pipe(dino_event_loop_t loop, dino_pipe_t pipe);

/**
 * Report a DINO_EVENT_EXIT item, once, when a process ends; at the next wait if
 * it already has. The process stays in the loop until dino_event_del_process,
 * which must come before dino_process_wait (that frees the process). After the
 * event, dino_process_wait returns at once.
 *
 * On Linux this needs pidfds (Linux 5.3); without them it fails.
 *
 * @param  loop      Loop to add to.
 * @param  process   Process from dino_process_spawn, not yet waited for.
 * @param  userdata  Returned with the process's event.
 * @return 0 on success, -1 on error (including a process already added).
 */
int dino_event_add_process(dino_event_loop_t loop, dino_process_t process, void *userdata);

/**
 * Stop watching a process. No wait that starts after this returns reports its
 * exit, as for dino_event_del_socket.
 *
 * @return 0 on success, -1 on error (including when process isn't added).
 */
int dino_event_del_process(dino_event_loop_t loop, dino_process_t process);

/**
 * Wait until something in the loop is ready.
 *
 * @param  loop        Loop to wait on.
 * @param  out         Receives up to max ready items.
 * @param  max         Size of out; must be at least 1. Items that don't fit
 *                     stay ready for the next wait. When due timers and
 *                     other items don't all fit, waits take turns at which
 *                     fills out first, so neither starves the other.
 * @param  timeout_ms  Milliseconds to wait; -1 to wait forever; 0 to check.
 * @return Number of items written to out, 0 when the time ran out, -1 on error.
 */
int dino_event_wait(dino_event_loop_t loop, dino_event_t *out, int max, int timeout_ms);

/**
 * Make a wait on this loop return a DINO_EVENT_WAKE item, now or, if no thread
 * is waiting, at the next wait. Wakes not yet reported are merged into one.
 * Use it to stop a loop or hand it work from another thread.
 *
 * @param  loop  Loop to wake.
 * @return 0 on success, -1 on error.
 */
int dino_event_wake(dino_event_loop_t loop);

/**
 * Add a timer: a wait reports a DINO_EVENT_TIMER item after delay_ms, then
 * every interval_ms. Timers never fire early. A repeating timer that falls
 * behind (nobody waited for a while) fires once and skips the periods it
 * missed, rather than firing for each.
 *
 * Every timer, one-shot included, stays allocated until dino_event_timer_del
 * or dino_event_loop_destroy.
 *
 * @param  loop         Loop to add to.
 * @param  delay_ms     Milliseconds until the first firing; 0 fires at the next wait.
 * @param  interval_ms  Milliseconds between later firings; 0 fires once.
 * @param  userdata     Returned with the timer's events.
 * @return Timer handle, or DINO_INVALID_EVENT_TIMER on failure.
 */
dino_event_timer_t dino_event_timer_add(
    dino_event_loop_t loop, uint32_t delay_ms, uint32_t interval_ms, void *userdata);

/**
 * Delete a timer, fired or not. No wait that starts after this returns
 * reports it, as for dino_event_del_socket.
 *
 * @param  loop   Loop it was added to.
 * @param  timer  Timer from dino_event_timer_add.
 * @return 0 on success, -1 on error.
 */
int dino_event_timer_del(dino_event_loop_t loop, dino_event_timer_t timer);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_EVENT_H
