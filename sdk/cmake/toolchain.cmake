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

# The target rides along with the compiler command rather than CMAKE_C_FLAGS,
# so user-supplied flags can never drop it and silently build for the host.
set(CMAKE_C_COMPILER "zig;cc;-target;${ZIG_TARGET}" CACHE STRING "C Compiler" FORCE)
set(CMAKE_CXX_COMPILER "zig;c++;-target;${ZIG_TARGET}" CACHE STRING "C++ Compiler" FORCE)

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
        # file(COPY) is the only pre-3.19 way to set the executable bit.
        file(WRITE "${dir}/src/${name}" "#!/bin/sh\nexec zig ${name} \"$@\"\n")
        file(COPY "${dir}/src/${name}" DESTINATION "${dir}"
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
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

# Statically link. _INIT values only seed the cache on the first configure, so
# reconfiguring never duplicates them and user-supplied flags are kept.
# DINO_DYNAMIC is only for `dino test --memcheck`: valgrind can't see the
# allocator inside a static binary.
if(NOT DINO_DYNAMIC)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
endif()

# Per-configuration flags (minified Release, symbols in Debug) live in flags.cmake.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/flags.cmake")

# Projects configured without a build type still get the minified build.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug, Release, RelWithDebInfo, MinSizeRel")
endif()

# Don't use host machine glibc
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
