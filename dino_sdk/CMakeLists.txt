cmake_minimum_required(VERSION 3.15)
project(DinoSDK C)

add_library(dino_core STATIC
    src/core/thread.c
)

target_include_directories(dino_core PUBLIC include)
