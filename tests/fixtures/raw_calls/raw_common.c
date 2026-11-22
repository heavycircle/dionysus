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

    sleep(1); // expected-warning {{sleep() counts whole seconds}}
#if defined(_WIN32) || defined(_GNU_SOURCE) || defined(_DEFAULT_SOURCE) || defined(_BSD_SOURCE)
    usleep(1); // expected-warning {{usleep() is obsolete}}
#endif

    pthread_create(&t, NULL, thread_main, NULL); // expected-warning {{use dino_thread_create()}}
    pthread_join(t, NULL);                       // expected-warning {{use dino_thread_join()}}
    pthread_detach(t);                           // expected-warning {{use dino_thread_detach()}}
    pthread_mutex_init(&m, NULL);                // expected-warning {{use dino_mutex_create()}}
    pthread_mutex_lock(&m);                      // expected-warning {{use dino_mutex_lock()}}
    pthread_mutex_trylock(&m);                   // expected-warning {{use dino_mutex_trylock()}}
    pthread_mutex_unlock(&m);                    // expected-warning {{use dino_mutex_unlock()}}
    pthread_cond_init(&c, NULL);                 // expected-warning {{use dino_cond_create()}}
    pthread_cond_wait(&c, &m);                   // expected-warning {{use dino_cond_wait()}}
#ifndef _WIN32
    struct timespec deadline = { 0, 0 };
    pthread_cond_timedwait(&c, &m, &deadline); // expected-warning {{use dino_cond_timedwait()}}
#endif
    pthread_cond_signal(&c);                     // expected-warning {{use dino_cond_signal()}}
    pthread_cond_broadcast(&c);                  // expected-warning {{use dino_cond_broadcast()}}
    pthread_cond_destroy(&c);                    // expected-warning {{use dino_cond_destroy()}}
    pthread_mutex_destroy(&m);                   // expected-warning {{use dino_mutex_destroy()}}
    pthread_once(&once, init_once);              // expected-warning {{use dino_once()}}
    sched_yield();                               // expected-warning {{use dino_thread_yield()}}
    struct timeval tv;
    gettimeofday(&tv, NULL);                     // expected-warning {{use dino_time_unix_ms()}}

    // The per-call escape hatch: no warning, and none expected.
    DINO_PLATFORM_CALL(sleep(2));
    if (DINO_PLATFORM_CALL(sleep(3)) != 0)
        return;

    // Flagged calls inside a system header's inline functions stay quiet.
    vendor_lock(&m);
    (void)vendor_nap();
    // A system header's macro expands in the project's code, where clang
    // reports it: documented, with DINO_PLATFORM_CALL as the way out.
    (void)VENDOR_NAP_MACRO(); // expected-warning {{sleep() counts whole seconds}}
    (void)DINO_PLATFORM_CALL(VENDOR_NAP_MACRO());

    // Calls without a dino replacement stay quiet.
    (void)pthread_self();
}
