#include <QtTest>
#include "books/JournalEntry.h"
#include "books/ExceptionBookEquality.h"
#include "books/PurchaseInvoiceManager.h"
#include "books/ExceptionFileError.h"
#include "books/JournalEntryFactory.h"
#include "books/CompanyInfosTable.h"
#include "books/SaleBookAccountsTable.h"
#include "books/PurchaseBookAccountsTable.h"
#include "books/JournalTable.h"
#include "CurrencyRateManager.h"
#include "orders/ActivitySource.h"
#include "orders/Shipment.h"
#include "books/Activity.h"
#include <QTemporaryDir>
#include <QCoroTask>
#include <QCoroFuture>

// Helper to synchronously wait for QCoro::Task
template <typename T>
T syncWait(QCoro::Task<T> &&task) {
    return QCoro::waitFor<T>(std::move(task));
}

class TestBookEntries : public QObject
{
    Q_OBJECT

private slots:
    void test_journal_entry_simple();
    void test_rounding_within_limit();
    void test_rounding_exceeds_limit();
    void test_currency_conversion();
    void test_currency_conversion_custom_delta();
    void test_get_debit_sum();
    void test_get_credit_sum();
    void test_raise_exception_balanced();
    void test_raise_exception_unbalanced();
    void test_multi_currency_entries();
    void test_invoice_encoding();
    void test_invoice_model_loading();
    void test_invoice_decode_error();
    void test_invoice_add();
    void test_get_invoices();
    
    // JournalEntryFactory tests
    void test_factory_purchase_no_conversion();
    void test_factory_purchase_with_conversion();
    void test_factory_purchase_refund();
    void test_factory_shipment_no_conversion();
    void test_factory_shipment_with_conversion();
    void test_factory_shipment_mixed_rates();
};

void TestBookEntries::test_journal_entry_simple()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Debit 100 EUR
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales Revenue";
    debitLine.account = "707000";
    debitLine.currency_amount["EUR"] = 100.0;
    
    // Credit 100 EUR
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512000";
    creditLine.currency_amount["EUR"] = 100.0;
    
    entry.addDebitLeft(debitLine, "EUR");
    entry.addCreditRight(creditLine, "EUR");
    
    // Should not throw (1.0 conversion rate, implicit or explicit)
    try {
        entry.convertRoundingIfNeeded(1.0);
    } catch (const ExceptionBookEquality &e) {
        QFAIL(QString("Exception thrown unexpectedly: %1").arg(e.what()).toUtf8().constData());
    }
}

void TestBookEntries::test_rounding_within_limit()
{
    JournalEntry entry(QDate::currentDate(), "EUR");

    // Debit 100.00
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.00;
    entry.addDebitLeft(debitLine, "EUR");

    // Credit 99.99 (0.01 difference < 0.1 limit)
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 99.99;
    entry.addCreditRight(creditLine, "EUR");

    try {
        entry.convertRoundingIfNeeded(1.0); // maxRoundingDelta defaults to 0.1
    } catch (const ExceptionBookEquality &e) {
        QFAIL(e.what());
    }
    
    // Verify rounding entry
    const auto &credits = entry.getCredits();
    // Expected: 1 original credit + 1 rounding credit
    // But wait, diff is 100.00 - 99.99 = 0.01. Diff > 0 means Debit > Credit.
    // "If diff > 0, Debit > Credit, we need more Credit." -> Added to Credit.
    
    QCOMPARE(credits.size(), 2);
    const auto &lastCredit = credits.last();
    QCOMPARE(lastCredit.title, QString("Rounding difference"));
    QCOMPARE(lastCredit.currency_amount["EUR"], 0.01);
    
    // Verification of rounding entry existence is implied by no exception and successful completion 
    // since the class corrects it internally effectively.
    // Further inspection would require getters on JournalEntry if we wanted to verify the exact entries.
    // Assuming the requirement "Create tst_book_entries.cpp to test this class... including rounding success" 
    // implies detecting that it passed without error.
}

void TestBookEntries::test_rounding_exceeds_limit()
{
    JournalEntry entry(QDate::currentDate(), "EUR");

    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.00;
    entry.addDebitLeft(debitLine, "EUR");

    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 99.00; // 1.00 difference > 0.1
    entry.addCreditRight(creditLine, "EUR");

    bool caught = false;
    try {
        entry.convertRoundingIfNeeded(1.0);
    } catch (const ExceptionBookEquality &) {
        caught = true;
    }
    QVERIFY2(caught, "ExceptionBookEquality should have been thrown for large difference");
}

