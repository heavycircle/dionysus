#include <dino/core/error.h>

#include <winsock2.h>

#include <windows.h>

#include "error_internal.h"

int dino__error_from_os(void)
{
    return dino__error_from_code((long)GetLastError());
}

int dino__error_from_code(long os_code)
{
    switch (os_code)
    {
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_HANDLE:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_NO_UNICODE_TRANSLATION:
    case WSAEINVAL:
    case WSAEBADF:
    case WSAENOTSOCK:
    case WSAEFAULT:
    case WSAEAFNOSUPPORT:
    case WSAEDESTADDRREQ:
        return DINO_EINVAL;
    case WSAEWOULDBLOCK:
    case WSAEINTR:
    case ERROR_BUSY:
        return DINO_EAGAIN;
    case WSAEINPROGRESS:
    case WSAEALREADY:
        return DINO_EINPROGRESS;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case WSAENOBUFS:
        return DINO_ENOMEM;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_DIRECTORY:
        return DINO_ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_BAD_EXE_FORMAT:
    case WSAEACCES:
        return DINO_EACCES;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return DINO_EEXIST;
    case ERROR_TOO_MANY_OPEN_FILES:
    case WSAEMFILE:
        return DINO_EMFILE;
    case WSAECONNREFUSED:
        return DINO_ECONNREFUSED;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case ERROR_NETNAME_DELETED:
        return DINO_ECONNRESET;
    case ERROR_BROKEN_PIPE:
    case ERROR_PIPE_NOT_CONNECTED:
    // "The pipe is being closed": a write with no reader left.
    case ERROR_NO_DATA:
    case WSAESHUTDOWN:
        return DINO_EPIPE;
    case WSAEADDRINUSE:
        return DINO_EADDRINUSE;
    case WSAETIMEDOUT:
    case ERROR_SEM_TIMEOUT:
        return DINO_ETIMEDOUT;
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
    case WSAENETDOWN:
    case WSAEADDRNOTAVAIL:
        return DINO_EUNREACH;
    case WSAENOTCONN:
        return DINO_ENOTCONN;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:
    case WSAEOPNOTSUPP:
    case WSAEPROTONOSUPPORT:
        return DINO_ENOTSUP;
    default:
        return DINO_EOTHER;
    }
}
