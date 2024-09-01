if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

find_package(Elfutils 0.158 REQUIRED)
find_package(Boost 1.41.0 REQUIRED COMPONENTS program_options)

include_directories(
    ${PROJECT_SOURCE_DIR}/3rdparty/
)

add_definitions("-DHAVE_STDINT_H")

add_executable(heaptrack_interpret
    heaptrack_interpret.cpp
    dwarfdiecache.cpp
    symbolcache.cpp
    demangler.cpp
)

target_link_libraries(heaptrack_interpret
    PRIVATE ${LIBDW_LIBRARIES} tsl::robin_map
    Boost::program_options

)

target_include_directories(heaptrack_interpret
    PRIVATE ${LIBDW_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS}
)

install(TARGETS heaptrack_interpret
    RUNTIME DESTINATION ${LIBEXEC_INSTALL_DIR}
)

set_target_properties(heaptrack_interpret PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${LIBEXEC_INSTALL_DIR}"
)

find_package(LibRustcDemangle)
set_package_properties(LibRustcDemangle PROPERTIES
        DESCRIPTION "Demangling for Rust symbols, written in Rust."
        PURPOSE "Demangling of Rust symbols"
        URL "https://github.com/alexcrichton/rustc-demangle"
        TYPE RUNTIME)

find_package(LibDDemangle)
set_package_properties(LibDDemangle PROPERTIES
        DESCRIPTION "Demangling for D symbols, written in D."
        PURPOSE "Demangling of D symbols"
        URL "https://github.com/lievenhey/d_demangler"
        TYPE RUNTIME)
