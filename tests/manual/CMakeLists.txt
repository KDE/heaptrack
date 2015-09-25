set(CMAKE_BUILD_TYPE Debug)

add_executable(test_c test.c)
add_executable(test_cpp test.cpp)
add_executable(threaded threaded.cpp)
target_link_libraries(threaded ${CMAKE_THREAD_LIBS_INIT})

add_executable(callgraph callgraph.cpp)

add_library(testlib SHARED lib.cpp)
add_executable(test_lib test_lib.cpp)
target_link_libraries(test_lib testlib)
