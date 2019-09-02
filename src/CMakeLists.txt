include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_subdirectory(util)

if (HEAPTRACK_BUILD_TRACK)
    add_subdirectory(track)
endif()

if (HEAPTRACK_BUILD_INTERPRET)
    add_subdirectory(interpret)
endif()

if (HEAPTRACK_BUILD_PRINT OR HEAPTRACK_BUILD_GUI)
    add_subdirectory(analyze)
endif()
