// Linked into dino_test() executables on Linux targets together with
// -Wl,--wrap for every call wrapped below (the list in sdk/CMakeLists.txt; the
// link fails if the two disagree). libc is linked statically, so its own
// allocations (strdup, fopen, ...) go through these wrappers too. A test fails if anything
// is still allocated when the process exits. DINO_LEAK_CHECK=0 disables it.
#include <grp.h>
#include <locale.h>
#include <malloc.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long  g_live;
static pid_t g_pid;
// Nonzero inside a wrapped call. An allocation is counted once, by the
// outermost wrapper, however libc implements it; and libc calls that keep what
// they allocate count nothing at all (see UNTRACKED below).
static __thread int g_untracked;

static void count(long delta)
{
    if (!g_untracked)
        __atomic_add_fetch(&g_live, delta, __ATOMIC_RELAXED);
}

// An allocator entry point: counted when it returns memory.
#define ALLOCATOR(name, params, args)                                                              \
    void *__real_##name params;                                                                    \
    void *__wrap_##name params                                                                     \
    {                                                                                              \
        g_untracked++;                                                                             \
        void *p = __real_##name args;                                                              \
        g_untracked--;                                                                             \
        if (p)                                                                                     \
            count(1);                                                                              \
        return p;                                                                                  \
    }

ALLOCATOR(malloc, (size_t size), (size))
ALLOCATOR(calloc, (size_t n, size_t size), (n, size))
ALLOCATOR(aligned_alloc, (size_t alignment, size_t size), (alignment, size))
ALLOCATOR(memalign, (size_t alignment, size_t size), (alignment, size))
ALLOCATOR(valloc, (size_t size), (size))

void *__real_realloc(void *ptr, size_t size);
void *__real_reallocarray(void *ptr, size_t n, size_t size);
void  __real_free(void *ptr);
int   __real_posix_memalign(void **out, size_t alignment, size_t size);

static void count_resize(void *ptr, void *p, int to_nothing)
{
    if (!ptr && p)
        count(1);
    // A resize to 0 bytes may free and return NULL; a failed resize keeps ptr.
    else if (ptr && !p && to_nothing)
        count(-1);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    g_untracked++;
    void *p = __real_realloc(ptr, size);
    g_untracked--;
    count_resize(ptr, p, size == 0);
    return p;
}

void *__wrap_reallocarray(void *ptr, size_t n, size_t size)
{
    g_untracked++;
    void *p = __real_reallocarray(ptr, n, size);
    g_untracked--;
    count_resize(ptr, p, n == 0 || size == 0);
    return p;
}

void __wrap_free(void *ptr)
{
    if (ptr)
        count(-1);
    g_untracked++;
    __real_free(ptr);
    g_untracked--;
}

int __wrap_posix_memalign(void **out, size_t alignment, size_t size)
{
    g_untracked++;
    int rc = __real_posix_memalign(out, alignment, size);
    g_untracked--;
    if (rc == 0)
        count(1);
    return rc;
}

// A libc call that keeps what it allocates for the life of the process, or
// until the next call of its family: a result buffer, a cache, a list. None
// of it is the program's to free, so nothing is counted inside the call,
// neither the allocations nor the frees that replace them.
#define UNTRACKED(type, name, params, args)                                                        \
    type __real_##name params;                                                                     \
    type __wrap_##name params                                                                      \
    {                                                                                              \
        g_untracked++;                                                                             \
        type result = __real_##name args;                                                          \
        g_untracked--;                                                                             \
        return result;                                                                             \
    }
#define UNTRACKED_VOID(name, params, args)                                                         \
    void __real_##name params;                                                                     \
    void __wrap_##name params                                                                      \
    {                                                                                              \
        g_untracked++;                                                                             \
        __real_##name args;                                                                        \
        g_untracked--;                                                                             \
    }

// The environment: its strings and the environ array.
UNTRACKED(
    int, setenv, (const char *name, const char *value, int overwrite), (name, value, overwrite))
