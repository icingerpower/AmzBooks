cmake_minimum_required(VERSION 3.5)

project(AmzBooksAutoTests LANGUAGES CXX)

enable_testing()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
find_package(QCoro6 REQUIRED COMPONENTS Core)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TestOrders test_orders.cpp)
add_test(NAME TestOrders COMMAND TestOrders)

add_executable(TestTaxResolver test_tax_resolver.cpp)
add_test(NAME TestTaxResolver COMMAND TestTaxResolver)

target_link_libraries(TestOrders PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestOrders PRIVATE ../AmzBooksLib)

target_link_libraries(TestTaxResolver PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestTaxResolver PRIVATE ../AmzBooksLib ../../common)

# Copy amazon-vat-reports to build directory (for both or just one, doing it generally for TestTaxResolver is enough as it needs it)
add_custom_command(TARGET TestTaxResolver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-vat-reports
    $<TARGET_FILE_DIR:TestTaxResolver>/data/amazon-vat-reports
)


# Copy amazon-vat-reports to build directory
add_custom_command(TARGET TestOrders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-vat-reports
    $<TARGET_FILE_DIR:TestOrders>/data/amazon-vat-reports
)

add_executable(TestVatRateResolver test_vat_rate.cpp)
add_test(NAME TestVatRateResolver COMMAND TestVatRateResolver)

target_link_libraries(TestVatRateResolver PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestVatRateResolver PRIVATE ../AmzBooksLib ../../common)

add_custom_command(TARGET TestVatRateResolver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-vat-reports
    $<TARGET_FILE_DIR:TestVatRateResolver>/data/amazon-vat-reports
)


add_executable(TestOrderManager test_order_manager.cpp)
add_test(NAME TestOrderManager COMMAND TestOrderManager)
target_link_libraries(TestOrderManager PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib QCoro::Core)
target_include_directories(TestOrderManager PRIVATE ../AmzBooksLib ../../common)

add_custom_command(TARGET TestOrderManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-vat-reports
    $<TARGET_FILE_DIR:TestOrderManager>/data/amazon-vat-reports
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-fba-invoicing
    $<TARGET_FILE_DIR:TestOrderManager>/data/amazon-fba-invoicing
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-transactions
    $<TARGET_FILE_DIR:TestOrderManager>/data/amazon-transactions
)

add_executable(TestBookAccounts test_book_accounts.cpp)
add_test(NAME TestBookAccounts COMMAND TestBookAccounts)
target_link_libraries(TestBookAccounts PRIVATE Qt${QT_VERSION_MAJOR}::Test -Wl,--whole-archive AmzBooksLib -Wl,--no-whole-archive)
target_include_directories(TestBookAccounts PRIVATE ../AmzBooksLib)

add_custom_command(TARGET TestBookAccounts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-vat-reports
    $<TARGET_FILE_DIR:TestBookAccounts>/data/amazon-vat-reports
)

add_executable(TestCurrencies test_currencies.cpp)
add_test(NAME TestCurrencies COMMAND TestCurrencies)
target_link_libraries(TestCurrencies PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestCurrencies PRIVATE ../AmzBooksLib)

add_executable(TestBookEntries test_book_entries.cpp)
add_test(NAME TestBookEntries COMMAND TestBookEntries)
target_link_libraries(TestBookEntries PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestBookEntries PRIVATE ../AmzBooksLib)

add_custom_command(TARGET TestBookEntries POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-vat-reports
    $<TARGET_FILE_DIR:TestBookEntries>/data/amazon-vat-reports
)

add_executable(TestJournalTable tst_journal_table.cpp)
add_test(NAME TestJournalTable COMMAND TestJournalTable)
target_link_libraries(TestJournalTable PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestJournalTable PRIVATE ../AmzBooksLib)


add_executable(TestFileImportAmazonTransactions test_file_import_amazon_transactions.cpp)
add_test(NAME TestFileImportAmazonTransactions COMMAND TestFileImportAmazonTransactions)
target_link_libraries(TestFileImportAmazonTransactions PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestFileImportAmazonTransactions PRIVATE ../AmzBooksLib ../../common)

add_custom_command(TARGET TestFileImportAmazonTransactions POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-vat-reports
    $<TARGET_FILE_DIR:TestFileImportAmazonTransactions>/data/amazon-vat-reports
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-transactions
    $<TARGET_FILE_DIR:TestFileImportAmazonTransactions>/data/amazon-transactions
)

