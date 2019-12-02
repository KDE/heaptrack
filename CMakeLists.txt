if (CMAKE_VERSION VERSION_LESS "2.8.12")
    cmake_minimum_required(VERSION 2.8.9)
    set(HEAPTRACK_BUILD_GUI OFF)
else()
    cmake_minimum_required(VERSION 2.8.12)
endif()

project(heaptrack)
enable_testing()

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
endif()

set(HEAPTRACK_VERSION_MAJOR 1)
set(HEAPTRACK_VERSION_MINOR 2)
set(HEAPTRACK_VERSION_PATCH 0)
set(HEAPTRACK_LIB_VERSION 1.2.0)
set(HEAPTRACK_LIB_SOVERSION 2)
set(HEAPTRACK_FILE_FORMAT_VERSION 2)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(FeatureSummary)
find_package(Boost 1.41.0 COMPONENTS system filesystem iostreams)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

if (${Boost_IOSTREAMS_FOUND})
    find_package(Zstd)
endif()
set_package_properties(Zstd PROPERTIES TYPE RECOMMENDED PURPOSE "Zstandard offers better (de)compression performance compared with gzip/zlib, making heaptrack faster and datafiles smaller.")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
  HEAPTRACK_BUILD_BACKTRACE
  "Enable this option to build libbacktrace. It will be enabled implicitly if HEAPTRACK_BUILD_INTERPRET is enabled."
  OFF
)

option(
  HEAPTRACK_BUILD_INTERPRET
  "Disable this option to skip building the interpret part for heaptrack."
  ${HEAPTRACK_BUILD_INTERPRET_DEFAULT}
)

if (HEAPTRACK_BUILD_INTERPRET)
    set(HEAPTRACK_BUILD_BACKTRACE ON)
endif()

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
  "Disable this option to skip building the Qt5 / KF5 based GUI for heaptrack."
  ${HEAPTRACK_BUILD_ANALYZE_DEFAULT}
)

option(
  HEAPTRACK_USE_LIBUNWIND
  "Define preferred unwind functionality - Libunwind as ON and unwind_tables as OFF."
  ON
)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if (NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wpedantic")
endif()

include (CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <unordered_map>
    #include <atomic>
    thread_local int tls;
    int main() { return 0; }"
    HAVE_CXX11_SUPPORT)

if (NOT HAVE_CXX11_SUPPORT)
    message(FATAL_ERROR "Your compiler is too old and does not support the required C++11 features.")
endif()


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

if (HEAPTRACK_BUILD_TRACK)
    if (HEAPTRACK_USE_LIBUNWIND)
        find_package(Libunwind REQUIRED)
    endif()

    check_cxx_source_compiles(
        "#include <stdio_ext.h>
        #include <fcntl.h>
        #include <dlfcn.h>
        #include <link.h>
        int main() { return 0; }"
        HAVE_LINUX_HEADERS)

    if (NOT HAVE_LINUX_HEADERS)
        message(FATAL_ERROR "You are missing some Linux headers required to compile heaptrack.")
    endif()
endif()

add_subdirectory(3rdparty)
add_subdirectory(src)
add_subdirectory(tests)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