UNTRACKED(int, unsetenv, (const char *name), (name))
UNTRACKED(int, putenv, (char *string), (string))
UNTRACKED(int, clearenv, (void), ())

// The list of exit handlers, which grows by a block after the first 32: C++
// registers one for every object with static storage and a destructor.
UNTRACKED(int, atexit, (void (*handler)(void)), (handler))
UNTRACKED(int, __cxa_atexit, (void (*handler)(void *), void *arg, void *dso), (handler, arg, dso))

// The user and group databases: the entry returned and the file being read.
UNTRACKED(struct passwd *, getpwuid, (uid_t uid), (uid))
UNTRACKED(struct passwd *, getpwnam, (const char *name), (name))
UNTRACKED(struct passwd *, getpwent, (void), ())
UNTRACKED_VOID(setpwent, (void), ())
UNTRACKED_VOID(endpwent, (void), ())
UNTRACKED(struct group *, getgrgid, (gid_t gid), (gid))
UNTRACKED(struct group *, getgrnam, (const char *name), (name))
UNTRACKED(struct group *, getgrent, (void), ())
UNTRACKED_VOID(setgrent, (void), ())
UNTRACKED_VOID(endgrent, (void), ())

// The host entry returned.
UNTRACKED(struct hostent *, gethostbyname, (const char *name), (name))
UNTRACKED(struct hostent *, gethostbyname2, (const char *name, int family), (name, family))
UNTRACKED(struct hostent *, gethostbyaddr, (const void *address, socklen_t length, int family),
    (address, length, family))

// The time zone: a copy of TZ, when it is 32 characters or longer. On the
// 32-bit targets musl's headers send the calls that take a time_t to these
// 64-bit names, which are the ones the program then refers to.
UNTRACKED_VOID(tzset, (void), ())
UNTRACKED(size_t, strftime, (char *buffer, size_t size, const char *format, const struct tm *tm),
    (buffer, size, format, tm))
#ifdef _REDIR_TIME64
UNTRACKED(struct tm *, __localtime64, (const time_t *t), (t))
UNTRACKED(struct tm *, __localtime64_r, (const time_t *t, struct tm *tm), (t, tm))
UNTRACKED(time_t, __mktime64, (struct tm * tm), (tm))
UNTRACKED(char *, __ctime64, (const time_t *t), (t))
UNTRACKED(char *, __ctime64_r, (const time_t *t, char *buffer), (t, buffer))
#else
UNTRACKED(struct tm *, localtime, (const time_t *t), (t))
UNTRACKED(struct tm *, localtime_r, (const time_t *t, struct tm *tm), (t, tm))
UNTRACKED(time_t, mktime, (struct tm * tm), (tm))
UNTRACKED(char *, ctime, (const time_t *t), (t))
UNTRACKED(char *, ctime_r, (const time_t *t, char *buffer), (t, buffer))
#endif

// Locales: every one that was ever named stays loaded, so the locale objects
// themselves aren't counted either.
UNTRACKED(char *, setlocale, (int category, const char *name), (category, name))
UNTRACKED(locale_t, newlocale, (int mask, const char *name, locale_t base), (mask, name, base))
UNTRACKED(locale_t, duplocale, (locale_t locale), (locale))
UNTRACKED_VOID(freelocale, (locale_t locale), (locale))

__attribute__((constructor)) static void remember_process(void)
{
    g_pid = getpid();
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
    // A child made by fork() starts with the parent's count and never frees
    // what the parent held. The test is the parent.
    if (getpid() != g_pid)
        return;

    long live = __atomic_load_n(&g_live, __ATOMIC_RELAXED);
    if (live == 0)
        return;
    fflush(NULL);
    if (live > 0)
        fprintf(stderr, "dino leak check: %ld allocation(s) still live at exit\n", live);
    else
        // Memory was freed that was never counted, so a leak of the same size
        // would be hidden: the count can't be trusted either way.
        fprintf(stderr,
            "dino leak check: %ld more frees than allocations; the count is wrong, which "
            "is a bug in the check (DINO_LEAK_CHECK=0 turns it off)\n",
            -live);
    _exit(1);
}
