#include <stdlib.h>
#include <utest.h>

#include <dino/util/logging.h>

// Its own executable, so nothing has set the level before this test.
UTEST(logging_default, without_dino_log_the_level_depends_on_ndebug)
{
    if (getenv("DINO_LOG"))
        UTEST_SKIP("DINO_LOG is set");
#ifdef NDEBUG
    EXPECT_EQ(DINO_LOG_LEVEL_INFO, dino_log_get_level());
#else
    EXPECT_EQ(DINO_LOG_LEVEL_DEBUG, dino_log_get_level());
#endif
}

UTEST_MAIN()