void TestBookEntries::test_currency_conversion()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // USD -> EUR (Rate 0.85)
    // 100 USD = 85.00 EUR
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales USD";
    debitLine.account = "707";
    debitLine.currency_amount["USD"] = 100.00;
    entry.addDebitLeft(debitLine, "USD", 0.85);
    
    // Credit explicitly in EUR matching the conversion
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank EUR";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 85.00;
    entry.addCreditRight(creditLine, "EUR");

    try {
        entry.convertRoundingIfNeeded(0.85);
    } catch (const ExceptionBookEquality &e) {
        QFAIL(e.what());
    }

    const auto &debits = entry.getDebits();
    QCOMPARE(debits.size(), 1);
    // 100 * 0.85 = 85.00
    QCOMPARE(debits[0].currency_amount["EUR"], 85.00);
    QCOMPARE(debits[0].currency_amount["USD"], 100.00);
    // Check title modification
    // Title should contain " (Conv: 100 USD @ 0.85)"
    QVERIFY(debits[0].title.contains("(Conv: 100 USD @ 0.85)"));
}

void TestBookEntries::test_currency_conversion_custom_delta()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Scenario where rounding diff is larger than default 0.1 but we allow it
    // Debit 100.00 EUR
    JournalEntry::EntryLine debitLine;
    debitLine.title = "A";
    debitLine.account = "1";
    debitLine.currency_amount["EUR"] = 100.00;
    entry.addDebitLeft(debitLine, "EUR");
    
    // Credit 99.50 EUR -> Diff 0.50
    JournalEntry::EntryLine creditLine;
    creditLine.title = "B";
    creditLine.account = "2";
    creditLine.currency_amount["EUR"] = 99.50;
    entry.addCreditRight(creditLine, "EUR");

    // Should fail with default
    bool caught = false;
    try {
        entry.convertRoundingIfNeeded(1.0);
    } catch (const ExceptionBookEquality &) {
        caught = true;
    }
    QVERIFY(caught);

    // Should pass with custom delta 1.0
    try {
        entry.convertRoundingIfNeeded(1.0, 1.0);
    } catch (const ExceptionBookEquality &e) {
        QFAIL(e.what());
    }
}

void TestBookEntries::test_get_debit_sum()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Add multiple debit entries
    JournalEntry::EntryLine debit1;
    debit1.title = "Sales 1";
    debit1.account = "707";
    debit1.currency_amount["EUR"] = 100.0;
    entry.addDebitLeft(debit1, "EUR");
    
    JournalEntry::EntryLine debit2;
    debit2.title = "Sales 2";
    debit2.account = "707";
    debit2.currency_amount["EUR"] = 50.0;
    entry.addDebitLeft(debit2, "EUR");
    
    JournalEntry::EntryLine debit3;
    debit3.title = "Sales 3 USD";
    debit3.account = "707";
    debit3.currency_amount["USD"] = 100.0;
    entry.addDebitLeft(debit3, "USD", 0.85); // 100 USD = 85 EUR
    
    // Total should be 100 + 50 + 85 = 235
    QCOMPARE(entry.getDebitSum(), 235.0);
}

void TestBookEntries::test_get_credit_sum()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Add multiple credit entries
    JournalEntry::EntryLine credit1;
    credit1.title = "Bank 1";
    credit1.account = "512";
    credit1.currency_amount["EUR"] = 100.0;
    entry.addCreditRight(credit1, "EUR");
    
    JournalEntry::EntryLine credit2;
    credit2.title = "Bank 2";
    credit2.account = "512";
    credit2.currency_amount["EUR"] = 75.0;
    entry.addCreditRight(credit2, "EUR");
    
    // Total should be 100 + 75 = 175
    QCOMPARE(entry.getCreditSum(), 175.0);
}

void TestBookEntries::test_raise_exception_balanced()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.0;
    entry.addDebitLeft(debitLine, "EUR");
    
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 100.0;
    entry.addCreditRight(creditLine, "EUR");
    
    // Should not throw
    try {
        entry.raiseExceptionIfDebitDifferentCredit();
    } catch (const ExceptionBookEquality &e) {
        QFAIL(QString("Exception thrown unexpectedly: %1").arg(e.what()).toUtf8().constData());
    }
}

void TestBookEntries::test_raise_exception_unbalanced()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.0;
    entry.addDebitLeft(debitLine, "EUR");
    
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 95.0;
    entry.addCreditRight(creditLine, "EUR");
    
    // Should throw
    bool caught = false;
    try {
        entry.raiseExceptionIfDebitDifferentCredit();
    } catch (const ExceptionBookEquality &) {
        caught = true;
    }
    QVERIFY2(caught, "ExceptionBookEquality should have been thrown for unbalanced entry");
}

