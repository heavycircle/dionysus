function(setup_module target_name)
    set(EXTRA_SOURCES ${ARGN})
    file(RELATIVE_PATH REL_PATH "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    string(REPLACE "/" "_" target_name ${REL_PATH})
    message(STATUS "[Module] Target: ${target_name}")

    # Collect files for compilation.
    file(GLOB_RECURSE SRC_FILES *.c)
    file(GLOB_RECURSE HDR_FILES *.h)
    add_library(${target_name} STATIC ${SRC_FILES} ${EXTRA_SOURCES})

    # Handle internal includes.
    target_include_directories(${target_name} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    )

    set_property(GLOBAL APPEND PROPERTY DIONYSUS_ALL_MODULES ${target_name})

    # Install all files.
    install(TARGETS ${target_name} DESTINATION lib)
    install(FILES ${HDR_FILES} DESTINATION "include/${REL_PATH}")
endfunction()
