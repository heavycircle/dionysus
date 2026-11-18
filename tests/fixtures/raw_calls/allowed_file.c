// The per-file opt-out: defined before any include, so nothing below warns.
// As 1, the value -D and DINO_NO_PLATFORM_WARNINGS give it, so the two can meet.
#define DINO_ALLOW_PLATFORM_CALLS 1
#include <pthread.h>
#include <unistd.h>

void allowed_file(void);
void allowed_file(void)
{
    pthread_mutex_t m;
    pthread_mutex_init(&m, NULL);
    sleep(1);
    pthread_mutex_destroy(&m);
}
