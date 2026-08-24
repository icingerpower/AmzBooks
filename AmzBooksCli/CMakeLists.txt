cmake_minimum_required(VERSION 3.5)

project(AmzBooksCli LANGUAGES CXX)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Sql)
find_package(QCoro6 REQUIRED COMPONENTS Core)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AmzBooksImportCli main_import.cpp)

# --whole-archive keeps the self-registering importer static initializers
# (DECLARE_IMPORTER_FILE) that the linker would otherwise drop.
target_link_libraries(AmzBooksImportCli PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Sql
    QCoro::Core
    -Wl,--whole-archive AmzBooksLib -Wl,--no-whole-archive
)
target_include_directories(AmzBooksImportCli PRIVATE ../AmzBooksLib ../../common)
