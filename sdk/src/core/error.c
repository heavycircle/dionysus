#include <dino/core/error.h>

#include "error_internal.h"

static __thread int g_error;

int dino_error(void)
{
    return g_error;
}

void dino__error_set(int code)
{
    g_error = code;
}

const char *dino_error_message(int code)
{
    switch (code)
    {
    case 0:
        return "no error";
    case DINO_EINVAL:
        return "invalid argument";
    case DINO_EAGAIN:
        return "not ready, try again";
    case DINO_EINPROGRESS:
        return "connection in progress";
    case DINO_ENOMEM:
        return "out of memory";
    case DINO_ENOENT:
        return "no such file or program";
    case DINO_EACCES:
        return "permission denied";
    case DINO_EEXIST:
        return "already exists";
    case DINO_EMFILE:
        return "too many open files";
    case DINO_ECONNREFUSED:
        return "connection refused";
    case DINO_ECONNRESET:
        return "connection reset";
    case DINO_EPIPE:
        return "the other end is closed";
    case DINO_EADDRINUSE:
        return "address already in use";
    case DINO_ETIMEDOUT:
        return "timed out";
    case DINO_EUNREACH:
        return "network or host unreachable";
    case DINO_ENOTCONN:
        return "not connected";
    case DINO_ENOTSUP:
        return "not supported here";
    default:
        return "error";
    }
}
