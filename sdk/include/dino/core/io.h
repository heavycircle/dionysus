#ifndef DINO_CORE_IO_H
#define DINO_CORE_IO_H

#include <stdint.h>

#include <dino/core/network.h>

/* ---- Poll event flags (dino's own values; dino_poll translates them per platform) ---- */
#define DINO_POLLIN  0x0001
#define DINO_POLLOUT 0x0004
#define DINO_POLLERR 0x0008
#define DINO_POLLHUP 0x0010

/**
 * Cross-platform pollfd descriptor.
 */
typedef struct
{
    dino_socket_t sock;
    short         events;
    short         revents;
} dino_pollfd_t;

/**
 * Opaque fd_set handle. Always heap-allocated via dino_fd_set_create.
 *
 * The internal layout differs between Linux (128-byte bitmask) and
 * Windows ({count, SOCKET[64]}), so the type must remain opaque.
 */
typedef uintptr_t dino_fd_set_t;

#define DINO_INVALID_FD_SET ((dino_fd_set_t)0)

/* ---- Poll ---- */

/**
 * Wait for readiness events on a set of sockets.
 *
 * Every call passes and scans the whole set, so the cost grows with the number of
 * sockets; for many sockets, use the event loop in dino/core/event.h.
 *
 * Request DINO_POLLIN and/or DINO_POLLOUT in events. revents always uses DINO_*
 * values, and DINO_POLLERR and DINO_POLLHUP are reported whether requested or
 * not. Every revents is reset, so it is 0 on timeout.
 *
 * @param  fds        Array of dino_pollfd_t descriptors.
 * @param  nfds       Number of entries in fds.
 * @param  timeout_ms Milliseconds to wait; -1 to block; 0 for immediate return.
 * @return Number of descriptors with events, 0 on timeout, -1 on error.
 */
int dino_poll(dino_pollfd_t *fds, int nfds, int timeout_ms);

/* ---- fd_set lifecycle ---- */

/**
 * Allocate and zero an opaque fd_set.
 *
 * @return Handle, or DINO_INVALID_FD_SET on failure.
 */
dino_fd_set_t dino_fd_set_create(void);

/**
 * Destroy and free an opaque fd_set.
 *
 * @param  set  Handle returned by dino_fd_set_create.
 */
void dino_fd_set_destroy(dino_fd_set_t set);

/**
 * Zero all bits in the set (equivalent to FD_ZERO).
 *
 * @param  set  Handle to zero.
 */
void dino_fd_set_zero(dino_fd_set_t set);

/**
 * Add a socket to the set (equivalent to FD_SET).
 *
 * @param  set   Target set.
 * @param  sock  Socket to add.
 */
void dino_fd_set_add(dino_fd_set_t set, dino_socket_t sock);

/**
 * Remove a socket from the set (equivalent to FD_CLR).
 *
 * @param  set   Target set.
 * @param  sock  Socket to remove.
 */
void dino_fd_set_del(dino_fd_set_t set, dino_socket_t sock);

/**
 * Test whether a socket is in the set (equivalent to FD_ISSET).
 *
 * @param  sock  Socket to test.
 * @param  set   Set to test against.
 * @return Non-zero if present, 0 if absent.
 */
int dino_fd_isset(dino_socket_t sock, dino_fd_set_t set);

/* ---- Select ---- */

/**
 * Wait for readiness on sets of sockets using select(2).
 *
 * On Windows, max_fd is ignored (WinSock select ignores nfds).
 * On Linux, max_fd should be the highest socket fd value plus 1.
 *
 * Pass DINO_INVALID_FD_SET for any set you do not wish to monitor.
 * timeout_ms: -1 to block indefinitely, 0 for immediate return.
 *
 * @param  max_fd     Highest fd + 1 (Linux only).
 * @param  rfds       Read-ready set, or DINO_INVALID_FD_SET.
 * @param  wfds       Write-ready set, or DINO_INVALID_FD_SET.
 * @param  efds       Exception set, or DINO_INVALID_FD_SET.
 * @param  timeout_ms Milliseconds to wait.
 * @return Number of ready sockets, 0 on timeout, -1 on error.
 */
int dino_select(dino_socket_t max_fd, dino_fd_set_t rfds, dino_fd_set_t wfds, dino_fd_set_t efds,
    long timeout_ms);

#endif // DINO_CORE_IO_H
