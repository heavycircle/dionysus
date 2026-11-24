// The per-file opt-out: defined before any include, so nothing below warns.
// As 1, the value -D and DINO_NO_PLATFORM_WARNINGS give it, so the two can meet.
#define DINO_ALLOW_PLATFORM_CALLS 1
#include <pthread.h>
#include <unistd.h>

void allowed_file(void);
void allowed_file(void)
{
    pthread_condattr_t attr;
    pthread_condattr_setclock(&attr, 1);
#if defined(_WIN32) || defined(_GNU_SOURCE) || defined(_DEFAULT_SOURCE) || defined(_BSD_SOURCE)
    usleep(1);
#endif
}
