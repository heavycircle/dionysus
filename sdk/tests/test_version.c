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

UTEST(version, number_and_at_least)
{
    EXPECT_EQ(DINO_VERSION_MAJOR * 10000 + DINO_VERSION_MINOR * 100 + DINO_VERSION_PATCH,
        DINO_VERSION_NUMBER);
    EXPECT_TRUE(DINO_VERSION_AT_LEAST(DINO_VERSION_MAJOR, DINO_VERSION_MINOR, DINO_VERSION_PATCH));
    EXPECT_TRUE(DINO_VERSION_AT_LEAST(0, 9, 0));
    EXPECT_FALSE(
        DINO_VERSION_AT_LEAST(DINO_VERSION_MAJOR, DINO_VERSION_MINOR, DINO_VERSION_PATCH + 1));
    EXPECT_FALSE(DINO_VERSION_AT_LEAST(DINO_VERSION_MAJOR + 1, 0, 0));
}

// Usable where it's meant to be: in #if.
#if !DINO_VERSION_AT_LEAST(0, 1, 0) || DINO_VERSION_NUMBER < 900
#error "DINO_VERSION_AT_LEAST and DINO_VERSION_NUMBER must work in #if"
#endif

#if DINO_VERSION_MAJOR < 0 || DINO_VERSION_MINOR < 0 || DINO_VERSION_PATCH < 0
#error "the version numbers must be usable in #if"
#endif

UTEST_MAIN()
