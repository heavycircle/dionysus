#ifndef DINO_UTIL_LOGGING_H
#define DINO_UTIL_LOGGING_H

#include <stdio.h>

/**
 * Debug log to stdout, prefixed with "[D] ". The format must be a string literal.
 *
 * Under NDEBUG nothing is printed, but the arguments are still type-checked, so
 * release builds report the same format errors and no unused-variable warnings.
 * Taking the format as part of __VA_ARGS__ allows calls without arguments
 * without relying on the GNU ", ##__VA_ARGS__" extension.
 */
#ifdef NDEBUG
#define LOG_DEBUG(...) ((void)(0 && printf("[D] " __VA_ARGS__)))
#else
#define LOG_DEBUG(...) ((void)printf("[D] " __VA_ARGS__))
#endif // NDEBUG

#endif // DINO_UTIL_LOGGING_H
