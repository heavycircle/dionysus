# Files

`#include <dino/core/fs.h>` · `dino_core`

Most file work needs nothing from the SDK: `fopen`, `fread`, `fwrite` and `fclose` from `stdio.h`
behave the same on every target (see [dino vs libc](libc.md)). This module covers two things that
don't.

## File sizes over 2 GB

On Windows, `long` is 32 bits even on 64-bit systems, so `ftell` and `fseek` fail on files of 2 GB
or more, and `stat`'s size can be truncated. `dino_file_size` works for files of any size, and
takes UTF-8 paths on Windows too:

```c
#include <inttypes.h>
#include <stdio.h>

#include <dino/core/fs.h>

int main(void)
{
    const char *path = "dino_fs_doc_example.tmp";
    FILE       *f    = fopen(path, "wb");
    if (!f)
        return 1;
    fputs("twelve bytes", f);
    fclose(f);

    uint64_t size;
    if (dino_file_size(path, &size) != 0)
        return 1;
    printf("%s is %" PRIu64 " bytes\n", path, size);
    remove(path);

    // A missing file is an error, not a size of 0.
    int missing = dino_file_size(path, &size);
    return size == 0 && missing == -1 ? 0 : 1;
}
```

| Function | Returns |
| --- | --- |
| `int dino_file_size(const char *path, uint64_t *size)` | `0` with the size in `*size`; `-1` (and `*size` 0) if the file doesn't exist, isn't a regular file (a directory, for example), can't be examined, or for a `NULL` argument |

## Stdio for programs the kernel starts

Programs started directly by the Linux kernel (through `call_usermodehelper`, for example as a
`core_pattern` handler) get no stdin, stdout or stderr. The first files such a program opens then
become descriptors 0, 1 and 2, and a later `printf` writes into one of them.

`dino_fs_init()` points any of the three that are closed at `/dev/null` and leaves open ones alone.
It's already run before `main()` in every program that links `dino_core` (see [runtime](runtime.md)),
so you only call it yourself if you build with `DINO_NO_AUTO_RUNTIME`.

| Function | Does | Returns |
| --- | --- | --- |
| `dino_fs_init()` | Point closed stdio descriptors at `/dev/null`. Does nothing on Windows. | `0`, `-1` if `/dev/null` couldn't be opened |

See also: [`examples/fs_example`](../../examples/fs_example/main.c).
