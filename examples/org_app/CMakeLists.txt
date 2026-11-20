cmake_minimum_required(VERSION 3.20)
project(DinoOrgApp C)

# org::config is defined by examples/org_library. It's available because that
# directory is on DINO_LIBRARY_PATH, the same way dino_core and dino_util are
# always available:
#
#   DINO_LIBRARY_PATH=$PWD/examples/org_library dino run -C examples/org_app
#
# Linking it is the only line this project needs.
dino_executable(org_app main.c)
target_link_libraries(org_app PRIVATE org::config)
