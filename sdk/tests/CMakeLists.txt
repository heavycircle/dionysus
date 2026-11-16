# The SDK's own tests. Every test_*.c is one test executable:
#
#   DINO_SDK_PATH=$PWD/sdk dino test --all -C sdk/tests
cmake_minimum_required(VERSION 3.15)
project(DinoSdkTests C)

file(GLOB tests CONFIGURE_DEPENDS test_*.c)
foreach(test ${tests})
    get_filename_component(name ${test} NAME_WE)
    dino_test(${name} ${test})
endforeach()
