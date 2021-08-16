# - Try to find libdw
# Once done this will define
#
#  LIBDW_FOUND - system has libdwarf
#  LIBDW_INCLUDE_DIRS - the libdwarf include directory
#  LIBDW_LIBRARIES - Link these to use libdwarf
#  LIBDW_DEFINITIONS - Compiler switches required for using libdwarf
#

if (LIBDW_LIBRARIES AND LIBDW_INCLUDE_DIRS)
  set (ElfUtils_FIND_QUIETLY TRUE)
endif (LIBDW_LIBRARIES AND LIBDW_INCLUDE_DIRS)

find_path (DWARF_INCLUDE_DIR
    NAMES
      dwarf.h
    PATHS
      /usr/include
      /usr/local/include
      /opt/local/include
      /sw/include
      ENV CPATH) # PATH and INCLUDE will also work
find_path (LIBDW_INCLUDE_DIR
    NAMES
      elfutils/libdw.h elfutils/libdwfl.h
    PATHS
      /usr/include
      /usr/local/include
      /opt/local/include
      /sw/include
      ENV CPATH)
if (DWARF_INCLUDE_DIR AND LIBDW_INCLUDE_DIR)
    set (LIBDW_INCLUDE_DIRS  ${DWARF_INCLUDE_DIR} ${LIBDW_INCLUDE_DIR})
endif (DWARF_INCLUDE_DIR AND LIBDW_INCLUDE_DIR)

find_library (LIBDW_LIBRARIES
    NAMES
      dw
    PATHS
      /usr/lib
      /usr/local/lib
      /opt/local/lib
      /sw/lib
      ENV LIBRARY_PATH   # PATH and LIB will also work
      ENV LD_LIBRARY_PATH)

include (FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set LIBDW_FOUND to TRUE
# if all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ElfUtils DEFAULT_MSG
    LIBDW_LIBRARIES
    LIBDW_INCLUDE_DIR)

mark_as_advanced(LIBDW_INCLUDE_DIR LIBDW_LIBRARIES)
