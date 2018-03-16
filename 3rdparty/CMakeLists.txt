include(ECMEnableSanitizers)

add_subdirectory(libbacktrace)

if (ZSTD_FOUND)
add_subdirectory(boost-zstd)
endif()