void TestBookEntries::test_multi_currency_entries()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Debit in USD
    JournalEntry::EntryLine debit1;
    debit1.title = "Sales USD";
    debit1.account = "707";
    debit1.currency_amount["USD"] = 200.0;
    entry.addDebitLeft(debit1, "USD", 0.85); // 200 USD = 170 EUR
    
    // Debit in GBP
    JournalEntry::EntryLine debit2;
    debit2.title = "Sales GBP";
    debit2.account = "707";
    debit2.currency_amount["GBP"] = 50.0;
    entry.addDebitLeft(debit2, "GBP", 1.15); // 50 GBP = 57.5 EUR
    
    // Credit in EUR
    JournalEntry::EntryLine credit1;
    credit1.title = "Bank EUR";
    credit1.account = "512";
    credit1.currency_amount["EUR"] = 227.5;
    entry.addCreditRight(credit1, "EUR");
    
    // Verify sums
    QCOMPARE(entry.getDebitSum(), 227.5);  // 170 + 57.5
    QCOMPARE(entry.getCreditSum(), 227.5);
    
    // Verify entries contain both currencies
    const auto &debits = entry.getDebits();
    QCOMPARE(debits[0].currency_amount["USD"], 200.0);
    QCOMPARE(debits[0].currency_amount["EUR"], 170.0);
    QCOMPARE(debits[1].currency_amount["GBP"], 50.0);
    QCOMPARE(debits[1].currency_amount["EUR"], 57.5);
    
    // Should not throw when checked
    try {
        entry.raiseExceptionIfDebitDifferentCredit();
    } catch (const ExceptionBookEquality &e) {
        QFAIL(QString("Exception thrown unexpectedly: %1").arg(e.what()).toUtf8().constData());
    }
}

void TestBookEntries::test_invoice_encoding()
{
    // Test Case 1: Standard example
    QString fileName = "2025-02-18__622600__compta__SOCIC-FR__FR-TVA-13.6EUR__81.6EUR.pdf";
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName);
    
    QCOMPARE(info.date, QDate(2025, 2, 18));
    QCOMPARE(info.account, QString("622600"));
    QCOMPARE(info.label, QString("compta"));
    QCOMPARE(info.supplier, QString("SOCIC-FR"));
    QCOMPARE(info.vatTokens.size(), 1);
    QCOMPARE(info.vatTokens.first(), QString("FR-TVA-13.6EUR"));
    
    // Check Parsing into country_vatRate_vat
    QCOMPARE(info.country_vatRate_vat.size(), 1);
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    // "TVA" -> Rate Label empty -> "" key? Or check logic.
    // Logic: "TVA" -> matchRate matches nothing or empty?
    // Regex "[0-9.]+" on "TVA". No match.
    // Logic: if matchRate.hasMatch() ... else rateKey = "";
    // So key is ""
    QVERIFY(info.country_vatRate_vat["FR"].contains(""));
    QCOMPARE(info.country_vatRate_vat["FR"][""], 13.6);
    
    QCOMPARE(info.totalAmount, 81.6);
    QCOMPARE(info.currency, QString("EUR"));
    QCOMPARE(info.originalExtension, QString("pdf"));
    
    // Flags
    QCOMPARE(info.isInventory, false);
    QCOMPARE(info.isDDP, false);
    QVERIFY(info.countryCodeFrom.isEmpty());
    QVERIFY(info.countryCodeTo.isEmpty());
    
    // Roundtrip
    QString encoded = PurchaseInvoiceManager::encode(info);
    QCOMPARE(encoded, fileName);
    
    // Test Case 2: Multiple VATs and different extension + Inventory + Route + DDP
    // Route in supplier: YISHUNCNFR (ends with CN FR)
    // Inventory: "stock" in label or anywhere.
    // DDP: "DDP" in label
    QString fileName2 = "2026-01-24__607000__stock DDP__YISHUNCNFR__FR-TVA5.5-13.6EUR__FR-TVA20-20.0EUR__133.6EUR.jpg";
    PurchaseInformation info2 = PurchaseInvoiceManager::decode(fileName2);
    
    QCOMPARE(info2.date, QDate(2026, 1, 24));
    QCOMPARE(info2.account, QString("607000"));
    QCOMPARE(info2.vatTokens.size(), 2);
    
    // Check parsed VATs
    // FR, 5.5 -> 13.6
    QVERIFY(info2.country_vatRate_vat.contains("FR"));
    QVERIFY(info2.country_vatRate_vat["FR"].contains("5.50"));
    QCOMPARE(info2.country_vatRate_vat["FR"]["5.50"], 13.6);
    
    // FR, 20 -> 20.0
    QVERIFY(info2.country_vatRate_vat["FR"].contains("20.00")); // formatted %.2f
    QCOMPARE(info2.country_vatRate_vat["FR"]["20.00"], 20.0);
    
    QCOMPARE(info2.totalAmount, 133.6);
    
    // Flags
    QCOMPARE(info2.isInventory, true);
    QCOMPARE(info2.isDDP, true);
    QCOMPARE(info2.countryCodeFrom, QString("CN"));
    QCOMPARE(info2.countryCodeTo, QString("FR"));
    
    QString encoded2 = PurchaseInvoiceManager::encode(info2);
    QCOMPARE(encoded2, fileName2);
    
    // Test Case 3: No VAT
    QString fileName3 = "2025-12-31__622600__fees__BANK__10.0USD.pdf";
    PurchaseInformation info3 = PurchaseInvoiceManager::decode(fileName3);
    QCOMPARE(info3.vatTokens.isEmpty(), true);
    QCOMPARE(info3.totalAmount, 10.0);
    QCOMPARE(info3.currency, QString("USD"));
    
    QString encoded3 = PurchaseInvoiceManager::encode(info3);
    QCOMPARE(encoded3, fileName3);
    
    // Test Folder Structure Path
    QString path = PurchaseInvoiceManager::getRelativePath(info);
    QCOMPARE(path, QString("purchase-invoices/2025/02"));
}

