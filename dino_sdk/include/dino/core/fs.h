#ifndef DINO_CORE_FS_H
#define DINO_CORE_FS_H

#include <stdint.h>

/**
 * Get the size of a file.
 *
 * Supports large files up to the maximum size of uint64_t.
 *
 * @param  filepath     Path to the file.
 * @return Size of the file.
 */
uint64_t dino_file_size(const char *filepath);

/**
 * Initialize fs environment.
 *
 * This method is crucial to ensure functionality of modules spawned by
 * the kernel's call_usermodehelper. call_usermodehelper does not auto-
 * initialize stdin, stdout, and stderr, which breaks writing functionality
 * for future file descriptors.
 *
 * It's fine for us to initialize all three to /dev/null, since we never
 * receive output from kernel-spawned modules anyway. TODO: In the future,
 * find a way to recover their output.
 *
 * @return 0 on success, non-zero on failure.
 */
int dino_fs_init(void);

/**
 * Teardown file system environment.
 */
void dino_fs_cleanup(void);

#endif
