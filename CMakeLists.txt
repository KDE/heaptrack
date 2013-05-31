cmake_minimum_required(VERSION 2.8)
project(mallocinfo)

add_library(dumpmallocinfo SHARED dumpmallocinfo.cpp)
target_link_libraries(dumpmallocinfo -lpthread)

add_definitions("-std=c++11")

add_executable(mallocinfo main.cpp)
target_link_libraries(mallocinfo dumpmallocinfo)

add_executable(test_c test.c)

install(TARGETS mallocinfo RUNTIME DESTINATION bin)
