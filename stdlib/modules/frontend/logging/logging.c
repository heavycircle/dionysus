#include "logging.h"

void putc(char c)
{
    __asm__ volatile("mov $1, %%rax\n"
                     "mov $1, %%rdi\n"
                     "mov %0, %%rsi\n"
                     "mov $1, %%rdx\n"
                     "syscall"
                     :
                     : "r"(&c)
                     : "rax", "rdi", "rsi", "rdx");
}

void dionysus_vprintf(const char *fmt, __builtin_va_list args)
{
    const char *p;

    for (p = fmt; *p != '\0'; ++p)
    {
        if (*p != '%')
        {
            putc(*p);
            continue;
        }

        ++p;
        switch (*p)
        {
        case 'c':
        {
            char i = (char)__builtin_va_arg(args, int);
            putc(i);
            break;
        }
        case 's':
        {
            char *s = (char *)__builtin_va_arg(args, char *);
            while (*s)
                putc(*s++);
            break;
        }
        case 'd':
        {
            char i = (char)__builtin_va_arg(args, int);
            putc(i);
            break;
        }
        case '%':
        {
            putc('%');
            break;
        }
        }
    }
}

void dprintf(const char *fmt, ...)
{
    __builtin_va_list args;

    __builtin_va_start(args, fmt);
    dionysus_vprintf(fmt, args);
    __builtin_va_end(args);
}

void logging_print(char level, const char *fmt, ...)
{
    __builtin_va_list args;

    dprintf("[%c] ", level);

    __builtin_va_start(args, fmt);
    dionysus_vprintf(fmt, args);
    __builtin_va_end(args);

#ifdef WINDOWS
    dprintf("\r");
#endif
    dprintf("\n");
}
