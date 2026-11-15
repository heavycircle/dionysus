#ifndef DINO_CORE_RUNTIME_H
#define DINO_CORE_RUNTIME_H

/**
 * Explicitly initialise all Dino subsystems.
 *
 * Call this in main() if you are not using DINO_ENABLE_AUTO_RUNTIME.
 *
 * @return 0 on success, non-zero if any subsystem fails to initialise.
 */
int dino_runtime_init(void);

/**
 * Explicitly tear down all Dino subsystems.
 *
 * Call before process exit if you called dino_runtime_init() explicitly.
 */
void dino_runtime_cleanup(void);

/**
 * DINO_ENABLE_AUTO_RUNTIME
 *
 * Place this macro at file scope in exactly ONE .c file in your project.
 * It installs constructor/destructor functions that initialise and tear down
 * all Dino subsystems automatically — you do not then need to call
 * dino_runtime_init() or dino_runtime_cleanup() in main().
 *
 * Example:
 *   #include <dino/core/runtime.h>
 *   DINO_ENABLE_AUTO_RUNTIME;
 *
 * If you do not use this macro, call dino_runtime_init() before using any
 * Dino API and dino_runtime_cleanup() before process exit.
 */
#define DINO_ENABLE_AUTO_RUNTIME                                                                   \
    static void _dino_auto_init(void) __attribute__((constructor));                                \
    static void _dino_auto_fini(void) __attribute__((destructor));                                 \
    static void _dino_auto_init(void)                                                              \
    {                                                                                              \
        dino_runtime_init();                                                                       \
    }                                                                                              \
    static void _dino_auto_fini(void)                                                              \
    {                                                                                              \
        dino_runtime_cleanup();                                                                    \
    }

#endif // DINO_CORE_RUNTIME_H
