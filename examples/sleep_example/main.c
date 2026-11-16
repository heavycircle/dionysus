#include <stdint.h>
#include <stdio.h>

#include <dino/core/time.h>

#define SLEEP_MS 3000

int main(void)
{
    uint64_t start = dino_time_monotonic_ns();
    dino_sleep_ms(SLEEP_MS);
    uint64_t elapsed_ms = (dino_time_monotonic_ns() - start) / 1000000;

    printf("slept %llu ms\n", (unsigned long long)elapsed_ms);
    return elapsed_ms >= SLEEP_MS ? 0 : 1;
}
