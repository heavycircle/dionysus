# Default: Host operating system
set(ZIG_TARGET "x86_64-linux-musl" CACHE STRING "Zig cross-compilation target")

if(ZIG_TARGET MATCHES "windows")
    set(CMAKE_SYSTEM_NAME Windows)
elseif(ZIG_TARGET MATCHES "linux")
    set(CMAKE_SYSTEM_NAME Linux)
elseif(ZIG_TARGET MATCHES "macos" OR ZIG_TARGET MATCHES "darwin")
    set(CMAKE_SYSTEM_NAME Darwin)
else()
    set(CMAKE_SYSTEM_NAME Generic)
endif()

# Left empty when cross-compiling. What CMake reports when it builds on that
# platform itself, since projects and third-party modules match on it:
# `uname -m` on Linux, PROCESSOR_ARCHITECTURE on Windows. An architecture with
# no entry keeps zig's name.
string(REGEX REPLACE "-.*" "" CMAKE_SYSTEM_PROCESSOR "${ZIG_TARGET}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(dino_native_names x86_64=AMD64 aarch64=ARM64)
else()
    set(dino_native_names x86=i686 arm=armv7l powerpc=ppc powerpc64=ppc64 powerpc64le=ppc64le)
endif()
foreach(dino_pair IN LISTS dino_native_names)
    string(REPLACE "=" ";" dino_pair "${dino_pair}")
    list(GET dino_pair 0 dino_zig_name)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL dino_zig_name)
        list(GET dino_pair 1 CMAKE_SYSTEM_PROCESSOR)
    endif()
endforeach()
unset(dino_native_names)
unset(dino_pair)
unset(dino_zig_name)

# zig's default CPU for 32-bit x86 is the Pentium 4, which needs SSE2. 32-bit
# x86 Linux means i686 (Pentium Pro and later), as Debian's and Ubuntu's i386
# ports do; a project's own -mcpu comes later on the command line and wins.
# Windows needs SSE2 itself, so its targets keep zig's default.
set(dino_cpu "")
if(ZIG_TARGET MATCHES "^x86-linux")
    set(dino_cpu ";-mcpu=i686")
endif()

# The target rides along with the compiler command rather than CMAKE_C_FLAGS,
# so user-supplied flags can never drop it and silently build for the host.
set(CMAKE_C_COMPILER "zig;cc;-target;${ZIG_TARGET}${dino_cpu}" CACHE STRING "C Compiler" FORCE)
set(CMAKE_CXX_COMPILER "zig;c++;-target;${ZIG_TARGET}${dino_cpu}" CACHE STRING "C++ Compiler" FORCE)
# A cross compiler: CMake then identifies it by compiling alone. Otherwise its
# compiler-ID step links a program with no flags at all, and zig builds a
# Debug libc for it (about a minute, cold) that a Release build never uses.
set(CMAKE_C_COMPILER_TARGET "${ZIG_TARGET}")
set(CMAKE_CXX_COMPILER_TARGET "${ZIG_TARGET}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMake wants ar/ranlib as standalone executables, but zig only offers them as
# subcommands. Small wrappers keep archiving independent of host binutils.
function(dino_zig_tool name)
    set(dir "${CMAKE_BINARY_DIR}/CMakeFiles/dino-tools")
    if(CMAKE_HOST_WIN32)
        set(tool "${dir}/${name}.cmd")
        file(WRITE "${tool}" "@zig ${name} %*\r\n")
    else()
        set(tool "${dir}/${name}")
        file(WRITE "${tool}" "#!/bin/sh\nexec zig ${name} \"$@\"\n")
        file(CHMOD "${tool}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE)
    endif()
    string(TOUPPER "${name}" var)
    set(CMAKE_${var} "${tool}" CACHE FILEPATH "zig ${name} wrapper" FORCE)
endfunction()
dino_zig_tool(ar)
dino_zig_tool(ranlib)

# try_compile projects (check_function_exists and friends) read this file again
# without the cache, so pass the target on, or every check runs against the
# default target. They link a real executable: compiling alone would find any
# function, since the check declares it itself.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ZIG_TARGET DINO_DYNAMIC)

# Statically link where zig can: musl and MinGW, which covers every named
# target. Any other C library (glibc, macOS, the BSDs) must be linked
# dynamically, and zig refuses -static for it. _INIT values only seed the
# cache on the first configure, so reconfiguring never duplicates them and
# user-supplied flags are kept. DINO_DYNAMIC also forces it, for `dino test
# --memcheck`: valgrind can't see the allocator inside a static binary.
if(NOT ZIG_TARGET MATCHES "-musl|-windows-gnu")
    set(DINO_DYNAMIC ON)
endif()
if(NOT DINO_DYNAMIC)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
endif()

# Per-configuration flags (minified Release, symbols in Debug) live in flags.cmake.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/flags.cmake")

# Projects configured without a build type still get the minified build.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug, Release, RelWithDebInfo, MinSizeRel")
endif()
# CMake's own checks (the ABI check, check_function_exists) link with the flags
# of this build type, not Release's: each set of link flags makes zig build
# another libc, and a Debug configure would otherwise also build a Release one.
set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")

# Don't use host machine glibc
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
