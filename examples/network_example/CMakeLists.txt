cmake_minimum_required(VERSION 3.15)
project(DinoNetworkExample C)

add_executable(network_example main.c)
target_link_libraries(network_example PRIVATE dino_core)
