cmake_minimum_required(VERSION 3.14)

project(AmzBooksLib LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Gui Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui Sql)

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



target_link_libraries(AmzBooksLib PUBLIC Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Sql)

target_compile_definitions(AmzBooksLib PRIVATE AMZBOOKSLIB_LIBRARY)
