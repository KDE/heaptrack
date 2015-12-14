include_directories(../..)

include (CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <malloc.h>
    int main() { return mallinfo().uordblks > 0; }"
    HAVE_MALLOC_H)

if (HAVE_MALLOC_H)
    add_executable(bench_pointermap bench_pointermap.cpp)
    add_executable(bench_pointerhash bench_pointerhash.cpp)

    find_package(SparseHash)
    if(SPARSEHASH_FOUND)
        include_directories(${SPARSEHASH_INCLUDE_DIRS})
        add_executable(bench_pointersparsehash bench_pointersparsehash.cpp)
    endif()
endif()
