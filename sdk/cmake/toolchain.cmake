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

# Use zig cc
set(CMAKE_C_COMPILER "zig;cc" CACHE STRING "C Compiler" FORCE)
set(CMAKE_CXX_COMPILER "zig;c++" CACHE STRING "C++ Compiler" FORCE)

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

# Statically link and minify
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
set(MINIFY_C_FLAGS "-target ${ZIG_TARGET} -ffunction-sections -fdata-sections -Oz -fno-ident")

# 64-bit Windows SEH requires unwind tables, and zig builds the mingw CRT with these flags.
if(NOT ZIG_TARGET MATCHES "(x86_64|aarch64)-windows")
    string(APPEND MINIFY_C_FLAGS " -fno-unwind-tables -fno-asynchronous-unwind-tables")
endif()
set(MINIFY_LD_FLAGS "-target ${ZIG_TARGET} -s -static -Wl,--gc-sections")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MINIFY_C_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MINIFY_LD_FLAGS}" CACHE STRING "" FORCE)

# Don't use host machine glibc
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
