if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

if(HEAPTRACK_BUILD_TRACK)
    add_subdirectory(libbacktrace)
endif()

if (ZSTD_FOUND)
    add_subdirectory(boost-zstd)
endif()
