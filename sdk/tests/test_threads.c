// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <windows.h>
#else
#define _GNU_SOURCE // pthread_getattr_np
#include <pthread.h>
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <utest.h>

#include <dino/core/error.h>
#include <dino/core/time.h>

#include "../src/core/mutex_internal.h"
#include "../src/core/thread_internal.h"

#define NUM_THREADS 4
// Small enough to stay quick under qemu, large enough to race without a lock.
#define INCREMENTS 20000

static dino__mutex_t g_mutex;
static long          g_counter;

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
        if (dino__mutex_lock(g_mutex) != 0)
        {
            (*errors)++;
            continue;
        }
        g_counter++;
        if (dino__mutex_unlock(g_mutex) != 0)
            (*errors)++;
    }
    return 0;
}

static int try_lock(void *arg)
{
    int *result = arg;
    *result     = dino__mutex_trylock(g_mutex);
    if (*result == 0)
        dino__mutex_unlock(g_mutex);
    return 0;
}

UTEST(thread, runs_with_argument_and_joins)
{
    int            slot = 0;
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, store_arg, &slot));
    dino__thread_join(t, NULL);
    EXPECT_EQ(42, slot);
}

static int square(void *arg)
{
    int n = (int)(intptr_t)arg;
    return n * n;
}

UTEST(thread, join_returns_the_result)
{
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, square, (void *)(intptr_t)12));
    int result = 0;
    EXPECT_EQ(0, dino__thread_join(t, &result));
    EXPECT_EQ(144, result);
}

UTEST(thread, many_threads_each_return_their_own_result)
{
    enum
    {
        COUNT = 32
    };
    dino__thread_t threads[COUNT];
    for (int i = 0; i < COUNT; i++)
        ASSERT_EQ(0, dino__thread_create(&threads[i], square, (void *)(intptr_t)i));
    for (int i = 0; i < COUNT; i++)
    {
        int result = -1;
        EXPECT_EQ(0, dino__thread_join(threads[i], &result));
        EXPECT_EQ(i * i, result);
    }
}

UTEST(thread, invalid_arguments_are_rejected)
{
    dino__thread_t t = (dino__thread_t)1;
    EXPECT_EQ(-1, dino__thread_create(&t, NULL, NULL));
    EXPECT_EQ(DINO__INVALID_THREAD, t);
    EXPECT_EQ(-1, dino__thread_create(NULL, NULL, NULL));
    EXPECT_EQ(-1, dino__thread_join(DINO__INVALID_THREAD, NULL));
    EXPECT_EQ(-1, dino__thread_detach(DINO__INVALID_THREAD));
}

// A thread can use most of its 8 MB, on every platform: Windows threads had
// the executable's 16 MB and Linux ones 8 MB, so code that fit on one didn't
// necessarily fit on the other.
static int use_stack(void *arg)
{
    (void)arg;
    volatile char big[6 * 1024 * 1024];
    for (size_t i = 0; i < sizeof(big); i += 4096)
        big[i] = (char)i;
    return big[sizeof(big) - 4096] == (char)(sizeof(big) - 4096) ? 7 : 0;
}

// What the system reserved for the calling thread's stack.
static size_t own_stack_bytes(void)
{
#ifdef _WIN32
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return (size_t)(high - low);
#else
    pthread_attr_t attr;
    size_t         size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) != 0)
        return 0;
    pthread_attr_getstacksize(&attr, &size);
    pthread_attr_destroy(&attr);
    return size;
#endif
}

static int report_stack(void *arg)
{
    *(size_t *)arg = own_stack_bytes();
    return 0;
}

UTEST(thread, stack_is_eight_megabytes_everywhere)
{
    dino__thread_t t;
    size_t         size = 0;
    ASSERT_EQ(0, dino__thread_create(&t, report_stack, &size));
    ASSERT_EQ(0, dino__thread_join(t, NULL));
    // The system may round up, or add a guard page.
    EXPECT_GE(size, (size_t)8 * 1024 * 1024);
    EXPECT_LE(size, (size_t)8 * 1024 * 1024 + 64 * 1024);
}

UTEST(thread, stack_holds_six_megabytes)
{
    dino__thread_t t;
    int            result = 0;
    ASSERT_EQ(0, dino__thread_create(&t, use_stack, NULL));
    ASSERT_EQ(0, dino__thread_join(t, &result));
    EXPECT_EQ(7, result);
}

