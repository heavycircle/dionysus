#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <threads.h>

#include "dino_portability.h"

// MinGW's C library has no <threads.h>, so C11 threads don't build for Windows.
#if defined(DINO__WARNINGS) && !defined(DINO__THREADS_H) && (defined(_THREADS_H))
#define DINO__THREADS_H

__typeof__(thrd_create) thrd_create __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_create() from <pthread.h>, which works on every target")));
__typeof__(thrd_join) thrd_join __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_join() from <pthread.h>, which works on every target")));
__typeof__(thrd_detach) thrd_detach __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_detach() from <pthread.h>, which works on every target")));
__typeof__(thrd_yield) thrd_yield __attribute__((deprecated(
    "C11 threads don't exist on Windows; use sched_yield() from <sched.h>, which works on every target")));
__typeof__(mtx_init) mtx_init __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_mutex_init() from <pthread.h>, which works on every target")));
__typeof__(mtx_lock) mtx_lock __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_mutex_lock() from <pthread.h>, which works on every target")));
__typeof__(mtx_trylock) mtx_trylock __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_mutex_trylock() from <pthread.h>, which works on every target")));
__typeof__(mtx_unlock) mtx_unlock __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_mutex_unlock() from <pthread.h>, which works on every target")));
__typeof__(mtx_destroy) mtx_destroy __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_mutex_destroy() from <pthread.h>, which works on every target")));
__typeof__(cnd_init) cnd_init __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_cond_init() from <pthread.h>, which works on every target")));
__typeof__(cnd_wait) cnd_wait __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_cond_wait() from <pthread.h>, which works on every target")));
__typeof__(cnd_timedwait) cnd_timedwait __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_cond_timedwait() from <dino/core/thread.h>")));
__typeof__(cnd_signal) cnd_signal __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_cond_signal() from <pthread.h>, which works on every target")));
__typeof__(cnd_broadcast) cnd_broadcast __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_cond_broadcast() from <pthread.h>, which works on every target")));
__typeof__(cnd_destroy) cnd_destroy __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_cond_destroy() from <pthread.h>, which works on every target")));
__typeof__(call_once) call_once __attribute__((deprecated(
    "C11 threads don't exist on Windows; use pthread_once() from <pthread.h>, which works on every target")));

#endif
