include(ECMEnableSanitizers)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
)

add_library(sharedprint STATIC
    accumulatedtracedata.cpp
)

target_link_libraries(sharedprint LINK_PUBLIC
    ${Boost_LIBRARIES}
    ${ZLIB_LIBRARIES}
)

add_subdirectory(print)

if (KF5_FOUND)
    add_subdirectory(gui)
endif()
