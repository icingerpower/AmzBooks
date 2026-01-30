cmake_minimum_required(VERSION 3.14)

project(AmzBooksLib LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Sql)

# Compile and install QCoro
# cd /home/cedric/code
# git clone https://github.com/qcoro/qcoro.git
# cd qcoro
# mkdir build
# cd build
# cmake .. -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64 -DUSE_QT_VERSION=6 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/cedric/Qt/6.7.3/lib/cmake
# make
# This will install QCoro into /usr/local/ prefix, change it by passing -DCMAKE_INSTALL_PREFIX=/usr
# to the cmake command above.
# sudo make install
set(Qt_DIR_HINT "${Qt${QT_VERSION_MAJOR}_DIR}")
message("Qt hints: ${Qt_DIR_HINT}")
set(QCoro_DIR_HINT_1 "/usr/local/lib/cmake/QCoro6")
set(QCoro_DIR_HINT_2 "/usr/lib/cmake/QCoro6")
set(QCoro_DIR_HINT_3 "${Qt_DIR_HINT}/../../../../lib/cmake/QCoro6") # only if you installed QCoro into the same Qt prefix
set(QCoro_DIR_HINT_4 "${Qt_DIR_HINT}/../../../../lib/cmake/lib/cmake/QCoro6") # only if you installed QCoro into the same Qt prefix
message("QCoro hints: ${QCoro_DIR_HINT_1} ; ${QCoro_DIR_HINT_2} ; ${QCoro_DIR_HINT_3} ; ${QCoro_DIR_HINT_4}")

list(APPEND CMAKE_PREFIX_PATH "${Qt_DIR_HINT}/../../../../lib/cmake")

find_package(QCoro6 REQUIRED COMPONENTS Network
    HINTS
        "${QCoro_DIR_HINT_1}"
        "${QCoro_DIR_HINT_2}"
        "${QCoro_DIR_HINT_3}"
        "${QCoro_DIR_HINT_4}"
)

include(../../common/utils/utils.cmake)

include(orders/orders.cmake)
include(books/books.cmake)

add_library(AmzBooksLib STATIC
  AmzBooksLib.cpp
  AmzBooksLib.h
  CountriesEu.h
  CountriesEu.cpp
  CurrencyRateManager.h
  CurrencyRateManager.cpp
  ExceptionRateCurrency.h
  ExceptionRateCurrency.cpp
  ${ORDERS_FILES}
  ${BOOKS_FILES}
  ${UTILS_FILES}
)



target_link_libraries(AmzBooksLib
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Network
    QCoro::Network
)

target_include_directories(AmzBooksLib PUBLIC ../../common ${CMAKE_CURRENT_SOURCE_DIR})



target_compile_definitions(AmzBooksLib PRIVATE AMZBOOKSLIB_LIBRARY)
