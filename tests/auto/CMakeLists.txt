if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

set(CMAKE_BUILD_TYPE Debug)

include_directories(
    ${LIBUNWIND_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)
add_definitions(-DCATCH_CONFIG_MAIN)

add_executable(tst_trace tst_trace.cpp)
target_link_libraries(tst_trace ${LIBUNWIND_LIBRARY})
add_test(NAME tst_trace COMMAND tst_trace)

find_package(Boost 1.41.0 COMPONENTS system filesystem)
if (${Boost_FILESYSTEM_FOUND} AND ${Boost_SYSTEM_FOUND})
    include_directories(
        ${Boost_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}/../../src/
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/track
    )
    add_executable(tst_libheaptrack
        tst_libheaptrack.cpp
        ../../src/track/libheaptrack.cpp)
    target_link_libraries(tst_libheaptrack
        LINK_PRIVATE
            ${CMAKE_DL_LIBS}
            ${CMAKE_THREAD_LIBS_INIT}
            ${LIBUNWIND_LIBRARY}
            rt
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_libheaptrack COMMAND tst_libheaptrack)
endif()
