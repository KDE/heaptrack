# FindxxHash.cmake
#
# Finds the xxHash library.
#
# Imported target:
#   xxHash::xxhash
#
# Result variables:
#   xxHash_FOUND
#   xxHash_VERSION
#   xxHash_INCLUDE_DIRS
#   xxHash_LIBRARIES
#
# Usage:
#   find_package(xxHash 0.8 REQUIRED)
#   target_link_libraries(my_target PRIVATE xxHash::xxhash)
#
# The module first tries pkg-config (libxxhash), then falls back to
# searching for xxhash.h and libxxhash directly.

include(FindPackageHandleStandardArgs)

# ---------------------------------------------------------------------------
# pkg-config
# ---------------------------------------------------------------------------

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(PC_xxHash QUIET IMPORTED_TARGET libxxhash)
endif()

# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------

find_path(xxHash_INCLUDE_DIR
    NAMES xxhash.h
    HINTS
        ${PC_xxHash_INCLUDE_DIRS}
        ${PC_xxHash_INCLUDEDIR}
        ${PC_xxHash_INCLUDE_DIR}
)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

find_library(xxHash_LIBRARY
    NAMES xxhash libxxhash
    HINTS
        ${PC_xxHash_LIBRARY_DIRS}
        ${PC_xxHash_LIBDIR}
        ${PC_xxHash_LIBRARY_DIR}
)

# ---------------------------------------------------------------------------
# Version
#
# Prefer the version reported by pkg-config, but always inspect xxhash.h
# when available. This ensures the headers and library being consumed are
# at least associated with a known xxHash version.
# ---------------------------------------------------------------------------

set(xxHash_VERSION)

if(PC_xxHash_VERSION)
    set(xxHash_VERSION "${PC_xxHash_VERSION}")
endif()

if(xxHash_INCLUDE_DIR AND EXISTS "${xxHash_INCLUDE_DIR}/xxhash.h")
    file(STRINGS
        "${xxHash_INCLUDE_DIR}/xxhash.h"
        _xxHash_version_lines
        REGEX "^#define[ \t]+XXH_VERSION_(MAJOR|MINOR|RELEASE)[ \t]+[0-9]+"
    )

    foreach(_line IN LISTS _xxHash_version_lines)
        if(_line MATCHES
            "^#define[ \t]+XXH_VERSION_MAJOR[ \t]+([0-9]+)")
            set(_xxHash_version_major "${CMAKE_MATCH_1}")

        elseif(_line MATCHES
            "^#define[ \t]+XXH_VERSION_MINOR[ \t]+([0-9]+)")
            set(_xxHash_version_minor "${CMAKE_MATCH_1}")

        elseif(_line MATCHES
            "^#define[ \t]+XXH_VERSION_RELEASE[ \t]+([0-9]+)")
            set(_xxHash_version_release "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    if(DEFINED _xxHash_version_major
       AND DEFINED _xxHash_version_minor
       AND DEFINED _xxHash_version_release)

        set(xxHash_VERSION
            "${_xxHash_version_major}.${_xxHash_version_minor}.${_xxHash_version_release}"
        )
    endif()

    unset(_xxHash_version_lines)
    unset(_xxHash_version_major)
    unset(_xxHash_version_minor)
    unset(_xxHash_version_release)
endif()

# ---------------------------------------------------------------------------
# Validate package
# ---------------------------------------------------------------------------

find_package_handle_standard_args(xxHash
    REQUIRED_VARS
        xxHash_INCLUDE_DIR
        xxHash_LIBRARY
    VERSION_VAR
        xxHash_VERSION
)

# ---------------------------------------------------------------------------
# Imported target
# ---------------------------------------------------------------------------

if(xxHash_FOUND)

    set(xxHash_INCLUDE_DIRS
        "${xxHash_INCLUDE_DIR}"
    )

    set(xxHash_LIBRARIES
        "${xxHash_LIBRARY}"
    )

    if(NOT TARGET xxHash::xxhash)
        add_library(xxHash::xxhash UNKNOWN IMPORTED)

        set_target_properties(xxHash::xxhash PROPERTIES
            IMPORTED_LOCATION
                "${xxHash_LIBRARY}"

            INTERFACE_INCLUDE_DIRECTORIES
                "${xxHash_INCLUDE_DIR}"
        )

        # Preserve additional linker flags discovered by pkg-config.
        if(TARGET PkgConfig::PC_xxHash)
            get_target_property(
                _xxHash_pc_link_libs
                PkgConfig::PC_xxHash
                INTERFACE_LINK_LIBRARIES
            )

            if(_xxHash_pc_link_libs)
                set_property(
                    TARGET xxHash::xxhash
                    APPEND PROPERTY
                    INTERFACE_LINK_LIBRARIES
                    "${_xxHash_pc_link_libs}"
                )
            endif()
        endif()
    endif()

endif()

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------

mark_as_advanced(
    xxHash_INCLUDE_DIR
    xxHash_LIBRARY
)
