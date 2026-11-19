#ifndef DINO_CORE_RUNTIME_H
#define DINO_CORE_RUNTIME_H

#ifdef __cplusplus
extern "C"
{
#endif

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
 * @return 0 on success, -1 if any subsystem fails to initialise.
 */
int dino_runtime_init(void);

/**
 * Tear down every Dino subsystem.
 *
 * Optional: the OS releases everything at exit. Later calls that need a
 * subsystem initialise it again.
 */
void dino_runtime_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_RUNTIME_H
