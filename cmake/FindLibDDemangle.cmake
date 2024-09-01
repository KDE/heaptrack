if (LIBD_DEMANGLE_LIBRARIES)
    set (LibDDemangle_FIND_QUIETLY TRUE)
endif()

find_library(LIBD_DEMANGLE_LIBRARIES
    NAMES
      d_demangle
    PATHS
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /sw/lib
      ENV LIBRARY_PATH
      ENV LD_LIBRARY_PATH)

include (FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set LIBRUSTC_DEMANGLE_FOUND to TRUE if all listed variables are TRUE
find_package_handle_standard_args(LibDDemangle DEFAULT_MSG
    LIBD_DEMANGLE_LIBRARIES)

mark_as_advanced(LIBD_DEMANGLE_LIBRARIES)
