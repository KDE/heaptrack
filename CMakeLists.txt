cmake_minimum_required(VERSION 2.8)
project(heaptrack)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11")

find_package(Boost 1.45.0 COMPONENTS iostreams)
find_package(Threads)

include_directories(
    ${Boost_INCLUDE_DIRS}
)

add_subdirectory(tests)
add_subdirectory(libbacktrace)

add_library(malloctrace SHARED malloctrace.cpp)
target_link_libraries(malloctrace -ldl backtrace)

add_executable(malloctrace_eval malloctrace_main.cpp)
target_link_libraries(malloctrace_eval ${Boost_LIBRARIES})

add_executable(malloctrace_interpret malloctrace_interpret.cpp)
target_link_libraries(malloctrace_interpret backtrace)

# FIXME: install
# install(TARGETS heaptrack RUNTIME DESTINATION bin)
