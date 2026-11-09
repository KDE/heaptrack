if (Boost_FILESYSTEM_FOUND)
    add_executable(tst_inject tst_inject.cpp)
    set_target_properties(tst_inject PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(tst_inject
            ${CMAKE_DL_LIBS}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_inject COMMAND tst_inject)

    add_executable(tst_follow_children tst_follow_children.cpp)
    set_target_properties(tst_follow_children PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}")
    target_link_libraries(tst_follow_children
            ${CMAKE_DL_LIBS}
            ${Boost_FILESYSTEM_LIBRARY}
            Threads::Threads
    )
    add_test(NAME tst_follow_children COMMAND tst_follow_children)
endif()
