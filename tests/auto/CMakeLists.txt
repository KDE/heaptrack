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
add_test(NAME tst_trace COMMAND tst_trace )
