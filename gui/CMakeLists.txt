set(CMAKE_AUTOMOC 1)

qt5_wrap_ui(UIFILES
    mainwindow.ui
)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(ECMAddTests)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(heaptrack_gui
    gui.cpp
    mainwindow.cpp
    model.cpp
    modeltest.cpp
    proxy.cpp
    ${UIFILES}
)

add_definitions(-Wall
    -DQT_NO_URL_CAST_FROM_STRING
    -DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII
    -DQT_NO_CAST_FROM_BYTEARRAY
    -DQT_USE_QSTRINGBUILDER
)

target_link_libraries(heaptrack_gui
    Qt5::Widgets
    KF5::ItemModels
    KF5::ThreadWeaver
    KF5::ConfigWidgets
    KF5::I18n
    ${Boost_LIBRARIES}
    sharedprint)

install(TARGETS heaptrack_gui ${INSTALL_TARGETS_DEFAULT_ARGS})