add_executable(TestFileImportAmazonFbaInvoicing test_file_import_amazon_fba_invoicing.cpp)
add_test(NAME TestFileImportAmazonFbaInvoicing COMMAND TestFileImportAmazonFbaInvoicing)
target_link_libraries(TestFileImportAmazonFbaInvoicing PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib QCoro::Core)
    target_include_directories(TestFileImportAmazonFbaInvoicing PRIVATE ../AmzBooksLib ../../common)
    # Force rebuild

add_custom_command(TARGET TestFileImportAmazonFbaInvoicing POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/amazon-fba-invoicing
    $<TARGET_FILE_DIR:TestFileImportAmazonFbaInvoicing>/data/amazon-fba-invoicing
    # Also copy fbacenters.csv which is likely in data root or specific place?
    # FbaCentersTable loads from "workingDir/fbacenters.csv".
    # User said: "Analyse the folder data/amazon-fba-invoicing ... there are one subfolder per year".
    # But FbaCentersTable.cpp line 14: `workingDir.absoluteFilePath("fbacenters.csv")`.
    # I need `fbacenters.csv` in the test working directory.
    # Where is it? `list_dir data` in Step 586 didn't show it.
    # It might be in `data/` root? Step 586 showed only Helpers.
    # Let me check data root again or FbaCentersTable expected location.
    # Wait, Step 591 showing FbaCentersTable.h doesn't clarify source.
    # Step 586 `list_dir data` -> `amazon-fba-invoicing` (dir), `amazon-payments` (dir)...
    # It says "contains 6 subdirectories and 0 files".
    # So `fbacenters.csv` is NOT in `data/`.
    # Maybe it's generated? `_fillIfEmpty()` in `FbaCentersTable.cpp` line 154 fills defaults if empty.
    # So if it doesn't exist, it creates it.
    # So I don't need to copy it from source if I rely on defaults.
    # BUT, prompt said: "To get the shipping country code ... use FbaCentersTable::getCountryCode".
    # And FbaCentersTable has hardcoded defaults.
    # I should ensure the test can write to working dir or finds it.
    # If I run test in build/TestDir, it will create `fbacenters.csv` there. Good.
)

add_executable(TestRecorders test_recorders.cpp)
add_test(NAME TestRecorders COMMAND TestRecorders)
target_link_libraries(TestRecorders PRIVATE Qt${QT_VERSION_MAJOR}::Test -Wl,--whole-archive AmzBooksLib -Wl,--no-whole-archive)

add_executable(TestBanks test_banks.cpp)
add_test(NAME TestBanks COMMAND TestBanks)
target_link_libraries(TestBanks PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestBanks PRIVATE ../AmzBooksLib ../../common)

add_custom_command(TARGET TestBanks POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/bankAccounts
    $<TARGET_FILE_DIR:TestBanks>/data/bankAccounts
)

add_executable(TestFileImportTemuOrders test_file_import_temu_orders.cpp)
add_test(NAME TestFileImportTemuOrders COMMAND TestFileImportTemuOrders)
target_link_libraries(TestFileImportTemuOrders PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestFileImportTemuOrders PRIVATE ../AmzBooksLib ../../common)

add_custom_command(TARGET TestFileImportTemuOrders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/temu-orders
    $<TARGET_FILE_DIR:TestFileImportTemuOrders>/data/temu-orders
)


add_executable(TestBooksConnection test_books_connections.cpp)
add_test(NAME TestBooksConnection COMMAND TestBooksConnection)
target_link_libraries(TestBooksConnection PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestBooksConnection PRIVATE ../AmzBooksLib)


add_executable(TestServiceSales test_service_sales.cpp)
add_test(NAME TestServiceSales COMMAND TestServiceSales)
target_link_libraries(TestServiceSales PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestServiceSales PRIVATE ../AmzBooksLib)

add_executable(TestInvoicing test_invoicing.cpp)
add_test(NAME TestInvoicing COMMAND TestInvoicing)
target_link_libraries(TestInvoicing PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(TestInvoicing PRIVATE ../AmzBooksLib)


add_executable(TestPurchaseFileSettingsTree TestPurchaseFileSettingsTree.cpp)
add_test(NAME TestPurchaseFileSettingsTree COMMAND TestPurchaseFileSettingsTree)
target_link_libraries(TestPurchaseFileSettingsTree PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
    target_include_directories(TestPurchaseFileSettingsTree PRIVATE ../AmzBooksLib)

add_executable(TestProfit test_profit.cpp)
add_test(NAME TestProfit COMMAND TestProfit)
target_link_libraries(TestProfit PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)

add_executable(testInventory test_inventory.cpp)
add_test(NAME testInventory COMMAND testInventory)
target_link_libraries(testInventory PRIVATE Qt${QT_VERSION_MAJOR}::Test AmzBooksLib)
target_include_directories(testInventory PRIVATE ../AmzBooksLib)
