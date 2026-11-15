# Injected by `dino build` via CMAKE_PROJECT_INCLUDE, so it runs after every project() call.
# This is what lets a plain CMakeLists.txt link `dino_core` without vendoring the SDK.
include_guard(GLOBAL)

if(NOT TARGET dino_core)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." "${CMAKE_BINARY_DIR}/_dino" EXCLUDE_FROM_ALL)
endif()

function(dino_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dino_core)
endfunction()
