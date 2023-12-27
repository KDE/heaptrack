if (ECM_FOUND)
    include(ECMEnableSanitizers)
endif()

find_package(Boost 1.41.0 REQUIRED COMPONENTS headers iostreams program_options system filesystem)

configure_file(analyze_config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/analyze_config.h)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/3rdparty/
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_library(sharedprint STATIC
    accumulatedtracedata.cpp
    suppressions.cpp
)

target_link_libraries(sharedprint
    PUBLIC
        ${Boost_LIBRARIES}
        ${ZLIB_LIBRARIES}
        tsl::robin_map
)

if (ZSTD_FOUND AND NOT BOOST_IOSTREAMS_HAS_ZSTD)
    target_link_libraries(sharedprint LINK_PUBLIC
        boost-zstd
    )
endif()

add_subdirectory(print)

if(HEAPTRACK_BUILD_GUI)
    if(QT_VERSION_MAJOR EQUAL 6)
        set(KChartName KChart6)
    else()
        set(KChartName KChart)
    endif()

    find_package(Qt${QT_VERSION_MAJOR} ${QT_MIN_VERSION} ${REQUIRED_IN_APPIMAGE} NO_MODULE OPTIONAL_COMPONENTS Widgets)
    set_package_properties(Qt${QT_VERSION_MAJOR} PROPERTIES TYPE RECOMMENDED PURPOSE "Required for the heaptrack_gui executable.")
    find_package(ECM 1.0.0 ${REQUIRED_IN_APPIMAGE} NO_MODULE)
    set_package_properties(ECM PROPERTIES TYPE RECOMMENDED PURPOSE "KDE's extra-cmake-modules, required for the heaptrack_gui executable.")
    if(Qt${QT_VERSION_MAJOR}_FOUND AND ECM_FOUND)
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH})
            find_package(KF${QT_VERSION_MAJOR} ${REQUIRED_IN_APPIMAGE} COMPONENTS CoreAddons I18n ItemModels ThreadWeaver ConfigWidgets KIO IconThemes)
            find_package(${KChartName} ${REQUIRED_IN_APPIMAGE} "2.6.0")
            set_package_properties(${KChartName} PROPERTIES TYPE RECOMMENDED PURPOSE "Required for the heaptrack_gui executable. Get it from the kdiagram module.")
        if(KF${QT_VERSION_MAJOR}I18n_FOUND)
            ki18n_install(../../po)
        endif()
    else()
        message(WARNING "Required Qt/KF dependencies for building heaptrack GUI not found")
    endif()
endif()

if(KChart6_FOUND)
    set(KChart_FOUND TRUE)
endif()

if (KF${QT_VERSION_MAJOR}_FOUND)
    add_subdirectory(gui)
endif()
