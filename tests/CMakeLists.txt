set(CMAKE_BUILD_TYPE Debug)

add_executable(test_c test.c)
target_link_libraries(test_c libheaptrack)
add_executable(test_cpp test.cpp)
target_link_libraries(test_cpp libheaptrack)
add_executable(threaded threaded.cpp)
target_link_libraries(threaded ${CMAKE_THREAD_LIBS_INIT} libheaptrack)
