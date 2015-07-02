cmake_minimum_required(VERSION 2.8.9)
project(heaptrack)

set(HEAPTRACK_VERSION_MAJOR 0)
set(HEAPTRACK_VERSION_MINOR 1)
set(HEAPTRACK_VERSION_PATCH 0)
set(HEAPTRACK_LIB_VERSION 1.0.0)
set(HEAPTRACK_LIB_SOVERSION 1)

find_package(Boost 1.41.0 REQUIRED COMPONENTS iostreams program_options)
find_package(Threads REQUIRED)
find_package(Qt5 5.2.0 NO_MODULE OPTIONAL_COMPONENTS Widgets)
find_package(ECM 1.0.0 NO_MODULE)
find_package(ZLIB REQUIRED)
include(FeatureSummary)

if(Qt5_FOUND AND ECM_FOUND)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})
    find_package(KF5 OPTIONAL_COMPONENTS CoreAddons I18n ItemModels ThreadWeaver ConfigWidgets I18n)
    find_package(KChart "2.6.0" REQUIRED)
    if (KF5_FOUND AND KChart_FOUND)
        add_subdirectory(gui)
    endif()
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wpedantic")

add_subdirectory(3rdparty)

check_library_exists (${LIBUNWIND_LIBRARY} unw_backtrace "" HAVE_UNW_BACKTRACE)
if (NOT HAVE_UNW_BACKTRACE)
    message(FATAL_ERROR "Your libunwind version is apparently too old and does not have the unw_backtrace function.")
endif()

check_library_exists (${LIBUNWIND_LIBRARY} _ULx86_64_set_cache_size "" HAVE_UNW_SET_CACHE_SIZE)

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

add_subdirectory(tests)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    3rdparty/
    ${LIBUNWIND_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(HEAPTRACK_DEBUG_BUILD 1)
else()
    set(HEAPTRACK_DEBUG_BUILD 0)
endif()

configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_library(heaptrack_preload MODULE heaptrack_preload.cpp libheaptrack.cpp)
target_link_libraries(heaptrack_preload LINK_PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT} backtrace rt)
set_target_properties(heaptrack_preload PROPERTIES VERSION ${HEAPTRACK_LIB_VERSION} SOVERSION ${HEAPTRACK_LIB_SOVERSION})
add_library(heaptrack_inject MODULE heaptrack_inject.cpp libheaptrack.cpp)
target_link_libraries(heaptrack_inject LINK_PRIVATE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT} backtrace rt)
set_target_properties(heaptrack_inject PROPERTIES VERSION ${HEAPTRACK_LIB_VERSION} SOVERSION ${HEAPTRACK_LIB_SOVERSION})

add_executable(heaptrack_interpret heaptrack_interpret.cpp)
target_link_libraries(heaptrack_interpret backtrace)

add_library(sharedprint STATIC accumulatedtracedata.cpp)
target_link_libraries(sharedprint LINK_PUBLIC ${Boost_LIBRARIES} ${ZLIB_LIBRARIES})
add_executable(heaptrack_print heaptrack_print.cpp)
target_link_libraries(heaptrack_print LINK_PRIVATE sharedprint)

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

configure_file(heaptrack.sh.cmake ${CMAKE_CURRENT_BINARY_DIR}/heaptrack @ONLY)

install(TARGETS heaptrack_print RUNTIME DESTINATION ${BIN_INSTALL_DIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/heaptrack DESTINATION ${BIN_INSTALL_DIR})
install(TARGETS heaptrack_interpret RUNTIME DESTINATION ${LIBEXEC_INSTALL_DIR})
install(TARGETS heaptrack_inject LIBRARY DESTINATION ${LIB_INSTALL_DIR}/heaptrack/)
install(TARGETS heaptrack_preload LIBRARY DESTINATION ${LIB_INSTALL_DIR}/heaptrack/)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
