#ifndef MODULES_RUNTIME_ARCH_X86_64_SYSCALL_H
#define MODULES_RUNTIME_ARCH_X86_64_SYSCALL_H

#include "syscall_tbl.h"

static __inline long __syscall0(long n)
{
    unsigned int ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall1(long n, long a1)
{
    unsigned int ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall2(long n, long a1, long a2)
{
    unsigned int ret;
    __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall3(long n, long a1, long a2, long a3)
{
    unsigned int ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                         : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
    unsigned int ret;
    register long r10 __asm__("r10") = a4;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                         : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
    unsigned int ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                         : "rcx", "r11", "memory");
    return ret;
}

static __inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    unsigned int ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
    return ret;
}

#endif // MODULES_RUNTIME_ARCH_X86_64_SYSCALL_H
