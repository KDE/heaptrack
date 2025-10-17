cmake_minimum_required(VERSION 3.16.0)

project(heaptrack)
enable_testing()

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
endif()

set(HEAPTRACK_VERSION_MAJOR 1)
set(HEAPTRACK_VERSION_MINOR 6)
set(HEAPTRACK_VERSION_PATCH 80)
set(HEAPTRACK_LIB_VERSION 1.6.80)
set(HEAPTRACK_LIB_SOVERSION 2)
set(HEAPTRACK_FILE_FORMAT_VERSION 3)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(APPIMAGE_BUILD "configure build for bundling in an appimage" OFF)

set(REQUIRED_IN_APPIMAGE "")
if (APPIMAGE_BUILD)
    set(REQUIRED_IN_APPIMAGE REQUIRED)
endif()

include(FeatureSummary)
find_package(Boost 1.60.0 ${REQUIRED_IN_APPIMAGE} COMPONENTS filesystem iostreams container)
# boost_system no longer exists in 1.89
if (Boost_VERSION VERSION_LESS 1.89.0)
    find_package(Boost ${REQUIRED_IN_APPIMAGE} COMPONENTS system)
else()
    set(Boost_SYSTEM_FOUND TRUE)
endif()
set_package_properties(Boost PROPERTIES TYPE RECOMMENDED PURPOSE "Boost container libraries can greatly improve performance (via pmr allocators)")
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

if (${Boost_IOSTREAMS_FOUND})
    find_package(ZSTD ${REQUIRED_IN_APPIMAGE})

    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_INCLUDES ${Boost_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LIBRARIES ${Boost_LIBRARIES})
    check_cxx_source_compiles("#include <boost/iostreams/filter/zstd.hpp>
        int main() { boost::iostreams::zstd_decompressor(); return 0; }"
        BOOST_IOSTREAMS_HAS_ZSTD
    )
    cmake_pop_check_state()
endif()
set_package_properties(ZSTD PROPERTIES TYPE RECOMMENDED PURPOSE "Zstandard offers better (de)compression performance compared with gzip/zlib, making heaptrack faster and datafiles smaller.")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(HEAPTRACK_BUILD_TRACK_DEFAULT ON)
    set(HEAPTRACK_BUILD_INTERPRET_DEFAULT ON)
else()
    set(HEAPTRACK_BUILD_TRACK_DEFAULT OFF)
    set(HEAPTRACK_BUILD_INTERPRET_DEFAULT OFF)
endif()

option(
  HEAPTRACK_BUILD_TRACK
  "Disable this option to skip building the tracker part for heaptrack, e.g. to only build the GUI."
  ${HEAPTRACK_BUILD_TRACK_DEFAULT}
)

option(
  HEAPTRACK_BUILD_INTERPRET
  "Disable this option to skip building the interpret part for heaptrack."
  ${HEAPTRACK_BUILD_INTERPRET_DEFAULT}
)

if (CMAKE_CROSSCOMPILING)
    set(HEAPTRACK_BUILD_ANALYZE_DEFAULT OFF)
else()
    set(HEAPTRACK_BUILD_ANALYZE_DEFAULT ON)
endif()

option(
  HEAPTRACK_BUILD_PRINT
  "Disable this option to skip building heaptrack_print, e.g. when you're cross-compiling."
  ${HEAPTRACK_BUILD_ANALYZE_DEFAULT}
)

option(
  HEAPTRACK_BUILD_GUI
  "Disable this option to skip building the Qt / KDE Frameworks based GUI for heaptrack."
  ${HEAPTRACK_BUILD_ANALYZE_DEFAULT}
)

# possibly works with older Qt6, but I didn't test that yet
set(QT_MIN_VERSION 6.5.0)

option(
  HEAPTRACK_USE_LIBUNWIND
  "Define preferred unwind functionality - Libunwind as ON and unwind_tables as OFF."
  ON
)

option(
  HEAPTRACK_USE_SYSTEM_ROBINMAP
  "Use system robin-map instead of bundled"
  OFF
)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if (NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror=undef -Werror=return-type")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

include (CheckCXXSourceCompiles)

# cfree() does not exist in glibc 2.26+.
# See: https://bugs.kde.org/show_bug.cgi?id=383889
include(CheckSymbolExists)
check_symbol_exists(cfree malloc.h HAVE_CFREE)
check_symbol_exists(valloc stdlib.h HAVE_VALLOC)

set(BIN_INSTALL_DIR "bin")
set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)")
set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}")
set(LIBEXEC_INSTALL_DIR "${LIB_INSTALL_DIR}/heaptrack/libexec")

file(RELATIVE_PATH LIBEXEC_REL_PATH
    "${CMAKE_INSTALL_PREFIX}/${BIN_INSTALL_DIR}"
    "${CMAKE_INSTALL_PREFIX}/${LIBEXEC_INSTALL_DIR}")

file(RELATIVE_PATH LIB_REL_PATH
    "${CMAKE_INSTALL_PREFIX}/${BIN_INSTALL_DIR}"
    "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}/heaptrack")

set(ECM_ENABLE_SANITIZERS "" CACHE STRING "semicolon-separated list of sanitizers to enable for code that is not injected into client applications")

if (ECM_ENABLE_SANITIZERS)
    find_package(ECM 1.0.0 NO_MODULE)
    if (ECM_FOUND)
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH})
    endif()
endif()

if (HEAPTRACK_BUILD_TRACK)
    if (HEAPTRACK_USE_LIBUNWIND)
        find_package(Libunwind REQUIRED)
    endif()

    check_cxx_source_compiles(
        "#ifdef __linux__
        #include <stdio_ext.h>
        #endif
        #include <fcntl.h>
        #include <dlfcn.h>
        #include <link.h>
        int main() { return 0; }"
        HAVE_LINUX_HEADERS)

    if (NOT HAVE_LINUX_HEADERS)
        message(FATAL_ERROR "You are missing some Linux/BSD headers required to compile heaptrack.")
    endif()
endif()

if (HEAPTRACK_USE_SYSTEM_ROBINMAP)
    find_package(tsl-robin-map CONFIG REQUIRED)
endif()

add_subdirectory(3rdparty)
add_subdirectory(src)
add_subdirectory(tests)

# Let releaseme know about this:
# SKIP_PO_INSTALL
# (KF5I18n is optional in src/analyze/CMakeLists.txt...)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
