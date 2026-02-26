#ifndef MODULES_RUNTIME_SYSCALL_H
#define MODULES_RUNTIME_SYSCALL_H

#ifdef __arm__
#include <modules/runtime/arch/arm/syscall.h>
#elif __i386__
#include <modules/runtime/arch/i386/syscall.h>
#else
#include <modules/runtime/arch/x86_64/syscall.h>
#endif

#ifdef USE_SOCKETCALLS
#define __NR_socketcall 102
#define __SC_socket 1
#define __SC_bind 2
#define __SC_connect 3
#define __SC_listen 4
#define __SC_accept 5
#define __SC_getsockname 6
#define __SC_getpeername 7
#define __SC_socketpair 8
#define __SC_send 9
#define __SC_recv 10
#define __SC_sendto 11
#define __SC_recvfrom 12
#define __SC_shutdown 13
#define __SC_setsockopt 14
#define __SC_getsockopt 15
#define __SC_sendmsg 16
#define __SC_recvmsg 17
#define __SC_accept4 18
#define __SC_recvmmsg 19
#define __SC_sendmmsg 20
#endif // USE_SOCKETCALLS

#endif // MODULES_RUNTIME_SYSCALL_H