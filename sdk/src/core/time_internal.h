#ifndef DINO_CORE_TIME_INTERNAL_H
#define DINO_CORE_TIME_INTERNAL_H

#include <stdint.h>

#define NS_PER_US 1000ULL
#define NS_PER_MS 1000000ULL
#define NS_PER_S  1000000000ULL

// Sleep until at least ns nanoseconds have passed on the monotonic clock. Any
// ns is safe: the deadline saturates instead of wrapping.
// Internal to the SDK; time_posix.c and time_win32.c each implement it.
void dino__sleep_ns(uint64_t ns);

#endif // DINO_CORE_TIME_INTERNAL_H
