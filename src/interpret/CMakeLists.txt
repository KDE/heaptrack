if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/3rdparty/
)

add_executable(heaptrack_interpret
    heaptrack_interpret.cpp
)

target_link_libraries(heaptrack_interpret
    backtrace
)

install(TARGETS heaptrack_interpret
    RUNTIME DESTINATION ${LIBEXEC_INSTALL_DIR}
)

set_target_properties(heaptrack_interpret PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${LIBEXEC_INSTALL_DIR}"
)
