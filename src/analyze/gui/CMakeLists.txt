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

include_directories(${CMAKE_CURRENT_BINARY_DIR})

configure_file(gui_config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/gui_config.h)

set(SRCFILES
    gui.cpp
    mainwindow.cpp
    treemodel.cpp
    treeproxy.cpp
    costdelegate.cpp
    modeltest.cpp
    parser.cpp
    flamegraph.cpp
    stacksmodel.cpp
    topproxy.cpp
    callercalleemodel.cpp
)

set(LIBRARIES
    Qt5::Widgets
    KF5::ItemModels
    KF5::ThreadWeaver
    KF5::ConfigWidgets
    KF5::I18n
    KF5::KIOWidgets
    sharedprint
    ${CMAKE_THREAD_LIBS_INIT}
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

add_definitions(-Wall
    -DQT_NO_URL_CAST_FROM_STRING
    -DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII
    -DQT_NO_CAST_FROM_BYTEARRAY
    -DQT_USE_QSTRINGBUILDER
)

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
