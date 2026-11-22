# Imports the SDK from DINO_SDK_PREBUILT_DIR, built on its own by `dino build`
# with DINO_SDK_PREBUILT=1 (cli/prebuilt.zig), instead of compiling it in the
# project. Only CMake knows the project's effective flags, so this is where
# the prebuilt set is checked against them: anything that could change the
# SDK's object code and isn't known not to keeps the source build. An unknown
# flag costs a slower build, never a wrong object. Sets DINO__PREBUILT_USED.
set(DINO__PREBUILT_USED OFF)

function(_dino_prebuilt_reason out)
    set(dir "${DINO_SDK_PREBUILT_DIR}")
    if(NOT EXISTS "${dir}/dino-prebuilt.cmake")
        set(${out} "${dir} is incomplete" PARENT_SCOPE)
        return()
    endif()
    include("${dir}/dino-prebuilt.cmake")
    if(NOT DINO__PREBUILT_TARGET STREQUAL ZIG_TARGET OR NOT DINO__PREBUILT_BUILD_TYPE STREQUAL CMAKE_BUILD_TYPE)
        set(${out} "it was built for ${DINO__PREBUILT_TARGET} ${DINO__PREBUILT_BUILD_TYPE}, not ${ZIG_TARGET} ${CMAKE_BUILD_TYPE}" PARENT_SCOPE)
        return()
    endif()

    # Settings made before project() that the SDK's sources would inherit.
    foreach(property COMPILE_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES)
        get_directory_property(value ${property})
        if(value)
            set(${out} "the project sets ${property} for the SDK too (${value})" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    foreach(variable CMAKE_POSITION_INDEPENDENT_CODE CMAKE_INTERPROCEDURAL_OPTIMIZATION
            CMAKE_INTERPROCEDURAL_OPTIMIZATION_${CMAKE_BUILD_TYPE} CMAKE_C_VISIBILITY_PRESET)
        if(${variable})
            set(${out} "the project sets ${variable}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    # The flags C sources get. Before C is enabled they come from CFLAGS;
    # the per-configuration ones must be dino's own (flags.cmake).
    string(TOUPPER "${CMAKE_BUILD_TYPE}" config)
    get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    set(flags "${CMAKE_C_FLAGS}")
    if(NOT "C" IN_LIST languages)
        string(APPEND flags " $ENV{CFLAGS}")
        if(DEFINED CMAKE_C_FLAGS_${config})
            set(${out} "CMAKE_C_FLAGS_${config} is set" PARENT_SCOPE)
            return()
        endif()
    else()
        string(STRIP "${CMAKE_C_FLAGS_${config}}" own)
        string(STRIP "${CMAKE_C_FLAGS_${config}_INIT}" init)
        if(NOT own STREQUAL init)
            set(${out} "CMAKE_C_FLAGS_${config} is '${own}', not dino's '${init}'" PARENT_SCOPE)
            return()
        endif()
    endif()
    # Flags known not to change object code: warnings (but -Wa, -Wp, and -Wl,
    # pass options on to other tools), the language standard, and how
    # diagnostics look. Never a list of flags known to matter.
    separate_arguments(flags UNIX_COMMAND "${flags}")
    foreach(flag IN LISTS flags)
        if(flag MATCHES "^-W[apl],")
        elseif(flag MATCHES "^-W" OR flag MATCHES "^-(w|pedantic|pedantic-errors)$" OR flag MATCHES "^-std="
                OR flag MATCHES "^-f(no-)?(color-diagnostics|diagnostics-color(=.*)?|caret-diagnostics|show-column|diagnostics-show-option)$"
                OR flag MATCHES "^-f(error-limit|message-length)=[0-9]+$")
            continue()
        endif()
        set(${out} "C flag ${flag} could change the SDK's code" PARENT_SCOPE)
        return()
    endforeach()
    set(${out} "" PARENT_SCOPE)
endfunction()

_dino_prebuilt_reason(_dino_reason)
if(_dino_reason)
    message(NOTICE "dino: compiling the SDK in this project, not using the prebuilt one: ${_dino_reason}")
else()
    include("${DINO_SDK_PREBUILT_DIR}/dino-prebuilt.cmake")
    set(_dino_lib "${DINO_SDK_PREBUILT_DIR}")
    add_library(dino_core STATIC IMPORTED GLOBAL)
    set_target_properties(dino_core PROPERTIES
        IMPORTED_LOCATION "${_dino_lib}/libdino_core.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DINO_SDK_PATH}/include;${_dino_lib}/include")
    if(WIN32)
        set_property(TARGET dino_core APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32)
    endif()
    add_library(dino_util STATIC IMPORTED GLOBAL)
    set_target_properties(dino_util PROPERTIES
        IMPORTED_LOCATION "${_dino_lib}/libdino_util.a"
        INTERFACE_LINK_LIBRARIES dino_core)
    add_library(dino_testing INTERFACE)

    set(DINO__SDK_DIR "${DINO_SDK_PATH}")
    if(WIN32)
        set(DINO_PLATFORM win32)
    else()
        set(DINO_PLATFORM posix)
    endif()
    set(DINO__TEST_OBJECTS "${_dino_lib}/unbuffered.o")
    set(DINO__LEAK_CHECK_OBJECTS "")
    if(DINO__PREBUILT_LEAK_CHECK)
        set(DINO__LEAK_CHECK_OBJECTS "${_dino_lib}/leakcheck.o")
    endif()
    set(DINO__UTF8_OBJECTS "")
    if(DINO__PREBUILT_UTF8_CONSOLE)
        set(DINO__UTF8_OBJECTS "${_dino_lib}/utf8_console.o")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/interface.cmake")
    set(DINO__PREBUILT_USED ON)
    unset(_dino_lib)
endif()
unset(_dino_reason)
