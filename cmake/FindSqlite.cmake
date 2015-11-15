# - Try to find Sqlite
# Once done this will define
#
#  SQLITE_FOUND - system has Sqlite
#  SQLITE_INCLUDE_DIR - the Sqlite include directory
#  SQLITE_LIBRARIES - Link these to use Sqlite
#  SQLITE_MIN_VERSION - The minimum SQLite version
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#
# Copyright (c) 2008, Gilles Caulier, <caulier.gilles@gmail.com>
# Copyright (c) 2010, Christophe Giboudeaux, <cgiboudeaux@gmail.com>
# Copyright (c) 2014, Daniel Vrátil <dvratil@redhat.com>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if(NOT SQLITE_MIN_VERSION)
  set(SQLITE_MIN_VERSION "3.6.16")
endif(NOT SQLITE_MIN_VERSION)

if ( SQLITE_INCLUDE_DIR AND SQLITE_LIBRARIES )
   # in cache already
   SET(Sqlite_FIND_QUIETLY TRUE)
endif ( SQLITE_INCLUDE_DIR AND SQLITE_LIBRARIES )

# use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_LIBRARY() calls
if( NOT WIN32 )
  find_package(PkgConfig)

  pkg_check_modules(PC_SQLITE sqlite3)

  set(SQLITE_DEFINITIONS ${PC_SQLITE_CFLAGS_OTHER})
endif( NOT WIN32 )

if(PC_SQLITE_FOUND)
  find_path(SQLITE_INCLUDE_DIR
            NAMES sqlite3.h
            PATHS ${PC_SQLITE_INCLUDEDIR}
            NO_DEFAULT_PATH
           )

  find_library(SQLITE_LIBRARIES
               NAMES sqlite3
               PATHS ${PC_SQLITE_LIBDIR}
               NO_DEFAULT_PATH
              )
else(PC_SQLITE_FOUND)
  find_path(SQLITE_INCLUDE_DIR
            NAMES sqlite3.h
           )

  find_library(SQLITE_LIBRARIES
               NAMES sqlite3
              )
endif(PC_SQLITE_FOUND)

if( UNIX )
  find_file(SQLITE_STATIC_LIBRARIES
            libsqlite3.a
            ${PC_SQLITE_LIBDIR}
           )
else( UNIX )
  # todo find static libs for other systems
  # fallback to standard libs
  set( SQLITE_STATIC_LIBRARIES ${SQLITE_LIBRARIES} )
endif( UNIX )

if(EXISTS ${SQLITE_INCLUDE_DIR}/sqlite3.h)
  file(READ ${SQLITE_INCLUDE_DIR}/sqlite3.h SQLITE3_H_CONTENT)
  string(REGEX MATCH "SQLITE_VERSION[ ]*\"[0-9.]*\"\n" SQLITE_VERSION_MATCH "${SQLITE3_H_CONTENT}")

  if(SQLITE_VERSION_MATCH)
    string(REGEX REPLACE ".*SQLITE_VERSION[ ]*\"(.*)\"\n" "\\1" SQLITE_VERSION ${SQLITE_VERSION_MATCH})

    if(SQLITE_VERSION VERSION_LESS "${SQLITE_MIN_VERSION}")
        message(STATUS "Sqlite ${SQLITE_VERSION} was found, but at least version ${SQLITE_MIN_VERSION} is required")
        set(SQLITE_VERSION_OK FALSE)
    else(SQLITE_VERSION VERSION_LESS "${SQLITE_MIN_VERSION}")
        set(SQLITE_VERSION_OK TRUE)
    endif(SQLITE_VERSION VERSION_LESS "${SQLITE_MIN_VERSION}")

  endif(SQLITE_VERSION_MATCH)

  if (SQLITE_VERSION_OK)
    file(WRITE ${CMAKE_BINARY_DIR}/sqlite_check_unlock_notify.cpp
         "#include <sqlite3.h>
          int main(int argc, char **argv) {
            return sqlite3_unlock_notify(0, 0, 0);
          }")
    try_compile(SQLITE_HAS_UNLOCK_NOTIFY
                ${CMAKE_BINARY_DIR}/sqlite_check_unlock_notify
                ${CMAKE_BINARY_DIR}/sqlite_check_unlock_notify.cpp
                LINK_LIBRARIES ${SQLITE_LIBRARIES}
                CMAKE_FLAGS "-DINCLUDE_DIRECTORIES:PATH=${SQLITE_INCLUDE_DIR}")
    if (NOT SQLITE_HAS_UNLOCK_NOTIFY)
        message(STATUS "Sqlite ${SQLITE_VERSION} was found, but it is not compiled with -DSQLITE_ENABLE_UNLOCK_NOTIFY")
    endif()
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args( Sqlite DEFAULT_MSG
                                   SQLITE_INCLUDE_DIR
                                   SQLITE_LIBRARIES
                                   SQLITE_VERSION_OK
                                   SQLITE_HAS_UNLOCK_NOTIFY)

# show the SQLITE_INCLUDE_DIR and SQLITE_LIBRARIES variables only in the advanced view
mark_as_advanced( SQLITE_INCLUDE_DIR SQLITE_LIBRARIES )

