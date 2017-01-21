if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(HEAPTRACK_DEBUG_BUILD 1)
else()
    set(HEAPTRACK_DEBUG_BUILD 0)
endif()

configure_file(config.h.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

configure_file(libunwind_config.h.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libunwind_config.h
)
