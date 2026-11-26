#pragma GCC system_header
// POSIX poll() on Windows, where MinGW ships no <poll.h>. Winsock defines
// struct pollfd and the POLL* flags; the SDK defines poll() over WSAPoll
// (sdk/src/libc/poll_win32.c). Like WSAPoll, it waits on sockets only.
#ifndef DINO_WIN32_POLL_H
#define DINO_WIN32_POLL_H

#include <sys/socket.h>

typedef unsigned long nfds_t;

#ifndef DINO_NO_LIBC_SHIMS
#ifdef __cplusplus
extern "C" int poll(struct pollfd *fds, nfds_t nfds, int timeout);
#else
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
#endif
#endif

#endif
