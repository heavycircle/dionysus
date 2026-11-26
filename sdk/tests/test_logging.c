#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <utest.h>

#include <dino/util/logging.h>

#include "../src/core/thread_internal.h"

#define LOG_FILE         "test_logging.log"
#define NUM_THREADS      4
#define LINES_PER_THREAD 200

// Empty when the log file can't be made, so callers' checks fail instead of
// reading an unset buffer.
static size_t capture(char *buf, size_t size, void (*emit)(void))
{
    buf[0]  = '\0';
    FILE *f = fopen(LOG_FILE, "w+");
    if (!f)
        return 0;
    dino_log_set_output(f);
    emit();
    dino_log_set_output(NULL);
    rewind(f);
    size_t n = fread(buf, 1, size - 1, f);
    buf[n]   = '\0';
    fclose(f);
    remove(LOG_FILE);
    return n;
}

static size_t count_lines(const char *s)
{
    size_t lines = 0;
    for (; *s; s++)
        lines += *s == '\n';
    return lines;
}

static void emit_every_macro(void)
{
    int value = 7;
    DINO_LOG_TRACE("trace without arguments");
    DINO_LOG_TRACE("trace with %d", value);
    DINO_LOG_DEBUG("debug without arguments");
    DINO_LOG_DEBUG("debug with %d", value);
    DINO_LOG_INFO("info without arguments");
    DINO_LOG_INFO("info with %d", value);
    DINO_LOG_WARN("warn without arguments");
    DINO_LOG_WARN("warn with %d", value);
    DINO_LOG_ERROR("error without arguments");
    DINO_LOG_ERROR("error with %d", value);
}

UTEST(logging, every_macro_with_and_without_arguments)
{
    char buf[4096];
    dino_log_set_level(DINO_LOG_LEVEL_TRACE);
    capture(buf, sizeof buf, emit_every_macro);

#ifdef NDEBUG
    // Trace and debug are compiled out.
    EXPECT_EQ(6u, count_lines(buf));
    EXPECT_EQ(NULL, strstr(buf, "[D]"));
#else
    EXPECT_EQ(10u, count_lines(buf));
    EXPECT_NE(NULL, strstr(buf, "[T] test_logging.c:"));
    EXPECT_NE(NULL, strstr(buf, "[D] test_logging.c:"));
#endif
    EXPECT_NE(NULL, strstr(buf, ": info with 7\n"));
    EXPECT_NE(NULL, strstr(buf, "[W] test_logging.c:"));
    EXPECT_NE(NULL, strstr(buf, "[E] test_logging.c:"));
}

static int emit_line;

static void emit_one(void)
{
    emit_line = __LINE__ + 1;
    DINO_LOG_INFO("value %d", 42);
}

UTEST(logging, line_format)
{
    char buf[256];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    capture(buf, sizeof buf, emit_one);

    unsigned h, m, s, ms;
    char     letter, file[64];
    int      line, value;
    ASSERT_EQ(8,
        sscanf(buf, "%2u:%2u:%2u.%3u [%c] %63[^:]:%d: value %d", &h, &m, &s, &ms, &letter, file,
            &line, &value));
    EXPECT_LT(h, 24u);
    EXPECT_LT(m, 60u);
    EXPECT_LT(s, 60u);
    EXPECT_EQ('I', letter);
    EXPECT_STREQ("test_logging.c", file);
    EXPECT_EQ(emit_line, line);
    EXPECT_EQ(42, value);
    EXPECT_EQ(1u, count_lines(buf));
    EXPECT_EQ('\n', buf[strlen(buf) - 1]);
}

static void emit_all_levels(void)
{
    DINO_LOG_INFO("dropped");
    DINO_LOG_WARN("kept");
    DINO_LOG_ERROR("kept");
}

UTEST(logging, level_filters_lower_levels)
{
    char buf[1024];
    dino_log_set_level(DINO_LOG_LEVEL_WARN);
    EXPECT_EQ(DINO_LOG_LEVEL_WARN, dino_log_get_level());
    capture(buf, sizeof buf, emit_all_levels);
    EXPECT_EQ(2u, count_lines(buf));
    EXPECT_EQ(NULL, strstr(buf, "dropped"));

    dino_log_set_level(DINO_LOG_LEVEL_OFF);
    capture(buf, sizeof buf, emit_all_levels);
    EXPECT_STREQ("", buf);
}

UTEST(logging, invalid_level_is_ignored)
{
    dino_log_set_level(DINO_LOG_LEVEL_ERROR);
    dino_log_set_level((dino_log_level_t)99);
    dino_log_set_level((dino_log_level_t)-5);
    EXPECT_EQ(DINO_LOG_LEVEL_ERROR, dino_log_get_level());
}

