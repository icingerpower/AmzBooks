cmake_minimum_required(VERSION 3.5)

project(AmzBooksAutoTests LANGUAGES CXX)

enable_testing()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TestOrders tst_testorders.cpp)
add_test(NAME TestOrders COMMAND TestOrders)

add_executable(TestTaxResolver tst_test_tax_resolver.cpp)
add_test(NAME TestTaxResolver COMMAND TestTaxResolver)

target_link_libraries(TestOrders PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestOrders PRIVATE ../AmzBooksLib)

target_link_libraries(TestTaxResolver PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestTaxResolver PRIVATE ../AmzBooksLib ../../common)

# Copy eu-vat-reports to build directory (for both or just one, doing it generally for TestTaxResolver is enough as it needs it)
add_custom_command(TARGET TestTaxResolver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/eu-vat-reports
    $<TARGET_FILE_DIR:TestTaxResolver>/data/eu-vat-reports
)


# Copy eu-vat-reports to build directory
add_custom_command(TARGET TestOrders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/eu-vat-reports
    $<TARGET_FILE_DIR:TestOrders>/data/eu-vat-reports
)

add_executable(TestVatRateResolver tst_test_vat_rate.cpp)
add_test(NAME TestVatRateResolver COMMAND TestVatRateResolver)

target_link_libraries(TestVatRateResolver PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestVatRateResolver PRIVATE ../AmzBooksLib ../../common)

add_custom_command(TARGET TestVatRateResolver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/eu-vat-reports
    $<TARGET_FILE_DIR:TestVatRateResolver>/data/eu-vat-reports
)


add_executable(TestOrderManager tst_testordermanager.cpp)
add_test(NAME TestOrderManager COMMAND TestOrderManager)
target_link_libraries(TestOrderManager PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestOrderManager PRIVATE ../AmzBooksLib)

add_custom_command(TARGET TestOrderManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/eu-vat-reports
    $<TARGET_FILE_DIR:TestOrderManager>/data/eu-vat-reports
)

