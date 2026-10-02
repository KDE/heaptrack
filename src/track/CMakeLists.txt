# heaptrack: bash script to inject/preload
configure_file(heaptrack.sh.cmake
    ${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}/heaptrack @ONLY
)

install(PROGRAMS ${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}/heaptrack
    DESTINATION ${BIN_INSTALL_DIR}
)

# heaptrack_env: runtime environment tests
add_executable(heaptrack_env heaptrack_env.cpp)

install(TARGETS heaptrack_env
    RUNTIME DESTINATION ${LIBEXEC_INSTALL_DIR}
)

set_target_properties(heaptrack_env PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${LIBEXEC_INSTALL_DIR}"
)

# heaptrack_unwind: gather backtraces
if (HEAPTRACK_USE_LIBUNWIND)
    add_library(heaptrack_unwind STATIC trace_libunwind.cpp)
    target_include_directories(heaptrack_unwind PRIVATE ${LIBUNWIND_INCLUDE_DIRS})
    target_link_libraries(heaptrack_unwind PUBLIC ${LIBUNWIND_LIBRARIES})
else()
    add_library(heaptrack_unwind STATIC trace_unwind_tables.cpp)
endif()

set_property(TARGET heaptrack_unwind PROPERTY POSITION_INDEPENDENT_CODE ON)

# heaptrack_common: shared libheaptrack code
add_library(heaptrack_common STATIC libheaptrack.cpp)
set_property(TARGET heaptrack_common PROPERTY POSITION_INDEPENDENT_CODE ON)

if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(LIBUTIL_LIBRARY "util")
endif()

target_link_libraries(heaptrack_common PUBLIC
    ${CMAKE_DL_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    ${LIBUTIL_LIBRARY}
    rt
    tsl::robin_map
    heaptrack_unwind
)

# heaptrack_preload: track a newly started process
add_library(heaptrack_preload MODULE
    heaptrack_preload.cpp
)

target_link_libraries(heaptrack_preload PRIVATE
    heaptrack_common
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
)

target_link_libraries(heaptrack_inject PRIVATE
    heaptrack_common
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
