set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set global installation directory.
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "..." FORCE)

include(framework)
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")
