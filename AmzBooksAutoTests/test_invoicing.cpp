#include <QTest>
#include <QTemporaryDir>

#include "books/InvoiceGenerator.h"
#include "books/CompanyInfosTable.h"
#include "books/CompanyAddressTable.h"
#include "books/VatNumbersTable.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/ServiceClientManager.h"
#include "books/VatResolver.h"
#include "CurrencyRateManager.h"
#include "books/TaxResolver.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"
#include "orders/OrderManager.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "orders/Address.h"
#include "orders/Shipment.h"

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
    void test_getNextInvoiceNumbers_twoOrdersSameContext();

    // Delete and recreate tests
    void test_deleteAndRecreateInvoice();

    // Persistence and model tests
    void test_persistence();
    void test_sort();
    void test_columnCount();
    void test_headerData();

    // Invoice generation tests
    void test_generateInvoice();

    // "Facture d'origine" correctness tests
    void test_twoIndependentSales_noFractureOrigine();
    void test_refundSale_hasFractureOrigine();

    // vatOnPayment flag tests
    void test_vatOnPayment_defaultFalse();

    // Regeneration tests
    void test_regenerateInvoices();

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

    QString invoiceNum = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr", "ship-1");
    
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
    
    QString inv1 = generator.getBaseInvoiceNumber(QDate(2026, 1, 1), context, "Amazon", "amazon.fr", "ship-jan");
    QString inv2 = generator.getBaseInvoiceNumber(QDate(2026, 2, 1), context, "Amazon", "amazon.fr", "ship-feb");
    
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
    
    QString inv1 = generator.getBaseInvoiceNumber(date, context1, "Amazon", "amazon.fr", "ship-fr");
    QString inv2 = generator.getBaseInvoiceNumber(date, context2, "Amazon", "amazon.de", "ship-de");
    
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
    
    QString inv1 = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr", "ship-1");
    QString inv2 = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr", "ship-2");
    QString inv3 = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr", "ship-3");
    
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
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, std::nullopt, {"order-1"});

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
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, existingInvoice, {"order-A", "order-A"});

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
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.de", invoicesToDo, existingInvoice, {"ord", "ord", "ord", "ord"});

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
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, std::nullopt, {});

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
    
    QStringList result = generator.getNextInvoiceNumbers(date, context, "Amazon", "amazon.fr", invoicesToDo, existingInvoice, {"ord-1", "ord-1", "ord-1"});

    // VERIFY 34: Correct size
    QCOMPARE(result.size(), 3);
    
    // VERIFY 35: First uses existing
    QCOMPARE(result[0], QString("EXIST-001"));
    
    // VERIFY 36: Second is empty (invoiceToDo = false)
    QVERIFY(result[1].isEmpty());
    
    // VERIFY 37: Third gets revision
    QCOMPARE(result[2], QString("EXIST-001-R01"));
}

