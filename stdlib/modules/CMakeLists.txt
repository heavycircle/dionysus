subdirlist(SUBDIRS ${CMAKE_CURRENT_LIST_DIR})
foreach(DIRECTORY ${SUBDIRS})
    add_subdirectory(${DIRECTORY})
endforeach()