// Users' own wrappers can pass anything; a missing file name or format
// mustn't crash. volatile keeps the compiler from seeing the NULL format.
static const char *volatile g_no_file;
static const char *volatile g_no_format;

static void emit_nulls(void)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    dino_log_write(DINO_LOG_LEVEL_ERROR, g_no_file, 7, g_no_format);
#pragma GCC diagnostic pop
}

UTEST(logging, missing_file_and_format_are_tolerated)
{
    dino_log_set_level(DINO_LOG_LEVEL_TRACE);
    char buf[256];
    EXPECT_GT(capture(buf, sizeof(buf), emit_nulls), 0u);
    EXPECT_TRUE(strstr(buf, "?:7:") != NULL);
}

static void emit_long(void)
{
    char big[3000];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    DINO_LOG_ERROR("%s", big);
}

UTEST(logging, long_lines_are_truncated_with_a_newline)
{
    char buf[4096];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    capture(buf, sizeof buf, emit_long);
    EXPECT_EQ(1u, count_lines(buf));
    EXPECT_EQ(1023u, strlen(buf));
    EXPECT_NE(NULL, strstr(buf, "[E] test_logging.c:"));
    EXPECT_EQ('\n', buf[strlen(buf) - 1]);
}

static void emit_with_newline(void)
{
    DINO_LOG_INFO("already ends\n");
}

UTEST(logging, a_format_ending_in_a_newline_gets_no_second_one)
{
    char buf[256];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    capture(buf, sizeof buf, emit_with_newline);
    EXPECT_EQ(1u, count_lines(buf));
    EXPECT_NE(NULL, strstr(buf, ": already ends\n"));
}

// "HH:MM:SS.mmm [I] f.c:1: " is 24 bytes, so a message of 998 fills the 1024
// byte buffer exactly, with the newline and the terminator.
static char message[1100];

static void emit_message(void)
{
    dino_log_write(DINO_LOG_LEVEL_INFO, "f.c", 1, "%s", message);
}

static size_t xs(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        n += *s == 'x';
    return n;
}

UTEST(logging, lines_at_the_length_limit)
{
    char buf[2048];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);

    memset(message, 'x', 998);
    message[998] = '\0';
    EXPECT_EQ(1023u, capture(buf, sizeof buf, emit_message));
    EXPECT_EQ(998u, xs(buf));
    EXPECT_EQ('\n', buf[1022]);

    // One more is one too many: the last x makes way for the newline.
    message[998] = 'x';
    message[999] = '\0';
    EXPECT_EQ(1023u, capture(buf, sizeof buf, emit_message));
    EXPECT_EQ(998u, xs(buf));

    // A newline of its own that just fits stays the only one.
    message[997] = '\n';
    message[998] = '\0';
    EXPECT_EQ(1022u, capture(buf, sizeof buf, emit_message));
    EXPECT_EQ(1u, count_lines(buf));
}

static void emit_windows_path(void)
{
    dino_log_write(DINO_LOG_LEVEL_WARN, "C:\\src\\lib\\net.c", 12, "msg");
}

UTEST(logging, a_windows_path_is_cut_to_its_file_name)
{
    char buf[256];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    capture(buf, sizeof buf, emit_windows_path);
    EXPECT_NE(NULL, strstr(buf, "[W] net.c:12: msg\n"));
}

static void emit_bad_levels(void)
{
    dino_log_write(DINO_LOG_LEVEL_OFF, "f.c", 1, "off");
    dino_log_write((dino_log_level_t)99, "f.c", 1, "too high");
    dino_log_write((dino_log_level_t)-1, "f.c", 1, "negative");
}

// What isn't a level to write at writes nothing, even with everything enabled.
UTEST(logging, writing_at_off_or_an_invalid_level_writes_nothing)
{
    char buf[256];
    dino_log_set_level(DINO_LOG_LEVEL_TRACE);
    EXPECT_EQ(0u, capture(buf, sizeof buf, emit_bad_levels));
}

struct received
{
    int              calls;
    dino_log_level_t level;
    char             file[64];
    int              line;
    char             message[1100];
};

static void receive(
    dino_log_level_t level, const char *file, int line, const char *message, void *userdata)
{
    struct received *r = userdata;
    r->calls++;
    r->level = level;
    snprintf(r->file, sizeof(r->file), "%s", file);
    r->line = line;
    snprintf(r->message, sizeof(r->message), "%s", message);
    errno = EBADF; // the logger restores the caller's
}