static atomic_int  g_go;
static atomic_int  g_done;
static atomic_uint g_ran;

static int wait_for_go(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_go))
        dino__thread_yield();
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
    unsigned       before = atomic_load(&g_ran);
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, wait_for_go, NULL));
    EXPECT_EQ(0, dino__thread_detach(t));
    atomic_store(&g_go, 1);
    wait_for_done();
    EXPECT_EQ(before + 1, atomic_load(&g_ran));
}

// Detach after it finished: detach frees it.
UTEST(thread, detach_after_the_thread_finishes)
{
    atomic_store(&g_go, 1);
    atomic_store(&g_done, 0);
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, wait_for_go, NULL));
    wait_for_done();
    EXPECT_EQ(0, dino__thread_detach(t));
}

static dino__thread_t g_self;

static int join_self(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_go))
        dino__thread_yield();
    return dino__thread_join(g_self, NULL);
}

UTEST(thread, joining_itself_fails_instead_of_deadlocking)
{
    atomic_store(&g_go, 0);
    ASSERT_EQ(0, dino__thread_create(&g_self, join_self, NULL));
    atomic_store(&g_go, 1);
    int result = 0;
    EXPECT_EQ(0, dino__thread_join(g_self, &result));
    EXPECT_EQ(-1, result);
}

static int report_id(void *arg)
{
    *(uint64_t *)arg = dino_thread_current_id();
    return 0;
}

UTEST(thread, current_id_differs_between_threads)
{
    uint64_t       other = 0;
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, report_id, &other));
    ASSERT_EQ(0, dino__thread_join(t, NULL));
    EXPECT_NE(0u, other);
    EXPECT_NE(dino_thread_current_id(), other);
    EXPECT_EQ(dino_thread_current_id(), dino_thread_current_id());
}

UTEST(mutex, create_lock_unlock_destroy)
{
    dino__mutex_t m = dino__mutex_create();
    ASSERT_NE(DINO__INVALID_MUTEX, m);
    EXPECT_EQ(0, dino__mutex_lock(m));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    dino__mutex_destroy(m);
}

UTEST(mutex, is_recursive)
{
    dino__mutex_t m = dino__mutex_create();
    ASSERT_NE(DINO__INVALID_MUTEX, m);
    EXPECT_EQ(0, dino__mutex_lock(m));
    EXPECT_EQ(0, dino__mutex_lock(m));
    EXPECT_EQ(0, dino__mutex_trylock(m));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    dino__mutex_destroy(m);
}

// Busy is "not now", the answer of every wait that runs out of time, and not
// a failure: -1 is for a handle that isn't a mutex.
UTEST(mutex, trylock_times_out_while_another_thread_holds_it)
{
    g_mutex = dino__mutex_create();
    ASSERT_NE(DINO__INVALID_MUTEX, g_mutex);

    int            result = 0;
    dino__thread_t t;
    ASSERT_EQ(0, dino__mutex_lock(g_mutex));
    ASSERT_EQ(0, dino__thread_create(&t, try_lock, &result));
    dino__thread_join(t, NULL);
    EXPECT_EQ(DINO_TIMEOUT, result);

    ASSERT_EQ(0, dino__mutex_unlock(g_mutex));
    ASSERT_EQ(0, dino__thread_create(&t, try_lock, &result));
    dino__thread_join(t, NULL);
    EXPECT_EQ(0, result);

    dino__mutex_destroy(g_mutex);
}

UTEST(mutex, serializes_concurrent_increments)
{
    g_mutex   = dino__mutex_create();
    g_counter = 0;
    ASSERT_NE(DINO__INVALID_MUTEX, g_mutex);

    dino__thread_t threads[NUM_THREADS];
    int            errors[NUM_THREADS] = { 0 };
    for (int i = 0; i < NUM_THREADS; i++)
        ASSERT_EQ(0, dino__thread_create(&threads[i], increment, &errors[i]));
    for (int i = 0; i < NUM_THREADS; i++)
        dino__thread_join(threads[i], NULL);

    for (int i = 0; i < NUM_THREADS; i++)
        EXPECT_EQ(0, errors[i]);
    EXPECT_EQ((long)NUM_THREADS * INCREMENTS, g_counter);
    dino__mutex_destroy(g_mutex);
}

