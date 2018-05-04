#.rst:
# FindLibUnwind
# -----------
#
# Find LibUnwind
#
# Find LibUnwind headers and library
#
# ::
#
#   LIBUNWIND_FOUND                     - True if libunwind is found.
#   LIBUNWIND_INCLUDE_DIRS              - Directory where libunwind headers are located.
#   LIBUNWIND_LIBRARIES                 - Unwind libraries to link against.
#   LIBUNWIND_HAS_UNW_GETCONTEXT        - True if unw_getcontext() is found (optional).
#   LIBUNWIND_HAS_UNW_INIT_LOCAL        - True if unw_init_local() is found (optional).
#   LIBUNWIND_HAS_UNW_BACKTRACE         - True if unw_backtrace() is found (required).
#   LIBUNWIND_HAS_UNW_BACKTRACE_SKIP    - True if unw_backtrace_skip() is found (optional).
#   LIBUNWIND_VERSION_STRING            - version number as a string (ex: "5.0.3")

#=============================================================================
# Copyright 2014 ZBackup contributors
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)


find_path(LIBUNWIND_INCLUDE_DIR libunwind.h )
if(NOT EXISTS "${LIBUNWIND_INCLUDE_DIR}/unwind.h")
  MESSAGE("Found libunwind.h but corresponding unwind.h is absent!")
  SET(LIBUNWIND_INCLUDE_DIR "")
endif()

find_library(LIBUNWIND_LIBRARY unwind)

if(LIBUNWIND_INCLUDE_DIR AND EXISTS "${LIBUNWIND_INCLUDE_DIR}/libunwind-common.h")
  file(STRINGS "${LIBUNWIND_INCLUDE_DIR}/libunwind-common.h" LIBUNWIND_HEADER_CONTENTS REGEX "#define UNW_VERSION_[A-Z]+\t[0-9]*")

  string(REGEX REPLACE ".*#define UNW_VERSION_MAJOR\t([0-9]*).*" "\\1" LIBUNWIND_VERSION_MAJOR "${LIBUNWIND_HEADER_CONTENTS}")
  string(REGEX REPLACE ".*#define UNW_VERSION_MINOR\t([0-9]*).*" "\\1" LIBUNWIND_VERSION_MINOR "${LIBUNWIND_HEADER_CONTENTS}")
  string(REGEX REPLACE ".*#define UNW_VERSION_EXTRA\t([0-9]*).*" "\\1" LIBUNWIND_VERSION_EXTRA "${LIBUNWIND_HEADER_CONTENTS}")

  if(LIBUNWIND_VERSION_EXTRA)
    set(LIBUNWIND_VERSION_STRING "${LIBUNWIND_VERSION_MAJOR}.${LIBUNWIND_VERSION_MINOR}.${LIBUNWIND_VERSION_EXTRA}")
  else(not LIBUNWIND_VERSION_EXTRA)
    set(LIBUNWIND_VERSION_STRING "${LIBUNWIND_VERSION_MAJOR}.${LIBUNWIND_VERSION_MINOR}")
  endif()
  unset(LIBUNWIND_HEADER_CONTENTS)
endif()

if (LIBUNWIND_LIBRARY)
  include(CheckCSourceCompiles)
  set(CMAKE_REQUIRED_QUIET_SAVE ${CMAKE_REQUIRED_QUIET})
  set(CMAKE_REQUIRED_QUIET ${LibUnwind_FIND_QUIETLY})
  set(CMAKE_REQUIRED_LIBRARIES_SAVE ${CMAKE_REQUIRED_LIBRARIES})
  set(CMAKE_REQUIRED_LIBRARIES ${LIBUNWIND_LIBRARY})
  set(CMAKE_REQUIRED_INCLUDES_SAVE ${CMAKE_REQUIRED_INCLUDES})
  set(CMAKE_REQUIRED_INCLUDES ${LIBUNWIND_INCLUDE_DIR})
  check_c_source_compiles("#define UNW_LOCAL_ONLY 1\n#include <libunwind.h>\nint main() { unw_context_t context; unw_getcontext(&context); return 0; }"
                          LIBUNWIND_HAS_UNW_GETCONTEXT)
  check_c_source_compiles("#define UNW_LOCAL_ONLY 1\n#include <libunwind.h>\nint main() { unw_context_t context; unw_cursor_t cursor; unw_getcontext(&context); unw_init_local(&cursor, &context); return 0; }"
                          LIBUNWIND_HAS_UNW_INIT_LOCAL)
  check_c_source_compiles("#define UNW_LOCAL_ONLY 1\n#include <libunwind.h>\nint main() { void* buf[10]; unw_backtrace(&buf, 10); return 0; }" LIBUNWIND_HAS_UNW_BACKTRACE)
  check_c_source_compiles ("#define UNW_LOCAL_ONLY 1\n#include <libunwind.h>\nint main() { void* buf[10]; unw_backtrace_skip(&buf, 10, 2); return 0; }" LIBUNWIND_HAS_UNW_BACKTRACE_SKIP)
  check_c_source_compiles ("#define UNW_LOCAL_ONLY 1\n#include <libunwind.h>\nint main() { return unw_set_cache_size(unw_local_addr_space, 1024, 0); }" LIBUNWIND_HAS_UNW_SET_CACHE_SIZE)
  set(CMAKE_REQUIRED_QUIET ${CMAKE_REQUIRED_QUIET_SAVE})
  set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES_SAVE})
  set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES_SAVE})
endif ()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibUnwind  REQUIRED_VARS  LIBUNWIND_INCLUDE_DIR
                                                            LIBUNWIND_LIBRARY
                                                            LIBUNWIND_HAS_UNW_BACKTRACE
                                             VERSION_VAR    LIBUNWIND_VERSION_STRING
                                 )

if (LIBUNWIND_FOUND)
  set(LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARY})
  set(LIBUNWIND_INCLUDE_DIRS ${LIBUNWIND_INCLUDE_DIR})
endif ()

mark_as_advanced( LIBUNWIND_INCLUDE_DIR LIBUNWIND_LIBRARY )
