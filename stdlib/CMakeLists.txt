cmake_minimum_required(VERSION 3.15)

if(NOT PROJECT_SOURCE_DIR)
    project(stdlib C ASM)
    message("Building Standalone: stdlib")

    message(STATUS "CMAKE_SYSTEM_NAME:      ${CMAKE_SYSTEM_NAME}")
    message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
    message(STATUS "CMAKE_BUILD_TYPE:       ${CMAKE_BUILD_TYPE}")
    message(STATUS "CMAKE_INSTALL_PREFIX:   ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "CMAKE_TOOLCHAIN_FILE:   ${CMAKE_TOOLCHAIN_FILE}")
else()
    message("Building Integrated: stdlib")
endif()

# Get shared project code.
set(CMAKE_MODULE_PATH "${DIONYSUS_ROOT}/cmake" ${CMAKE_MODULE_PATH})
message("CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")
include(framework)

message(STATUS "CMAKE_C_FLAGS:      ${CMAKE_C_FLAGS}")

# Add the code
add_subdirectory(modules)
