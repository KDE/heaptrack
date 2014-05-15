cmake_minimum_required(VERSION 2.8)
project(mallocinfo)

add_library(dumpmallocinfo SHARED dumpmallocinfo.cpp)
target_link_libraries(dumpmallocinfo -lpthread)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11")

add_executable(mallocinfo main.cpp)
target_link_libraries(mallocinfo dumpmallocinfo)

add_library(malloctrace SHARED malloctrace.cpp)
target_link_libraries(malloctrace -ldl -lunwind -lunwind-x86_64)

add_executable(test_c test.c)
add_executable(test_cpp test.cpp)

install(TARGETS mallocinfo RUNTIME DESTINATION bin)
