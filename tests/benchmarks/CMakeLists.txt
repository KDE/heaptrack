include_directories(
    ../..
    ${Boost_INCLUDE_DIRS}
)

include (CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <malloc.h>
    int main() { return mallinfo().uordblks > 0; }"
    HAVE_MALLOC_H)

if (HAVE_MALLOC_H)
    add_executable(bench_pointermap bench_pointermap.cpp)
    add_executable(bench_pointerhash bench_pointerhash.cpp)

    add_executable(measure_malloc_overhead measure_malloc_overhead.cpp)
endif()

add_executable(bench_parser bench_parser.cpp)
target_link_libraries(bench_parser sharedprint heaptrack_gui_private)

add_executable(bench_linereader bench_linereader.cpp)

find_package(Qt5 5.10.0 CONFIG OPTIONAL_COMPONENTS Core)
if (Qt5Core_FOUND AND Boost_CONTAINER_FOUND)
    add_executable(bench_tree bench_tree.cpp)
    target_include_directories(bench_tree PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(bench_tree Qt5::Core
    ${Boost_CONTAINER_LIBRARY})
endif()
