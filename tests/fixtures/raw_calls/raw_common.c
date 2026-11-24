// clang-format off: clang -verify reads each call's expectation from that call's own line.
// Headers that exist on Linux and Windows (winpthreads, MinGW's unistd.h).
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <vendor_lib.h>

static void *thread_main(void *arg)
{
    return arg;
}

static void init_once(void) {}

void raw_common(void);
void raw_common(void)
{
    pthread_t       t;
    pthread_mutex_t m;
    pthread_cond_t  c;
    pthread_once_t  once = PTHREAD_ONCE_INIT;

#if defined(_WIN32) || defined(_GNU_SOURCE) || defined(_DEFAULT_SOURCE) || defined(_BSD_SOURCE)
    usleep(1); // expected-warning {{usleep() rounds down to whole milliseconds}}
#endif
    pthread_condattr_t attr;
    pthread_condattr_setclock(&attr, 1); // expected-warning {{use dino_cond_timedwait()}}

    // The per-call escape hatch: no warning, and none expected.
    DINO_PLATFORM_CALL(pthread_condattr_setclock(&attr, 1));
    if (DINO_PLATFORM_CALL(pthread_condattr_setclock(&attr, 1)) != 0)
        return;

    // Flagged calls inside a system header's inline functions stay quiet.
    (void)vendor_set_clock(&attr);
    // A system header's macro expands in the project's code, where clang
    // reports it: documented, with DINO_PLATFORM_CALL as the way out.
    (void)VENDOR_CLOCK_MACRO(&attr); // expected-warning {{use dino_cond_timedwait()}}
    (void)DINO_PLATFORM_CALL(VENDOR_CLOCK_MACRO(&attr));

    // Calls that behave the same on every target stay quiet.
    sleep(1);
    pthread_create(&t, NULL, thread_main, NULL);
    pthread_join(t, NULL);
    pthread_detach(t);
    pthread_mutex_init(&m, NULL);
    pthread_mutex_lock(&m);
    pthread_mutex_trylock(&m);
    pthread_mutex_unlock(&m);
    pthread_cond_init(&c, NULL);
    pthread_cond_wait(&c, &m);
    struct timespec deadline = { 0, 0 };
    pthread_cond_timedwait(&c, &m, &deadline);
    pthread_cond_signal(&c);
    pthread_cond_broadcast(&c);
    pthread_cond_destroy(&c);
    pthread_mutex_destroy(&m);
    pthread_once(&once, init_once);
    sched_yield();
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Calls without a dino replacement stay quiet.
    (void)pthread_self();
}
