cmake_minimum_required(VERSION 3.5)

project(AmzBooksAutoTests LANGUAGES CXX)

enable_testing()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TestOrders tst_testorders.cpp)
add_test(NAME TestOrders COMMAND TestOrders)

target_link_libraries(TestOrders PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestOrders PRIVATE ../AmzBooksLib)

