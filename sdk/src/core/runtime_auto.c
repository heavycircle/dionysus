#include <dino/core/fs.h>

// Internal, not API (the dino__ prefix, CONTRIBUTING.md#design-rules): user
// code must not call or declare it, and it may change or disappear.
//
// Nothing references this object, so dino_core's link options name this symbol
// to pull it out of the archive (sdk/CMakeLists.txt); it's a file of its own so
// DINO_NO_AUTO_RUNTIME leaves it out even when runtime.c is linked. Only setup
// that must precede main() belongs here: anything that can fail is done lazily
// by the call that needs it, where the error can be returned.
//
// Priority 101 is the first one programs may use. Without a priority this ran
// in link order, after the constructors and C++ initializers of the program's
// own objects, which come before the SDK's archive on the link line.
__attribute__((constructor(101))) void dino__runtime_auto(void)
{
    // Must run before the program opens files, or they could become fds 0-2.
    // Nothing can be done about a failure here; the descriptors are occupied
    // either way (see dino_fs_init).
    dino_fs_init();
}
