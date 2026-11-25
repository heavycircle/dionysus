#include <stdio.h>
#include <utest.h>

#include <dino/version.h>

// dino/version.h is generated, so nothing else compiles it.
UTEST(version, matches_the_numbers_and_cmake)
{
    char expected[64];
    snprintf(expected, sizeof(expected), "%d.%d.%d%s", DINO_VERSION_MAJOR, DINO_VERSION_MINOR,
        DINO_VERSION_PATCH, DINO_VERSION_PRERELEASE);
    EXPECT_STREQ(expected, DINO_VERSION);

    snprintf(expected, sizeof(expected), "%d.%d.%d", DINO_VERSION_MAJOR, DINO_VERSION_MINOR,
        DINO_VERSION_PATCH);
    EXPECT_STREQ(SDK_VERSION, expected);
    EXPECT_TRUE(DINO_VERSION_PRERELEASE[0] == '\0' || DINO_VERSION_PRERELEASE[0] == '-');
}

#if DINO_VERSION_MAJOR < 0 || DINO_VERSION_MINOR < 0 || DINO_VERSION_PATCH < 0
#error "the version numbers must be usable in #if"
#endif

UTEST_MAIN()