void TestInvoicing::test_getNextInvoiceNumbers_twoOrdersSameContext()
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

    // Two different orders grouped under the same tax context
    QList<bool> invoicesToDo = {true, true};

    QStringList result = generator.getNextInvoiceNumbers(
        date, context, "Amazon", "amazon.fr", invoicesToDo, std::nullopt,
        {"order-1", "order-2"});

    // VERIFY 48: Two distinct orders each receive their own sequential number
    QCOMPARE(result.size(), 2);
    QVERIFY(result[0].endsWith("-001"));
    QVERIFY(result[1].endsWith("-002"));

    // VERIFY 49: The two numbers share the same prefix but are different
    QVERIFY(result[0] != result[1]);

    // Now simulate a refund for order-1: it should receive the original
    // invoice number with -R01 appended, not a new sequential number.
    QList<bool> refundTodo = {true, true};
    QStringList refundResult = generator.getNextInvoiceNumbers(
        date, context, "Amazon", "amazon.fr", refundTodo, std::nullopt,
        {"order-1", "order-1"});

    // VERIFY 50: The base entry returns the cached original number for order-1
    QCOMPARE(refundResult[0], result[0]);

    // VERIFY 51: The refund entry gets -R01 appended to the original number
    QCOMPARE(refundResult[1], result[0] + "-R01");
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
        
        QString inv1 = generator.getBaseInvoiceNumber(QDate(2026, 10, 1), context, "Amazon", "amazon.fr", "inv-1");
        QString inv2 = generator.getBaseInvoiceNumber(QDate(2026, 10, 2), context, "Amazon", "amazon.fr", "inv-2");
        
        // Trigger save by generating invoice (mocking PDF path)
        auto resInfo = InvoicingInfo::create(nullptr, {}, "DUMMY");
        QVERIFY(resInfo.ok());
        InvoicingInfo info = *resInfo.value;
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
    generator.getBaseInvoiceNumber(QDate(2026, 12, 1), context, "Amazon", "amazon.fr", "ship-dec");
    generator.getBaseInvoiceNumber(QDate(2026, 11, 1), context, "Amazon", "amazon.fr", "ship-nov");
    generator.getBaseInvoiceNumber(QDate(2026, 10, 1), context, "Amazon", "amazon.fr", "ship-oct");
    
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
    QString invoiceNum = generator.getBaseInvoiceNumber(date, context, "Amazon", "amazon.fr", "inv-001");
    
    // Create InvoicingInfo
    // Create InvoicingInfo
    // We need a valid object to pass to generateInvoice.
    // Accessing result value directly. 
    InvoicingInfo info = *InvoicingInfo::create(nullptr, {}, invoiceNum).value; // Safe because we provide number
    
    // Add some items... but LineItem logic is complex to mock without full object graph?
    // InvoicingInfo stores LineItems. 
    // We need to create LineItem.
    // LineItem::create returns Result<LineItem>.
    auto resItem = LineItem::create("SKU1", "Product 1", 100.0, 0.20, 2);
    if (resItem.ok()) {
        QList<LineItem> items;
        items.append(*resItem.value);
        // We can create a new info with items + number
        auto resInfoWithItems = InvoicingInfo::create(nullptr, items, invoiceNum);
        if (resInfoWithItems.ok()) info = *resInfoWithItems.value;
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

// ========== DELETE AND RECREATE TESTS ==========

void TestInvoicing::test_deleteAndRecreateInvoice()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1); // ensure at least one address row for generateInvoice
    CurrencyRateManager currencyRates(tempDir.path(), "");
    OrderManager orderManager(tempDir.path());

    // One single InvoiceGenerator instance used throughout the whole test,
    // matching the real-world scenario where the generator lives for the
    // lifetime of the application session.
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);

    TaxResolver::TaxContext context;
    context.taxScheme = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "FR";
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo = "FR";

    const QDate date(2026, 2, 15);
    const QString saleId = "svc-del-recreate-001";

    // === First creation ===
    // Simulates: ServiceSalesBooksTable::createSale → InvoiceGenerator::getBaseInvoiceNumber
    QString inv1 = generator.getBaseInvoiceNumber(date, context, "Service", "", saleId);

    // VERIFY 52: first invoice number was assigned
    QVERIFY(!inv1.isEmpty());

    // VERIFY 53: it is the first invoice in this context
    QVERIFY(inv1.endsWith("-001"));

    // Generate the invoice PDF (saves inv1 to CSV and records in OrderManager)
    auto lineItemRes = LineItem::create("SVC", "Software Dev", 600.0, 0.20, 1);
    QVERIFY(lineItemRes.ok());
    QList<LineItem> items = {*lineItemRes.value};
    auto resInfo = InvoicingInfo::create(nullptr, items, inv1);
    QVERIFY(resInfo.ok());
    InvoicingInfo info = *resInfo.value;
    Address addr("Client Corp", "1 Rue Test", "", "", "Paris", "75001", "FR", "", "", "", "", "");
    const QString pdfPath1 = tempDir.filePath("inv1.pdf");
    generator.generateInvoice(inv1, "", pdfPath1, addr, info, saleId, orderManager);

    // VERIFY 54: PDF was created (which means CSV was saved and OrderManager was updated)
    QVERIFY(QFile::exists(pdfPath1));

    // VERIFY 55: generator holds exactly one record after save
    QCOMPARE(generator.rowCount(), 1);

    // === Delete ===
    // Simulates: ServiceSalesBooksTable::remove → m_invoiceGenerator->removeInvoiceByNumber
    generator.removeInvoiceByNumber(inv1);

    // VERIFY 56: the record is gone from the generator (and from the CSV)
    QCOMPARE(generator.rowCount(), 0);

    // === Second creation with the same saleId ===
    // Simulates: ServiceSalesBooksTable::createSale called again for the same sale
    QString inv2 = generator.getBaseInvoiceNumber(date, context, "Service", "", saleId);

    // VERIFY 57: the regenerated number is IDENTICAL to the original
    QCOMPARE(inv2, inv1);

    // Generate the second invoice to confirm the full round-trip
    auto resInfo2 = InvoicingInfo::create(nullptr, items, inv2);
    QVERIFY(resInfo2.ok());
    InvoicingInfo info2 = *resInfo2.value;
    const QString pdfPath2 = tempDir.filePath("inv2.pdf");
    generator.generateInvoice(inv2, "", pdfPath2, addr, info2, saleId, orderManager);

    // VERIFY 58: second PDF was also created
    QVERIFY(QFile::exists(pdfPath2));

    // VERIFY 59: a different shipment afterwards advances the counter correctly
    QString inv3 = generator.getBaseInvoiceNumber(date, context, "Service", "", "svc-del-recreate-002");
    QVERIFY(inv3.endsWith("-002"));
}

