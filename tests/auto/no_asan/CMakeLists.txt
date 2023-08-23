if ("${Boost_FILESYSTEM_FOUND}" AND "${Boost_SYSTEM_FOUND}")
    add_executable(tst_inject tst_inject.cpp)
    set_target_properties(tst_inject PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(tst_inject
            ${CMAKE_DL_LIBS}
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_inject COMMAND tst_inject)
endif()
