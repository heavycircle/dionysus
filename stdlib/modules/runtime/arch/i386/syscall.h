#ifndef MODULES_RUNTIME_SYSCALL_ARCH_I386_SYSCALL_H
#define MODULES_RUNTIME_SYSCALL_ARCH_I386_SYSCALL_H

#include "syscall_tbl.h"

#define SYSCALL "int $0x80"

static __inline long __syscall0(long n)
{
    unsigned int ret;
    __asm__ __volatile__(SYSCALL : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

static __inline long __syscall1(long n, long a1)
{
    unsigned int ret;
    __asm__ __volatile__(SYSCALL : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

static __inline long __syscall2(long n, long a1, long a2)
{
    unsigned int ret;
    __asm__ __volatile__(SYSCALL : "=a"(ret) : "a"(n), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static __inline long __syscall3(long n, long a1, long a2, long a3)
{
    unsigned int ret;
    __asm__ __volatile__(SYSCALL : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static __inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
    unsigned int ret;
    __asm__ __volatile__(SYSCALL
                         : "=a"(ret)
                         : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
                         : "memory");
    return ret;
}

static __inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
    unsigned int ret;
    __asm__ __volatile__(SYSCALL
                         : "=a"(ret)
                         : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                         : "memory");
    return ret;
}

#pragma GCC push_options
#pragma GCC optimize "-fomit-frame-pointer"
static __inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    unsigned int ret;
    register long _a6 __asm__("ebp") = a6;
    __asm__ __volatile__(SYSCALL
                         : "=a"(ret)
                         : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5), "r"(_a6)
                         : "memory", "cc");
    return ret;
}
#pragma GCC pop_options

#endif // MODULES_RUNTIME_SYSCALL_ARCH_I386_SYSCALL_H
