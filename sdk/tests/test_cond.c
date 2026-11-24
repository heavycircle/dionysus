#include <pthread.h>
#include <utest.h>

#include <dino/core/thread.h>
#include <dino/core/time.h>

// Threads are plain pthreads; dino_cond_init and dino_cond_timedwait add the
// monotonic timed wait that pthread_condattr_setclock can't give on Windows.

struct shared
{
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             ready;
};

static void *signal_later(void *arg)
{
    struct shared *s = arg;
    dino_sleep_ms(20);
    pthread_mutex_lock(&s->mutex);
    s->ready = 1;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return NULL;
}

UTEST(cond, timedwait_wakes_on_signal)
{
    struct shared s = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
    ASSERT_EQ(0, dino_cond_init(&s.cond));
    pthread_t t;
    ASSERT_EQ(0, pthread_create(&t, NULL, signal_later, &s));
    pthread_mutex_lock(&s.mutex);
    int rc = 0;
    while (!s.ready && rc == 0)
        rc = dino_cond_timedwait(&s.cond, &s.mutex, 5000);
    pthread_mutex_unlock(&s.mutex);
    EXPECT_EQ(0, rc);
    EXPECT_EQ(1, s.ready);
    pthread_join(t, NULL);
    pthread_cond_destroy(&s.cond);
}

UTEST(cond, timedwait_times_out)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  cond;
    ASSERT_EQ(0, dino_cond_init(&cond));
    pthread_mutex_lock(&mutex);
    uint64_t start = dino_time_monotonic_ns();
    EXPECT_EQ(DINO_TIMEOUT, dino_cond_timedwait(&cond, &mutex, 30));
    EXPECT_GE(dino_time_monotonic_ns() - start, 25000000u);
    EXPECT_EQ(DINO_TIMEOUT, dino_cond_timedwait(&cond, &mutex, 0));
    pthread_mutex_unlock(&mutex);
    pthread_cond_destroy(&cond);
}

UTEST(cond, bad_arguments_fail)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  cond;
    EXPECT_EQ(-1, dino_cond_init(NULL));
    ASSERT_EQ(0, dino_cond_init(&cond));
    EXPECT_EQ(-1, dino_cond_timedwait(NULL, &mutex, 0));
    EXPECT_EQ(-1, dino_cond_timedwait(&cond, NULL, 0));
    EXPECT_EQ(-1, dino_cond_timedwait(&cond, &mutex, -2));
    pthread_cond_destroy(&cond);
}

static void *record_id(void *arg)
{
    *(uint64_t *)arg = dino_thread_current_id();
    return NULL;
}

UTEST(thread, current_id_differs_between_threads)
{
    uint64_t  other = 0;
    pthread_t t;
    ASSERT_EQ(0, pthread_create(&t, NULL, record_id, &other));
    pthread_join(t, NULL);
    EXPECT_NE(0u, dino_thread_current_id());
    EXPECT_NE(0u, other);
    EXPECT_NE(other, dino_thread_current_id());
}

UTEST_MAIN()