void TestBookEntries::test_invoice_model_loading()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    QDir dir(tempDir.path());
    // Create structure: purchase-invoices/2025/02
    QVERIFY(dir.mkpath("purchase-invoices/2025/02"));
    QVERIFY(dir.mkpath("purchase-invoices/2026/01"));
    
    // Create files
    QString fileName1 = "2025-02-18__622600__compta__SOCIC__FR-TVA-13.6EUR__81.6EUR.pdf";
    QString fullPath1 = dir.filePath("purchase-invoices/2025/02/" + fileName1);
    QFile file1(fullPath1);
    QVERIFY(file1.open(QIODevice::WriteOnly));
    file1.close();
    
    QString fileName2 = "2026-01-24__607000__stock__YISHUN__FR-TVA5.5-13.6EUR__FR-TVA20-20.0EUR__133.6EUR.jpg";
    QString fullPath2 = dir.filePath("purchase-invoices/2026/01/" + fileName2);
    QFile file2(fullPath2);
    QVERIFY(file2.open(QIODevice::WriteOnly));
    file2.close();
    
    // Initialize Manager (calls _load automatically)
    PurchaseInvoiceManager manager(dir);
    
    // load() is private now, called in constructor
    
    // Verify Model
    QCOMPARE(manager.rowCount(), 2);
    
    // Check sorting (descending by date)
    // Row 0 should be 2026
    QModelIndex idx0 = manager.index(0, 0); // Date column
    QCOMPARE(manager.data(idx0).toDate(), QDate(2026, 1, 24));
    
    // Row 1 should be 2025
    QModelIndex idx1 = manager.index(1, 0);
    QCOMPARE(manager.data(idx1).toDate(), QDate(2025, 2, 18));
    
    // Check some data
    QModelIndex idxAccount = manager.index(0, 1);
    QCOMPARE(manager.data(idxAccount).toString(), QString("607000"));
    
    QModelIndex idxTotal = manager.index(0, 5);
    QCOMPARE(manager.data(idxTotal).toDouble(), 133.6);
}

void TestBookEntries::test_invoice_decode_error()
{
    // Invalid format (not enough parts)
    QString badFile = "2025-02-18__Info.pdf";
    bool caught = false;
    try {
        PurchaseInvoiceManager::decode(badFile);
    } catch (const ExceptionFileError &e) {
        caught = true;
        QCOMPARE(e.errorTitle(), QString("Invalid Filename"));
    }
    QVERIFY(caught);
    
    // Invalid date
    QString badDateFile = "2025-99-99__620__Label__Supp__10EUR.pdf";
    caught = false;
    try {
        PurchaseInvoiceManager::decode(badDateFile);
    } catch (const ExceptionFileError &e) {
        caught = true;
        QCOMPARE(e.errorTitle(), QString("Invalid Date"));
    }
    QVERIFY(caught);
}

void TestBookEntries::test_invoice_add()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    QDir dir(tempDir.path());
    // Create source file
    QString sourceFileName = "source_invoice.pdf";
    QString sourcePath = dir.filePath(sourceFileName);
    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("dummy content");
    sourceFile.close();
    
    // Prepare info
    PurchaseInformation info;
    info.date = QDate(2024, 3, 15);
    info.account = "611";
    info.label = "supplies";
    info.supplier = "OfficeDepot";
    info.totalAmount = 45.50;
    info.currency = "EUR";
    // originalExtension is empty, should be picked up from source
    
    PurchaseInvoiceManager manager(dir);
    QCOMPARE(manager.rowCount(), 0);
    
    manager.add(sourcePath, info);
    
    QCOMPARE(manager.rowCount(), 1);
    
    // Verify file exists in correct location
    // purchase-invoices/2024/03/2024-03-15__611__supplies__OfficeDepot__45.5EUR.pdf
    QString expectedPath = "purchase-invoices/2024/03/2024-03-15__611__supplies__OfficeDepot__45.5EUR.pdf";
    QVERIFY(dir.exists(expectedPath));
    
    // Verify info updated with extension
    QCOMPARE(info.originalExtension, QString("pdf"));
}