UTEST(logging, a_callback_gets_each_message_instead_of_the_stream)
{
    struct received r = { 0 };
    char            buf[256];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    dino_log_set_callback(receive, &r);
    errno    = ERANGE;
    int line = __LINE__ + 1;
    DINO_LOG_WARN("to the callback %d\n", 3);
    EXPECT_EQ(ERANGE, errno);
    DINO_LOG_DEBUG("filtered out");
    EXPECT_EQ(1, r.calls);
    EXPECT_EQ(DINO_LOG_LEVEL_WARN, r.level);
    EXPECT_STREQ("test_logging.c", r.file);
    EXPECT_EQ(line, r.line);
    // The message alone: no time, level or place, and no newline.
    EXPECT_STREQ("to the callback 3", r.message);

    // While a callback is set, the stream gets nothing; NULL brings it back.
    EXPECT_EQ(0u, capture(buf, sizeof buf, emit_one));
    EXPECT_EQ(2, r.calls);
    EXPECT_STREQ("value 42", r.message);
    dino_log_set_callback(NULL, NULL);
    EXPECT_NE(0u, capture(buf, sizeof buf, emit_one));
    EXPECT_EQ(2, r.calls);
}

static void emit_keeping_errno(void)
{
    errno = ERANGE;
    DINO_LOG_ERROR("errno is %d", errno);
}

// Into a stream opened for reading, so the write itself fails and sets errno:
// only a restore can leave the caller's value.
UTEST(logging, errno_is_preserved)
{
    FILE *f = fopen(LOG_FILE, "w");
    ASSERT_TRUE(f != NULL);
    fclose(f);
    f = fopen(LOG_FILE, "r");
    ASSERT_TRUE(f != NULL);
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    dino_log_set_output(f);
    emit_keeping_errno();
    int after = errno;
    dino_log_set_output(NULL);
    fclose(f);
    remove(LOG_FILE);
    EXPECT_EQ(ERANGE, after);
}

static int log_lines(void *arg)
{
    int id = *(int *)arg;
    for (int i = 0; i < LINES_PER_THREAD; i++)
        DINO_LOG_INFO("thread %d line %d end", id, i);
    return 0;
}

static int thread_errors;

static void emit_from_threads(void)
{
    dino__thread_t threads[NUM_THREADS];
    int            ids[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
    {
        ids[i] = i;
        if (dino__thread_create(&threads[i], log_lines, &ids[i]) != 0)
            thread_errors++;
    }
    for (int i = 0; i < NUM_THREADS; i++)
        dino__thread_join(threads[i], NULL);
}

static volatile int keep_logging;

static int log_until_stopped(void *arg)
{
    (void)arg;
    while (keep_logging)
        DINO_LOG_INFO("still logging");
    return 0;
}

// Once set_output returns, no thread writes to the old stream, so closing it
// right away is safe while other threads keep logging.
UTEST(logging, output_can_be_switched_and_closed_while_threads_log)
{
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    dino__thread_t threads[NUM_THREADS];
    keep_logging = 1;
    for (int i = 0; i < NUM_THREADS; i++)
        ASSERT_EQ(0, dino__thread_create(&threads[i], log_until_stopped, NULL));
    FILE *current = NULL;
    for (int i = 0; i < 200; i++)
    {
        FILE *next = fopen(LOG_FILE, "w");
        ASSERT_TRUE(next != NULL);
        dino_log_set_output(next);
        if (current)
            fclose(current);
        current = next;
    }
    dino_log_set_output(NULL);
    fclose(current);
    keep_logging = 0;
    for (int i = 0; i < NUM_THREADS; i++)
        dino__thread_join(threads[i], NULL);
    remove(LOG_FILE);
}

// Each line is one write, so lines from different threads never interleave.
UTEST(logging, threads_write_whole_lines)
{
    static char buf[NUM_THREADS * LINES_PER_THREAD * 96];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    thread_errors = 0;
    capture(buf, sizeof buf, emit_from_threads);
    ASSERT_EQ(0, thread_errors);
    EXPECT_EQ((size_t)NUM_THREADS * LINES_PER_THREAD, count_lines(buf));

    int bad = 0;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n"))
    {
        const char *msg = strstr(line, ": ");
        int         id, i, end = 0;
        if (!msg || sscanf(msg + 2, "thread %d line %d end%n", &id, &i, &end) != 2 || !end
            || line[strlen(line) - 1] != 'd')
            bad++;
    }
    EXPECT_EQ(0, bad);
}

UTEST_MAIN()
