if ("${Boost_FILESYSTEM_FOUND}" AND "${Boost_SYSTEM_FOUND}")
    add_executable(tst_inject tst_inject.cpp)
    target_compile_definitions(tst_inject PRIVATE -DCATCH_CONFIG_MAIN)
    target_link_libraries(tst_inject
            ${CMAKE_DL_LIBS}
            ${Boost_SYSTEM_LIBRARY}
            ${Boost_FILESYSTEM_LIBRARY}
    )
    add_test(NAME tst_inject COMMAND tst_inject)
endif()
