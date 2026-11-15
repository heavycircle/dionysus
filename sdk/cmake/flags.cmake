# Loaded through CMAKE_USER_MAKE_RULES_OVERRIDE, which CMake reads after its
# compiler defaults. Per-config _INIT values set in the toolchain file itself
# would get "-O3 -DNDEBUG" appended, silently replacing -Oz.
set(DINO_MINIFY_FLAGS "-Oz -DNDEBUG -ffunction-sections -fdata-sections -fno-ident")

# 64-bit Windows SEH requires unwind tables, and zig builds the mingw CRT with these flags.
if(NOT ZIG_TARGET MATCHES "(x86_64|aarch64)-windows")
    string(APPEND DINO_MINIFY_FLAGS " -fno-unwind-tables -fno-asynchronous-unwind-tables")
endif()

set(CMAKE_C_FLAGS_DEBUG_INIT "-O0 -g")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2 -g -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE_INIT "${DINO_MINIFY_FLAGS}")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT "${DINO_MINIFY_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL_INIT "-s -Wl,--gc-sections")
