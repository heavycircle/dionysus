cmake_minimum_required(VERSION 3.15)

# Projects that still add_subdirectory() the SDK would otherwise redefine the
# target that Dino.cmake already created.
if(TARGET dino_core)
    return()
endif()

project(DinoSDK VERSION 0.1.2 LANGUAGES C)
# Dropped only on the release commit; must match build.zig.zon (CONTRIBUTING.md#versioning).
set(DINO_VERSION_PRERELEASE "")

add_library(dino_core STATIC
    src/core/fs.c
    src/core/io.c
    src/core/mutex.c
    src/core/network.c
    src/core/runtime.c
    src/core/thread.c
)

configure_file(cmake/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/dino/version.h" @ONLY)
target_include_directories(dino_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/include")

if(WIN32)
    target_link_libraries(dino_core PRIVATE ws2_32)
endif()
