
set(CMAKE_BUILD_TYPE Debug)
configure_file(tst_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/tst_config.h @ONLY)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_BINARY_DIR}/../../src/
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/track
)

if ("${Boost_FILESYSTEM_FOUND}" AND "${Boost_SYSTEM_FOUND}")
    if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        set(LIBUTIL_LIBRARY "util")
    endif()

    include_directories(
        ${Boost_INCLUDE_DIRS}
    )
endif()

# tst_inject isn't asan-safe, so include it before we enable sanitizers
add_subdirectory(no_asan)

if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

add_executable(tst_trace tst_trace.cpp ../../src/interpret/dwarfdiecache.cpp ../../src/interpret/demangler.cpp)
set_target_properties(tst_trace PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
target_link_libraries(tst_trace
    PRIVATE
        heaptrack_unwind
        Threads::Threads
        ${LIBDW_LIBRARIES}
        tsl::robin_map
)
target_include_directories(tst_trace PRIVATE ${LIBDW_INCLUDE_DIRS} )

add_test(NAME tst_trace COMMAND tst_trace)

configure_file(tst_heaptrack_interpret.cmake.sh ${CMAKE_CURRENT_BINARY_DIR}/tst_heaptrack_interpret.sh @ONLY)
add_test(NAME tst_heaptrack_interpret COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tst_heaptrack_interpret.sh)

if ("${Boost_FILESYSTEM_FOUND}" AND "${Boost_SYSTEM_FOUND}")
    add_executable(tst_libheaptrack
        tst_libheaptrack.cpp
        ../../src/track/libheaptrack.cpp)
    set_target_properties(tst_libheaptrack PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(tst_libheaptrack
        LINK_PRIVATE
            ${CMAKE_DL_LIBS}
            ${CMAKE_THREAD_LIBS_INIT}
            ${LIBUTIL_LIBRARY}
            heaptrack_unwind
            rt
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_libheaptrack COMMAND tst_libheaptrack)

    add_executable(tst_io tst_io.cpp)
    set_target_properties(tst_io PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(tst_io
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_io COMMAND tst_io)

    if (TARGET heaptrack_gui_private)
        find_package(Qt${QT_VERSION_MAJOR} ${QT_MIN_VERSION} CONFIG OPTIONAL_COMPONENTS Test)
        if (Qt${QT_VERSION_MAJOR}Test_FOUND)
            set(CMAKE_AUTOMOC ON)
            add_executable(test_template_elision tst_template_elision.cpp)
            target_link_libraries(test_template_elision
                    heaptrack_gui_private
                    Qt${QT_VERSION_MAJOR}::Test
            )
            add_test(NAME test_template_elision COMMAND test_template_elision)

            add_executable(tst_parser tst_parser.cpp)
            set_target_properties(tst_parser PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
            target_link_libraries(tst_parser
                    ${Boost_SYSTEM_LIBRARY}
                    ${Boost_FILESYSTEM_LIBRARY}
                    heaptrack_gui_private
                    Qt${QT_VERSION_MAJOR}::Test
            )
            add_test(NAME tst_parser COMMAND tst_parser)
        endif()
    endif()
endif()
