set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set global installation directory.
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "..." FORCE)

if(CMAKE_BUILD_TYPE STREQUAL "release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -ffreestanding -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,-s")
endif()

message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")
