project(test_middle_elide LANGUAGES CXX)

find_package(QT NAMES Qt5 Qt6 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(test_middle_elide
    tst_middle_elide.cpp
)

add_test(NAME test_middle_elide COMMAND test_middle_elide)

target_link_libraries(test_middle_elide PRIVATE
                      heaptrack_gui_private
                      Qt${QT_VERSION_MAJOR}::Test)

