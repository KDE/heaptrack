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
