#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <pthread.h>

#include "dino_portability.h"

// On Windows these come from winpthreads, a library dino_thread_* doesn't need.
// pthread_sigmask, pthread_kill and the like have no dino equivalent and stay quiet.
#if defined(DINO__WARNINGS) && !defined(DINO__PTHREAD_H) && (defined(_PTHREAD_H) || defined(WIN_PTHREADS_H))
#define DINO__PTHREAD_H

__typeof__(pthread_create) pthread_create __attribute__((deprecated(
    "pthread_create() needs winpthreads on Windows; use dino_thread_create() from <dino/core/thread.h>")));
__typeof__(pthread_join) pthread_join __attribute__((deprecated(
    "pthread_join() needs winpthreads on Windows; use dino_thread_join() from <dino/core/thread.h>")));
__typeof__(pthread_detach) pthread_detach __attribute__((deprecated(
    "pthread_detach() needs winpthreads on Windows; use dino_thread_detach() from <dino/core/thread.h>")));
__typeof__(pthread_mutex_init) pthread_mutex_init __attribute__((deprecated(
    "pthread_mutex_init() needs winpthreads on Windows; use dino_mutex_create() from <dino/core/mutex.h>")));
__typeof__(pthread_mutex_destroy) pthread_mutex_destroy __attribute__((deprecated(
    "pthread_mutex_destroy() needs winpthreads on Windows; use dino_mutex_destroy() from <dino/core/mutex.h>")));
__typeof__(pthread_mutex_lock) pthread_mutex_lock __attribute__((deprecated(
    "pthread_mutex_lock() needs winpthreads on Windows; use dino_mutex_lock() from <dino/core/mutex.h>")));
__typeof__(pthread_mutex_trylock) pthread_mutex_trylock __attribute__((deprecated(
    "pthread_mutex_trylock() needs winpthreads on Windows; use dino_mutex_trylock() from <dino/core/mutex.h>")));
__typeof__(pthread_mutex_unlock) pthread_mutex_unlock __attribute__((deprecated(
    "pthread_mutex_unlock() needs winpthreads on Windows; use dino_mutex_unlock() from <dino/core/mutex.h>")));
__typeof__(pthread_cond_init) pthread_cond_init __attribute__((deprecated(
    "pthread_cond_init() needs winpthreads on Windows; use dino_cond_create() from <dino/core/mutex.h>")));
__typeof__(pthread_cond_destroy) pthread_cond_destroy __attribute__((deprecated(
    "pthread_cond_destroy() needs winpthreads on Windows; use dino_cond_destroy() from <dino/core/mutex.h>")));
__typeof__(pthread_cond_wait) pthread_cond_wait __attribute__((deprecated(
    "pthread_cond_wait() needs winpthreads on Windows; use dino_cond_wait() from <dino/core/mutex.h>")));
// An inline function in winpthreads' header there: too late to add an attribute.
#ifndef _WIN32
__typeof__(pthread_cond_timedwait) pthread_cond_timedwait __attribute__((deprecated(
    "pthread_cond_timedwait() needs winpthreads on Windows; use dino_cond_timedwait() from <dino/core/mutex.h>")));
#endif
__typeof__(pthread_cond_signal) pthread_cond_signal __attribute__((deprecated(
    "pthread_cond_signal() needs winpthreads on Windows; use dino_cond_signal() from <dino/core/mutex.h>")));
__typeof__(pthread_cond_broadcast) pthread_cond_broadcast __attribute__((deprecated(
    "pthread_cond_broadcast() needs winpthreads on Windows; use dino_cond_broadcast() from <dino/core/mutex.h>")));
__typeof__(pthread_once) pthread_once __attribute__((deprecated(
    "pthread_once() needs winpthreads on Windows; use dino_once() from <dino/core/mutex.h>")));

#endif
