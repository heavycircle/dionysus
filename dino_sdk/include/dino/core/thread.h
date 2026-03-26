#ifndef DINO_CORE_THREAD_H
#define DINO_CORE_THREAD_H

#include <stdint.h>

typedef uintptr_t dino_thread_t;

#ifdef _WIN32
#define DINO_THREAD_RET    unsigned long
#define DINO_THREAD_CC     __stdcall
#define DINO_THREAD_RETURN return 0
#else
#define DINO_THREAD_RET void *
#define DINO_THREAD_CC
#define DINO_THREAD_RETURN return NULL
#endif

typedef DINO_THREAD_RET(DINO_THREAD_CC *dino_thread_worker_t)(void *arg);
#define DINO_THREAD_FUNC DINO_THREAD_RET DINO_THREAD_CC

/**
 * OS-agnostic wrapper for pthread_create.
 *
 * This method either uses pthread_create or the WinAPI for closing threads.
 *
 * @param  thread       Thread to await.
 * @param  worker       Worker function to run inside the thread.
 * @param  arg          Argument to worker function.
 * @return Returns 0. On error returns the error number.
 */
int dino_thread_create(dino_thread_t *thread, dino_thread_worker_t worker, void *arg);

/**
 * OS-agnostic wrapper for pthread_join.
 *
 * This method either uses pthread_join or the WinAPI for closing threads.
 *
 * @param  dino_thread_t    Thread to await.
 */
void dino_thread_join(dino_thread_t thread);

#endif // DINO_CORE_THREAD_H
