// Linked into every dino_test() executable. A test that hangs is killed when
// its timeout runs out, and anything still in stdout's buffer goes with it:
// the "[ RUN ]" line that names the case, and on Windows all the output.
#include <stdio.h>

__attribute__((constructor)) static void dino__test_unbuffered(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
}
