cmake_minimum_required(VERSION 3.21)
project(hyprtab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(hyprtab
    src/main.cpp
    src/backend.cpp
    src/backend.h
)

qt_add_qml_module(hyprtab
    URI Hyprtab
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(hyprtab PRIVATE Qt6::Quick)
