setup_app(string_example)

install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/string_example"
    DESTINATION string_example
    RENAME string_example
)
