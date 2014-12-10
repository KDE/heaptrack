# based on: https://github.com/ValveSoftware/vogl/blob/master/src/libbacktrace/CMakeLists.txt
#
#    CMakeLists.txt -- libbacktrace CMake build script
#    Contributed by Alexander Monakov, ISP RAS
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     (1) Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#
#     (2) Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#
#     (3) The name of the author may not be used to
#     endorse or promote products derived from this software without
#     specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.  */

cmake_minimum_required (VERSION 2.8)

project (libbacktrace)

set (BACKTRACE_SUPPORTED 1)

include (CheckSymbolExists)
if (true)
    # When unwind.h is in /usr/local/include, check_symbol_exists() is failing. But
    #  these functions are in the Linux Standard Base Core Spec and should just exist
    #  for Linux, yes?  I'm assuming that's the case for now and using them.
    #  (Otherwise stack tracing won't work anyway).
    set (HAVE_BACKTRACE 1)
    set (HAVE_GETIPINFO 1)
else()
    check_symbol_exists (_Unwind_Backtrace unwind.h HAVE_BACKTRACE)
    check_symbol_exists (_Unwind_GetIPInfo unwind.h HAVE_GETIPINFO)
endif()

if (HAVE_BACKTRACE)
    set (BACKTRACE_FILE backtrace.c simple.c)
else ()
    message("WARNING: No unwind.h, setting BACKTRACE_SUPPORTED=0")
    set (BACKTRACE_FILE nounwind.c)
    set (BACKTRACE_SUPPORTED 0)
endif ()

include (CheckCCompilerFlag)
check_c_compiler_flag ("-funwind-tables" FLAG_UNWIND_TABLES)
if (FLAG_UNWIND_TABLES)
    add_definitions ("-funwind-tables")
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions("-D_DEBUG -DDEBUG")
endif()

# Adjust warnings
if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions ("-Wno-switch -Wno-enum-compare")
endif ()

# Add these compiler options to match how voglcore and voglcommon are compiled, and to get libbacktrace compiling with gcc.
# -fno-strict-aliasing is particularly important if voglcore is called, and -fvisibility=hidden must be used otherwide symbols from this lib could be made visible in libvogltrace.
add_definitions ("-g -fno-omit-frame-pointer -fno-strict-aliasing -fno-math-errno -fvisibility=hidden")
SET(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -DNDEBUG")

if ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
   if ( NOT BUILD_X64 )
      # Fix startup crash in dlopen_notify_callback (called indirectly from our dlopen() function) when tracing glxspheres on my AMD dev box (x86 release only)
      #add_definitions ("-mstack-alignment=8")
   endif()
endif()

# clang doesn't print colored diagnostics when invoked from Ninja
if ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  if (UNIX AND CMAKE_GENERATOR STREQUAL "Ninja")
      add_definitions ("-fcolor-diagnostics")
  endif()
endif()

check_c_source_compiles (
    "int i;
    int main() {
    __sync_bool_compare_and_swap (&i, i, i);
    __sync_lock_test_and_set (&i, 1);
    __sync_lock_release (&i);}"
    HAVE_SYNC_FUNCTIONS)

check_c_source_compiles (
    "int i;
    int main() {
    __atomic_load_n (&i, __ATOMIC_ACQUIRE);
    __atomic_store_n (&i, i, __ATOMIC_RELEASE);}"
    HAVE_ATOMIC_FUNCTIONS)

if (HAVE_SYNC_FUNCTIONS OR HAVE_ATOMIC_FUNCTIONS)
    set (BACKTRACE_SUPPORTS_THREADS 1)
else ()
    set (BACKTRACE_SUPPORTS_THREADS 0)
endif ()

include(CMakeDetermineCompilerId)
if (CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF")
    set (FORMAT_FILE elf.c dwarf.c)
    math (EXPR BACKTRACE_ELF_SIZE 8*${CMAKE_C_SIZEOF_DATA_PTR})
else ()
    message("WARNING: Unknown executable format \"${CMAKE_EXECUTABLE_FORMAT}\", setting BACKTRACE_SUPPORTED=0")
    set (FORMAT_FILE unknown.c)
    set (BACKTRACE_SUPPORTED 0)
endif ()

check_symbol_exists (mmap sys/mman.h HAVE_MMAP)

if (HAVE_MMAP)
    set (VIEW_FILE mmapio.c)
    check_symbol_exists (MAP_ANONYMOUS sys/mman.h HAVE_MMAP_ANONYMOUS)
    check_symbol_exists (MAP_ANON sys/mman.h HAVE_MMAP_ANON)
    if (HAVE_MMAP_ANONYMOUS AND HAVE_MMAP_ANON)
    set (ALLOC_FILE mmap.c)
    else ()
    set (ALLOC_FILE alloc.c)
    endif ()
else ()
    set (VIEW_FILE read.c)
    set (ALLOC_FILE alloc.c)
endif ()

if (ALLOC_FILE STREQUAL "alloc.c")
    set (BACKTRACE_USES_MALLOC 1)
else ()
    set (BACKTRACE_USES_MALLOC 0)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions("-D_DEBUG -DDEBUG")
endif()
add_definitions (-D_GNU_SOURCE)
set (CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS} -D_GNU_SOURCE)
check_symbol_exists (dl_iterate_phdr link.h HAVE_DL_ITERATE_PHDR)

include (CheckFunctionExists)
check_function_exists (fcntl HAVE_FCNTL)

check_function_exists (strnlen HAVE_DECL_STRNLEN)

check_function_exists (getexecname HAVE_GETEXECNAME)

find_library(LIBUNWIND_LIBRARY NAMES unwind libunwind REQUIRE)

include (CheckLibraryExists)
check_library_exists (${LIBUNWIND_LIBRARY} unw_backtrace_skip "" HAVE_UNW_BACKTRACE_SKIP)

include (CheckIncludeFile)
find_path(DWARF_INCLUDE_DIR dwarf.h
      PATH_SUFFIXES libdwarf)
if (NOT DWARF_INCLUDE_DIR)
    message("WARNING: No dwarf.h, setting BACKTRACE_SUPPORTED=0")
    set (FORMAT_FILE unknown.c)
    set (BACKTRACE_SUPPORTED 0)
 else()
    include_directories(${DWARF_INCLUDE_DIR})
endif ()

configure_file (backtrace-supported.h.in backtrace-supported.h)

configure_file (config.h.in.cmake config.h)

find_path(LIBUNWIND_INCLUDE_DIR libunwind.h)

include_directories (BEFORE
    ${CMAKE_CURRENT_BINARY_DIR}
)
include_directories (
    auxincl
    ${LIBUNWIND_INCLUDE_DIR}
)

add_library (backtrace STATIC
    ${BACKTRACE_FILE}
    ${FORMAT_FILE}
    ${VIEW_FILE}
    ${ALLOC_FILE}
    atomic.c
    fileline.c
    posix.c
    print.c
    state.c
    sort.c
)

target_link_libraries(backtrace ${LIBUNWIND_LIBRARY})

set_target_properties (backtrace PROPERTIES
    COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}"
)
