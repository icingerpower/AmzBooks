#include <QTest>
#include <QTemporaryDir>

#include "books/InvoiceGenerator.h"
#include "books/CompanyInfosTable.h"
#include "books/CompanyAddressTable.h"
#include "CurrencyRateManager.h"
#include "books/TaxResolver.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "orders/Address.h"

class TestInvoicing : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Shortcut generation tests
    void test_shortenTaxScheme();
    void test_shortenTaxJurisdiction();
    void test_shortenChannel();
    void test_shortenStore();

    // getBaseInvoiceNumber tests
    void test_getBaseInvoiceNumber_format();
    void test_getBaseInvoiceNumber_differentDates();
    void test_getBaseInvoiceNumber_differentContexts();
    void test_getBaseInvoiceNumber_sequencing();

    // getNextInvoiceNumbers tests
    void test_getNextInvoiceNumbers_noExisting();
    void test_getNextInvoiceNumbers_withExisting();
    void test_getNextInvoiceNumbers_multipleRevisions();
    void test_getNextInvoiceNumbers_emptyList();
    void test_getNextInvoiceNumbers_mixedInvoicesToDo();

    // Persistence and model tests
    void test_persistence();
    void test_sort();
    void test_columnCount();
    void test_headerData();

    // Invoice generation tests
    void test_generateInvoice();

private:
    QTemporaryDir *m_tempDir = nullptr;
};

void TestInvoicing::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestInvoicing::cleanupTestCase()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

// ========== SHORTCUT GENERATION TESTS ==========

void TestInvoicing::test_shortenTaxScheme()
{
    // VERIFY 1: DomesticVat maps to DOM
    QCOMPARE(InvoiceGenerator::shortenTaxScheme(TaxScheme::DomesticVat), QString("DOM"));
    
    // VERIFY 2: EuOssUnion maps to OSS
    QCOMPARE(InvoiceGenerator::shortenTaxScheme(TaxScheme::EuOssUnion), QString("OSS"));
    
    // VERIFY 3: MarketplaceDeemedSupplier maps to MDS
    QCOMPARE(InvoiceGenerator::shortenTaxScheme(TaxScheme::MarketplaceDeemedSupplier), QString("MDS"));
    
    // VERIFY 4: Unknown maps to UNK
    QCOMPARE(InvoiceGenerator::shortenTaxScheme(TaxScheme::Unknown), QString("UNK"));
    
    // VERIFY 5: Exempt maps to EXE
    QCOMPARE(InvoiceGenerator::shortenTaxScheme(TaxScheme::Exempt), QString("EXE"));
    
    // VERIFY 6: OutOfScope maps to OOS
    QCOMPARE(InvoiceGenerator::shortenTaxScheme(TaxScheme::OutOfScope), QString("OOS"));
}

void TestInvoicing::test_shortenTaxJurisdiction()
{
    // VERIFY 7: Country maps to CTY
    QCOMPARE(InvoiceGenerator::shortenTaxJurisdiction(TaxJurisdictionLevel::Country), QString("CTY"));
    
    // VERIFY 8: Territory maps to TER
    QCOMPARE(InvoiceGenerator::shortenTaxJurisdiction(TaxJurisdictionLevel::Territory), QString("TER"));
    
    // VERIFY 9: Unknown maps to UNK
    QCOMPARE(InvoiceGenerator::shortenTaxJurisdiction(TaxJurisdictionLevel::Unknown), QString("UNK"));
}

void TestInvoicing::test_shortenChannel()
{
    // VERIFY 10: Amazon maps to AMZ
    QCOMPARE(InvoiceGenerator::shortenChannel("Amazon"), QString("AMZ"));
    
    // VERIFY 11: Temu maps to TMU
    QCOMPARE(InvoiceGenerator::shortenChannel("Temu"), QString("TMU"));
    
    // VERIFY 12: Unknown channel falls back to first 3 chars uppercase
    QCOMPARE(InvoiceGenerator::shortenChannel("MyCustomChannel"), QString("MYC"));
}

void TestInvoicing::test_shortenStore()
{
    // VERIFY 13: amazon.fr maps to FR
    QCOMPARE(InvoiceGenerator::shortenStore("amazon.fr"), QString("FR"));
    
    // VERIFY 14: amazon.de maps to DE
    QCOMPARE(InvoiceGenerator::shortenStore("amazon.de"), QString("DE"));
    
    // VERIFY 15: Unknown store tries to extract from TLD
    QCOMPARE(InvoiceGenerator::shortenStore("amazon.xyz"), QString("XYZ"));
}

