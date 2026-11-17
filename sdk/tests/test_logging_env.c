#include <utest.h>

#include <dino/util/logging.h>

// Its own executable, run with DINO_LOG=WaRn (sdk/tests/CMakeLists.txt): the
// variable is read once, on first use, so nothing may log before this test.
UTEST(logging_env, dino_log_sets_the_level_until_overridden)
{
    EXPECT_EQ(DINO_LOG_LEVEL_WARN, dino_log_get_level());
    dino_log_set_level(DINO_LOG_LEVEL_ERROR);
    EXPECT_EQ(DINO_LOG_LEVEL_ERROR, dino_log_get_level());
}

UTEST_MAIN()
