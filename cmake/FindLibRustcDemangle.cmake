if (LIBRUSTC_DEMANGLE_LIBRARIES)
  set (LibRustcDemangle_FIND_QUIETLY TRUE)
endif()

find_library(LIBRUSTC_DEMANGLE_LIBRARIES
    NAMES
      rustc_demangle
    PATHS
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /sw/lib
      ENV LIBRARY_PATH
      ENV LD_LIBRARY_PATH)

include (FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set LIBRUSTC_DEMANGLE_FOUND to TRUE if all listed variables are TRUE
find_package_handle_standard_args(LibRustcDemangle DEFAULT_MSG
    LIBRUSTC_DEMANGLE_LIBRARIES)

mark_as_advanced(LIBRUSTC_DEMANGLE_LIBRARIES)
