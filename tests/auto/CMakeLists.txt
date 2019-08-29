if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

set(CMAKE_BUILD_TYPE Debug)
configure_file(tst_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/tst_config.h @ONLY)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
add_definitions(-DCATCH_CONFIG_MAIN)

add_executable(tst_trace tst_trace.cpp)
target_link_libraries(tst_trace heaptrack_unwind)
add_test(NAME tst_trace COMMAND tst_trace)

if ("${Boost_FILESYSTEM_FOUND}" AND "${Boost_SYSTEM_FOUND}")
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
            heaptrack_unwind
            rt
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_libheaptrack COMMAND tst_libheaptrack)

    add_executable(tst_io tst_io.cpp)
    target_link_libraries(tst_io
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_io COMMAND tst_io)


    find_package(Qt5 5.2.0 CONFIG OPTIONAL_COMPONENTS Test)
    if (Qt5Test_FOUND)
        add_executable(tst_parser tst_parser.cpp)
        target_link_libraries(tst_parser
                ${Boost_SYSTEM_LIBRARY}
                ${Boost_FILESYSTEM_LIBRARY}
                heaptrack_gui_private
                Qt5::Test
        )
        add_test(NAME tst_parser COMMAND tst_parser)
    endif()

    add_executable(tst_inject tst_inject.cpp)
    target_link_libraries(tst_inject
            ${CMAKE_DL_LIBS}
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_inject COMMAND tst_inject)

endif()