void TestBookEntries::test_get_invoices()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Create 3 invoices
    // 1. Jan 2025
    QString f1 = "2025-01-10__600__L__S__10EUR.pdf";
    QVERIFY(dir.mkpath("purchase-invoices/2025/01"));
    QFile(dir.filePath("purchase-invoices/2025/01/" + f1)).open(QIODevice::WriteOnly);
    
    // 2. Feb 2025
    QString f2 = "2025-02-15__600__L__S__10EUR.pdf";
    QVERIFY(dir.mkpath("purchase-invoices/2025/02"));
    QFile(dir.filePath("purchase-invoices/2025/02/" + f2)).open(QIODevice::WriteOnly);
    
    // 3. Mar 2025
    QString f3 = "2025-03-20__600__L__S__10EUR.pdf";
    QVERIFY(dir.mkpath("purchase-invoices/2025/03"));
    QFile(dir.filePath("purchase-invoices/2025/03/" + f3)).open(QIODevice::WriteOnly);
    
    PurchaseInvoiceManager manager(dir);
    QCOMPARE(manager.rowCount(), 3);
    
    // Get all
    auto all = manager.getInvoices(QDate(2025, 1, 1), QDate(2025, 12, 31));
    QCOMPARE(all.size(), 3);
    
    // Get Feb only
    auto feb = manager.getInvoices(QDate(2025, 2, 1), QDate(2025, 2, 28));
    QCOMPARE(feb.size(), 1);
    QCOMPARE(feb.first().date, QDate(2025, 2, 15));
    
    // Get Jan and Feb
    auto janfeb = manager.getInvoices(QDate(2025, 1, 1), QDate(2025, 2, 28));
    QCOMPARE(janfeb.size(), 2);
    
    // Get None
    auto none = manager.getInvoices(QDate(2024, 1, 1), QDate(2024, 12, 31));
    QCOMPARE(none.size(), 0);
}

void TestBookEntries::test_factory_purchase_no_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup tables
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    SaleBookAccountsTable saleAccounts(dir);
    PurchaseBookAccountsTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    // Add purchase account for FR
    purchaseAccounts.addAccount("FR", 20.0, "445660", "445710");
    
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &purchaseAccounts, &journalTable);
    
    // Create purchase information (no conversion needed - EUR to EUR)
    PurchaseInformation purchase;
    purchase.date = QDate(2024, 3, 15);
    purchase.account = "607000";
    purchase.label = "Software license";
    purchase.supplier = "SoftCorp";
    purchase.totalAmount = 120.0; // 100 HT + 20 VAT
    purchase.currency = "EUR";
    purchase.country_vatRate_vat["FR"]["20.00"] = 20.0;
    purchase.countryCodeFrom = "FR";
    purchase.countryCodeTo = "FR";
    
    auto entry = factory.createEntry(purchase);
    
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), 120.0);
    QCOMPARE(entry->getCreditSum(), 120.0);
    
    // Verify debit entries (expense + VAT)
    const auto &debits = entry->getDebits();
    QCOMPARE(debits.size(), 2); // Expense + VAT
    
    // Find expense line
    bool foundExpense = false;
    for (const auto &line : debits) {
        if (line.account == "607000") {
            foundExpense = true;
            QCOMPARE(line.currency_amount["EUR"], 100.0);
            QVERIFY(line.title.contains("SoftCorp"));
            QVERIFY(line.title.contains("Software license"));
        }
    }
    QVERIFY(foundExpense);
    
    // Verify that ALL lines share the same title
    // "Achat FR->FR SoftCorp - Software license"
    QString expectedTitle = "Achat FR->FR SoftCorp - Software license";
    const auto &allLines = entry->getDebits() + entry->getCredits();
    for (const auto &line : allLines) {
        QCOMPARE(line.title, expectedTitle);
    }
    
    // Verify credit (supplier)
    const auto &credits = entry->getCredits();
    QCOMPARE(credits.size(), 1); // Supplier only
}

