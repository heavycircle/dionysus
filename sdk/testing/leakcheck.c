// Linked into dino_test() executables on Linux targets together with
// -Wl,--wrap for the allocation and environment calls below (see
// sdk/CMakeLists.txt). libc is linked statically, so its own allocations
// (strdup, fopen, ...) go through these wrappers too. A test fails if anything
// is still allocated when the process exits. DINO_LEAK_CHECK=0 disables it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *ptr, size_t size);
void  __real_free(void *ptr);
void *__real_aligned_alloc(size_t alignment, size_t size);
int   __real_posix_memalign(void **out, size_t alignment, size_t size);
int   __real_setenv(const char *name, const char *value, int overwrite);
int   __real_unsetenv(const char *name);
int   __real_putenv(char *string);
int   __real_clearenv(void);

static long g_live;
// Nonzero inside the environment calls: libc keeps the strings and the environ
// array they allocate for the life of the process, so neither those
// allocations nor the frees that replace them are counted.
static __thread int g_untracked;

static void count(long delta)
{
    if (!g_untracked)
        __atomic_add_fetch(&g_live, delta, __ATOMIC_RELAXED);
}

void *__wrap_malloc(size_t size)
{
    void *p = __real_malloc(size);
    if (p)
        count(1);
    return p;
}

void *__wrap_calloc(size_t count_, size_t size)
{
    void *p = __real_calloc(count_, size);
    if (p)
        count(1);
    return p;
}

void *__wrap_realloc(void *ptr, size_t size)
{
    void *p = __real_realloc(ptr, size);
    if (!ptr && p)
        count(1);
    // realloc(ptr, 0) may free and return NULL; a failed resize keeps ptr.
    else if (ptr && !p && size == 0)
        count(-1);
    return p;
}

void __wrap_free(void *ptr)
{
    if (ptr)
        count(-1);
    __real_free(ptr);
}

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    void *p = __real_aligned_alloc(alignment, size);
    if (p)
        count(1);
    return p;
}

int __wrap_posix_memalign(void **out, size_t alignment, size_t size)
{
    int rc = __real_posix_memalign(out, alignment, size);
    if (rc == 0)
        count(1);
    return rc;
}

int __wrap_setenv(const char *name, const char *value, int overwrite)
{
    g_untracked++;
    int rc = __real_setenv(name, value, overwrite);
    g_untracked--;
    return rc;
}

int __wrap_unsetenv(const char *name)
{
    g_untracked++;
    int rc = __real_unsetenv(name);
    g_untracked--;
    return rc;
}

int __wrap_putenv(char *string)
{
    g_untracked++;
    int rc = __real_putenv(string);
    g_untracked--;
    return rc;
}

int __wrap_clearenv(void)
{
    g_untracked++;
    int rc = __real_clearenv();
    g_untracked--;
    return rc;
}

// musl's exit() runs the atexit list first, C++ static destructors included,
// then destructors like this one, lowest priority last: anything freed by the
// program's own cleanup is freed by now. An atexit handler would run before
// the destructors of C++ globals constructed after it.
__attribute__((destructor(101))) static void report(void)
{
    const char *setting = getenv("DINO_LEAK_CHECK");
    if (setting && strcmp(setting, "0") == 0)
        return;

    long live = __atomic_load_n(&g_live, __ATOMIC_RELAXED);
    if (live > 0)
    {
        fflush(NULL);
        fprintf(stderr, "dino leak check: %ld allocation(s) still live at exit\n", live);
        _exit(1);
    }
    if (live < 0)
        fprintf(stderr, "dino leak check: %ld more frees than allocations\n", -live);
}
