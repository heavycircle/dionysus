#ifndef DINO_CORE_RUNTIME_H
#define DINO_CORE_RUNTIME_H

/**
 * The SDK sets itself up; programs don't need to call anything first:
 *
 *   - Before main(), any of stdin, stdout and stderr that are closed are
 *     pointed at /dev/null (dino_fs_init()). Linked into every program that
 *     links dino_core, unless built with -DDINO_NO_AUTO_RUNTIME=ON.
 *   - Subsystems that can fail start on first use and report failure from that
 *     call: dino_socket_create() runs dino_net_init().
 *
 * The functions below remain for programs that want to check for failure up
 * front or tear down explicitly.
 */

/**
 * Initialise every Dino subsystem now.
 *
 * Safe to call any number of times, from any thread.
 *
 * @return 0 on success, non-zero if any subsystem fails to initialise.
 */
int dino_runtime_init(void);

/**
 * Tear down every Dino subsystem.
 *
 * Optional: the OS releases everything at exit. Later calls that need a
 * subsystem initialise it again.
 */
void dino_runtime_cleanup(void);

/**
 * Deprecated: setup is automatic. Expands to a harmless declaration so existing
 * `DINO_ENABLE_AUTO_RUNTIME;` lines keep compiling; to be removed by the 1.0 API
 * review.
 */
#define DINO_ENABLE_AUTO_RUNTIME extern void dino__runtime_auto(void)

#endif // DINO_CORE_RUNTIME_H
