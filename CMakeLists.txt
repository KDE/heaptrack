cmake_minimum_required(VERSION 2.8)
project(heaptrack)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

find_package(Boost 1.45.0 COMPONENTS iostreams program_options)
find_package(Threads)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    3rdparty/
)

add_subdirectory(3rdparty)
add_subdirectory(tests)

add_library(libheaptrack SHARED libheaptrack.cpp)
# the final .so should be named libheaptrack, not liblibheaptrack
set_target_properties(libheaptrack PROPERTIES OUTPUT_NAME heaptrack)
target_link_libraries(libheaptrack ${CMAKE_DL_LIBS} backtrace)

add_executable(heaptrack_interpret heaptrack_interpret.cpp)
target_link_libraries(heaptrack_interpret backtrace)

add_executable(heaptrack_print heaptrack_print.cpp)
target_link_libraries(heaptrack_print ${Boost_LIBRARIES})

set(BIN_INSTALL_DIR "bin")
set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)")
set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}")
set(LIBEXEC_INSTALL_DIR "${LIB_INSTALL_DIR}/heaptrack/libexec")

file(RELATIVE_PATH LIBEXEC_REL_PATH
    "${CMAKE_INSTALL_PREFIX}/${BIN_INSTALL_DIR}"
    "${CMAKE_INSTALL_PREFIX}/${LIBEXEC_INSTALL_DIR}")

file(RELATIVE_PATH LIB_REL_PATH
    "${CMAKE_INSTALL_PREFIX}/${BIN_INSTALL_DIR}"
    "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}")

configure_file(heaptrack.sh.cmake ${CMAKE_CURRENT_BINARY_DIR}/heaptrack @ONLY)

install(TARGETS heaptrack_print RUNTIME DESTINATION ${BIN_INSTALL_DIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/heaptrack DESTINATION ${BIN_INSTALL_DIR})
install(TARGETS libheaptrack LIBRARY DESTINATION ${LIB_INSTALL_DIR})
install(TARGETS heaptrack_interpret RUNTIME DESTINATION ${LIBEXEC_INSTALL_DIR})