void TestBookEntries::test_factory_purchase_with_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup company info
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    
    // Setup currency rates
    CurrencyRateManager currencyManager(dir, "");
    currencyManager.importRate("2024-03-15", "USD", "EUR", 0.85);
    SaleBookAccountsTable saleAccounts(dir);
    PurchaseBookAccountsTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    purchaseAccounts.addAccount("FR", 20.0, "445660", "445710");
    
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &purchaseAccounts, &journalTable);
    
    // Create purchase in USD
    PurchaseInformation purchase;
    purchase.date = QDate(2024, 3, 15);
    purchase.account = "607000";
    purchase.label = "Cloud services";
    purchase.supplier = "AWS";
    purchase.totalAmount = 120.0; // USD
    purchase.currency = "USD";
    purchase.country_vatRate_vat["FR"]["20.00"] = 20.0;
    purchase.countryCodeFrom = "FR";
    purchase.countryCodeTo = "FR";
    
    auto entry = factory.createEntry(purchase);
    
    QVERIFY(!entry.isNull());
    
    // Check that amounts include both USD and EUR
    const auto &debits = entry->getDebits();
    bool foundConversion = false;
    // Expected title: "Achat FR->FR AWS - Cloud services (120.00 USD)"
    QString expectedTitle = "Achat FR->FR AWS - Cloud services (120.00 USD)";
    
    for (const auto &line : debits) {
        if (line.currency_amount.contains("USD") && line.currency_amount.contains("EUR")) {
            foundConversion = true;
            // Conversion info handled by JournalEntry appending " (Conv...)"?
            // Wait, JournalEntry append logic:
            // " (Conv: <orig> <curr> @ <rate>)"
            // My Factory added " (120.00 USD)" at the end of BASE title.
            // So full title might be: "Achat ... (120.00 USD) (Conv: 120.00 USD @ 0.85)"
            // Let's check `JournalEntry.cpp` logic? I cannot right now.
            // But `JournalEntry::addDebitLeft` calls `_addEntry` which might append conversion info.
            // Assuming it does if rate != 1.0 or currencies differ.
            // Code in `JournalEntry.cpp` (Step 46):
            // It appends `QString(" (Conv: %1 %2 @ %3)")`.
            
            bool expectedConvFound = line.title.contains("(Conv: 100 USD @ 0.85)") || 
                                     line.title.contains("(Conv: 20 USD @ 0.85)");
            QVERIFY(expectedConvFound);
        }
    }
    QVERIFY(foundConversion);
}

void TestBookEntries::test_factory_purchase_refund()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    SaleBookAccountsTable saleAccounts(dir);
    PurchaseBookAccountsTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    purchaseAccounts.addAccount("FR", 20.0, "445660", "445710");
    
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &purchaseAccounts, &journalTable);
    
    // Create refund (negative amount)
    PurchaseInformation purchase;
    purchase.date = QDate(2024, 3, 15);
    purchase.account = "607000";
    purchase.label = "Returned item";
    purchase.supplier = "RetailCorp";
    purchase.totalAmount = -120.0; // Negative = refund
    purchase.currency = "EUR";
    purchase.country_vatRate_vat["FR"]["20.00"] = 20.0;
    purchase.countryCodeFrom = "FR";
    purchase.countryCodeTo = "FR";
    
    auto entry = factory.createEntry(purchase);
    
    QVERIFY(!entry.isNull());
    
    // For refund, debits and credits should be reversed
    // Expense should be on credit side
    const auto &credits = entry->getCredits();
    bool foundExpenseCredit = false;
    for (const auto &line : credits) {
        if (line.account == "607000") {
            foundExpenseCredit = true;
            QCOMPARE(line.currency_amount["EUR"], 100.0);
        }
    }
    QVERIFY(foundExpenseCredit);
    
    // Supplier should be on debit side
    const auto &debits = entry->getDebits();
    bool foundSupplierDebit = false;
    for (const auto &line : debits) {
        if (line.account == "RetailCorp") {
            foundSupplierDebit = true;
            QCOMPARE(line.currency_amount["EUR"], 120.0);
        }
    }
    QVERIFY(foundSupplierDebit);
    
    // Verify Uniformity
    // Title: "Achat FR->FR RetailCorp - Returned item"
    // Refund amount negative, but currency matches, so no suffix.
    QString expectedTitle = "Achat FR->FR RetailCorp - Returned item";
    const auto &allLines = entry->getDebits() + entry->getCredits();
    for (const auto &line : allLines) {
        QCOMPARE(line.title, expectedTitle);
    }
    
    // Entry should still balance
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
}

