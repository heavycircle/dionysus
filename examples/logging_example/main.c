#include <dino/util/logging.h>

int main(void)
{
    // DINO_LOG=trace|debug|info|warn|error|off picks the level at run time;
    // dino_log_set_level() overrides it. Debug and trace vanish under NDEBUG.
    DINO_LOG_DEBUG("starting, level %d", (int)dino_log_get_level());
    DINO_LOG_INFO("lines go to stderr with the time, level, file and line");
    DINO_LOG_WARN("set DINO_LOG=error to hide this one");

    dino_log_set_level(DINO_LOG_LEVEL_ERROR);
    DINO_LOG_INFO("hidden: below the level set in code");
    DINO_LOG_ERROR("errors still show");

    return dino_log_get_level() == DINO_LOG_LEVEL_ERROR ? 0 : 1;
}
