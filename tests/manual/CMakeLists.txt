set(CMAKE_BUILD_TYPE Debug)

add_executable(test_c test.c)
add_executable(test_cpp test.cpp)
set_target_properties(test_cpp PROPERTIES
  INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR}/../../src/
)

add_executable(threaded threaded.cpp)
target_link_libraries(threaded ${CMAKE_THREAD_LIBS_INIT})

add_executable(callgraph callgraph.cpp)

add_library(testlib SHARED lib.cpp)
add_executable(test_lib test_lib.cpp)
target_link_libraries(test_lib testlib)

add_executable(test_aggregation test_aggregation.cpp)

add_executable(signals signals.cpp)
target_link_libraries(signals ${CMAKE_THREAD_LIBS_INIT})

add_executable(libc_leaks libc_leaks.c)

add_executable(peak peak.c)
set_target_properties(peak PROPERTIES
    COMPILE_FLAGS "-g3 -O0"
)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

add_executable(inlining inlining.cpp)

add_executable(embedded_lsan_suppressions embedded_lsan_suppressions.c)

add_library(testlib_indirect SHARED libindirect.cpp)
target_link_libraries(testlib_indirect ${CMAKE_DL_LIBS})

add_executable(dlopen dlopen.cpp)
target_compile_definitions(dlopen PRIVATE -DLIB_PATH="$<TARGET_FILE:testlib_indirect>")
target_link_libraries(dlopen ${CMAKE_DL_LIBS})
