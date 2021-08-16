if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

if (ZSTD_FOUND AND NOT BOOST_IOSTREAMS_HAS_ZSTD)
    add_subdirectory(boost-zstd)
endif()
