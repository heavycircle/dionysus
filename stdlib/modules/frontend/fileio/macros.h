#ifndef MODULES_FRONTEND_FILEIO_MACROS_H
#define MODULES_FRONTEND_FILEIO_MACROS_H

#define O_RDONLY 00000000
#define O_WRONLY 00000001
#define O_RDWR   00000002

#define O_CREAT    00000100 /* Create file if it doesn't exist */
#define O_EXCL     00000200 /* Error if O_CREAT and file exists */
#define O_NOCTTY   00000400 /* Don't assign controlling terminal */
#define O_TRUNC    00001000 /* Truncate to zero length */
#define O_APPEND   00002000 /* Append mode */
#define O_NONBLOCK 00004000 /* Non-blocking mode */

#endif // MODULES_FRONTEND_FILEIO_MACROS_H
