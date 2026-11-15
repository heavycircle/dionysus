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

# Statically link and minify
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
set(MINIFY_C_FLAGS "-target ${ZIG_TARGET} -ffunction-sections -fdata-sections -Oz -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident")
set(MINIFY_LD_FLAGS "-target ${ZIG_TARGET} -s -static -Wl,--gc-sections")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MINIFY_C_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MINIFY_LD_FLAGS}" CACHE STRING "" FORCE)

# Don't use host machine glibc
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
