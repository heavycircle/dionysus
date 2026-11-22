#include <dino/core/error.h>

#include <errno.h>

#include "error_internal.h"

int dino__error_from_os(void)
{
    return dino__error_from_code(errno);
}

int dino__error_from_code(long os_code)
{
    switch (os_code)
    {
    case EINVAL:
    case EBADF:
    case ENOTSOCK:
    case EFAULT:
    case EAFNOSUPPORT:
    case EDESTADDRREQ:
    case ENAMETOOLONG:
        return DINO_EINVAL;
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case EAGAIN:
    case EINTR:
    case EBUSY:
        return DINO_EAGAIN;
    case EINPROGRESS:
    case EALREADY:
        return DINO_EINPROGRESS;
    case ENOMEM:
    case ENOBUFS:
        return DINO_ENOMEM;
    case ENOENT:
    case ENOTDIR:
    case ESRCH:
    case ECHILD:
        return DINO_ENOENT;
    case EACCES:
    case EPERM:
    case EROFS:
        return DINO_EACCES;
    case EEXIST:
        return DINO_EEXIST;
    case EMFILE:
    case ENFILE:
        return DINO_EMFILE;
    case ECONNREFUSED:
        return DINO_ECONNREFUSED;
    case ECONNRESET:
    case ECONNABORTED:
        return DINO_ECONNRESET;
    case EPIPE:
        return DINO_EPIPE;
    case EADDRINUSE:
        return DINO_EADDRINUSE;
    case ETIMEDOUT:
        return DINO_ETIMEDOUT;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
    case EADDRNOTAVAIL:
        return DINO_EUNREACH;
    case ENOTCONN:
        return DINO_ENOTCONN;
    case ENOSYS:
    case EOPNOTSUPP:
    case EPROTONOSUPPORT:
        return DINO_ENOTSUP;
    default:
        return DINO_EOTHER;
    }
}
