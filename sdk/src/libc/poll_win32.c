#include <poll.h>

#include <stdlib.h>

#include "libc_win32_internal.h"

// MinGW has no poll(). Over WSAPoll, with what POSIX promises and WSAPoll
// doesn't: a negative fd (INVALID_SOCKET) is skipped, POLLERR, POLLHUP and
// POLLNVAL in events are ignored rather than rejected, a peer that closed its
// side is readable as well as POLLHUP, nothing to watch waits out the timeout
// instead of failing, and errno says why it failed. Sockets only, as WSAPoll.
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (nfds > 0 && !fds)
    {
        errno = EFAULT;
        return -1;
    }
    WSAPOLLFD  stack_buf[64];
    WSAPOLLFD *wfds = nfds <= 64 ? stack_buf : malloc(nfds * sizeof(WSAPOLLFD));
    if (!wfds)
    {
        errno = ENOMEM;
        return -1;
    }

    ULONG n = 0;
    for (nfds_t i = 0; i < nfds; i++)
    {
        fds[i].revents = 0;
        if (fds[i].fd == INVALID_SOCKET)
            continue;
        wfds[n].fd      = fds[i].fd;
        wfds[n].events  = (SHORT)(fds[i].events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
        wfds[n].revents = 0;
        n++;
    }

    int rc;
    if (n == 0)
    {
        Sleep(timeout < 0 ? INFINITE : (DWORD)timeout);
        rc = 0;
    }
    else
        rc = WSAPoll(wfds, n, timeout);

    if (rc == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        errno   = err == WSAENOBUFS ? ENOMEM : err == WSAEFAULT ? EFAULT : EINVAL;
        rc      = -1;
    }
    else if (rc > 0)
    {
        for (nfds_t i = 0, j = 0; i < nfds; i++)
        {
            if (fds[i].fd == INVALID_SOCKET)
                continue;
            SHORT revents = wfds[j++].revents;
            if ((revents & POLLHUP) && (fds[i].events & POLLIN))
                revents |= POLLRDNORM;
            fds[i].revents = revents;
        }
    }
    if (wfds != stack_buf)
        free(wfds);
    return rc;
}
