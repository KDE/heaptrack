cmake_minimum_required(VERSION 2.8)
project(mallocinfo)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11")

add_subdirectory(tests)
add_subdirectory(libbacktrace)

add_library(dumpmallocinfo SHARED dumpmallocinfo.cpp)
target_link_libraries(dumpmallocinfo -lpthread)

add_executable(mallocinfo main.cpp)
target_link_libraries(mallocinfo dumpmallocinfo)

add_library(malloctrace SHARED malloctrace.cpp)
target_link_libraries(malloctrace -ldl backtrace)

add_executable(malloctrace_eval malloctrace_main.cpp)
target_link_libraries(malloctrace_eval backtrace)

install(TARGETS mallocinfo RUNTIME DESTINATION bin)