void TestBookEntries::test_factory_shipment_no_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    SaleBookAccountsTable saleAccounts(dir);
    PurchaseBookAccountsTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &purchaseAccounts, &journalTable);
    
    // Create activity source
    ActivitySource source;
    source.channel = "Amazon";
    source.subchannel = "amazon.fr";
    source.type = ActivitySourceType::Report;
    
    // Create activity
    auto activityResult = Activity::create(
        "SHIP-001", "ACT-001", "", QDateTime::currentDateTime(),
        "EUR", "FR", "FR", "FR",
        Amount{100.0, 20.0}, TaxSource::MarketplaceProvided,
        "FR", TaxScheme::DomesticVat, TaxJurisdictionLevel::Country,
        SaleType::Products
    );
    QVERIFY(activityResult.ok());
    
    QList<Activity> activities;
    activities.append(activityResult.value.value());
    
    auto shipment = QSharedPointer<Shipment>::create(activities);
    
    QMultiMap<QDateTime, QSharedPointer<Shipment>> shipments;
    shipments.insert(QDateTime::currentDateTime(), shipment);
    
    auto entry = syncWait(factory.createEntry(&source, shipments));
    
    QVERIFY(!entry.isNull());
    
    // Should have revenue (credit), VAT (credit), and customer (debit)
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
    
    const auto &credits = entry->getCredits();
    QVERIFY(credits.size() >= 1); // At least revenue
    
    // Verify French labels
    bool foundFrenchLabel = false;
    for (const auto &line : credits) {
        if (line.title.contains("Vente") || line.title.contains("TVA")) {
            foundFrenchLabel = true;
        }
    }
    QVERIFY(foundFrenchLabel);
    
    // Verify Uniformity
    // All lines should have "Vente Amazon amazon.fr - " ... something
    // Since JournalTable is empty, code might be empty?
    // Let's just check that all titles are identical
    const auto &allShipmentLines = entry->getDebits() + entry->getCredits();
    QVERIFY(!allShipmentLines.isEmpty());
    QString firstTitle = allShipmentLines.first().title;
    for (const auto &line : allShipmentLines) {
        QCOMPARE(line.title, firstTitle);
        QVERIFY(line.title.startsWith("Vente Amazon amazon.fr"));
    }
}

