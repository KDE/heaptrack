include_directories(
    ${ZSTD_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
)

add_library(boost-zstd STATIC zstd.cpp)
target_link_libraries(boost-zstd LINK_PUBLIC
    ${ZSTD_LIBRARY}
)
