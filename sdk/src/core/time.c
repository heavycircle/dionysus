#include <dino/core/time.h>

#include "time_internal.h"

void dino_sleep_ms(uint32_t ms)
{
    dino__sleep_ns((uint64_t)ms * NS_PER_MS);
}

void dino_sleep_us(uint64_t us)
{
    dino__sleep_ns(us > UINT64_MAX / NS_PER_US ? UINT64_MAX : us * NS_PER_US);
}
