#pragma GCC system_header
// Shared by the wrapper headers in sdk/portability/. Each wrapper includes
// the real system header, then redeclares the calls that have a dino_*
// replacement with a warning attached, so code that links the SDK hears about
// them at the call site: in `dino build`, in clangd and in clang-tidy.
//
// Opting out (docs/guide/portability-warnings.md):
//   - a project: -DDINO_NO_PLATFORM_WARNINGS=ON
//   - a target or file: define DINO_ALLOW_PLATFORM_CALLS
//   - one call: DINO_PLATFORM_CALL(pid = fork());
#ifndef DINO_PORTABILITY_H
#define DINO_PORTABILITY_H

#define DINO_PLATFORM_CALL(...)                                                                    \
    _Pragma("clang diagnostic push")                                                               \
        _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")                          \
            __VA_ARGS__ _Pragma("clang diagnostic pop")

// The wrappers redeclare each call as `__typeof__(f) f` marked deprecated:
// __typeof__ keeps each libc's exact prototype, so nothing is restated by hand,
// and a deprecation, unlike clang's diagnose_if, stays quiet when the call is
// inside a system header (a library's inline function), which the project can't
// change. Spelled out rather than behind a macro, so a warning's note points at
// a readable line.
//
// Each wrapper redeclares only once the real header's own include guard is
// defined (musl's, glibc's or MinGW's). A project with a header of the same
// name, such as its own "process.h", reaches the wrapper first, and
// #include_next then finds the project's header, which declares none of the
// calls: there is nothing to redeclare, so the wrapper stays out of the way.
#ifndef DINO_ALLOW_PLATFORM_CALLS
#define DINO__WARNINGS
#endif

#endif // DINO_PORTABILITY_H
