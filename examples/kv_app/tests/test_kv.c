// Unit tests for the store. On Linux targets dino_test() also fails any test
// that leaks, so kv_destroy must free every key and value.
#include <stdio.h>
#include <utest.h>

#include "kv.h"

UTEST(kv, starts_empty)
{
    struct kv *db = kv_create();
    ASSERT_TRUE(db != NULL);
    EXPECT_EQ(0u, kv_count(db));
    EXPECT_TRUE(kv_get(db, "missing") == NULL);
    kv_destroy(db);
}

UTEST(kv, set_then_get)
{
    struct kv *db = kv_create();
    ASSERT_EQ(0, kv_set(db, "name", "dino"));
    EXPECT_STREQ("dino", kv_get(db, "name"));
    EXPECT_EQ(1u, kv_count(db));
    kv_destroy(db);
}

UTEST(kv, set_replaces_and_copies)
{
    struct kv *db      = kv_create();
    char       value[] = "first";
    ASSERT_EQ(0, kv_set(db, "k", value));
    value[0] = 'X'; // the store keeps its own copy
    EXPECT_STREQ("first", kv_get(db, "k"));
    ASSERT_EQ(0, kv_set(db, "k", "second"));
    EXPECT_STREQ("second", kv_get(db, "k"));
    EXPECT_EQ(1u, kv_count(db));
    kv_destroy(db);
}

UTEST(kv, delete)
{
    struct kv *db = kv_create();
    kv_set(db, "a", "1");
    kv_set(db, "b", "2");
    EXPECT_EQ(0, kv_del(db, "a"));
    EXPECT_TRUE(kv_get(db, "a") == NULL);
    EXPECT_STREQ("2", kv_get(db, "b"));
    EXPECT_EQ(-1, kv_del(db, "a"));
    EXPECT_EQ(1u, kv_count(db));
    kv_destroy(db);
}

UTEST(kv, grows_past_initial_capacity)
{
    struct kv *db = kv_create();
    char       key[16], value[16];
    for (int i = 0; i < 100; i++)
    {
        snprintf(key, sizeof(key), "k%d", i);
        snprintf(value, sizeof(value), "v%d", i);
        ASSERT_EQ(0, kv_set(db, key, value));
    }
    EXPECT_EQ(100u, kv_count(db));
    EXPECT_STREQ("v0", kv_get(db, "k0"));
    EXPECT_STREQ("v99", kv_get(db, "k99"));
    kv_destroy(db);
}

UTEST(kv, destroy_accepts_null)
{
    kv_destroy(NULL);
    EXPECT_TRUE(1);
}

UTEST_MAIN()
