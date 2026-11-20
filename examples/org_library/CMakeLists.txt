cmake_minimum_required(VERSION 3.20)
project(OrgLibrary C)

# Every dino project that has this directory on DINO_LIBRARY_PATH gets the
# targets below. On its own (`dino test -C examples/org_library`) it's an
# ordinary dino project, which is how its tests run.

if(DINO_SDK_VERSION VERSION_LESS 0.6)
    message(FATAL_ERROR "org_library needs the dino SDK 0.6 or newer (found ${DINO_SDK_VERSION})")
endif()

# Prefixed names keep clear of the SDK's dino_* targets and other libraries';
# the alias is the name projects are encouraged to link.
add_library(org_config STATIC src/config.c)
add_library(org::config ALIAS org_config)
target_include_directories(org_config PUBLIC include)
target_link_libraries(org_config PUBLIC dino_util)

# Only registered with `dino test` when this is the project being tested, never
# in the projects that link org::config.
file(GLOB TESTS CONFIGURE_DEPENDS tests/test_*.c)
foreach(test ${TESTS})
    get_filename_component(test_name ${test} NAME_WE)
    dino_test(${test_name} ${test})
    target_link_libraries(${test_name} PRIVATE org::config)
endforeach()
