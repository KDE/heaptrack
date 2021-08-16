if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

find_package(Elfutils REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}/3rdparty/
)

add_definitions("-DHAVE_STDINT_H")

add_executable(heaptrack_interpret
    heaptrack_interpret.cpp
    dwarfdiecache.cpp
    symbolcache.cpp
)

target_link_libraries(heaptrack_interpret
    PRIVATE ${LIBDW_LIBRARIES}
)

target_include_directories(heaptrack_interpret
    PRIVATE ${LIBDW_INCLUDE_DIRS}
)

install(TARGETS heaptrack_interpret
    RUNTIME DESTINATION ${LIBEXEC_INSTALL_DIR}
)

set_target_properties(heaptrack_interpret PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${LIBEXEC_INSTALL_DIR}"
)
