#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <utest.h>

#include <dino/core/thread.h>
#include <dino/util/logging.h>

#define LOG_FILE         "test_logging.log"
#define NUM_THREADS      4
#define LINES_PER_THREAD 200

static size_t capture(char *buf, size_t size, void (*emit)(void))
{
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
    LOG_DEBUG("alias without arguments\n");
    LOG_INFO("alias with %d\n", value);
}

UTEST(logging, every_macro_with_and_without_arguments)
{
    char buf[4096];
    dino_log_set_level(DINO_LOG_LEVEL_TRACE);
    capture(buf, sizeof buf, emit_every_macro);

#ifdef NDEBUG
    // Trace, debug and the debug alias are compiled out.
    EXPECT_EQ(7u, count_lines(buf));
    EXPECT_EQ(NULL, strstr(buf, "[D]"));
#else
    EXPECT_EQ(12u, count_lines(buf));
    EXPECT_NE(NULL, strstr(buf, "[T] test_logging.c:"));
    EXPECT_NE(NULL, strstr(buf, "[D] test_logging.c:"));
#endif
    EXPECT_NE(NULL, strstr(buf, ": info with 7\n"));
    EXPECT_NE(NULL, strstr(buf, "[W] test_logging.c:"));
    EXPECT_NE(NULL, strstr(buf, "[E] test_logging.c:"));
    EXPECT_NE(NULL, strstr(buf, ": alias with 7\n"));
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
    EXPECT_LE(strlen(buf), 1023u);
    EXPECT_EQ('\n', buf[strlen(buf) - 1]);
}

static void emit_keeping_errno(void)
{
    errno = ERANGE;
    DINO_LOG_ERROR("errno is %d", errno);
}

UTEST(logging, errno_is_preserved)
{
    char buf[256];
    dino_log_set_level(DINO_LOG_LEVEL_INFO);
    capture(buf, sizeof buf, emit_keeping_errno);
    EXPECT_EQ(ERANGE, errno);
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
    dino_thread_t threads[NUM_THREADS];
    int           ids[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
    {
        ids[i] = i;
        if (dino_thread_create(&threads[i], log_lines, &ids[i]) != 0)
            thread_errors++;
    }
    for (int i = 0; i < NUM_THREADS; i++)
        dino_thread_join(threads[i], NULL);
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