static int unlock_unowned(void *arg)
{
    return dino__mutex_unlock(*(dino__mutex_t *)arg);
}

UTEST(mutex, unlock_by_another_thread_fails_and_keeps_the_lock)
{
    g_mutex = dino__mutex_create();
    ASSERT_NE(DINO__INVALID_MUTEX, g_mutex);
    ASSERT_EQ(0, dino__mutex_lock(g_mutex));

    dino__thread_t t;
    int            result = 0;
    ASSERT_EQ(0, dino__thread_create(&t, unlock_unowned, &g_mutex));
    ASSERT_EQ(0, dino__thread_join(t, &result));
    EXPECT_EQ(-1, result);

    // Still held by this thread: another one can't take it.
    ASSERT_EQ(0, dino__thread_create(&t, try_lock, &result));
    dino__thread_join(t, NULL);
    EXPECT_NE(0, result);

    EXPECT_EQ(0, dino__mutex_unlock(g_mutex));
    dino__mutex_destroy(g_mutex);
}

UTEST(mutex, unlock_without_lock_fails)
{
    dino__mutex_t m = dino__mutex_create();
    ASSERT_NE(DINO__INVALID_MUTEX, m);
    EXPECT_EQ(-1, dino__mutex_unlock(m));

    // One unlock too many after a recursive lock.
    ASSERT_EQ(0, dino__mutex_lock(m));
    ASSERT_EQ(0, dino__mutex_lock(m));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    EXPECT_EQ(-1, dino__mutex_unlock(m));

    // And the mutex still works.
    ASSERT_EQ(0, dino__mutex_trylock(m));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    dino__mutex_destroy(m);
}

UTEST(mutex, invalid_handle_is_rejected)
{
    EXPECT_NE(0, dino__mutex_lock(DINO__INVALID_MUTEX));
    EXPECT_NE(0, dino__mutex_unlock(DINO__INVALID_MUTEX));
    EXPECT_EQ(-1, dino__mutex_trylock(DINO__INVALID_MUTEX));
    dino__mutex_destroy(DINO__INVALID_MUTEX);
}

/* ---- Condition variables ---- */

UTEST(cond, create_destroy_and_invalid_handles)
{
    dino__cond_t c = dino__cond_create();
    ASSERT_NE(DINO__INVALID_COND, c);
    dino__mutex_t m = dino__mutex_create();
    ASSERT_NE(DINO__INVALID_MUTEX, m);

    EXPECT_EQ(-1, dino__cond_wait(DINO__INVALID_COND, m));
    EXPECT_EQ(-1, dino__cond_wait(c, DINO__INVALID_MUTEX));
    EXPECT_EQ(-1, dino__cond_timedwait(DINO__INVALID_COND, m, 1));
    EXPECT_EQ(-1, dino__cond_signal(DINO__INVALID_COND));
    EXPECT_EQ(-1, dino__cond_broadcast(DINO__INVALID_COND));
    EXPECT_EQ(0, dino__cond_signal(c));
    EXPECT_EQ(0, dino__cond_broadcast(c));

    dino__cond_destroy(c);
    dino__cond_destroy(DINO__INVALID_COND);
    dino__mutex_destroy(m);
}

#define QUEUE_SIZE 8
#define ITEMS      2000

// A bounded queue: the producer waits while it's full, the consumer while it's empty.
UTEST(cond, waiting_on_an_unowned_mutex_fails)
{
    dino__mutex_t m = dino__mutex_create();
    dino__cond_t  c = dino__cond_create();
    ASSERT_NE(DINO__INVALID_MUTEX, m);
    ASSERT_NE(DINO__INVALID_COND, c);
    EXPECT_EQ(-1, dino__cond_wait(c, m));
    EXPECT_EQ(-1, dino__cond_timedwait(c, m, 10));
    dino__cond_destroy(c);
    dino__mutex_destroy(m);
}