// ========== getBaseInvoiceNumber TESTS ==========

void TestInvoicing::test_getBaseInvoiceNumber_format()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QDate date(2026, 2, 15);
    
    QString invoiceNum = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr");
    
    // VERIFY 16: Invoice number starts with correct year-month
    QVERIFY(invoiceNum.startsWith("202602"));
    
    // VERIFY 17: Invoice number contains DOM for DomesticVat
    QVERIFY(invoiceNum.contains("DOM"));
    
    // VERIFY 18: Invoice number contains FR for country
    QVERIFY(invoiceNum.contains("-FR-"));
    
    // VERIFY 19: Invoice number ends with sequence 001
    QVERIFY(invoiceNum.endsWith("-001"));
}

void TestInvoicing::test_getBaseInvoiceNumber_differentDates()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QString inv1 = generator.getBaseInvoiceNumber(QDate(2026, 1, 1), context, "Amazon", "amazon.fr");
    QString inv2 = generator.getBaseInvoiceNumber(QDate(2026, 2, 1), context, "Amazon", "amazon.fr");
    
    // VERIFY 20: Different months produce different prefixes
    QVERIFY(inv1.startsWith("202601"));
    QVERIFY(inv2.startsWith("202602"));
}

void TestInvoicing::test_getBaseInvoiceNumber_differentContexts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    QDate date(2026, 3, 1);
    
    // Context 1: Domestic France
    TaxResolver::TaxContext context1;
    context1.taxScheme = TaxScheme::DomesticVat;
    context1.taxDeclaringCountryCode = "FR";
    context1.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context1.countryCodeVatPaidTo = "FR";
    
    // Context 2: EU OSS Germany
    TaxResolver::TaxContext context2;
    context2.taxScheme = TaxScheme::EuOssUnion;
    context2.taxDeclaringCountryCode = "DE";
    context2.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context2.countryCodeVatPaidTo = "DE";
    
    QString inv1 = generator.getBaseInvoiceNumber(date, context1, "Amazon", "amazon.fr");
    QString inv2 = generator.getBaseInvoiceNumber(date, context2, "Amazon", "amazon.de");
    
    // VERIFY 21: Different tax schemes produce different invoice numbers
    QVERIFY(inv1.contains("DOM"));
    QVERIFY(inv2.contains("OSS"));
    
    // VERIFY 22: Different countries produce different parts
    QVERIFY(inv1.contains("-FR-AMZ-FR-"));
    QVERIFY(inv2.contains("-DE-AMZ-DE-"));
}

void TestInvoicing::test_getBaseInvoiceNumber_sequencing()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QDate date(2026, 4, 1);
    
    QString inv1 = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr");
    QString inv2 = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr");
    QString inv3 = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr");
    
    // VERIFY 23: First invoice ends with 001
    QVERIFY(inv1.endsWith("-001"));
    
    // VERIFY 24: Second invoice ends with 002
    QVERIFY(inv2.endsWith("-002"));
    
    // VERIFY 25: Third invoice ends with 003
    QVERIFY(inv3.endsWith("-003"));
}

// ========== getNextInvoiceNumbers TESTS ==========

void TestInvoicing::test_getNextInvoiceNumbers_noExisting()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QDate date(2026, 5, 1);
    QList<bool> invoicesToDo = {true};
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, std::nullopt);
    
    // VERIFY 26: Single shipment with no existing invoice generates new number
    QCOMPARE(result.size(), 1);
    QVERIFY(result[0].startsWith("202605"));
    QVERIFY(result[0].endsWith("-001"));
}

void TestInvoicing::test_getNextInvoiceNumbers_withExisting()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QDate date(2026, 6, 1);
    QList<bool> invoicesToDo = {true, true};
    QString existingInvoice = "MKT-12345"; // Marketplace-generated number
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, existingInvoice);
    
    // VERIFY 27: First shipment uses existing marketplace number
    QCOMPARE(result[0], QString("MKT-12345"));
    
    // VERIFY 28: Second shipment gets revision suffix -R01
    QCOMPARE(result[1], QString("MKT-12345-R01"));
}

