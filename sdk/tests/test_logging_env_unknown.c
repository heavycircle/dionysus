#include <stdio.h>
#include <string.h>
#include <utest.h>

#include <dino/util/logging.h>

#define ERR_FILE "test_logging_env_unknown.err"

// Its own executable, run with DINO_LOG=warnings (sdk/tests/CMakeLists.txt):
// a value that names no level is reported once on stderr and ignored.
UTEST(logging_env, an_unknown_value_is_reported_and_ignored)
{
    FILE *err = freopen(ERR_FILE, "w+", stderr);
    ASSERT_TRUE(err != NULL);
#ifdef NDEBUG
    EXPECT_EQ(DINO_LOG_LEVEL_INFO, dino_log_get_level());
#else
    EXPECT_EQ(DINO_LOG_LEVEL_DEBUG, dino_log_get_level());
#endif
    dino_log_get_level();
    fflush(err);
    rewind(err);
    char   text[512]  = { 0 };
    size_t n          = fread(text, 1, sizeof(text) - 1, err);
    text[n]           = '\0';
    const char *said  = "DINO_LOG=warnings is not a log level";
    const char *first = strstr(text, said);
    EXPECT_TRUE(first != NULL);
    // Once, however often the level is asked for.
    if (first)
        EXPECT_TRUE(strstr(first + strlen(said), said) == NULL);
    fclose(err);
    remove(ERR_FILE);
}

UTEST_MAIN()
