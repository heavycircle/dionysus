cmake_minimum_required(VERSION 3.15)
project(DinoSDK C)

add_library(dino_core STATIC
    src/core/fs.c
    src/core/io.c
    src/core/mutex.c
    src/core/network.c
    src/core/runtime.c
    src/core/thread.c
)

target_include_directories(dino_core PUBLIC include)

if(WIN32)
    target_link_libraries(dino_core PRIVATE ws2_32)
endif()