// ===========================================================================
// test_twoIndependentSales_noFractureOrigine
// Two service sales with different order IDs, same date and amount.
// When generating invoices for both, neither should have a "Facture d'origine"
// (i.e. neither invoice number should carry a -R revision suffix, and the
// prevNumber derived from the fixed logic must be empty for both).
// ===========================================================================
void TestInvoicing::test_twoIndependentSales_noFractureOrigine()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("Client A", "Consulting", "FR", "FR12345", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    VatNumbersTable vatNumbers(tempDir.path());
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates, &vatNumbers);

    ServiceSalesBooksTable serviceTable(nullptr, &orderManager, tempDir.path());

    const QDate date(2026, 2, 15);
    const double amount = 600.0;

    // Two independent sales: same date and amount, but different order IDs
    serviceTable.createSale(&clientManager, 0, date, amount, "EUR",
                            "ORDER-A", "Consulting", 1, "706000", vatResolver, taxResolver);
    serviceTable.createSale(&clientManager, 0, date, amount, "EUR",
                            "ORDER-B", "Consulting", 1, "706000", vatResolver, taxResolver);

    QCOMPARE(serviceTable.rowCount(), 2);

    // Simulate the PaneBookKeeping::generateInvoices logic
    auto noInvMap = orderManager.get_channel_site_ShipmentAndRefundsNoInvoices(date, date);
    QVERIFY(!noInvMap.isNull());

    int invoicesChecked = 0;
    for (auto chanIt = noInvMap->cbegin(); chanIt != noInvMap->cend(); ++chanIt) {
        const QString channel = chanIt.key();
        for (auto subIt = chanIt->cbegin(); subIt != chanIt->cend(); ++subIt) {
            for (auto ctxIt = subIt->cbegin(); ctxIt != subIt->cend(); ++ctxIt) {
                const TaxResolver::TaxContext &taxCtx = ctxIt.key();
                const OrderManager::ShipmentRefundsWithUpdates &entry = ctxIt.value();
                if (entry.shipmentsRefundsSameActivity.isEmpty()) continue;

                QStringList shipmentIds;
                for (const auto &shipment : entry.shipmentsRefundsSameActivity) {
                    if (shipment && !shipment->getActivities().isEmpty())
                        shipmentIds.append(shipment->getActivities().first().getEventId());
                    else
                        shipmentIds.append(QString());
                }

                std::optional<QString> existingNumber;
                if (entry.invoicingInfo) {
                    auto optNum = entry.invoicingInfo->getInvoiceNumber();
                    if (optNum.has_value() && !optNum->isEmpty())
                        existingNumber = optNum;
                }

                QStringList invoiceNumbers = generator.getNextInvoiceNumbers(
                    date, taxCtx, channel, "", entry.invoicesToDo, existingNumber, shipmentIds);

                QCOMPARE(invoiceNumbers.size(), 2);

                for (const QString &invNum : invoiceNumbers) {
                    QVERIFY(!invNum.isEmpty());

                    // Verify no -R revision suffix (not a refund/revision)
                    const int rIdx = invNum.lastIndexOf("-R");
                    bool isRevision = false;
                    if (rIdx != -1) {
                        bool ok;
                        invNum.mid(rIdx + 2).toInt(&ok);
                        isRevision = ok;
                    }
                    QVERIFY2(!isRevision,
                        qPrintable(QString("Invoice '%1' should not be a revision").arg(invNum)));

                    // Verify the fixed prevNumber logic gives empty prevNumber
                    QString prevNumber;
                    if (rIdx != -1) {
                        bool ok;
                        invNum.mid(rIdx + 2).toInt(&ok);
                        if (ok) prevNumber = invNum.left(rIdx);
                    }
                    QVERIFY2(prevNumber.isEmpty(),
                        qPrintable(QString("Invoice '%1' must not have a 'Facture d'origine'").arg(invNum)));

                    ++invoicesChecked;
                }

                // The two invoice numbers must be distinct sequential numbers
                QVERIFY(invoiceNumbers[0] != invoiceNumbers[1]);
                QVERIFY(invoiceNumbers[0].endsWith("-001") || invoiceNumbers[1].endsWith("-001"));
                QVERIFY(invoiceNumbers[0].endsWith("-002") || invoiceNumbers[1].endsWith("-002"));
            }
        }
    }

    QCOMPARE(invoicesChecked, 2);
}