// A wait releases one level of the recursive mutex, so at depth two it would
// sleep with the mutex still locked and nobody could signal it: refused.
UTEST(cond, waiting_on_a_mutex_held_twice_fails)
{
    dino__mutex_t m = dino__mutex_create();
    dino__cond_t  c = dino__cond_create();
    ASSERT_NE(DINO__INVALID_MUTEX, m);
    ASSERT_NE(DINO__INVALID_COND, c);
    ASSERT_EQ(0, dino__mutex_lock(m));
    ASSERT_EQ(0, dino__mutex_lock(m));
    EXPECT_EQ(-1, dino__cond_wait(c, m));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    EXPECT_EQ(-1, dino__cond_timedwait(c, m, 10));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    // Held once, a wait works as before.
    EXPECT_EQ(DINO_TIMEOUT, dino__cond_timedwait(c, m, 10));
    EXPECT_EQ(0, dino__mutex_unlock(m));
    EXPECT_EQ(-1, dino__mutex_unlock(m));
    dino__cond_destroy(c);
    dino__mutex_destroy(m);
}

struct queue
{
    dino__mutex_t lock;
    dino__cond_t  not_empty, not_full;
    int           items[QUEUE_SIZE];
    int           head, count;
};

static int produce(void *arg)
{
    struct queue *q = arg;
    for (int i = 1; i <= ITEMS; i++)
    {
        dino__mutex_lock(q->lock);
        while (q->count == QUEUE_SIZE)
            dino__cond_wait(q->not_full, q->lock);
        q->items[(q->head + q->count++) % QUEUE_SIZE] = i;
        dino__cond_signal(q->not_empty);
        dino__mutex_unlock(q->lock);
    }
    return 0;
}

UTEST(cond, producer_and_consumer_hand_over_every_item_in_order)
{
    struct queue q = { 0 };
    q.lock         = dino__mutex_create();
    q.not_empty    = dino__cond_create();
    q.not_full     = dino__cond_create();
    ASSERT_NE(DINO__INVALID_MUTEX, q.lock);
    ASSERT_NE(DINO__INVALID_COND, q.not_empty);
    ASSERT_NE(DINO__INVALID_COND, q.not_full);

    dino__thread_t producer;
    ASSERT_EQ(0, dino__thread_create(&producer, produce, &q));
    int out_of_order = 0;
    for (int expected = 1; expected <= ITEMS; expected++)
    {
        dino__mutex_lock(q.lock);
        while (q.count == 0)
            dino__cond_wait(q.not_empty, q.lock);
        int item = q.items[q.head];
        q.head   = (q.head + 1) % QUEUE_SIZE;
        q.count--;
        dino__cond_signal(q.not_full);
        dino__mutex_unlock(q.lock);
        out_of_order += item != expected;
    }
    EXPECT_EQ(0, dino__thread_join(producer, NULL));
    EXPECT_EQ(0, out_of_order);

    dino__cond_destroy(q.not_full);
    dino__cond_destroy(q.not_empty);
    dino__mutex_destroy(q.lock);
}

struct gate
{
    dino__mutex_t lock;
    dino__cond_t  cond;
    int           open, waiting;
};

static int wait_at_gate(void *arg)
{
    struct gate *g = arg;
    dino__mutex_lock(g->lock);
    g->waiting++;
    dino__cond_broadcast(g->cond);
    while (!g->open)
        dino__cond_wait(g->cond, g->lock);
    dino__mutex_unlock(g->lock);
    return 1;
}

UTEST(cond, broadcast_wakes_every_waiter)
{
    struct gate g = { dino__mutex_create(), dino__cond_create(), 0, 0 };
    ASSERT_NE(DINO__INVALID_MUTEX, g.lock);
    ASSERT_NE(DINO__INVALID_COND, g.cond);

    dino__thread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        ASSERT_EQ(0, dino__thread_create(&threads[i], wait_at_gate, &g));

    dino__mutex_lock(g.lock);
    while (g.waiting < NUM_THREADS)
        dino__cond_wait(g.cond, g.lock);
    g.open = 1;
    EXPECT_EQ(0, dino__cond_broadcast(g.cond));
    dino__mutex_unlock(g.lock);

    int woken = 0;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        int result = 0;
        EXPECT_EQ(0, dino__thread_join(threads[i], &result));
        woken += result;
    }
    EXPECT_EQ(NUM_THREADS, woken);
    dino__cond_destroy(g.cond);
    dino__mutex_destroy(g.lock);
}

