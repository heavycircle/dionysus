# What everything that links the SDK gets: include paths, options, link flags
# and the objects linked into tests. Shared by the source build
# (sdk/CMakeLists.txt) and the import of a prebuilt SDK (prebuilt.cmake), so
# the two can't drift. Expects dino_core, dino_util and dino_testing to exist,
# and:
#   DINO__SDK_DIR             the SDK's source directory
#   DINO_PLATFORM             win32 or posix
#   DINO__TEST_OBJECTS        objects every test links
#   DINO__LEAK_CHECK_OBJECTS  the leak check's object; empty where it can't work

# Wrappers for the system headers that declare calls with a dino_* replacement
# (sdk/portability/common/dino_portability.h). INTERFACE only: everything that
# links the SDK gets the warnings, the SDK's own sources never do. The POSIX-only
# headers stay off Windows include paths so __has_include(<poll.h>) still tells
# the truth there, and the reverse for the Windows ones.
target_include_directories(dino_core BEFORE INTERFACE
    "${DINO__SDK_DIR}/portability/common"
    "${DINO__SDK_DIR}/portability/${DINO_PLATFORM}")
option(DINO_NO_PLATFORM_WARNINGS "Don't warn about platform calls that have a dino_* replacement" OFF)
if(DINO_NO_PLATFORM_WARNINGS)
    target_compile_definitions(dino_core INTERFACE DINO_ALLOW_PLATFORM_CALLS)
endif()

# Links runtime_auto.c's constructor into every program, which wouldn't otherwise
# pull that object from the archive. Its only job (closed stdio) is Linux-only.
option(DINO_NO_AUTO_RUNTIME "Don't run the SDK's setup before main() (see dino/core/runtime.h)" OFF)
if(NOT WIN32 AND NOT DINO_NO_AUTO_RUNTIME)
    target_link_options(dino_core INTERFACE "LINKER:-u,dino__runtime_auto")
endif()

# SYSTEM: utest.h's own warnings and clang-tidy findings stay out of test code.
target_include_directories(dino_testing SYSTEM INTERFACE "${DINO__SDK_DIR}/testing")
target_link_libraries(dino_testing INTERFACE dino_util)
target_sources(dino_testing INTERFACE ${DINO__TEST_OBJECTS})

option(DINO_NO_LEAK_CHECK "Don't link the allocation counter into dino_test() executables" OFF)
if(DINO__LEAK_CHECK_OBJECTS AND NOT DINO_NO_LEAK_CHECK)
    target_sources(dino_testing INTERFACE ${DINO__LEAK_CHECK_OBJECTS})
    # Every call testing/leakcheck.c wraps, in its order.
    set(dino_leakcheck_wraps
        malloc calloc aligned_alloc memalign valloc realloc reallocarray free posix_memalign
        setenv unsetenv putenv clearenv
        atexit __cxa_atexit
        getpwuid getpwnam getpwent setpwent endpwent getgrgid getgrnam getgrent setgrent endgrent
        gethostbyname gethostbyname2 gethostbyaddr
        tzset strftime
        setlocale newlocale duplocale freelocale)
    # musl's names for the calls that take a 64-bit time_t on a 32-bit target.
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        list(APPEND dino_leakcheck_wraps __localtime64 __localtime64_r __mktime64 __ctime64 __ctime64_r)
    else()
        list(APPEND dino_leakcheck_wraps localtime localtime_r mktime ctime ctime_r)
    endif()
    foreach(wrapped ${dino_leakcheck_wraps})
        target_link_options(dino_testing INTERFACE "LINKER:--wrap=${wrapped}")
    endforeach()
endif()