void TestInvoicing::test_getNextInvoiceNumbers_multipleRevisions()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::EuOssUnion;
    context.taxDeclaringCountryCode = "DE";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "DE";
    
    QDate date(2026, 7, 1);
    QList<bool> invoicesToDo = {true, true, true, true};
    QString existingInvoice = "DE-INV-001";
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.de", invoicesToDo, existingInvoice);
    
    // VERIFY 29: First uses original
    QCOMPARE(result[0], QString("DE-INV-001"));
    
    // VERIFY 30: Second gets -R01
    QCOMPARE(result[1], QString("DE-INV-001-R01"));
    
    // VERIFY 31: Third gets -R02
    QCOMPARE(result[2], QString("DE-INV-001-R02"));
    
    // VERIFY 32: Fourth gets -R03
    QCOMPARE(result[3], QString("DE-INV-001-R03"));
}

void TestInvoicing::test_getNextInvoiceNumbers_emptyList()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QDate date(2026, 8, 1);
    QList<bool> invoicesToDo; // Empty
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, std::nullopt);
    
    // VERIFY 33: Empty list returns empty result
    QVERIFY(result.isEmpty());
}

void TestInvoicing::test_getNextInvoiceNumbers_mixedInvoicesToDo()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QDate date(2026, 9, 1);
    // Mixed: true, false, true (middle one already has invoice)
    QList<bool> invoicesToDo = {true, false, true};
    QString existingInvoice = "EXIST-001";
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, existingInvoice);
    
    // VERIFY 34: Correct size
    QCOMPARE(result.size(), 3);
    
    // VERIFY 35: First uses existing
    QCOMPARE(result[0], QString("EXIST-001"));
    
    // VERIFY 36: Second is empty (invoiceToDo = false)
    QVERIFY(result[1].isEmpty());
    
    // VERIFY 37: Third gets revision
    QCOMPARE(result[2], QString("EXIST-001-R01"));
}

// ========== PERSISTENCE AND MODEL TESTS ==========

void TestInvoicing::test_persistence()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    // Create and populate
    {

        
        CompanyInfosTable companyInfos(tempDir.path());
        CompanyAddressTable companyAddress(tempDir.path());
        companyAddress.insertRows(0, 1);
        CurrencyRateManager currencyRates(tempDir.path(), "");
        
        InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
        
        TaxResolver::TaxContext context;
        context.taxScheme = TaxScheme::DomesticVat;
        context.taxDeclaringCountryCode = "FR";
        context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
        context.countryCodeVatPaidTo = "FR";
        
        QString inv1 = generator.getBaseInvoiceNumber(QDate(2026, 10, 1), context, "Amazon", "amazon.fr");
        QString inv2 = generator.getBaseInvoiceNumber(QDate(2026, 10, 2), context, "Amazon", "amazon.fr");
        
        // Trigger save by generating invoice (mocking PDF path)
        InvoicingInfo info(nullptr);
        Address addr("", "", "", "", "", "", "", "", "", "", "", "");
        OrderManager orderManager(tempDir.path());
        generator.generateInvoice(inv1, "", tempDir.filePath("inv1.pdf"), addr, info, "ORD-1", orderManager);
        generator.generateInvoice(inv2, "", tempDir.filePath("inv2.pdf"), addr, info, "ORD-2", orderManager);

        // VERIFY 38: Row count before save (and after implicit save via generateInvoice)
        QCOMPARE(generator.rowCount(), 2);
    }
    
    // Reload and verify
    {
        CompanyInfosTable companyInfos(tempDir.path());
        CompanyAddressTable companyAddress(tempDir.path());
        companyAddress.insertRows(0, 1);
        CurrencyRateManager currencyRates(tempDir.path(), "");
        
        InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
        
        // VERIFY 39: Data persisted correctly
        QCOMPARE(generator.rowCount(), 2);
        
        // VERIFY 40: First record data correct
        QModelIndex idx = generator.index(0, InvoiceGenerator::ColInvoiceNumber);
        QString invoiceNum = generator.data(idx).toString();
        QVERIFY(invoiceNum.contains("202610"));
    }
}

