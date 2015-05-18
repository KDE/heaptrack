set(CMAKE_AUTOMOC 1)

qt5_wrap_ui(UIFILES
    mainwindow.ui
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(heaptrack_gui
    gui.cpp
    mainwindow.cpp
    model.cpp
    ${UIFILES}
)

target_link_libraries(heaptrack_gui Qt5::Widgets ${Boost_LIBRARIES} sharedprint)

install(TARGETS heaptrack_gui RUNTIME DESTINATION "bin")