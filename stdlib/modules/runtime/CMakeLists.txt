enable_language(ASM)

set(runtime "arch/${PLATFORM}/start.s")
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${runtime}")
    message(STATUS "Checked: ${CMAKE_CURRENT_SOURCE_DIR}/${runtime}")
    message(FATAL_ERROR "No startup code found for PLATFORM: ${PLATFORM}")
endif()

setup_module(runtime ${runtime})
