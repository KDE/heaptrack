add_executable(heaptrack_print
    heaptrack_print.cpp
)

target_link_libraries(heaptrack_print LINK_PRIVATE
    sharedprint
    ${CMAKE_THREAD_LIBS_INIT}
)

install(TARGETS heaptrack_print
    RUNTIME DESTINATION ${BIN_INSTALL_DIR}
)

set_target_properties(heaptrack_print PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}"
)
