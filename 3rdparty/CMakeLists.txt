if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

# Mark the specified target's include directories as INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
function(mark_as_system_target target)
    # see also: https://gitlab.kitware.com/cmake/cmake/-/issues/21211
    get_property(
        include_dirs
        TARGET ${target}
        PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    )
    set_property(TARGET ${target} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${include_dirs}")
endfunction()

if (ZSTD_FOUND AND NOT BOOST_IOSTREAMS_HAS_ZSTD)
    add_subdirectory(boost-zstd)
    mark_as_system_target(boost-zstd)
endif()

add_subdirectory(robin-map)

mark_as_system_target(robin_map)
