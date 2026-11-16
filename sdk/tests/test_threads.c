#include <stdint.h>
#include <utest.h>

#include <dino/core/mutex.h>
#include <dino/core/thread.h>

#define NUM_THREADS 4
// Small enough to stay quick under qemu, large enough to race without a lock.
#define INCREMENTS 20000

static dino_mutex_t g_mutex;
static long         g_counter;

static DINO_THREAD_FUNC store_arg(void *arg)
{
    int *slot = arg;
    *slot     = 42;
    DINO_THREAD_RETURN;
}

static DINO_THREAD_FUNC increment(void *arg)
{
    int *errors = arg;
    for (int i = 0; i < INCREMENTS; i++)
    {
        if (dino_mutex_lock(g_mutex) != 0)
        {
            (*errors)++;
            continue;
        }
        g_counter++;
        if (dino_mutex_unlock(g_mutex) != 0)
            (*errors)++;
    }
    DINO_THREAD_RETURN;
}

static DINO_THREAD_FUNC try_lock(void *arg)
{
    int *result = arg;
    *result     = dino_mutex_trylock(g_mutex);
    if (*result == 0)
        dino_mutex_unlock(g_mutex);
    DINO_THREAD_RETURN;
}

UTEST(thread, runs_with_argument_and_joins)
{
    int           slot = 0;
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, store_arg, &slot));
    dino_thread_join(t);
    EXPECT_EQ(42, slot);
}

UTEST(mutex, create_lock_unlock_destroy)
{
    dino_mutex_t m = dino_mutex_create();
    ASSERT_NE(DINO_INVALID_MUTEX, m);
    EXPECT_EQ(0, dino_mutex_lock(m));
    EXPECT_EQ(0, dino_mutex_unlock(m));
    dino_mutex_destroy(m);
}

UTEST(mutex, is_recursive)
{
    dino_mutex_t m = dino_mutex_create();
    ASSERT_NE(DINO_INVALID_MUTEX, m);
    EXPECT_EQ(0, dino_mutex_lock(m));
    EXPECT_EQ(0, dino_mutex_lock(m));
    EXPECT_EQ(0, dino_mutex_trylock(m));
    EXPECT_EQ(0, dino_mutex_unlock(m));
    EXPECT_EQ(0, dino_mutex_unlock(m));
    EXPECT_EQ(0, dino_mutex_unlock(m));
    dino_mutex_destroy(m);
}

UTEST(mutex, trylock_fails_while_another_thread_holds_it)
{
    g_mutex = dino_mutex_create();
    ASSERT_NE(DINO_INVALID_MUTEX, g_mutex);

    int           result = 0;
    dino_thread_t t;
    ASSERT_EQ(0, dino_mutex_lock(g_mutex));
    ASSERT_EQ(0, dino_thread_create(&t, try_lock, &result));
    dino_thread_join(t);
    EXPECT_NE(0, result);

    ASSERT_EQ(0, dino_mutex_unlock(g_mutex));
    ASSERT_EQ(0, dino_thread_create(&t, try_lock, &result));
    dino_thread_join(t);
    EXPECT_EQ(0, result);

    dino_mutex_destroy(g_mutex);
}

UTEST(mutex, serializes_concurrent_increments)
{
    g_mutex   = dino_mutex_create();
    g_counter = 0;
    ASSERT_NE(DINO_INVALID_MUTEX, g_mutex);

    dino_thread_t threads[NUM_THREADS];
    int           errors[NUM_THREADS] = { 0 };
    for (int i = 0; i < NUM_THREADS; i++)
        ASSERT_EQ(0, dino_thread_create(&threads[i], increment, &errors[i]));
    for (int i = 0; i < NUM_THREADS; i++)
        dino_thread_join(threads[i]);

    for (int i = 0; i < NUM_THREADS; i++)
        EXPECT_EQ(0, errors[i]);
    EXPECT_EQ((long)NUM_THREADS * INCREMENTS, g_counter);
    dino_mutex_destroy(g_mutex);
}

UTEST(mutex, invalid_handle_is_rejected)
{
    EXPECT_NE(0, dino_mutex_lock(DINO_INVALID_MUTEX));
    EXPECT_NE(0, dino_mutex_unlock(DINO_INVALID_MUTEX));
    EXPECT_NE(0, dino_mutex_trylock(DINO_INVALID_MUTEX));
    dino_mutex_destroy(DINO_INVALID_MUTEX);
}

UTEST_MAIN()
