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
set(QCoro_DIR_HINT_1 "/usr/local/lib/cmake/QCoro6")
set(QCoro_DIR_HINT_2 "/usr/lib/cmake/QCoro6")
set(QCoro_DIR_HINT_3 "${Qt_DIR_HINT}/../../../../lib/cmake/QCoro6") # only if you installed QCoro into the same Qt prefix
message("QCoro hints: ${QCoro_DIR_HINT_1} ; ${QCoro_DIR_HINT_2} ; ${QCoro_DIR_HINT_3}")

find_package(QCoro6 REQUIRED COMPONENTS Network
    HINTS
        "${QCoro_DIR_HINT_1}"
        "${QCoro_DIR_HINT_2}"
        "${QCoro_DIR_HINT_3}"
)

include(../../common/utils/utils.cmake)
include(model/model.cmake)
include(books/books.cmake)

add_library(AmzBooksLib STATIC
  AmzBooksLib.cpp
  AmzBooksLib.h
  books/VatCountries.h
  books/SaleBookAccountsTable.h
  books/SaleBookAccountsTable.cpp
  books/PurchaseBookAccountsTable.h
  books/PurchaseBookAccountsTable.cpp
  books/ExceptionTaxSchemeInvalid.h
  books/ExceptionTaxSchemeInvalid.cpp
  books/ExceptionVatAccountExisting.h
  books/ExceptionVatAccountExisting.cpp
  CountriesEu.h
  CountriesEu.cpp
  ${COUNTRY_FILES}
  ${UTILS_FILES}
  ${MODEL_FILES}
  ${BOOKS_FILES}
)



target_link_libraries(AmzBooksLib
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Sql
    QCoro::Network
)

target_compile_definitions(AmzBooksLib PRIVATE AMZBOOKSLIB_LIBRARY)
