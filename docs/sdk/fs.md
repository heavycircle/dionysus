# Files

`#include <dino/core/fs.h>` · `dino_core`

Most file work needs nothing from the SDK: `fopen`, `fread`, `fwrite` and `fclose` from `stdio.h`
behave the same on every target (see [dino vs libc](libc.md)), with UTF-8 paths on Windows too (see [UTF-8 on Windows](#utf-8-on-windows)). This module covers what doesn't: file sizes over 2 GB, renaming over a file and removing one, and stdio for programs the kernel starts.

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

| Function                                               | Returns                                                                                                                                                                    |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `int dino_file_size(const char *path, uint64_t *size)` | `0` with the size in `*size`; `-1` (and `*size` 0) if the file doesn't exist, isn't a regular file (a directory, for example), can't be examined, or for a `NULL` argument |

A symbolic link reports the size of the file it points to, on Windows as on Linux. A link to a
directory, or one whose target is gone, fails as the directory or the missing file would.

## Renaming and removing

The C library's `rename` and `remove` differ on Windows: `rename` fails when the destination exists, so the usual save (write `conf.tmp`, then rename it over `conf`) works once and then fails every time, and `remove` refuses a read-only file. Use these instead:

```c
FILE *f = fopen("conf.tmp", "wb");
/* ... write it, check fclose ... */
if (dino_file_rename("conf.tmp", "conf") != 0)
    fprintf(stderr, "save failed: %s\n", dino_error_message(dino_error()));
```

| Function                     | Does                                                                                  | Returns                                     |
| ---------------------------- | ------------------------------------------------------------------------------------- | ------------------------------------------- |
| `dino_file_rename(from, to)` | Rename a file or directory, replacing a file already at `to`. Within one file system. | `0`, `-1`                                   |
| `dino_file_remove(path)`     | Remove a file, read-only ones included, or an empty directory.                        | `0`, `-1` (`DINO_ENOENT` if it isn't there) |

- **Open files on Windows** can't be renamed or removed while open, by this program or another, unless they were opened with delete sharing, which `fopen` doesn't do. Close the file first.
- **Replacing a directory** with `dino_file_rename` works on Linux when the destination is an empty directory; Windows refuses. Rename directories only to new names.

## UTF-8 on Windows

Executables made with `dino_executable()` and `dino_test()` run with UTF-8 as their Windows code page, so `argv`, `fopen`, `getenv` and `printf` take and give UTF-8, as on Linux and as the SDK's own functions do. Two things make that work:

- **A manifest** linked into each Windows executable (`activeCodePage`), which Windows 10 version 1903 and later honor. Older Windows ignores it, and the C library's functions use the system's ANSI code page there; the SDK's functions take UTF-8 either way.
- **The console's code page** is set to UTF-8 while the program runs, so what it prints shows correctly, and put back as it was when it exits.

A project that links a manifest of its own sets `DINO_NO_UTF8_MANIFEST` (an executable can have only one), and adds `<activeCodePage>UTF-8</activeCodePage>` to its own manifest to keep this. Executables made with plain `add_executable()` don't get it.

## Stdio for programs the kernel starts

Programs started directly by the Linux kernel (through `call_usermodehelper`, for example as a
`core_pattern` handler) get no stdin, stdout or stderr. The first files such a program opens then
become descriptors 0, 1 and 2, and a later `printf` writes into one of them.

`dino_fs_init()` points any of the three that are closed at `/dev/null` and leaves open ones alone.
It's already run before `main()` in every program that links `dino_core` (see [runtime](runtime.md)),
so you only call it yourself if you build with `DINO_NO_AUTO_RUNTIME`, or to find out whether it worked: the run before `main()` has nowhere to report a failure.

If `/dev/null` can't be opened (the kernel started the program before `/dev` existed, or it runs in a chroot without one), the closed descriptors are opened on `/` instead. Reads and writes on them fail, but they are taken, so the program's own files still can't become stdin, stdout or stderr. `dino_fs_init()` returns `-1` in that case.

| Function         | Does                                                                    | Returns                                                                              |
| ---------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| `dino_fs_init()` | Point closed stdio descriptors at `/dev/null`. Does nothing on Windows. | `0`, `-1` if `/dev/null` couldn't be opened (the descriptors are then opened on `/`) |

See also: [`examples/fs_example`](../../examples/fs_example/main.c).
