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

#endif
