include(ExternalProject)

cmake_policy(SET CMP0114 NEW) # Child targets should be independent to adopt their steps.
cmake_policy(SET CMP0177 NEW) # Normalize install() destinations.

macro(get_target_name name target_name)
    set(project_id "${PLATFORM}-${CMAKE_BUILD_TYPE}")
    string(TOLOWER "${project_id}" project_id_lower)
    set(${target_name} "${name}_${project_id_lower}")
endmacro()

macro(add_dionysus_project name source_dir)
    get_target_name(${name} target_name)

    # Argparse
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs DEPENDS CMAKE_ARGS)
    cmake_parse_arguments(AP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(binary_dir "${CMAKE_BINARY_DIR}/${name}")
    set(install_dir "${CMAKE_INSTALL_PREFIX}")

    message("Setting up external project ${target_name} (${source_dir})")

    ExternalProject_Add(${target_name}
        SOURCE_DIR "${source_dir}"
        BINARY_DIR "${binary_dir}"
        INSTALL_DIR "${install_dir}"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            -DCMAKE_INSTALL_PREFIX=${install_dir}
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DARCH=${PLATFORM}
            -DDIONYSUS_ROOT=${DIONYSUS_ROOT}
            ${AP_CMAKE_ARGS}
        DEPENDS
            ${AP_DEPENDS}
        STEP_TARGETS install
    )

    ExternalProject_Add_StepTargets(${target_name} build install)
endmacro()
