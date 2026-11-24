#include <stdlib.h>
#include <string.h>

#include "libc_win32_internal.h"

// MinGW has no setenv or unsetenv. _wputenv_s keeps the C runtime's copy
// (getenv) in step, and SetEnvironmentVariableW sets what children see. That
// one goes last: _wputenv_s takes an empty value to mean delete, from both
// copies, so an empty variable exists for Win32 but not for getenv.
static int change(const char *name, const char *value)
{
    if (!name || !*name || strchr(name, '='))
    {
        errno = EINVAL;
        return -1;
    }
    wchar_t *wname  = dino__libc_widen(name);
    wchar_t *wvalue = value ? dino__libc_widen(value) : NULL;
    int      failed = !wname || (value && !wvalue);
    if (!failed && _wputenv_s(wname, wvalue ? wvalue : L"") != 0)
        failed = 1;
    else if (!failed && !SetEnvironmentVariableW(wname, wvalue))
    {
        errno  = dino__libc_errno();
        failed = 1;
    }
    free(wname);
    free(wvalue);
    return failed ? -1 : 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (!value)
    {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && name && *name && !strchr(name, '=') && getenv(name))
        return 0;
    return change(name, value);
}

int unsetenv(const char *name)
{
    return change(name, NULL);
}
