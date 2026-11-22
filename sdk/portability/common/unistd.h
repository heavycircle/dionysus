#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <unistd.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__UNISTD_H) && (defined(_UNISTD_H))
#define DINO__UNISTD_H

#ifndef _WIN32
__typeof__(pipe) pipe __attribute__((deprecated(
    "pipe() is POSIX-only; use dino_pipe_create() from <dino/core/pipe.h>")));
__typeof__(fork) fork __attribute__((deprecated(
    "fork() doesn't exist on Windows; to run a program use dino_process_spawn() from <dino/core/process.h>")));
__typeof__(execv) execv __attribute__((deprecated(
    "execv() is POSIX-only and replaces the process, which Windows can't do; spawn with dino_process_spawn() from <dino/core/process.h> and exit")));
__typeof__(execvp) execvp __attribute__((deprecated(
    "execvp() is POSIX-only and replaces the process, which Windows can't do; spawn with dino_process_spawn() from <dino/core/process.h> and exit")));
__typeof__(execve) execve __attribute__((deprecated(
    "execve() is POSIX-only and replaces the process, which Windows can't do; spawn with dino_process_spawn() from <dino/core/process.h> and exit")));
__typeof__(execl) execl __attribute__((deprecated(
    "execl() is POSIX-only and replaces the process, which Windows can't do; spawn with dino_process_spawn() from <dino/core/process.h> and exit")));
__typeof__(execlp) execlp __attribute__((deprecated(
    "execlp() is POSIX-only and replaces the process, which Windows can't do; spawn with dino_process_spawn() from <dino/core/process.h> and exit")));
__typeof__(execle) execle __attribute__((deprecated(
    "execle() is POSIX-only and replaces the process, which Windows can't do; spawn with dino_process_spawn() from <dino/core/process.h> and exit")));
__typeof__(getpid) getpid __attribute__((deprecated(
    "getpid() is _getpid() on Windows; use dino_process_current_id() from <dino/core/process.h>")));
#ifdef _GNU_SOURCE
__typeof__(gettid) gettid __attribute__((deprecated(
    "gettid() is Linux-only; use dino_thread_current_id() from <dino/core/thread.h>")));
__typeof__(pipe2) pipe2 __attribute__((deprecated(
    "pipe2() is Linux-only; use dino_pipe_create() from <dino/core/pipe.h>")));
#endif
#endif

__typeof__(sleep) sleep __attribute__((deprecated(
    "sleep() counts whole seconds and is a MinGW extra on Windows; use dino_sleep_ms() from <dino/core/time.h>")));
// Declared on Windows and, elsewhere, only in the default (non-strict) language
// mode or with _GNU_SOURCE: when unsure, no warning rather than a build error.
#if defined(_WIN32) || defined(_GNU_SOURCE) || defined(_DEFAULT_SOURCE) || defined(_BSD_SOURCE)
__typeof__(usleep) usleep __attribute__((deprecated(
    "usleep() is obsolete in POSIX and missing in strict modes; use dino_sleep_us() from <dino/core/time.h>")));
#endif

#endif