UTEST(cond, timedwait_times_out_after_at_least_the_timeout)
{
    dino__mutex_t m = dino__mutex_create();
    dino__cond_t  c = dino__cond_create();
    ASSERT_NE(DINO__INVALID_MUTEX, m);
    ASSERT_NE(DINO__INVALID_COND, c);

    dino__mutex_lock(m);
    uint64_t start = dino_time_monotonic_ns();
    int      rc;
    // Nothing signals, but a spurious wake-up may still return 0.
    while ((rc = dino__cond_timedwait(c, m, 50)) == 0)
        ;
    uint64_t elapsed_ms = (dino_time_monotonic_ns() - start) / 1000000;
    dino__mutex_unlock(m);

    EXPECT_EQ(DINO_TIMEOUT, rc);
    EXPECT_GE(elapsed_ms, 50u);

    dino__mutex_lock(m);
    rc = dino__cond_timedwait(c, m, 0);
    dino__mutex_unlock(m);
    EXPECT_EQ(DINO_TIMEOUT, rc);

    dino__cond_destroy(c);
    dino__mutex_destroy(m);
}

static int open_gate_later(void *arg)
{
    struct gate *g = arg;
    dino_sleep_ms(20);
    dino__mutex_lock(g->lock);
    g->open = 1;
    dino__cond_signal(g->cond);
    dino__mutex_unlock(g->lock);
    return 0;
}

UTEST(cond, timedwait_returns_when_signalled)
{
    struct gate g = { dino__mutex_create(), dino__cond_create(), 0, 0 };
    ASSERT_NE(DINO__INVALID_MUTEX, g.lock);
    ASSERT_NE(DINO__INVALID_COND, g.cond);

    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, open_gate_later, &g));
    dino__mutex_lock(g.lock);
    int rc = 0;
    // Generous: under emulation the signalling thread can be slow to start.
    while (!g.open && rc == 0)
        rc = dino__cond_timedwait(g.cond, g.lock, 10000);
    dino__mutex_unlock(g.lock);
    EXPECT_EQ(0, rc);
    EXPECT_EQ(1, g.open);

    EXPECT_EQ(0, dino__thread_join(t, NULL));
    dino__cond_destroy(g.cond);
    dino__mutex_destroy(g.lock);
}

UTEST(cond, timedwait_with_negative_timeout_waits_until_signalled)
{
    struct gate g = { dino__mutex_create(), dino__cond_create(), 0, 0 };
    ASSERT_NE(DINO__INVALID_MUTEX, g.lock);
    ASSERT_NE(DINO__INVALID_COND, g.cond);

    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, open_gate_later, &g));
    dino__mutex_lock(g.lock);
    int rc = 0;
    while (!g.open && rc == 0)
        rc = dino__cond_timedwait(g.cond, g.lock, -1);
    dino__mutex_unlock(g.lock);
    EXPECT_EQ(0, rc);
    EXPECT_EQ(1, g.open);

    EXPECT_EQ(0, dino__thread_join(t, NULL));
    dino__cond_destroy(g.cond);
    dino__mutex_destroy(g.lock);
}

/* ---- Once ---- */

static dino__once_t g_once = DINO__ONCE_INIT;
static atomic_int   g_once_runs;

static void init_once(void)
{
    // Slow enough that other threads arrive while it runs.
    dino_sleep_ms(20);
    atomic_fetch_add(&g_once_runs, 1);
}

static int run_once_from_thread(void *arg)
{
    (void)arg;
    if (dino__once(&g_once, init_once) != 0)
        return -1;
    // Every caller returns only after init_once finished.
    return atomic_load(&g_once_runs);
}

UTEST(once, runs_exactly_once_across_threads)
{
    dino__thread_t threads[NUM_THREADS * 2];
    for (int i = 0; i < NUM_THREADS * 2; i++)
        ASSERT_EQ(0, dino__thread_create(&threads[i], run_once_from_thread, NULL));
    for (int i = 0; i < NUM_THREADS * 2; i++)
    {
        int result = 0;
        EXPECT_EQ(0, dino__thread_join(threads[i], &result));
        EXPECT_EQ(1, result);
    }
    EXPECT_EQ(0, dino__once(&g_once, init_once));
    EXPECT_EQ(1, atomic_load(&g_once_runs));
}

UTEST(once, null_arguments_are_rejected)
{
    dino__once_t once = DINO__ONCE_INIT;
    EXPECT_EQ(-1, dino__once(NULL, init_once));
    EXPECT_EQ(-1, dino__once(&once, NULL));
}

UTEST_MAIN()
