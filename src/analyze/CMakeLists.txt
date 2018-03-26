if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

configure_file(analyze_config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/analyze_config.h)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/3rdparty/
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_library(sharedprint STATIC
    accumulatedtracedata.cpp
)

target_link_libraries(sharedprint LINK_PUBLIC
    ${Boost_LIBRARIES}
    ${ZLIB_LIBRARIES}
)

if (ZSTD_FOUND)
target_link_libraries(sharedprint LINK_PUBLIC
    boost-zstd
)
endif()

add_subdirectory(print)

if (KF5_FOUND)
    add_subdirectory(gui)
endif()
