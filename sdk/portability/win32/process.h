#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <process.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__PROCESS_H) && (defined(_INC_PROCESS))
#define DINO__PROCESS_H

_CRTIMP __typeof__(_beginthread) _beginthread __attribute__((deprecated(
    "_beginthread() is Windows-only; use pthread_create() from <pthread.h>, which works on every target")));
_CRTIMP __typeof__(_beginthreadex) _beginthreadex __attribute__((deprecated(
    "_beginthreadex() is Windows-only; use pthread_create() from <pthread.h>, which works on every target")));

_CRTIMP __typeof__(_getpid) _getpid __attribute__((deprecated(
    "_getpid() is Windows-only; use getpid() from <unistd.h>, which works on every target")));
_CRTIMP __typeof__(_cwait) _cwait __attribute__((deprecated(
    "_cwait() is Windows-only; use dino_process_wait() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnl) _spawnl __attribute__((deprecated(
    "_spawnl() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnle) _spawnle __attribute__((deprecated(
    "_spawnle() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnlp) _spawnlp __attribute__((deprecated(
    "_spawnlp() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnlpe) _spawnlpe __attribute__((deprecated(
    "_spawnlpe() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnv) _spawnv __attribute__((deprecated(
    "_spawnv() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnve) _spawnve __attribute__((deprecated(
    "_spawnve() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnvp) _spawnvp __attribute__((deprecated(
    "_spawnvp() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_spawnvpe) _spawnvpe __attribute__((deprecated(
    "_spawnvpe() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execl) _execl __attribute__((deprecated(
    "_execl() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execle) _execle __attribute__((deprecated(
    "_execle() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execlp) _execlp __attribute__((deprecated(
    "_execlp() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execlpe) _execlpe __attribute__((deprecated(
    "_execlpe() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execv) _execv __attribute__((deprecated(
    "_execv() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execve) _execve __attribute__((deprecated(
    "_execve() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execvp) _execvp __attribute__((deprecated(
    "_execvp() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));
_CRTIMP __typeof__(_execvpe) _execvpe __attribute__((deprecated(
    "_execvpe() is Windows-only, and on Windows starts a new process rather than replacing this one; use dino_process_spawn() from <dino/core/process.h>")));

#endif
