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
set(HEAPTRACK_VERSION_MINOR 1)
set(HEAPTRACK_VERSION_PATCH 0)
set(HEAPTRACK_LIB_VERSION 1.0.0)
set(HEAPTRACK_LIB_SOVERSION 1)
set(HEAPTRACK_FILE_FORMAT_VERSION 2)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(Libunwind REQUIRED)
find_package(Boost 1.41.0 REQUIRED COMPONENTS iostreams program_options)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
include(FeatureSummary)

option(
  HEAPTRACK_BUILD_GUI
  "Disable this option to skip building the Qt5 / KF5 based GUI for heaptrack."
  On
)

if(HEAPTRACK_BUILD_GUI)
    find_package(Qt5 5.2.0 NO_MODULE OPTIONAL_COMPONENTS Widgets)
    find_package(ECM 1.0.0 NO_MODULE)
    if(Qt5_FOUND AND ECM_FOUND)
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})
            find_package(KF5 OPTIONAL_COMPONENTS CoreAddons I18n ItemModels ThreadWeaver ConfigWidgets KIO)
            find_package(KChart "2.6.0")
            set_package_properties(KChart PROPERTIES TYPE RECOMMENDED PURPOSE "Required for the heaptrack_gui executable. Get it from the kdiagram module.")
    endif()
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wpedantic")

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

add_subdirectory(3rdparty)
add_subdirectory(src)
add_subdirectory(tests)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
