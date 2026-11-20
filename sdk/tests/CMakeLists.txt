# The SDK's own tests. Every test_*.c is one test executable:
#
#   DINO_SDK_PATH=$PWD/sdk dino test --all -C sdk/tests
cmake_minimum_required(VERSION 3.20)
project(DinoSdkTests C)

file(GLOB tests CONFIGURE_DEPENDS test_*.c)
foreach(test ${tests})
    get_filename_component(name ${test} NAME_WE)
    dino_test(${name} ${test})
endforeach()

set_tests_properties(test_logging_env PROPERTIES ENVIRONMENT DINO_LOG=WaRn)
# musl's setenv grows environ into memory it keeps for the life of the process,
# which the leak check would count.
set_tests_properties(test_env PROPERTIES ENVIRONMENT DINO_LEAK_CHECK=0)
