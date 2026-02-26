cmake_minimum_required(VERSION 3.15)
project(Dionysus VERSION "1.0.0" DESCRIPTION "Cross-Compiler" LANGUAGES C ASM)

# Get Caching Binary.
find_program(CCACHE_BINARY ccache)
if(CCACHE_BINARY)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_BINARY})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_BINARY})
    message(STATUS "Dionysus: Caching Enabled!")
endif()

# Default File Paths.
set(DIONYSUS_ROOT   "${CMAKE_SOURCE_DIR}")
set(DIONYSUS_STDLIB "${CMAKE_SOURCE_DIR}/stdlib")
set(DIONYSUS_APPS   "${CMAKE_SOURCE_DIR}/sample_apps")

# Default Build Information
set(PLATFORM x86_64)
set(CMAKE_BUILD_TYPE debug)

# Add cmake folder to PATH.
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(settings)
include(dionysus)

### Standard Library ###########################################

add_dionysus_project(stdlib ${DIONYSUS_STDLIB})
get_target_name(stdlib stdlib_target)

### Sample Apps ################################################

add_dionysus_project(example_apps ${DIONYSUS_APPS})
get_target_name(example_apps example_apps_target)

add_dependencies(${example_apps_target}-build ${stdlib_target}-install)
message("New Dependency: ${example_apps_target}-build <-- ${stdlib_target}-install")

### Merge Compile Commands #####################################

set(MERGE_PROJECTS stdlib example_apps)

add_custom_target(merge_compdb ALL
    COMMAND ${PYTHON_EXECUTABLE} "${DIONYSUS_ROOT}/dionysus/tools/merge_compdb.py"
        ${CMAKE_BINARY_DIR}
        "${PROJECT_SOURCE_DIR}/compile_commands.json"
        ${MERGE_PROJECTS}
    COMMENT "Merging compilation databases ..."
    VERBATIM
)

add_dependencies(merge_compdb ${example_apps_target}-build ${stdlib_target}-build)
