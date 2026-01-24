#include <QtTest>
#include "books/JournalEntry.h"
#include "books/ExceptionBookEquality.h"
#include "books/JournalEntry.h"
#include "books/ExceptionBookEquality.h"
#include "books/PurchaseInvoiceManager.h"
#include "books/ExceptionFileError.h"

class TestBookEntries : public QObject
{
    Q_OBJECT

private slots:
    void test_journal_entry_simple();
    void test_rounding_within_limit();
    void test_rounding_exceeds_limit();
    void test_currency_conversion();
    void test_currency_conversion_custom_delta();
    void test_invoice_encoding();
    void test_invoice_model_loading();
    void test_invoice_decode_error();
    void test_invoice_add();
};

void TestBookEntries::test_journal_entry_simple()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Debit 100 EUR
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales Revenue";
    debitLine.account = "707000";
    debitLine.amount = 100.0;
    
    // Credit 100 EUR
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512000";
    creditLine.amount = 100.0;
    
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
    debitLine.amount = 100.00;
    entry.addDebitLeft(debitLine, "EUR");

    // Credit 99.99 (0.01 difference < 0.1 limit)
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.amount = 99.99;
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
    QCOMPARE(lastCredit.amount, 0.01);
    
    // Verification of rounding entry existence is implied by no exception and successful completion 
    // since the class corrects it internally effectively.
    // Further inspection would require getters on JournalEntry if we wanted to verify the exact entries.
    // Assuming the requirement "Create tst_book_entries.cpp to test this class... including rounding success" 
    // implies detecting that it passed without error.
}

void TestBookEntries::test_rounding_exceeds_limit()
{
    JournalEntry entry(QDate::currentDate(), "EUR");

    JournalEntry::EntryLine debitLine{"Sales", "707", 100.00};
    entry.addDebitLeft(debitLine, "EUR");

    JournalEntry::EntryLine creditLine{"Bank", "512", 99.00}; // 1.00 difference > 0.1
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
    JournalEntry::EntryLine debitLine{"Sales USD", "707", 100.00};
    entry.addDebitLeft(debitLine, "USD");
    
    // Credit explicitly in EUR matching the conversion
    JournalEntry::EntryLine creditLine{"Bank EUR", "512", 85.00};
    entry.addCreditRight(creditLine, "EUR");

    try {
        entry.convertRoundingIfNeeded(0.85);
    } catch (const ExceptionBookEquality &e) {
        QFAIL(e.what());
    }

    const auto &debits = entry.getDebits();
    QCOMPARE(debits.size(), 1);
    // 100 * 0.85 = 85.00
    QCOMPARE(debits[0].amount, 85.00);
    // Check title modification
    // Title should contain " (Conv: 100 USD @ 0.85)"
    QVERIFY(debits[0].title.contains("(Conv: 100 USD @ 0.85)"));
}

void TestBookEntries::test_currency_conversion_custom_delta()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Scenario where rounding diff is larger than default 0.1 but we allow it
    // Debit 100.00 EUR
    JournalEntry::EntryLine debitLine{"A", "1", 100.00};
    entry.addDebitLeft(debitLine, "EUR");
    
    // Credit 99.50 EUR -> Diff 0.50
    JournalEntry::EntryLine creditLine{"B", "2", 99.50};
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

void TestBookEntries::test_invoice_encoding()
{
    // Test Case 1: Standard example
    QString fileName = "2025-02-18__622600__compta__SOCIC__FR-TVA-13.6EUR__81.6EUR.pdf";
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName);
    
    QCOMPARE(info.date, QDate(2025, 2, 18));
    QCOMPARE(info.account, QString("622600"));
    QCOMPARE(info.label, QString("compta"));
    QCOMPARE(info.supplier, QString("SOCIC"));
    QCOMPARE(info.vatTokens.size(), 1);
    QCOMPARE(info.vatTokens.first(), QString("FR-TVA-13.6EUR"));
    QCOMPARE(info.totalAmount, 81.6);
    QCOMPARE(info.currency, QString("EUR"));
    QCOMPARE(info.originalExtension, QString("pdf"));
    
    // Roundtrip
    QString encoded = PurchaseInvoiceManager::encode(info);
    QCOMPARE(encoded, fileName);
    
    // Test Case 2: Multiple VATs and different extension
    QString fileName2 = "2026-01-24__607000__stock__YISHUN__FR-TVA5.5-13.6EUR__FR-TVA20-20.0EUR__133.6EUR.jpg";
    PurchaseInformation info2 = PurchaseInvoiceManager::decode(fileName2);
    
    QCOMPARE(info2.date, QDate(2026, 1, 24));
    QCOMPARE(info2.account, QString("607000"));
    QCOMPARE(info2.vatTokens.size(), 2);
    QCOMPARE(info2.vatTokens[0], QString("FR-TVA5.5-13.6EUR"));
    QCOMPARE(info2.vatTokens[1], QString("FR-TVA20-20.0EUR"));
    QCOMPARE(info2.totalAmount, 133.6);
    
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
    info.date = QDate(2025, 3, 15);
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
    // purchase-invoices/2025/03/2025-03-15__611__supplies__OfficeDepot__45.5EUR.pdf
    QString expectedPath = "purchase-invoices/2025/03/2025-03-15__611__supplies__OfficeDepot__45.5EUR.pdf";
    QVERIFY(dir.exists(expectedPath));
    
    // Verify info updated with extension
    QCOMPARE(info.originalExtension, QString("pdf"));
}

QTEST_MAIN(TestBookEntries)
#include "tst_book_entries.moc"
