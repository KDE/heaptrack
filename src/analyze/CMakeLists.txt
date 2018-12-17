if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

find_package(Boost 1.41.0 REQUIRED COMPONENTS iostreams program_options)

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

if(HEAPTRACK_BUILD_GUI)
    find_package(Qt5 5.2.0 NO_MODULE OPTIONAL_COMPONENTS Widgets)
    find_package(ECM 1.0.0 NO_MODULE)
    if(Qt5_FOUND AND ECM_FOUND)
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})
            find_package(KF5 COMPONENTS CoreAddons I18n ItemModels ThreadWeaver ConfigWidgets KIO)
            find_package(KChart "2.6.0")
            set_package_properties(KChart PROPERTIES TYPE RECOMMENDED PURPOSE "Required for the heaptrack_gui executable. Get it from the kdiagram module.")
    endif()
endif()

if (KF5_FOUND)
    add_subdirectory(gui)
endif()
