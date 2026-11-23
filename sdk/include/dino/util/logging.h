#ifndef DINO_UTIL_LOGGING_H
#define DINO_UTIL_LOGGING_H

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    DINO_LOG_LEVEL_TRACE,
    DINO_LOG_LEVEL_DEBUG,
    DINO_LOG_LEVEL_INFO,
    DINO_LOG_LEVEL_WARN,
    DINO_LOG_LEVEL_ERROR,
    DINO_LOG_LEVEL_OFF,
} dino_log_level_t;

/**
 * Set the lowest level that is written.
 *
 * Overrides the DINO_LOG environment variable. Safe to call from any thread.
 *
 * @param level  DINO_LOG_LEVEL_TRACE through DINO_LOG_LEVEL_OFF; anything else
 *               is ignored.
 */
void dino_log_set_level(dino_log_level_t level);

/**
 * Get the lowest level that is written.
 *
 * Until dino_log_set_level() is called, this comes from the DINO_LOG
 * environment variable (trace, debug, info, warn or warning, error or off, any
 * case), read on first use; another value is reported once on stderr and
 * ignored. Without it the level is debug, or info under NDEBUG, which a
 * release build (dino's default) defines.
 *
 * @return The current level.
 */
dino_log_level_t dino_log_get_level(void);

/**
 * Send log lines to a stream instead of stderr.
 *
 * Once this returns, no thread writes to the previous stream any more, even
 * one that was logging meanwhile, so it may be closed: set NULL (or the new
 * stream), then fclose the old one.
 *
 * @param stream  Destination, or NULL for stderr.
 */
void dino_log_set_output(FILE *stream);

/**
 * Write one log line, if level is enabled. Normally called through the
 * DINO_LOG_* macros, which fill in the file and line.
 *
 * The line is "HH:MM:SS.mmm [L] file:line: message" (UTC time, file without
 * its directory), ending in a newline whether or not the format has one. It is
 * written with a single call, so lines from different threads don't mix. Lines
 * longer than 1 KB are truncated. errno is preserved.
 *
 * @param level  Level of this message.
 * @param file   Source file, usually __FILE__.
 * @param line   Source line, usually __LINE__.
 * @param fmt    printf format.
 */
void dino_log_write(dino_log_level_t level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * Log macros. The format must be a string literal.
 *
 * Under NDEBUG, DINO_LOG_TRACE and DINO_LOG_DEBUG print nothing, but their
 * arguments are still type-checked, so release builds report the same format
 * errors and no unused-variable warnings. Taking the format as part of
 * __VA_ARGS__ allows calls without arguments without relying on the GNU
 * ", ##__VA_ARGS__" extension.
 */
#define DINO__LOG_AT(level, ...) dino_log_write(level, __FILE__, __LINE__, __VA_ARGS__)

#ifdef NDEBUG
#define DINO_LOG_TRACE(...) ((void)(0 && printf(__VA_ARGS__)))
#define DINO_LOG_DEBUG(...) ((void)(0 && printf(__VA_ARGS__)))
#else
#define DINO_LOG_TRACE(...) DINO__LOG_AT(DINO_LOG_LEVEL_TRACE, __VA_ARGS__)
#define DINO_LOG_DEBUG(...) DINO__LOG_AT(DINO_LOG_LEVEL_DEBUG, __VA_ARGS__)
#endif // NDEBUG

#define DINO_LOG_INFO(...)  DINO__LOG_AT(DINO_LOG_LEVEL_INFO, __VA_ARGS__)
#define DINO_LOG_WARN(...)  DINO__LOG_AT(DINO_LOG_LEVEL_WARN, __VA_ARGS__)
#define DINO_LOG_ERROR(...) DINO__LOG_AT(DINO_LOG_LEVEL_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // DINO_UTIL_LOGGING_H
