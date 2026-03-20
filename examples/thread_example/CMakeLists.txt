cmake_minimum_required(VERSION 3.15)
project(DinoThreadExample C)

if(NOT DEFINED DINO_SDK_PATH)
    message(FATAL_ERROR "Missing DINO_SDK_PATH environment variable!")
endif()

add_subdirectory("${DINO_SDK_PATH}" dino_build)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE dino_core)
