// Linked into Windows executables by dino_executable() and dino_test(), with
// the manifest that makes the process's code page UTF-8 (cmake/utf8.manifest,
// cmake/interface.cmake). printf then writes UTF-8, which the console shows
// right only if its own code page is UTF-8 too. The console belongs to
// whoever started the program, so its code pages are put back at exit.
#include <stdlib.h>

#include <windows.h>

static UINT saved_output, saved_input;

static void restore_console(void)
{
    if (saved_output)
        SetConsoleOutputCP(saved_output);
    if (saved_input)
        SetConsoleCP(saved_input);
}

// Where the manifest has no effect (Windows before 10 1903) the process code
// page isn't UTF-8, and the console is left alone too: its text would
// otherwise be in two encodings.
__attribute__((constructor(101))) void dino__utf8_console(void)
{
    if (GetACP() != CP_UTF8)
        return;
    UINT output = GetConsoleOutputCP();
    UINT input  = GetConsoleCP();
    // 0: no console, as for a program whose output goes to a pipe.
    if (output && output != CP_UTF8 && SetConsoleOutputCP(CP_UTF8))
        saved_output = output;
    if (input && input != CP_UTF8 && SetConsoleCP(CP_UTF8))
        saved_input = input;
    if (saved_output || saved_input)
        atexit(restore_console);
}
