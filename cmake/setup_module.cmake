function(setup_module target_name)
    file(RELATIVE_PATH REL_PATH "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    string(REPLACE "/" "_" target_name ${REL_PATH})
    message(STATUS "[Module] Target: ${target_name}")

    # Collect files for compilation.
    set(EXTRA_SOURCES ${ARGN})
    file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS *.c)
    file(GLOB_RECURSE HDR_FILES CONFIGURE_DEPENDS *.h)
    add_library(${target_name} STATIC ${SRC_FILES} ${EXTRA_SOURCES})

    # Handle internal includes.
    target_include_directories(${target_name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
            DESTINATION "include/${REL_PATH}"
            FILES_MATCHING PATTERN "*.h")

    install(TARGETS ${target_name} 
            EXPORT ${PROJECT_NAME}Targets
            DESTINATION lib)

    set_property(GLOBAL APPEND PROPERTY DIONYSUS_ALL_MODULES ${target_name})
endfunction()