// ===========================================================================
// test_refundSale_hasFractureOrigine
// When a shipment and its refund share the same order ID, getNextInvoiceNumbers
// returns [base, base-R01]. The fixed prevNumber logic must give:
//   - invoice[0] (base)    → prevNumber = ""
//   - invoice[1] (base-R01) → prevNumber = base (triggers "Facture d'origine")
// ===========================================================================
void TestInvoicing::test_refundSale_hasFractureOrigine()
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

    const QDate date(2026, 2, 15);

    // Same shipmentId appears twice: original shipment + refund
    QList<bool> invoicesToDo = {true, true};
    QStringList shipmentIds = {"ORDER-REFUND", "ORDER-REFUND"};

    QStringList invoiceNumbers = generator.getNextInvoiceNumbers(
        date, context, "Service", "", invoicesToDo, std::nullopt, shipmentIds);

    QCOMPARE(invoiceNumbers.size(), 2);

    const QString &base   = invoiceNumbers[0];
    const QString &refund = invoiceNumbers[1];

    // Base invoice: no revision suffix
    QVERIFY(!base.isEmpty());
    QVERIFY(!base.contains("-R"));

    // Refund invoice: has -R01 suffix
    QVERIFY(refund.endsWith("-R01"));

    // Fixed prevNumber logic applied to the base: must be empty
    {
        QString prevNumber;
        const int rIdx = base.lastIndexOf("-R");
        if (rIdx != -1) {
            bool ok;
            base.mid(rIdx + 2).toInt(&ok);
            if (ok) prevNumber = base.left(rIdx);
        }
        QVERIFY2(prevNumber.isEmpty(),
            "Base invoice must not reference a prior invoice (no 'Facture d'origine')");
    }

    // Fixed prevNumber logic applied to the refund: must equal the base number
    {
        QString prevNumber;
        const int rIdx = refund.lastIndexOf("-R");
        if (rIdx != -1) {
            bool ok;
            refund.mid(rIdx + 2).toInt(&ok);
            if (ok) prevNumber = refund.left(rIdx);
        }
        QCOMPARE(prevNumber, base);
    }
}

