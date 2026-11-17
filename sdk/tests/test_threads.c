#include <stdatomic.h>
#include <stdint.h>
#include <utest.h>

#include <dino/core/mutex.h>
#include <dino/core/thread.h>
#include <dino/core/time.h>

#define NUM_THREADS 4
// Small enough to stay quick under qemu, large enough to race without a lock.
#define INCREMENTS 20000

static dino_mutex_t g_mutex;
static long         g_counter;

static int store_arg(void *arg)
{
    int *slot = arg;
    *slot     = 42;
    return 0;
}

static int increment(void *arg)
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
    return 0;
}

static int try_lock(void *arg)
{
    int *result = arg;
    *result     = dino_mutex_trylock(g_mutex);
    if (*result == 0)
        dino_mutex_unlock(g_mutex);
    return 0;
}

UTEST(thread, runs_with_argument_and_joins)
{
    int           slot = 0;
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, store_arg, &slot));
    dino_thread_join(t, NULL);
    EXPECT_EQ(42, slot);
}

static int square(void *arg)
{
    int n = (int)(intptr_t)arg;
    return n * n;
}

UTEST(thread, join_returns_the_result)
{
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, square, (void *)(intptr_t)12));
    int result = 0;
    EXPECT_EQ(0, dino_thread_join(t, &result));
    EXPECT_EQ(144, result);
}

UTEST(thread, many_threads_each_return_their_own_result)
{
    enum
    {
        COUNT = 32
    };
    dino_thread_t threads[COUNT];
    for (int i = 0; i < COUNT; i++)
        ASSERT_EQ(0, dino_thread_create(&threads[i], square, (void *)(intptr_t)i));
    for (int i = 0; i < COUNT; i++)
    {
        int result = -1;
        EXPECT_EQ(0, dino_thread_join(threads[i], &result));
        EXPECT_EQ(i * i, result);
    }
}

UTEST(thread, invalid_arguments_are_rejected)
{
    dino_thread_t t = (dino_thread_t)1;
    EXPECT_EQ(-1, dino_thread_create(&t, NULL, NULL));
    EXPECT_EQ(DINO_INVALID_THREAD, t);
    EXPECT_EQ(-1, dino_thread_join(DINO_INVALID_THREAD, NULL));
    EXPECT_EQ(-1, dino_thread_detach(DINO_INVALID_THREAD));
}

static atomic_int  g_go;
static atomic_int  g_done;
static atomic_uint g_ran;

static int wait_for_go(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_go))
        dino_thread_yield();
    atomic_fetch_add(&g_ran, 1);
    atomic_store(&g_done, 1);
    return 0;
}

static void wait_for_done(void)
{
    while (!atomic_load(&g_done))
        dino_sleep_ms(1);
    // The thread frees its block just after setting g_done; give it time to,
    // so the Linux leak check sees nothing live at exit.
    dino_sleep_ms(100);
}

// Detach while the thread is still running: the thread frees itself.
UTEST(thread, detach_before_the_thread_finishes)
{
    atomic_store(&g_go, 0);
    atomic_store(&g_done, 0);
    unsigned      before = atomic_load(&g_ran);
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, wait_for_go, NULL));
    EXPECT_EQ(0, dino_thread_detach(t));
    atomic_store(&g_go, 1);
    wait_for_done();
    EXPECT_EQ(before + 1, atomic_load(&g_ran));
}

// Detach after it finished: detach frees it.
UTEST(thread, detach_after_the_thread_finishes)
{
    atomic_store(&g_go, 1);
    atomic_store(&g_done, 0);
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, wait_for_go, NULL));
    wait_for_done();
    EXPECT_EQ(0, dino_thread_detach(t));
}

static dino_thread_t g_self;

static int join_self(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_go))
        dino_thread_yield();
    return dino_thread_join(g_self, NULL);
}

UTEST(thread, joining_itself_fails_instead_of_deadlocking)
{
    atomic_store(&g_go, 0);
    ASSERT_EQ(0, dino_thread_create(&g_self, join_self, NULL));
    atomic_store(&g_go, 1);
    int result = 0;
    EXPECT_EQ(0, dino_thread_join(g_self, &result));
    EXPECT_EQ(-1, result);
}

static int report_id(void *arg)
{
    *(uint64_t *)arg = dino_thread_current_id();
    return 0;
}

UTEST(thread, current_id_differs_between_threads)
{
    uint64_t      other = 0;
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, report_id, &other));
    ASSERT_EQ(0, dino_thread_join(t, NULL));
    EXPECT_NE(0u, other);
    EXPECT_NE(dino_thread_current_id(), other);
    EXPECT_EQ(dino_thread_current_id(), dino_thread_current_id());
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
    dino_thread_join(t, NULL);
    EXPECT_NE(0, result);

    ASSERT_EQ(0, dino_mutex_unlock(g_mutex));
    ASSERT_EQ(0, dino_thread_create(&t, try_lock, &result));
    dino_thread_join(t, NULL);
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
        dino_thread_join(threads[i], NULL);

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