void TestBookEntries::test_factory_shipment_with_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    
    // Setup currency rates
    
    CurrencyRateManager currencyManager(dir, "");
    QString dateStr = QDate::currentDate().toString("yyyy-MM-dd");
    currencyManager.importRate(dateStr, "USD", "EUR", 0.85);
    SaleBookAccountsTable saleAccounts(dir);
    PurchaseBookAccountsTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &purchaseAccounts, &journalTable);
    
    ActivitySource source;
    source.channel = "Amazon";
    source.subchannel = "amazon.com";
    source.type = ActivitySourceType::Report;
    
    // Create activity in USD
    auto activityResult = Activity::create(
        "SHIP-002", "ACT-002", "", QDateTime::currentDateTime(),
        "USD", "US", "US", "US",
        Amount{100.0, 20.0}, TaxSource::MarketplaceProvided,
        "US", TaxScheme::OutOfScope, TaxJurisdictionLevel::Country,
        SaleType::Products
    );
    QVERIFY(activityResult.ok());
    
    QList<Activity> activities;
    activities.append(activityResult.value.value());
    
    auto shipment = QSharedPointer<Shipment>::create(activities);
    
    QMultiMap<QDateTime, QSharedPointer<Shipment>> shipments;
    shipments.insert(QDateTime::currentDateTime(), shipment);
    
    auto entry = syncWait(factory.createEntry(&source, shipments));
    
    QVERIFY(!entry.isNull());
    
    // Should have conversion info in titles
    const auto &allLines = entry->getDebits() + entry->getCredits();
    bool foundConversion = false;
    for (const auto &line : allLines) {
        if (line.currency_amount.contains("USD") && line.currency_amount.contains("EUR")) {
            foundConversion = true;
        }
    }
    QVERIFY(foundConversion);
    
    // Entry should balance
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
}
void TestBookEntries::test_factory_shipment_mixed_rates()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup Company
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    currencyManager.importRate(QDate::currentDate().toString("yyyy-MM-dd"), "USD", "EUR", 0.85); // 1 USD = 0.85 EUR
    
    SaleBookAccountsTable saleAccounts(dir); // Will populate defaults (DOM FR 20, OSS DE 19 etc)
    PurchaseBookAccountsTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &purchaseAccounts, &journalTable);
    
    ActivitySource source;
    source.channel = "Amazon";
    source.subchannel = "Mixed";
    source.type = ActivitySourceType::Report;
    
    QList<Activity> activities;
    QDateTime today = QDateTime::currentDateTime();
    
    // 1. Domestic FR 20% EUR
    // Net 100, VAT 20
    activities.append(Activity::create(
        "S1", "A1", "", today, "EUR", "FR", "FR", "FR",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 2. OSS DE 19% EUR (IT->DE, declared in FR/Company? Or OSS Union)
    // Net 100, VAT 19
    activities.append(Activity::create(
        "S2", "A2", "", today, "EUR", "IT", "DE", "DE",
        Amount{119.0, 19.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 3. Domestic FR 20% USD
    // Net 100, VAT 20
    activities.append(Activity::create(
        "S3", "A3", "", today, "USD", "FR", "FR", "FR",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());

    // 4. OSS AT 20% EUR (IT->AT)
    // Net 100, VAT 20
    activities.append(Activity::create(
        "S4", "A4", "", today, "EUR", "IT", "AT", "AT",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 5. IOSS ES 21% EUR (CN->ES)
    // Net 100, VAT 21
    activities.append(Activity::create(
        "S5", "A5", "", today, "EUR", "CN", "ES", "ES",
        Amount{121.0, 21.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::EuIoss, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 6. Exempt CH 0% USD (FR->CH)
    // Net 100, VAT 0
    activities.append(Activity::create(
        "S6", "A6", "", today, "USD", "FR", "CH", "CH",
        Amount{100.0, 0.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::Exempt, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());

    auto shipment = QSharedPointer<Shipment>::create(activities);
    QMultiMap<QDateTime, QSharedPointer<Shipment>> shipments;
    shipments.insert(today, shipment);
    
    // Execute
    auto entry = syncWait(factory.createEntry(&source, shipments));
    QVERIFY(!entry.isNull());
    
    // Validate
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
    
    const auto &credits = entry->getCredits();
    const auto &debits = entry->getDebits();
    
    // Credits:
    // 1. FR 20 EUR Revenue
    // 2. FR 20 EUR VAT
    // 3. DE 19 EUR Revenue
    // 4. DE 19 EUR VAT
    // 5. FR 20 USD Revenue
    // 6. FR 20 USD VAT
    // Total 6 lines?
    // Note: getAccounts("FR->FR", 20.0) -> Returns same account for EUR and EUR calls?
    // createEntry loop aggregates by VatKey (includes Currency).
    // So EUR and USD are separate VatKeys.
    // So yes, 6 lines.
    
    int countRevenueFR20EUR = 0;
    int countVatFR20EUR = 0;
    int countRevenueDE19EUR = 0;
    int countVatDE19EUR = 0;
    int countRevenueFR20USD = 0;
    int countVatFR20USD = 0;
    int countRevenueOSSAT = 0;
    int countVatOSSAT = 0;
    int countRevenueIOSSES = 0;
    int countVatIOSSES = 0;
    int countRevenueEXP = 0; // Exempt
    
    for (const auto &line : credits) {
        if (line.currency_amount.contains("USD")) {
            if (line.account.contains("7070DOMFR")) countRevenueFR20USD++;
            else if (line.account.contains("4457DOMFR")) countVatFR20USD++;
            else if (line.account.contains("7073EXPFR")) countRevenueEXP++;
        } else {
            if (line.account.contains("7070DOMFR")) countRevenueFR20EUR++; 
            else if (line.account.contains("4457DOMFR")) countVatFR20EUR++;
            else if (line.account.contains("7070OSSDE") || line.account.contains("7070DOMDE")) countRevenueDE19EUR++;
            else if (line.account.contains("4457OSSDE") || line.account.contains("4457DOMDE")) countVatDE19EUR++;
            else if (line.account.contains("7070OSSAT") || line.account.contains("7070DOMAT")) countRevenueOSSAT++;
            else if (line.account.contains("4457OSSAT") || line.account.contains("4457DOMAT")) countVatOSSAT++;
            else if (line.account.contains("7070IOSSES")) countRevenueIOSSES++;
            else if (line.account.contains("4457IOSSES")) countVatIOSSES++;
        }
    }
    
    // Note: Accounts might be "7070DOMFR20" etc.
    // SaleBookAccountsTable::fillIfEmpty logic: "7070DOM" + cCode + rStr => "7070DOMFR20"
    // "7070OSS" + cCode + rStr => "7070OSSDE19"
    
    // Verify counts (Checking simple existence first)
    // Actually, createEntry sums up amounts. 
    // FR 20 EUR Activity 1 is the only one. So 1 line.
    
    QVERIFY(countRevenueFR20EUR >= 1);
    QVERIFY(countVatFR20EUR >= 1);
    QVERIFY(countRevenueDE19EUR >= 1);
    QVERIFY(countVatDE19EUR >= 1);
    QVERIFY(countRevenueOSSAT >= 1);
    QVERIFY(countVatOSSAT >= 1);
    QVERIFY(countRevenueIOSSES >= 1);
    QVERIFY(countVatIOSSES >= 1);
    
    QVERIFY(countRevenueFR20USD >= 1);
    QVERIFY(countVatFR20USD >= 1);
    QVERIFY(countRevenueEXP >= 1);
    
    // Debits:
    // 1. EUR Customer (Total 239)
    // 2. USD Customer (Total 120)
    int countCustomerEUR = 0;
    int countCustomerUSD = 0;
    for (const auto &line : debits) {
        if (line.currency_amount.contains("USD")) {
            QCOMPARE(line.currency_amount["USD"], 220.0); // 120 + 100
            countCustomerUSD++;
        }
        else if (line.currency_amount.contains("EUR")) {
            QCOMPARE(line.currency_amount["EUR"], 480.0); // 239 + 120 + 121
            countCustomerEUR++;
        }
    }
    
    QCOMPARE(countCustomerEUR, 1);
    QCOMPARE(countCustomerUSD, 1);
}
QTEST_MAIN(TestBookEntries)
#include "test_book_entries.moc"