// ===========================================================================
// test_vatOnPayment_defaultFalse
// InvoicingInfo created via the factory without specifying vatOnPayment must
// have getVatOnPayment() == false, and the flag must survive a JSON round-trip.
// ===========================================================================
void TestInvoicing::test_vatOnPayment_defaultFalse()
{
    // Default: vatOnPayment is false
    auto res = InvoicingInfo::create(nullptr, {}, QString("INV-VOP-001"));
    QVERIFY(res.ok());
    QVERIFY(!res.value->getVatOnPayment());

    // JSON round-trip: false must not write the key and must reload as false
    QJsonObject json = res.value->toJson();
    QVERIFY(!json.contains("vatOnPayment"));
    InvoicingInfo loaded = InvoicingInfo::fromJson(json);
    QVERIFY(!loaded.getVatOnPayment());

    // When explicitly set to true, the key is written and reloaded correctly
    res.value->setVatOnPayment(true);
    QJsonObject jsonTrue = res.value->toJson();
    QVERIFY(jsonTrue.contains("vatOnPayment"));
    QVERIFY(jsonTrue["vatOnPayment"].toBool());
    InvoicingInfo loadedTrue = InvoicingInfo::fromJson(jsonTrue);
    QVERIFY(loadedTrue.getVatOnPayment());
}

