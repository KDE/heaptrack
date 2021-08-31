if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

if (HEAPTRACK_BUILD_BACKTRACE)
    add_subdirectory(libbacktrace)
endif()

if (ZSTD_FOUND AND NOT BOOST_IOSTREAMS_HAS_ZSTD)
    add_subdirectory(boost-zstd)
endif()
