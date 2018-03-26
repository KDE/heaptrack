if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

add_subdirectory(libbacktrace)

if (ZSTD_FOUND)
add_subdirectory(boost-zstd)
endif()
