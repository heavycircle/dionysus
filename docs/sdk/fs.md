# Files

`#include <dino/core/fs.h>` · `dino_core`

Most file work needs nothing from the SDK: `fopen`, `fread`, `fwrite` and `fclose` from `stdio.h`
behave the same on every target (see [dino vs libc](libc.md)), with UTF-8 paths on Windows too (see [UTF-8 on Windows](#utf-8-on-windows)). So do `rename` and `remove`, which the SDK makes behave on Windows as on Linux (see [renaming and removing](#renaming-and-removing)). This module covers what's left: file sizes over 2 GB, creating directories, the temporary directory and the program's own path, and stdio for programs the kernel starts.

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

Use the C library's `rename` and `remove`; they behave as on Linux on every target. On Windows the UCRT's own versions differ, so the SDK defines them there under the same names: `rename` replaces a file already at the destination, so the usual save (write `conf.tmp`, then rename it over `conf`) works every time, and `remove` deletes a read-only file and an empty directory too. Both set `errno` as Linux would (`ENOENT`, `EACCES`, `ENOTEMPTY`, `EXDEV`, ...).

```c
FILE *f = fopen("conf.tmp", "wb");
/* ... write it, check fclose ... */
if (rename("conf.tmp", "conf") != 0)
    perror("save failed");
```

- **Open files on Windows** can't be renamed or removed while open, by this program or another, unless they were opened with delete sharing, which `fopen` doesn't do. Close the file first.
- **Replacing a directory** with `rename` works on Linux when the destination is an empty directory; Windows refuses. Rename directories only to new names.
- **Opting out:** a project with its own Windows `rename` or `remove` sets `DINO_NO_LIBC_SHIMS` before `project()` (see [projects](../guide/projects.md)).

## Creating directories

Use POSIX's `mkdir(path, mode)` from `<sys/stat.h>`. On Windows MinGW's `mkdir` takes the path alone, since Windows has no permission bits, so `mkdir(path, 0755)` wouldn't compile there: the SDK defines the two-argument form, which takes UTF-8 paths, ignores `mode`, and sets `errno` as Linux would (`EEXIST`, `ENOENT`, `ENOTDIR`, ...). MinGW's own one-argument `mkdir(path)` keeps working too. Since 1.0.

It's a macro, since C can't declare a second `mkdir`, which shows in a few places:

- **A project's own `mkdir` macro** wins when it's defined before `<sys/stat.h>` is included; defined after, it redefines the SDK's (a `-Wmacro-redefined` warning). Wrap it in `#ifndef mkdir`, or turn the SDK's off.
- **A member called `mkdir`**, such as `ops->mkdir(path, mode)` in a table of functions, is caught by the macro too. Call it as `(ops->mkdir)(path, mode)`.
- **Taking `mkdir`'s address** (instead of calling it) gets MinGW's one-argument version.
- **Turning it off:** set `DINO_NO_MKDIR_SHIM` before `project()` to leave only this one out, or `DINO_NO_LIBC_SHIMS` for every shim (see [projects](../guide/projects.md)).

To create every missing directory on the way, as `mkdir -p` does, use `dino_mkdir_p` from `dino/util/fs.h` (`dino_util`). A directory that already exists is fine, and on Windows it takes either separator and a drive (`C:\`) or share (`\\server\share\`) at the start.

```c
#include <sys/stat.h>

#include <dino/util/fs.h>

if (dino_mkdir_p("out/logs/2026", 0755) != 0)
    fprintf(stderr, "can't create out/logs/2026: %s\n", dino_error_message(dino_error()));
```

| Function                                                             | Returns                                                                                                     |
| -------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `int dino_mkdir_p(const char *path, mode_t mode)` (`dino/util/fs.h`) | `0`, or `-1` with `dino_error()`: `DINO_EEXIST` when a file that isn't a directory is in the way. Since 1.0 |

## The temporary directory and the program's own path

Neither has a portable name: the temporary directory is `$TMPDIR` or `/tmp` on Linux and `%TMP%` or `%TEMP%` on Windows, and the running program's path is `/proc/self/exe` on Linux and `GetModuleFileNameW` on Windows. Both functions return like `snprintf`: they write what fits in `buf`, NUL-terminated, and return the full length, so a result at least `size` means the path was cut short.

```c
char tmp[1024];
int  len = dino_path_temp(tmp, sizeof(tmp));
if (len < 0 || (size_t)len >= sizeof(tmp))
    return 1; // failed, or longer than the buffer
```

| Function                                           | Returns                                                                                                             |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `int dino_path_temp(char *buf, size_t size)`       | The temporary directory, UTF-8, without a trailing separator; its length, or `-1`. Since 1.0                        |
| `int dino_path_executable(char *buf, size_t size)` | The running program's full path, UTF-8 (under qemu-user, the program's, not qemu's); its length, or `-1`. Since 1.0 |

Create files in the temporary directory with `mkstemp`, or a directory with `mkdtemp`, so two programs never pick the same name.

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