// ===========================================================================
// test_regenerateInvoices
// Full round-trip:
//   1. Generate 3 invoices (2 in range [Jan-Feb 2025], 1 out-of-range [Mar 2025])
//      plus a revision pair also in range.
//   2. Call regenerateInvoices for [Jan-Feb 2025].
//   3. Verify that exactly the in-range PDFs are created under the output folder.
//   4. Verify that all attributes saved in the CSV and in OrderManager are intact
//      after regeneration (invoice number, date, tax scheme, channel, line items,
//      amounts, quantity, address, etc.).
// ===========================================================================
void TestInvoicing::test_regenerateInvoices()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid()); // VERIFY 1

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1); // ensure at least one company address row
    CurrencyRateManager currencyRates(tempDir.path(), "");
    VatNumbersTable vatNumbers(tempDir.path());
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress,
                                &currencyRates, &vatNumbers);

    // -----------------------------------------------------------------------
    // Tax contexts
    // -----------------------------------------------------------------------
    TaxResolver::TaxContext ctx1; // FR DomesticVat, Sale service channel
    ctx1.taxScheme                = TaxScheme::DomesticVat;
    ctx1.taxDeclaringCountryCode  = "FR";
    ctx1.taxJurisdictionLevel     = TaxJurisdictionLevel::Country;
    ctx1.countryCodeVatPaidTo     = "FR";

    TaxResolver::TaxContext ctx2; // DE EuOssUnion, Amazon channel
    ctx2.taxScheme                = TaxScheme::EuOssUnion;
    ctx2.taxDeclaringCountryCode  = "DE";
    ctx2.taxJurisdictionLevel     = TaxJurisdictionLevel::Country;
    ctx2.countryCodeVatPaidTo     = "DE";

    // -----------------------------------------------------------------------
    // Invoice 1 — Jan 2025, FR service sale (IN RANGE)
    // -----------------------------------------------------------------------
    const QDate   date1    = QDate(2025, 1, 15);
    const QString orderId1 = "ORD-REGEN-001";

    auto itemRes1 = LineItem::create("SVC1", "IT Consulting", 500.0, 0.20, 1);
    QVERIFY(itemRes1.ok()); // VERIFY 2

    Address addr1("Client Alpha", "1 Rue Test", "", "", "Paris", "75001", "FR",
                  "FR12345", "", "", "", "");
    orderManager.recordAddressesTo({{orderId1, addr1}});

    QString inv1 = generator.getBaseInvoiceNumber(date1, ctx1, "Sale service", "", orderId1);
    QVERIFY(!inv1.isEmpty()); // VERIFY 3

    auto infoRes1 = InvoicingInfo::create(nullptr, {*itemRes1.value}, inv1);
    QVERIFY(infoRes1.ok()); // VERIFY 4

    // Generate original PDF into a scratch folder (not the regen target)
    QTemporaryDir origDir;
    QVERIFY(origDir.isValid()); // VERIFY 5
    generator.generateInvoice(inv1, "", origDir.filePath("inv1.pdf"),
                               addr1, *infoRes1.value, orderId1, orderManager, date1);
    QVERIFY(QFile::exists(origDir.filePath("inv1.pdf"))); // VERIFY 6

    // -----------------------------------------------------------------------
    // Invoice 2 — Feb 2025, DE Amazon OSS sale (IN RANGE)
    // -----------------------------------------------------------------------
    const QDate   date2    = QDate(2025, 2, 20);
    const QString orderId2 = "ORD-REGEN-002";

    auto itemRes2 = LineItem::create("AMZ2", "Product A", 300.0, 0.19, 2);
    QVERIFY(itemRes2.ok()); // VERIFY 7

    Address addr2("Client Beta", "2 Test Street", "", "", "Berlin", "10115", "DE",
                  "DE98765", "", "", "", "");
    orderManager.recordAddressesTo({{orderId2, addr2}});

    QString inv2 = generator.getBaseInvoiceNumber(date2, ctx2, "Amazon", "amazon.de", orderId2);
    QVERIFY(!inv2.isEmpty()); // VERIFY 8

    auto infoRes2 = InvoicingInfo::create(nullptr, {*itemRes2.value}, inv2);
    QVERIFY(infoRes2.ok()); // VERIFY 9

    generator.generateInvoice(inv2, "", origDir.filePath("inv2.pdf"),
                               addr2, *infoRes2.value, orderId2, orderManager, date2);
    QVERIFY(QFile::exists(origDir.filePath("inv2.pdf"))); // VERIFY 10

    // -----------------------------------------------------------------------
    // Invoice 3 — Mar 2025, FR service sale (OUT OF RANGE)
    // -----------------------------------------------------------------------
    const QDate   date3    = QDate(2025, 3, 10);
    const QString orderId3 = "ORD-REGEN-003";

    auto itemRes3 = LineItem::create("SVC3", "Training", 800.0, 0.20, 4);
    QVERIFY(itemRes3.ok()); // VERIFY 11

    Address addr3("Client Gamma", "3 Avenue", "", "", "Lyon", "69001", "FR",
                  "FR11111", "", "", "", "");
    orderManager.recordAddressesTo({{orderId3, addr3}});

    QString inv3 = generator.getBaseInvoiceNumber(date3, ctx1, "Sale service", "", orderId3);
    auto infoRes3 = InvoicingInfo::create(nullptr, {*itemRes3.value}, inv3);
    QVERIFY(infoRes3.ok()); // VERIFY 12

    generator.generateInvoice(inv3, "", origDir.filePath("inv3.pdf"),
                               addr3, *infoRes3.value, orderId3, orderManager, date3);
    QVERIFY(QFile::exists(origDir.filePath("inv3.pdf"))); // VERIFY 13

    // -----------------------------------------------------------------------
    // Revision pair — Jan 2025, same orderId, base + R01 (IN RANGE)
    // -----------------------------------------------------------------------
    const QDate   dateRev    = QDate(2025, 1, 25);
    const QString orderIdRev = "ORD-REGEN-REV";

    auto itemResRev = LineItem::create("SVCR", "Analysis", 200.0, 0.20, 3);
    QVERIFY(itemResRev.ok()); // VERIFY 14

    Address addrRev("Client Delta", "4 Impasse", "", "", "Bordeaux", "33000", "FR",
                    "FR77777", "", "", "", "");
    orderManager.recordAddressesTo({{orderIdRev, addrRev}});

    // Use getNextInvoiceNumbers to produce base + revision in one call
    QList<bool>  revToDo = {true, true};
    QStringList  revSids = {orderIdRev, orderIdRev};
    QStringList  revNums = generator.getNextInvoiceNumbers(
        dateRev, ctx1, "Sale service", "", revToDo, std::nullopt, revSids);
    QCOMPARE(revNums.size(), 2); // VERIFY 15
    QVERIFY(revNums[1].endsWith("-R01")); // VERIFY 16

    auto infoResRevBase = InvoicingInfo::create(nullptr, {*itemResRev.value}, revNums[0]);
    QVERIFY(infoResRevBase.ok()); // VERIFY 17
    generator.generateInvoice(revNums[0], "", origDir.filePath("invRevBase.pdf"),
                               addrRev, *infoResRevBase.value, orderIdRev, orderManager, dateRev);
    QVERIFY(QFile::exists(origDir.filePath("invRevBase.pdf"))); // VERIFY 18

    auto infoResRevR = InvoicingInfo::create(nullptr, {*itemResRev.value}, revNums[1]);
    QVERIFY(infoResRevR.ok()); // VERIFY 19
    generator.generateInvoice(revNums[1], revNums[0], origDir.filePath("invRevR01.pdf"),
                               addrRev, *infoResRevR.value, orderIdRev, orderManager, dateRev);
    QVERIFY(QFile::exists(origDir.filePath("invRevR01.pdf"))); // VERIFY 20

    // Generator now holds 5 records (inv1, inv2, inv3, revBase, revR01)
    QCOMPARE(generator.rowCount(), 5); // VERIFY 21

    // -----------------------------------------------------------------------
    // Verify CSV attributes saved for inv1 before regeneration
    // -----------------------------------------------------------------------
    bool foundInv1 = false;
    for (int row = 0; row < generator.rowCount(); ++row) {
        QString num = generator.data(generator.index(row, InvoiceGenerator::ColInvoiceNumber)).toString();
        if (num == inv1) {
            foundInv1 = true;
            // VERIFY 22: Date attribute correct in CSV
            QCOMPARE(generator.data(generator.index(row, InvoiceGenerator::ColDate)).toDate(), date1);
            // VERIFY 23: TaxDeclaringCountry correct in CSV
            QCOMPARE(generator.data(generator.index(row, InvoiceGenerator::ColTaxDeclaringCountry)).toString(),
                     QString("FR"));
            // VERIFY 24: TaxScheme correct in CSV
            QCOMPARE(generator.data(generator.index(row, InvoiceGenerator::ColTaxScheme)).toString(),
                     QString("DomesticVat"));
            // VERIFY 25: Channel correct in CSV
            QCOMPARE(generator.data(generator.index(row, InvoiceGenerator::ColChannel)).toString(),
                     QString("Sale service"));
            // VERIFY 26: CountryVatPaidTo correct in CSV
            QCOMPARE(generator.data(generator.index(row, InvoiceGenerator::ColCountryVatPaidTo)).toString(),
                     QString("FR"));
            break;
        }
    }
    QVERIFY(foundInv1); // VERIFY 27

    // -----------------------------------------------------------------------
    // Call regenerateInvoices for Jan–Feb 2025
    // -----------------------------------------------------------------------
    QTemporaryDir regenDir;
    QVERIFY(regenDir.isValid()); // VERIFY 28

    const QDate dateFrom(2025, 1, 1);
    const QDate dateTo(2025, 2, 28);
    generator.regenerateInvoices(QDir(regenDir.path()), dateFrom, dateTo, orderManager);

    // Helper to build expected PDF path in the regen output folder
    auto sanitize = [](const QString &s) {
        QString r = s;
        r.replace('/', '-').replace('\\', '-');
        return r;
    };
    auto regenPdf = [&](const QDate &d, const QString &invNum) {
        return regenDir.filePath(QString("%1/%2/%3.pdf")
            .arg(d.year())
            .arg(d.month(), 2, 10, QChar('0'))
            .arg(sanitize(invNum)));
    };

    // VERIFY 29: inv1 regenerated (Jan 2025 — in range)
    QVERIFY(QFile::exists(regenPdf(date1, inv1)));

    // VERIFY 30: inv2 regenerated (Feb 2025 — in range)
    QVERIFY(QFile::exists(regenPdf(date2, inv2)));

    // VERIFY 31: inv3 NOT regenerated (Mar 2025 — out of range)
    QVERIFY(!QFile::exists(regenPdf(date3, inv3)));

    // VERIFY 32: revision base regenerated (Jan 2025 — in range)
    QVERIFY(QFile::exists(regenPdf(dateRev, revNums[0])));

    // VERIFY 33: revision R01 regenerated (Jan 2025 — in range)
    QVERIFY(QFile::exists(regenPdf(dateRev, revNums[1])));

    // VERIFY 34: generator still has 5 records (regeneration must not alter the registry)
    QCOMPARE(generator.rowCount(), 5);

    // -----------------------------------------------------------------------
    // Verify InvoicingInfo attributes preserved in OrderManager after regeneration
    // -----------------------------------------------------------------------
    auto stored1 = orderManager.getInvoicingInfo(orderId1);
    QVERIFY(!stored1.isNull()); // VERIFY 35

    // VERIFY 36: invoice number still correct for order 1
    QVERIFY(stored1->getInvoiceNumber().has_value());
    QCOMPARE(stored1->getInvoiceNumber().value(), inv1); // VERIFY 37

    // VERIFY 38: line item count preserved for order 1
    QCOMPARE(stored1->getItems().size(), 1);

    // VERIFY 39: line item name preserved
    QCOMPARE(stored1->getItems().first().getName(), QString("IT Consulting"));

    // VERIFY 40: line item total taxed (TTC) preserved
    QCOMPARE(stored1->getItems().first().getTotalTaxed(), 500.0);

    // VERIFY 41: line item quantity preserved
    QCOMPARE(stored1->getItems().first().getQuantity(), 1);

    auto stored2 = orderManager.getInvoicingInfo(orderId2);
    QVERIFY(!stored2.isNull()); // VERIFY 42

    // VERIFY 43: invoice number preserved for order 2
    QCOMPARE(stored2->getInvoiceNumber().value(), inv2);

    // VERIFY 44: line item name preserved for order 2
    QCOMPARE(stored2->getItems().first().getName(), QString("Product A"));

    // VERIFY 45: line item quantity preserved for order 2
    QCOMPARE(stored2->getItems().first().getQuantity(), 2);

    // VERIFY 46: line item total taxed (TTC) preserved for order 2
    QCOMPARE(stored2->getItems().first().getTotalTaxed(), 600.0); // 300 TTC × qty 2

    auto stored3 = orderManager.getInvoicingInfo(orderId3);
    QVERIFY(!stored3.isNull()); // VERIFY 47

    // VERIFY 48: invoice number for out-of-range order 3 is unchanged
    QCOMPARE(stored3->getInvoiceNumber().value(), inv3);

    // -----------------------------------------------------------------------
    // Verify address round-trip via getAddressTo for order 1
    // -----------------------------------------------------------------------
    auto reloadedAddr1 = orderManager.getAddressTo(orderId1);
    QVERIFY(!reloadedAddr1.isNull()); // VERIFY 49

    // VERIFY 50: full name round-trip
    QCOMPARE(reloadedAddr1->getFullName(), QString("Client Alpha"));
}

QTEST_MAIN(TestInvoicing)
#include "test_invoicing.moc"
