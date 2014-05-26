set(CMAKE_BUILD_TYPE Debug)

add_executable(test_c test.c)
add_executable(test_cpp test.cpp)
add_executable(threaded threaded.cpp)
target_link_libraries(threaded -lpthread)
