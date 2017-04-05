set(CMAKE_BUILD_TYPE Debug)

add_executable(test_c test.c)
add_executable(test_cpp test.cpp)
add_executable(threaded threaded.cpp)
target_link_libraries(threaded ${CMAKE_THREAD_LIBS_INIT})

add_executable(callgraph callgraph.cpp)

add_library(testlib SHARED lib.cpp)
add_executable(test_lib test_lib.cpp)
target_link_libraries(test_lib testlib)

add_executable(test_aggregation test_aggregation.cpp)

add_executable(phdr phdr.cpp)
target_link_libraries(phdr ${CMAKE_DL_LIBS})
ADD_CUSTOM_COMMAND(TARGET phdr POST_BUILD
    COMMAND objcopy --only-keep-debug $<TARGET_FILE:phdr> $<TARGET_FILE:phdr>.debug
    COMMAND objcopy -g --add-gnu-debuglink=phdr.debug $<TARGET_FILE:phdr>)
