cmake_minimum_required(VERSION 3.14)

project(AmzBooksLib LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui)

add_library(AmzBooksLib STATIC
  AmzBooksLib.cpp
  AmzBooksLib.h
)

target_link_libraries(AmzBooksLib PRIVATE Qt${QT_VERSION_MAJOR}::Gui)

target_compile_definitions(AmzBooksLib PRIVATE AMZBOOKSLIB_LIBRARY)
