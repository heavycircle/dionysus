#include <dino/util/logging.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <dino/core/thread.h>
#include <dino/core/time.h>

#define LINE_BYTES 1024
#define MS_PER_DAY 86400000ULL
#define UNRESOLVED (-1)

static atomic_int current_level = UNRESOLVED;
static FILE      *output;

// Held while a line is written and while the output is switched, so once
// dino_log_set_output returns no thread still writes to the old stream, and
// the caller may close it. A static ticket lock that yields: a mutex would be
// allocated once and never freed, which every dino_test leak check would
// report in a program that logs, and a plain spinlock lets threads that log
// without pause starve the others. Tickets are served in order.
static atomic_uint next_ticket;
static atomic_uint now_serving;

static void take_lock(void)
{
    unsigned ticket = atomic_fetch_add_explicit(&next_ticket, 1, memory_order_relaxed);
    while (atomic_load_explicit(&now_serving, memory_order_acquire) != ticket)
        dino_thread_yield();
}

static void drop_lock(void)
{
    atomic_fetch_add_explicit(&now_serving, 1, memory_order_release);
}

static const char *const level_names[]   = { "trace", "debug", "info", "warn", "error", "off" };
static const char        level_letters[] = "TDIWE";

static int names_equal(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != *b)
            return 0;
    return *a == *b;
}

static int default_level(void)
{
#ifdef NDEBUG
    int fallback = DINO_LOG_LEVEL_INFO;
#else
    int fallback = DINO_LOG_LEVEL_DEBUG;
#endif
    const char *env = getenv("DINO_LOG");
    if (!env)
        return fallback;
    for (int i = DINO_LOG_LEVEL_TRACE; i <= DINO_LOG_LEVEL_OFF; i++)
        if (names_equal(env, level_names[i]))
            return i;
    if (names_equal(env, "warning"))
        return DINO_LOG_LEVEL_WARN;
    // Said once, when the level is first needed, so a typo doesn't go unseen.
    fprintf(stderr,
        "dino: DINO_LOG=%s is not a log level (trace, debug, info, warn, error, off); using %s\n",
        env, level_names[fallback]);
    return fallback;
}

void dino_log_set_level(dino_log_level_t level)
{
    if (level >= DINO_LOG_LEVEL_TRACE && level <= DINO_LOG_LEVEL_OFF)
        atomic_store(&current_level, (int)level);
}

dino_log_level_t dino_log_get_level(void)
{
    int level = atomic_load(&current_level);
    if (level == UNRESOLVED)
    {
        // Losing the race means another thread resolved it or set it explicitly;
        // either way its value wins.
        int expected = UNRESOLVED;
        level        = default_level();
        if (!atomic_compare_exchange_strong(&current_level, &expected, level))
            level = expected;
    }
    return (dino_log_level_t)level;
}

void dino_log_set_output(FILE *stream)
{
    take_lock();
    output = stream;
    drop_lock();
}

static const char *base_name(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

void dino_log_write(dino_log_level_t level, const char *file, int line, const char *fmt, ...)
{
    if (level < dino_log_get_level() || level >= DINO_LOG_LEVEL_OFF || level < 0)
        return;
    // Callable from users' own wrappers, which may pass anything.
    if (!file)
        file = "?";
    if (!fmt)
        fmt = "";

    int  saved_errno = errno;
    char buf[LINE_BYTES];

    unsigned ms     = (unsigned)(dino_time_unix_ms() % MS_PER_DAY);
    int      prefix = snprintf(buf, sizeof buf, "%02u:%02u:%02u.%03u [%c] %s:%d: ", ms / 3600000,
             ms / 60000 % 60, ms / 1000 % 60, ms % 1000, level_letters[level], base_name(file), line);
    if (prefix < 0)
        prefix = 0;
    if ((size_t)prefix < sizeof buf)
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf + prefix, sizeof buf - (size_t)prefix, fmt, args);
        va_end(args);
    }

    // Some C libraries return -1 on truncation and leave the buffer unterminated.
    buf[sizeof buf - 1] = '\0';
    size_t len          = strlen(buf);
    if (len == 0 || buf[len - 1] != '\n')
    {
        if (len == sizeof buf - 1)
            len--;
        buf[len++] = '\n';
    }

    take_lock();
    FILE *out = output ? output : stderr;
    fwrite(buf, 1, len, out);
    fflush(out);
    drop_lock();
    errno = saved_errno;
}
