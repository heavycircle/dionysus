#ifndef DINO_CORE_MUTEX_H
#define DINO_CORE_MUTEX_H

#include <stdint.h>

typedef uintptr_t dino_mutex_t;

#define DINO_INVALID_MUTEX ((dino_mutex_t)0)

/**
 * Allocate and initialise a recursive mutex.
 *
 * @return Opaque mutex handle, or DINO_INVALID_MUTEX on failure.
 */
dino_mutex_t dino_mutex_create(void);

/**
 * Destroy a mutex and free its memory.
 *
 * @param mutex  Handle returned by dino_mutex_create.
 */
void dino_mutex_destroy(dino_mutex_t mutex);

/**
 * Acquire the mutex, blocking if necessary.
 *
 * @param mutex  Handle to lock.
 * @return 0 on success, non-zero on error.
 */
int dino_mutex_lock(dino_mutex_t mutex);

/**
 * Release the mutex.
 *
 * @param mutex  Handle to unlock.
 * @return 0 on success, non-zero on error.
 */
int dino_mutex_unlock(dino_mutex_t mutex);

/**
 * Attempt to acquire the mutex without blocking.
 *
 * @param mutex  Handle to try-lock.
 * @return 0 if acquired, non-zero if already held or on error.
 */
int dino_mutex_trylock(dino_mutex_t mutex);

#endif // DINO_CORE_MUTEX_H
