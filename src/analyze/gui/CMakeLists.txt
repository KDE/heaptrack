set(CMAKE_AUTOMOC 1)

ki18n_wrap_ui(UIFILES
    mainwindow.ui
)

unset(BIN_INSTALL_DIR)
unset(LIB_INSTALL_DIR)
unset(LIBEXEC_INSTALL_DIR)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMAddTests)
include(ECMInstallIcons)
include(ECMAddAppIcon)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

option(APPIMAGE_BUILD "configure build for bundling in an appimage" OFF)

configure_file(gui_config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/gui_config.h)

add_definitions(-Wall
    -DQT_NO_URL_CAST_FROM_STRING
    -DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII
    -DQT_NO_CAST_FROM_BYTEARRAY
    -DQT_USE_QSTRINGBUILDER
)

add_library(heaptrack_gui_private STATIC
    util.cpp
    parser.cpp
)
target_link_libraries(heaptrack_gui_private PUBLIC
    KF5::I18n
    KF5::CoreAddons # KFormat
    KF5::ThreadWeaver
    Qt5::Core
    sharedprint
    pthread
    ${CMAKE_THREAD_LIBS_INIT}
)

set(SRCFILES
    gui.cpp
    mainwindow.cpp
    treemodel.cpp
    treeproxy.cpp
    costdelegate.cpp
    flamegraph.cpp
    stacksmodel.cpp
    topproxy.cpp
    callercalleemodel.cpp
    resources.qrc
)

ecm_add_app_icon(SRCFILES ICONS
    ${CMAKE_CURRENT_SOURCE_DIR}/128-heaptrack_app_icon.png
    ${CMAKE_CURRENT_SOURCE_DIR}/512-heaptrack_app_icon.png
)

set(LIBRARIES
    Qt5::Widgets
    KF5::ItemModels
    KF5::ConfigWidgets
    KF5::I18n
    KF5::KIOWidgets
    sharedprint
    heaptrack_gui_private
)

if (KChart_FOUND)
    list(APPEND SRCFILES
        chartwidget.cpp
        chartmodel.cpp
        chartproxy.cpp
        histogramwidget.cpp
        histogrammodel.cpp
    )
    list(APPEND LIBRARIES
        KChart
    )
endif()

add_executable(heaptrack_gui
    ${SRCFILES}
    ${UIFILES}
)

target_link_libraries(heaptrack_gui
    ${LIBRARIES}
)

install(TARGETS heaptrack_gui
    ${INSTALL_TARGETS_DEFAULT_ARGS}
)

set_target_properties(heaptrack_gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${BIN_INSTALL_DIR}"
)

install(PROGRAMS org.kde.heaptrack.desktop
    DESTINATION ${XDG_APPS_INSTALL_DIR}
)

install(FILES org.kde.heaptrack.appdata.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)

ecm_install_icons(ICONS
  16-apps-heaptrack.png
  22-apps-heaptrack.png
  32-apps-heaptrack.png
  48-apps-heaptrack.png
  64-apps-heaptrack.png
  128-apps-heaptrack.png
  DESTINATION ${KDE_INSTALL_ICONDIR}
  THEME hicolor
)
