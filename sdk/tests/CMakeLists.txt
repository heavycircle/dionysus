# The SDK's own tests. Every test_*.c is one test executable:
#
#   DINO_SDK_PATH=$PWD/sdk dino test --all -C sdk/tests
cmake_minimum_required(VERSION 3.20)
# Warnings in the SDK's own sources fail this build; in a project they never do.
set(DINO_SDK_WERROR ON)
project(DinoSdkTests C)

file(GLOB tests CONFIGURE_DEPENDS test_*.c)
foreach(test ${tests})
    get_filename_component(name ${test} NAME_WE)
    dino_test(${name} ${test})
endforeach()

set_tests_properties(test_logging_env PROPERTIES ENVIRONMENT DINO_LOG=WaRn)
# Its sleeping threads are still running, holding their handles, at exit.
set_tests_properties(test_sleep_forever PROPERTIES ENVIRONMENT DINO_LEAK_CHECK=0)
