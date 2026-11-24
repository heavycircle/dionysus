// utest.h for dino_test() programs: sheredom/utest.h (vendor/utest.h,
// unmodified), with Windows' own headers first. utest.h declares
// QueryPerformanceCounter itself when <windows.h> isn't in yet, which then
// conflicts with a later <windows.h>, and the socket headers include it.
// Winsock 2 goes first so <windows.h> doesn't bring in Winsock 1; if the
// test already included <windows.h>, utest.h uses its declarations.
#ifndef DINO_TESTING_UTEST_H
#define DINO_TESTING_UTEST_H

#if defined(_WIN32) && !defined(_WINDOWS_) && !defined(_INC_WINDOWS)
#include <winsock2.h>

#include <windows.h>
#endif

#include "vendor/utest.h"

#endif
