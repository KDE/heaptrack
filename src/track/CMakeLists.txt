include_directories(
    ${Boost_INCLUDE_DIRS}
)

# heaptrack: bash script to inject/preload
configure_file(heaptrack.sh.cmake
    ${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}/heaptrack @ONLY
)

install(PROGRAMS ${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}/heaptrack
    DESTINATION ${BIN_INSTALL_DIR}
)

if (HEAPTRACK_USE_LIBUNWIND)
    include_directories(${LIBUNWIND_INCLUDE_DIR})

    add_library(heaptrack_unwind STATIC trace_libunwind.cpp)
    target_link_libraries(heaptrack_unwind LINK_PRIVATE ${LIBUNWIND_LIBRARY})
else()
    add_library(heaptrack_unwind STATIC trace_unwind_tables.cpp)
endif()

set_property(TARGET heaptrack_unwind PROPERTY POSITION_INDEPENDENT_CODE ON)

# heaptrack_preload: track a newly started process
add_library(heaptrack_preload MODULE
    heaptrack_preload.cpp
    libheaptrack.cpp
)

target_link_libraries(heaptrack_preload LINK_PRIVATE
    ${CMAKE_DL_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    heaptrack_unwind
    rt
)

set_target_properties(heaptrack_preload PROPERTIES
    VERSION ${HEAPTRACK_LIB_VERSION}
    SOVERSION ${HEAPTRACK_LIB_SOVERSION}
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${LIB_INSTALL_DIR}/heaptrack"
)

install(TARGETS heaptrack_preload
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}/heaptrack/
)

# heaptrack_inject: track an already running process
add_library(heaptrack_inject MODULE
    heaptrack_inject.cpp
    libheaptrack.cpp
)

target_link_libraries(heaptrack_inject LINK_PRIVATE
    ${CMAKE_DL_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    heaptrack_unwind
    rt
)

set_target_properties(heaptrack_inject PROPERTIES
    VERSION ${HEAPTRACK_LIB_VERSION}
    SOVERSION ${HEAPTRACK_LIB_SOVERSION}
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${LIB_INSTALL_DIR}/heaptrack"
)

install(TARGETS heaptrack_inject
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}/heaptrack/
)

# public API for custom pool allocators or static binaries
install(FILES heaptrack_api.h
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include
)
