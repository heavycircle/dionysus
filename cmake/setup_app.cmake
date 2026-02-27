function(setup_app target_name)
    message(STATUS "Configuring App: ${target_name}")

    file(GLOB_RECURSE SRC_FILES *.c)
    add_executable(${target_name} ${SRC_FILES})

    set_target_properties(${target_name} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    target_include_directories(${target_name} PRIVATE "${CMAKE_INSTALL_PREFIX}/include")
    target_compile_options(${target_name} PRIVATE -nostdlib -fno-builtin)
    target_link_options(${target_name} PRIVATE -nostdlib -static -Wl,--gc-sections)

    file(GLOB DIONYSUS_LIBS "${CMAKE_INSTALL_PREFIX}/lib/lib*.a")
    target_link_libraries(${target_name} PRIVATE ${DIONYSUS_LIBS})

    install(TARGETS ${target_name} DESTINATION bin) # TODO - May be optional
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/${target_name}"
        DESTINATION ${target_name}
        RENAME ${target_name}
    )
endfunction()
