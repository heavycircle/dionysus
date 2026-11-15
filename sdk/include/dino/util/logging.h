#ifndef DINO_UTIL_LOGGING_H
#define DINO_UTIL_LOGGING_H

#ifdef NDEBUG

#define LOG_INIT(fmt, ...)

#else

#define LOG_DEBUG(fmt, ...) printf("[D] " fmt, __VA_ARGS__)

#endif // NDEBUG

#endif // DINO_UTIL_LOGGING_H
