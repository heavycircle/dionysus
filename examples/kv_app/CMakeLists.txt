cmake_minimum_required(VERSION 3.15)
project(DinoKvApp C)

# The application's logic is a library, so the program and the tests link
# exactly the same code. main.c stays a thin layer on top.
add_library(kv STATIC src/kv.c src/protocol.c src/server.c)
target_include_directories(kv PUBLIC src)
target_link_libraries(kv PUBLIC dino_core)

dino_executable(kv_app src/main.c)
target_link_libraries(kv_app PRIVATE kv)

# Every tests/test_*.c is a test executable (utest.h); `dino test` runs them.
file(GLOB TESTS CONFIGURE_DEPENDS tests/test_*.c)
foreach(test ${TESTS})
    get_filename_component(test_name ${test} NAME_WE)
    dino_test(${test_name} ${test})
    target_link_libraries(${test_name} PRIVATE kv)
endforeach()
