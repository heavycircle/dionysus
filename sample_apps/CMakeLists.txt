cmake_minimum_required(VERSION 3.15)
project(sample_apps)

# Get shared project code.
set(CMAKE_MODULE_PATH "${DIONYSUS_ROOT}/cmake" ${CMAKE_MODULE_PATH})
include(framework)

# Add source code
add_subdirectory(source)
