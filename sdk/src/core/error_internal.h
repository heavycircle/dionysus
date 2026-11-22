#ifndef DINO_CORE_ERROR_INTERNAL_H
#define DINO_CORE_ERROR_INTERNAL_H

#include <dino/core/error.h>

// Internal to the SDK. Every path on which a public function fails records a
// reason first, or dino_error() would report the one from some earlier call.

void dino__error_set(int code);

// The code for one of the operating system's own: an errno value, or a
// GetLastError() / WSAGetLastError() value.
int dino__error_from_code(long os_code);

// The code for the system call that just failed, read from errno or
// GetLastError(). Call it before anything else that can fail: a close() on the
// way out overwrites it.
int dino__error_from_os(void);

// "return dino__fail(DINO_EINVAL);" records why and returns -1.
static inline int dino__fail(int code)
{
    dino__error_set(code);
    return -1;
}

// For calls that return their error instead of setting errno (pthreads,
// WSAStartup): "return dino__fail_code(rc);".
static inline int dino__fail_code(long os_code)
{
    dino__error_set(dino__error_from_code(os_code));
    return -1;
}

// The same for a system call that just failed.
static inline int dino__fail_os(void)
{
    dino__error_set(dino__error_from_os());
    return -1;
}

// For functions that return a handle: record the system's reason, then return
// the handle's invalid value.
static inline void dino__error_set_os(void)
{
    dino__error_set(dino__error_from_os());
}

#endif /* DINO_CORE_ERROR_INTERNAL_H */