void TestInvoicing::test_sort()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    // Add in non-chronological order
    generator.getBaseInvoiceNumber(QDate(2026, 12, 1), context, "Amazon", "amazon.fr");
    generator.getBaseInvoiceNumber(QDate(2026, 11, 1), context, "Amazon", "amazon.fr");
    generator.getBaseInvoiceNumber(QDate(2026, 10, 1), context, "Amazon", "amazon.fr");
    
    // Sort ascending by date
    generator.sort(InvoiceGenerator::ColDate, Qt::AscendingOrder);
    
    // VERIFY 41: First row is October after ascending sort
    QDate firstDate = generator.data(generator.index(0, InvoiceGenerator::ColDate)).toDate();
    QCOMPARE(firstDate, QDate(2026, 10, 1));
    
    // Sort descending by date
    generator.sort(InvoiceGenerator::ColDate, Qt::DescendingOrder);
    
    // VERIFY 42: First row is December after descending sort
    firstDate = generator.data(generator.index(0, InvoiceGenerator::ColDate)).toDate();
    QCOMPARE(firstDate, QDate(2026, 12, 1));
}

void TestInvoicing::test_columnCount()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    // VERIFY 43: Column count is 8 (Date, TaxDeclaringCountry, TaxScheme, TaxJurisdiction, CountryVatPaidTo, Channel, Store, InvoiceNumber)
    QCOMPARE(generator.columnCount(), 8);
}

void TestInvoicing::test_headerData()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    // VERIFY 44: Date header is correct
    QCOMPARE(generator.headerData(InvoiceGenerator::ColDate, Qt::Horizontal).toString(), QString("Date"));
    
    // VERIFY 45: Invoice Number header is correct  
    QCOMPARE(generator.headerData(InvoiceGenerator::ColInvoiceNumber, Qt::Horizontal).toString(), QString("Invoice Number"));
}

void TestInvoicing::test_generateInvoice()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    

    
    // Setup environment
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);
    
    // Create base data
    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";
    
    QDate date(2026, 11, 15);
    
    // Get invoice number (does NOT save to CSV yet)
    QString invoiceNum = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr");
    
    // Create InvoicingInfo
    InvoicingInfo info(nullptr); // no shipment pointer needed for this test part
    info.setInvoiceNumber(invoiceNum);
    // Add some items... but LineItem logic is complex to mock without full object graph?
    // InvoicingInfo stores LineItems. 
    // We need to create LineItem.
    // LineItem::create returns Result<LineItem>.
    auto resItem = LineItem::create("SKU1", "Product 1", 100.0, 0.20, 2);
    if (resItem.ok()) {
        // info.setItems... requires Activity list too.
        // This is getting complicated to unit test without mocking OrderManager/Activities.
        // For now, let's test the file generation part with empty items if allowed,
        // or just minimal set.
    }
    
    Address addr("John Doe", "123 Rue de la Paix", "", "", "Paris", "75000", "FR", "", "", "", "", "");
    QString orderId = "ORD-123";
    QString pdfPath = tempDir.filePath("invoice.pdf");
    QString csvPath = tempDir.filePath("invoices.csv");
    
    // Verify CSV is NOT saved yet (logic change: getBaseInvoiceNumber defers saving)
    // Actually, getBaseInvoiceNumber in my implementation REMOVED the _save() call.
    // So CSV should NOT contain this invoice yet?
    // Wait, the test_persistence checks rowCount... 
    // If I removed _save() from getBaseInvoiceNumber, test_persistence MIGHT FAIL if it relies on _save being called implicitly!
    // Let's check test_persistence:
    // It creates generator 1, calls getBaseInvoiceNumber. Then creates generator 2 and checks rowCount.
    // If _save is removed, generator 2 will have 0 rows!
    // So test_persistence will fail.
    // I MUST fix test_persistence or accept that it only works after generateInvoice.
    // The requirement was: "defer CSV saving ... so if PDF ... fails, we don't have corrupted database".
    // This implies `getBaseInvoiceNumber` purely implicitly updates memory.
    // Persistence only happens on `generateInvoice`.
    // So `test_persistence` needs to call `generateInvoice` to trigger save?
    // Or I should manually trigger save in test? _save is private.
    // So `test_persistence` as written IS INVALID with the new logic.
    // I should update `test_persistence` to call `generateInvoice` or similar.
    
    // Generate Invoice
    OrderManager orderManager(m_tempDir->path());
    generator.generateInvoice(invoiceNum, "", pdfPath, addr, info, orderId, orderManager);
    
    // VERIFY 46: PDF file created
    QVERIFY(QFile::exists(pdfPath));
    
    // VERIFY 47: CSV file created/updated
    QVERIFY(QFile::exists(csvPath));
    
    // Verify content of CSV
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly));
    QString content = csv.readAll();
    QVERIFY(content.contains(invoiceNum));
}

QTEST_MAIN(TestInvoicing)
#include "test_invoicing.moc"
