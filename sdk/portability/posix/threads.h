#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <threads.h>

#include "dino_portability.h"

// MinGW's C library has no <threads.h>, so C11 threads don't build for Windows.
#if defined(DINO__WARNINGS) && !defined(DINO__THREADS_H) && (defined(_THREADS_H))
#define DINO__THREADS_H

__typeof__(thrd_create) thrd_create __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_thread_create() from <dino/core/thread.h>")));
__typeof__(thrd_join) thrd_join __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_thread_join() from <dino/core/thread.h>")));
__typeof__(thrd_detach) thrd_detach __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_thread_detach() from <dino/core/thread.h>")));
__typeof__(thrd_yield) thrd_yield __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_thread_yield() from <dino/core/thread.h>")));
__typeof__(mtx_init) mtx_init __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_mutex_create() from <dino/core/mutex.h>")));
__typeof__(mtx_lock) mtx_lock __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_mutex_lock() from <dino/core/mutex.h>")));
__typeof__(mtx_trylock) mtx_trylock __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_mutex_trylock() from <dino/core/mutex.h>")));
__typeof__(mtx_unlock) mtx_unlock __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_mutex_unlock() from <dino/core/mutex.h>")));
__typeof__(mtx_destroy) mtx_destroy __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_mutex_destroy() from <dino/core/mutex.h>")));
__typeof__(cnd_init) cnd_init __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_cond_create() from <dino/core/mutex.h>")));
__typeof__(cnd_wait) cnd_wait __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_cond_wait() from <dino/core/mutex.h>")));
__typeof__(cnd_timedwait) cnd_timedwait __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_cond_timedwait() from <dino/core/mutex.h>")));
__typeof__(cnd_signal) cnd_signal __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_cond_signal() from <dino/core/mutex.h>")));
__typeof__(cnd_broadcast) cnd_broadcast __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_cond_broadcast() from <dino/core/mutex.h>")));
__typeof__(cnd_destroy) cnd_destroy __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_cond_destroy() from <dino/core/mutex.h>")));
__typeof__(call_once) call_once __attribute__((deprecated(
    "C11 threads don't exist on Windows; use dino_once() from <dino/core/mutex.h>")));

#endif
