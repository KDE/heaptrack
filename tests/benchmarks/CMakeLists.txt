include_directories(
    ../..
    ${Boost_INCLUDE_DIRS}
)

include (CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <malloc.h>
    int main() { return mallinfo2().uordblks > 0; }"
    HAVE_MALLOC_H)

if (HAVE_MALLOC_H)
    add_executable(bench_pointermap bench_pointermap.cpp)
    set_target_properties(bench_pointermap PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(bench_pointermap PRIVATE tsl::robin_map)

    add_executable(bench_pointerhash bench_pointerhash.cpp)
    set_target_properties(bench_pointerhash PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(bench_pointerhash PRIVATE tsl::robin_map)

    add_executable(measure_malloc_overhead measure_malloc_overhead.cpp)
    set_target_properties(measure_malloc_overhead PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
endif()

add_executable(bench_linereader bench_linereader.cpp)
set_target_properties(bench_linereader PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")

if (TARGET heaptrack_gui_private)
    add_executable(bench_parser bench_parser.cpp)
    set_target_properties(bench_parser PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(bench_parser sharedprint heaptrack_gui_private)
endif()

if (TARGET Qt${QT_VERSION_MAJOR}::Core AND Boost_CONTAINER_FOUND)
    add_executable(bench_tree bench_tree.cpp)
    set_target_properties(bench_tree PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_include_directories(bench_tree PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(bench_tree Qt${QT_VERSION_MAJOR}::Core
    ${Boost_CONTAINER_LIBRARY})
endif()
