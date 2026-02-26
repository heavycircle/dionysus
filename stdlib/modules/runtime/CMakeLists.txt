enable_language(ASM)

set(runtime "arch/${ARCH}/start.s")
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${runtime}")
    message(STATUS "Checked: ${CMAKE_CURRENT_SOURCE_DIR}/${runtime}")
    message(FATAL_ERROR "No startup code found for ARCH: ${ARCH}")
endif()

setup_module(runtime ${runtime})
