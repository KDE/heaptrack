cmake_minimum_required(VERSION 2.8)
project(heaptrack)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11")

find_package(Boost 1.45.0 COMPONENTS iostreams)
find_package(Threads)

include_directories(
    ${Boost_INCLUDE_DIRS}
    3rdparty/
)

add_subdirectory(3rdparty)
add_subdirectory(tests)

add_library(libheaptrack SHARED heaptrack.cpp)
set_target_properties(libheaptrack
  PROPERTIES OUTPUT_NAME heaptrack)
target_link_libraries(libheaptrack ${CMAKE_DL_LIBS} backtrace)

add_executable(heaptrack_eval heaptrack_main.cpp)
target_link_libraries(heaptrack_eval ${Boost_LIBRARIES})

add_executable(heaptrack_interpret heaptrack_interpret.cpp)
target_link_libraries(heaptrack_interpret backtrace)

# TODO: support running from an installed location
# install(TARGETS libheaptrack heaptrack_eval heaptrack_interpret
#   RUNTIME DESTINATION bin
#   LIBRARY DESTINATION lib
#   ARCHIVE DESTINATION lib
# )
