#include <stdio.h>
#include <utest.h>

#include <dino/util/logging.h>

#define LOG_FILE "test_logging_env_off.log"

// Its own executable, run with DINO_LOG=OFF (sdk/tests/CMakeLists.txt): not
// even errors are written.
UTEST(logging_env, off_writes_nothing)
{
    EXPECT_EQ(DINO_LOG_LEVEL_OFF, dino_log_get_level());
    FILE *f = fopen(LOG_FILE, "w+");
    ASSERT_TRUE(f != NULL);
    dino_log_set_output(f);
    DINO_LOG_ERROR("hidden");
    dino_log_set_output(NULL);
    EXPECT_EQ(0L, ftell(f));
    fclose(f);
    remove(LOG_FILE);
}

UTEST_MAIN()
