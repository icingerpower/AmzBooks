#include <QTest>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QCoroTask>

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
#include "books/Activity.h"
#include "orders/OrderManager.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "orders/Address.h"
#include "orders/Shipment.h"
#include "orders/Refund.h"
#include "orders/ActivitySource.h"
#include "orders/ActivitySourceType.h"
#include "orders/SaleType.h"
#include "orders/TaxSource.h"
#include "orders/ImporterFileAmazonVatEu.h"
#include "orders/ImporterFileAmazonFbaInvoicing.h"

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

    // Refund numbering when sale invoice only exists in OrderManager
    void test_twoRefundsAfterSaleInvoiceInOrderManager();

    // Refund invoice numbers inherit parent sale's Amazon invoice number
    // (the refunds must be grouped with their parent sale's shipment)
    void test_refundInvoiceNumbers_synthetic();   // uses synthetic data, no CSV files
    void test_refundInvoiceNumbers_realData();    // loads real CSV files

    // Regression: stale OM entry written under Amazon order ID must not poison lookups
    void test_refundInvoiceNumbers_staleOmData();

    // Regression: service sale getInvoicingInfo must not return null before invoice is generated
    void test_serviceSale_getInvoicingInfo_beforeInvoiceGeneration();

    // Feature: multiple articles and fractional quantity in a service sale
    void test_serviceSale_multipleArticles();
    void test_serviceSale_fractionalQuantity();

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
        generator.generateInvoice(inv1, "", tempDir.filePath("inv1.pdf"), addr, info, "ORD-1", orderManager, QDate(2026, 10, 1));
        generator.generateInvoice(inv2, "", tempDir.filePath("inv2.pdf"), addr, info, "ORD-2", orderManager, QDate(2026, 10, 2));

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
    generator.generateInvoice(invoiceNum, "", pdfPath, addr, info, orderId, orderManager, date);
    
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
    generator.generateInvoice(inv1, "", pdfPath1, addr, info, saleId, orderManager, date);

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
    generator.generateInvoice(inv2, "", pdfPath2, addr, info2, saleId, orderManager, date);

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
    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    serviceTable.createSale(&clientManager, 0, date, "EUR", "ORDER-A", "706000",
                            {Item{"Consulting", amount, 1.0}}, vatResolver, taxResolver);
    serviceTable.createSale(&clientManager, 0, date, "EUR", "ORDER-B", "706000",
                            {Item{"Consulting", amount, 1.0}}, vatResolver, taxResolver);

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

// ===========================================================================
// test_twoRefundsAfterSaleInvoiceInOrderManager
//
// Regression test for the bug where refund invoices receive wrong numbers when
// the sale invoice was stored directly in OrderManager (e.g., imported from
// Amazon FBA invoicing) without going through InvoiceGenerator.
//
// Scenario: one item ordered in qty 2, then refunded in two separate operations.
//   Sale invoice (in OrderManager only): 202601-DOM-IT-AMZ-IT-001
//   Expected refund 1:                   202601-DOM-IT-AMZ-IT-001-R01
//   Expected refund 2:                   202601-DOM-IT-AMZ-IT-001-R02
//
// Bug (before fix):
//   getNextInvoiceNumbers did not find the sale invoice in its own registry and
//   created a fresh base number, yielding -001 and -001-R01 instead of -R01/-R02.
// ===========================================================================
void TestInvoicing::test_twoRefundsAfterSaleInvoiceInOrderManager()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);

    TaxResolver::TaxContext context;
    context.taxScheme               = TaxScheme::DomesticVat;
    context.taxDeclaringCountryCode = "IT";
    context.taxJurisdictionLevel    = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo    = "IT";

    const QDate   date    = QDate(2026, 1, 15);
    const QString orderId = "AMZ-ORDER-IT-001";
    const QString channel = "Amazon";
    const QString store   = "amazon.it";

    // --- STEP 1: Record the sale invoice in OrderManager only ---
    // Simulates a shipment imported via Amazon FBA invoicing: the invoice number
    // comes from Amazon and is persisted in OrderManager.  InvoiceGenerator has
    // never seen this invoice (its registry is still empty).
    const QString saleInvoiceNumber = "202601-DOM-IT-AMZ-IT-001";
    auto itemRes = LineItem::create("ITEM-SKU", "Test Product IT", 100.0, 0.22, 2);
    QVERIFY(itemRes.ok());
    auto saleInfoRes = InvoicingInfo::create(nullptr, {*itemRes.value}, saleInvoiceNumber);
    QVERIFY(saleInfoRes.ok());
    InvoicingInfo saleInfo = *saleInfoRes.value;
    orderManager.recordInvoicingInfo(orderId, &saleInfo);

    // Sanity: InvoiceGenerator has no records yet
    QCOMPARE(generator.rowCount(), 0);

    // Sanity: OrderManager holds the sale invoice number
    auto storedSale = orderManager.getInvoicingInfo(orderId);
    QVERIFY(!storedSale.isNull());
    QVERIFY(storedSale->getInvoiceNumber().has_value());
    QCOMPARE(*storedSale->getInvoiceNumber(), saleInvoiceNumber);

    // --- STEP 2: Generate invoice numbers for 2 refunds (same orderId as the sale) ---
    // This replicates the PaneBookkeeping::generateInvoices() call for the group
    // returned by get_channel_site_ShipmentAndRefundsNoInvoices.
    // The OrderManager parameter lets getNextInvoiceNumbers discover the pre-existing
    // sale invoice and produce -R01 / -R02 instead of a new base number.
    QList<bool>  invoicesToDo = {true, true};
    QStringList  refundIds    = {orderId, orderId};

    QStringList invoiceNumbers = generator.getNextInvoiceNumbers(
        date, context, channel, store, invoicesToDo, std::nullopt, refundIds,
        &orderManager);

    QCOMPARE(invoiceNumbers.size(), 2);

    // VERIFY: first refund must be -R01 (not a fresh -001)
    QCOMPARE(invoiceNumbers[0], saleInvoiceNumber + "-R01");

    // VERIFY: second refund must be -R02 (not -001-R01)
    QCOMPARE(invoiceNumbers[1], saleInvoiceNumber + "-R02");

    // --- STEP 3: Generate the PDFs to confirm the full round-trip ---
    Address addr("Customer IT", "Via Roma 1", "", "", "Roma", "00100", "IT",
                 "", "", "", "", "");
    orderManager.recordAddressesTo({{orderId, addr}});

    auto r1Res = InvoicingInfo::create(nullptr, {*itemRes.value}, invoiceNumbers[0]);
    QVERIFY(r1Res.ok());
    const QString pdf1 = tempDir.filePath(invoiceNumbers[0] + ".pdf");
    generator.generateInvoice(invoiceNumbers[0], saleInvoiceNumber, pdf1,
                               addr, *r1Res.value, orderId, orderManager, date);
    QVERIFY(QFile::exists(pdf1));

    auto r2Res = InvoicingInfo::create(nullptr, {*itemRes.value}, invoiceNumbers[1]);
    QVERIFY(r2Res.ok());
    const QString pdf2 = tempDir.filePath(invoiceNumbers[1] + ".pdf");
    generator.generateInvoice(invoiceNumbers[1], saleInvoiceNumber, pdf2,
                               addr, *r2Res.value, orderId, orderManager, date);
    QVERIFY(QFile::exists(pdf2));
}

// ===========================================================================
// Helper: locate a data sub-directory by climbing the directory tree.
// ===========================================================================
static QString findDataDir(const QString &subPath)
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 7; ++i) {
        QString candidate = dir.absoluteFilePath(subPath);
        if (QFileInfo::exists(candidate))
            return candidate;
        if (!dir.cdUp() || dir.isRoot())
            break;
    }
    return {};
}

// ===========================================================================
// Helper: replicate the generateInvoices logic from PaneBookKeeping for a
// given noInvoicesMap, returning the list of generated invoice numbers for
// entries where invoicesToDo is true.
// ===========================================================================
static QStringList generateInvoiceNumbers(
    const QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext,
        OrderManager::ShipmentRefundsWithUpdates>>> &noInvoicesMap,
    InvoiceGenerator &generator,
    const OrderManager &orderManager,
    const QDate &fallbackDate)
{
    QStringList result;
    for (auto chanIt = noInvoicesMap.cbegin(); chanIt != noInvoicesMap.cend(); ++chanIt) {
        const QString &channel = chanIt.key();
        for (auto storeIt = chanIt.value().cbegin(); storeIt != chanIt.value().cend(); ++storeIt) {
            const QString &store = storeIt.key();
            for (auto ctxIt = storeIt.value().cbegin(); ctxIt != storeIt.value().cend(); ++ctxIt) {
                const TaxResolver::TaxContext &taxContext = ctxIt.key();
                const OrderManager::ShipmentRefundsWithUpdates &entry = ctxIt.value();
                if (entry.shipmentsRefundsSameActivity.isEmpty()) continue;

                QDate date;
                const auto &first = entry.shipmentsRefundsSameActivity.first();
                if (first && !first->getActivities().isEmpty())
                    date = first->getActivities().first().getDateTime().date();
                if (!date.isValid()) date = fallbackDate;

                std::optional<QString> existingNumber;
                if (entry.invoicingInfo) {
                    auto optNum = entry.invoicingInfo->getInvoiceNumber();
                    if (optNum.has_value() && !optNum->isEmpty())
                        existingNumber = optNum;
                }

                QStringList shipmentIds;
                for (const auto &shipment : entry.shipmentsRefundsSameActivity) {
                    if (shipment && !shipment->getActivities().isEmpty())
                        shipmentIds.append(shipment->getActivities().first().getEventId());
                    else
                        shipmentIds.append(QString());
                }

                QStringList invoiceNumbers = generator.getNextInvoiceNumbers(
                    date, taxContext, channel, store,
                    entry.invoicesToDo, existingNumber, shipmentIds, &orderManager);

                for (int i = 0; i < invoiceNumbers.size(); ++i) {
                    if (entry.invoicesToDo.value(i, false) && !invoiceNumbers[i].isEmpty())
                        result.append(invoiceNumbers[i]);
                }
            }
        }
    }
    return result;
}

// ===========================================================================
// test_refundInvoiceNumbers_synthetic
//
// Reproduces the PaneBookKeeping::generateInvoices bug using synthetic data:
//   - One sale shipment (SHIP-001, order ORDER-001) with external invoice AMZINV-001
//   - Two refunds (REF-001, REF-002) for the same order
//
// Expected: refunds receive AMZINV-001-R01 and AMZINV-001-R02.
// Bug (before fix): each refund is in its own group → each gets a fresh
//   sequential base number instead of inheriting AMZINV-001.
// ===========================================================================
void TestInvoicing::test_refundInvoiceNumbers_synthetic()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    ActivitySource source{ActivitySourceType::Report, "Amazon", "amazon.it", "SyntheticTest"};

    const QDate saleDate(2026, 1, 27);
    const QDate refDate1(2026, 1, 28);
    const QDate refDate2(2026, 1, 29);

    // Sale: eventId = order ID, activityId = shipment ID
    auto saleActRes = Activity::create(
        "ORDER-001", "SHIP-001", "",
        QDateTime(saleDate, QTime(10, 0)), QDateTime(saleDate, QTime(10, 0)),
        "EUR", "IT", "IT", false, "IT",
        Amount(4.91, 1.08),
        TaxSource::MarketplaceProvided, "IT",
        TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products);
    QVERIFY(saleActRes.ok());
    Shipment sale(QList<Activity>{*saleActRes.value}, QString(), true);

    // Refund 1
    auto ref1ActRes = Activity::create(
        "ORDER-001", "REF-001", "",
        QDateTime(refDate1, QTime(10, 0)), QDateTime(refDate1, QTime(10, 0)),
        "EUR", "IT", "IT", false, "IT",
        Amount(-2.46, -0.54),
        TaxSource::MarketplaceProvided, "IT",
        TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products);
    QVERIFY(ref1ActRes.ok());
    Refund ref1(QList<Activity>{*ref1ActRes.value}, QString(), true);

    // Refund 2
    auto ref2ActRes = Activity::create(
        "ORDER-001", "REF-002", "",
        QDateTime(refDate2, QTime(10, 0)), QDateTime(refDate2, QTime(10, 0)),
        "EUR", "IT", "IT", false, "IT",
        Amount(-2.45, -0.54),
        TaxSource::MarketplaceProvided, "IT",
        TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products);
    QVERIFY(ref2ActRes.ok());
    Refund ref2(QList<Activity>{*ref2ActRes.value}, QString(), true);

    // Record all via recordShipmentsFromSource (as PaneOrderFiles does, using eventId as orderId)
    QList<OrderManager::ShipmentFromSourceEntry> entries;
    entries.append({"ORDER-001", &sale,  QDate(), false, false});
    entries.append({"ORDER-001", &ref1,  QDate(), false, false});
    entries.append({"ORDER-001", &ref2,  QDate(), false, false});
    orderManager.recordShipmentsFromSource(&source, entries);

    Address addr("Customer IT", "Via Roma 1", "", "", "Roma", "00100", "IT", "", "", "", "", "");
    orderManager.recordAddressesTo({{"ORDER-001", addr}});
    QDate pubDate1(2026, 1, 31);
    orderManager.publish(pubDate1);

    // Record sale's external invoice number (simulates Amazon FBA invoicing import)
    auto itemRes = LineItem::create("ITEM-SKU", "Ice Pack Set", 4.91, 0.22, 1);
    QVERIFY(itemRes.ok());
    auto saleInfoRes = InvoicingInfo::create(nullptr, {*itemRes.value}, "AMZINV-001");
    QVERIFY(saleInfoRes.ok());
    InvoicingInfo saleInfo = *saleInfoRes.value;
    orderManager.recordInvoicingInfo("SHIP-001", &saleInfo);

    // -----------------------------------------------------------------------
    // STEP 1: Verify getShipmentAndRefundsNoInvoices groups all three together
    // Expected (after fix): 1 group with sale(invoiceToDo=false) + 2 refunds(true)
    // Bug (before fix): 2 separate groups (one per refund), sale not included
    // -----------------------------------------------------------------------
    auto noInvList = orderManager.getShipmentAndRefundsNoInvoices(
        QDate(2025, 1, 1), QDate(2026, 12, 31));
    QVERIFY(!noInvList.isNull());
    QCOMPARE(noInvList->size(), 1); // must be 1 group for order ORDER-001

    const auto &group = noInvList->first();
    // First entry (ordered by event_date) is the sale, which has an invoice
    QCOMPARE(group.shipmentsRefundsSameActivity.size(), 3);
    QCOMPARE(group.invoicesToDo.value(0), false); // sale — has invoice
    QCOMPARE(group.invoicesToDo.value(1), true);  // refund 1
    QCOMPARE(group.invoicesToDo.value(2), true);  // refund 2
    QVERIFY(group.invoicingInfo != nullptr);
    QVERIFY(group.invoicingInfo->getInvoiceNumber().has_value());
    QCOMPARE(*group.invoicingInfo->getInvoiceNumber(), QString("AMZINV-001"));

    // -----------------------------------------------------------------------
    // STEP 2: Generate invoice numbers (mirrors PaneBookKeeping::generateInvoices)
    // -----------------------------------------------------------------------
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);

    auto noInvoicesMap = orderManager.get_channel_site_ShipmentAndRefundsNoInvoices(
        QDate(2025, 1, 1), QDate(2026, 12, 31));
    QVERIFY(!noInvoicesMap.isNull());
    QVERIFY(!noInvoicesMap->isEmpty());

    QStringList invoiceNumbers = generateInvoiceNumbers(
        *noInvoicesMap, generator, orderManager, QDate(2026, 1, 1));

    // Must generate exactly 2 invoice numbers: AMZINV-001-R01 and AMZINV-001-R02
    QCOMPARE(invoiceNumbers.size(), 2);
    QVERIFY(invoiceNumbers.contains(QStringLiteral("AMZINV-001-R01")));
    QVERIFY(invoiceNumbers.contains(QStringLiteral("AMZINV-001-R02")));
}

// ===========================================================================
// test_refundInvoiceNumbers_realData
//
// Loads the real vat-eu-2026-01.csv and invoicing-fba-ue_2026-02-08.csv
// files and verifies that:
//   - Order 404-4309379-2683555 has two refunds
//   - getShipmentAndRefundsNoInvoices returns a group where the first
//     shipment is UXLjbQj0f with invoice IT60000NENVBFT
//   - The two refunds get invoice numbers IT60000NENVBFT-R01 and
//     IT60000NENVBFT-R02
// ===========================================================================
void TestInvoicing::test_refundInvoiceNumbers_realData()
{
    const QString vatDir = findDataDir("data/amazon-vat-reports/2026");
    const QString fbaDir = findDataDir("data/amazon-fba-invoicing/2026");

    if (vatDir.isEmpty() || fbaDir.isEmpty()) {
        QSKIP("Real data directories not found — skipping test_refundInvoiceNumbers_realData");
    }

    const QString vatFile = vatDir + "/vat-eu-2026-01.csv";
    const QString fbaFile = fbaDir + "/invoicing-fba-ue_2026-02-08.csv";

    if (!QFileInfo::exists(vatFile) || !QFileInfo::exists(fbaFile)) {
        QSKIP("Real CSV files not found — skipping test_refundInvoiceNumbers_realData");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    OrderManager orderManager(tempDir.path());

    // Helper: record importer results into OrderManager the same way PaneOrderFiles does
    auto importAndRecord = [&](AbstractImporterFile &importer, const QString &filePath) {
        AbstractImporter::ReturnOrderInfos res;
        try {
            res = QCoro::waitFor(importer.loadReport(filePath));
        } catch (...) {
            QFAIL("Exception while loading CSV");
        }
        QVERIFY2(res.errorReturned.isEmpty(), qPrintable(res.errorReturned));
        QVERIFY(res.orderInfos);

        ActivitySource source = importer.getActivitySource();

        QList<OrderManager::ShipmentFromSourceEntry> entries;
        for (const auto &ship : res.orderInfos->shipments)
            entries.append({ship.getActivities().first().getEventId(), &ship, QDate(), importer.isWrongIfConflict(), false});
        for (const auto &ref : res.orderInfos->refunds)
            entries.append({ref.getActivities().first().getEventId(), &ref, QDate(), importer.isWrongIfConflict(), false});
        orderManager.recordShipmentsFromSource(&source, entries);

        QHash<QString, Address> addrMap;
        for (const auto &addr : res.orderInfos->orderAddresses)
            addrMap.insert(addr.orderId, addr.address);
        orderManager.recordAddressesTo(addrMap);

        for (const auto &inv : res.orderInfos->invoicingInfos)
            orderManager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
    };

    // Import FBA invoicing first (shipments with line items), then VAT EU
    // (adds invoice numbers and imports refunds)
    {
        ImporterFileAmazonFbaInvoicing fbaImporter(tempDir.path());
        importAndRecord(fbaImporter, fbaFile);
    }
    {
        ImporterFileAmazonVatEu vatImporter(tempDir.path());
        importAndRecord(vatImporter, vatFile);
    }

    QDate pubDate(2026, 3, 1);
    orderManager.publish(pubDate);

    // -----------------------------------------------------------------------
    // STEP 1: Verify getShipmentAndRefundsNoInvoices for order 404-4309379-2683555
    // The group should contain:
    //   - UXLjbQj0f (sale, invoiceToDo=false, invoice=IT60000NENVBFT) — first by date
    //   - refund 1 (invoiceToDo=true)
    //   - refund 2 (invoiceToDo=true)
    // -----------------------------------------------------------------------
    auto noInvList = orderManager.getShipmentAndRefundsNoInvoices(
        QDate(2025, 1, 1), QDate(2026, 12, 31));
    QVERIFY(!noInvList.isNull());

    // Find the group for order 404-4309379-2683555
    const OrderManager::ShipmentRefundsWithUpdates *targetGroup = nullptr;
    for (const auto &grp : *noInvList) {
        if (grp.invoicingInfo &&
            grp.invoicingInfo->getInvoiceNumber().has_value() &&
            *grp.invoicingInfo->getInvoiceNumber() == QLatin1String("IT60000NENVBFT")) {
            targetGroup = &grp;
            break;
        }
    }
    QVERIFY2(targetGroup != nullptr,
             "No group found with invoicingInfo.invoiceNumber == IT60000NENVBFT");

    // First shipment (ordered by event_date) should be UXLjbQj0f (the sale)
    QVERIFY(!targetGroup->shipmentsRefundsSameActivity.isEmpty());
    const auto &firstShip = targetGroup->shipmentsRefundsSameActivity.first();
    QVERIFY(firstShip);
    QCOMPARE(firstShip->getId(), QStringLiteral("UXLjbQj0f"));
    QCOMPARE(targetGroup->invoicesToDo.value(0), false); // sale has invoice already

    // Must have exactly 3 entries: sale + 2 refunds
    QCOMPARE(targetGroup->shipmentsRefundsSameActivity.size(), 3);
    QCOMPARE(targetGroup->invoicesToDo.value(1), true);
    QCOMPARE(targetGroup->invoicesToDo.value(2), true);

    // -----------------------------------------------------------------------
    // STEP 2: Generate invoice numbers (mirrors PaneBookKeeping::generateInvoices)
    // -----------------------------------------------------------------------
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);

    auto noInvoicesMap = orderManager.get_channel_site_ShipmentAndRefundsNoInvoices(
        QDate(2025, 1, 1), QDate(2026, 12, 31));
    QVERIFY(!noInvoicesMap.isNull());
    QVERIFY(!noInvoicesMap->isEmpty());

    QStringList invoiceNumbers = generateInvoiceNumbers(
        *noInvoicesMap, generator, orderManager, QDate(2026, 1, 1));

    // Find the two invoice numbers for order 404-4309379-2683555
    QStringList orderInvNumbers;
    for (const QString &n : invoiceNumbers) {
        if (n.startsWith(QStringLiteral("IT60000NENVBFT")))
            orderInvNumbers.append(n);
    }

    QCOMPARE(orderInvNumbers.size(), 2);
    QVERIFY(orderInvNumbers.contains(QStringLiteral("IT60000NENVBFT-R01")));
    QVERIFY(orderInvNumbers.contains(QStringLiteral("IT60000NENVBFT-R02")));
}

// ===========================================================================
// test_refundInvoiceNumbers_staleOmData
//
// Regression test for the bug where generateInvoice() recorded invoicing info
// under the Amazon order ID (getEventId()) instead of the shipment root ID
// (getActivityId()).  On subsequent runs, getInvoicingInfo() found this stale
// entry first and used its wrong invoice number as the base, producing numbers
// like "202601-DOM-IT-AMZ-AMA-001-R01-R05" instead of "IT600007ENVBFT-R01".
//
// Fix: getInvoicingInfo() now checks whether the primary-lookup result was stored
// under an Amazon order ID (detected by the presence of shipments with
// order_id = rootId).  If so, it falls through to the order-level fallback,
// which returns the correct sale invoice stored under the real shipment root.
//
// Additionally, generateInvoice() now accepts an optional shipmentId parameter
// so that PaneBookKeeping can record under the actual shipment root, preventing
// stale entries from accumulating.
// ===========================================================================
void TestInvoicing::test_refundInvoiceNumbers_staleOmData()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ActivitySource source{ActivitySourceType::Report, "Amazon", "Amazon EU", "StaleTest"};
    OrderManager orderManager(tempDir.path());

    const QString amazonOrderId = "ORDER-STALE-001";
    const QString shipmentId    = "SHIP-STALE-001";
    const QString saleInvoice   = "IT600007ENVBFT";
    // Simulates the wrong entry written by a previous broken run of generateInvoice()
    const QString staleInvoice  = "202601-DOM-IT-AMZ-AMA-001-R01-R04";

    // --- Step 1: Add the sale shipment to the DB ---
    const QDate saleDate(2026, 1, 27);
    auto saleActRes = Activity::create(
        amazonOrderId, shipmentId, "",
        QDateTime(saleDate, QTime(10, 0)), QDateTime(saleDate, QTime(10, 0)),
        "EUR", "IT", "IT", false, "IT",
        Amount(100.0, 22.0),
        TaxSource::MarketplaceProvided, "IT",
        TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products);
    QVERIFY(saleActRes.ok());
    Shipment sale(QList<Activity>{*saleActRes.value}, QString(), true);

    QList<OrderManager::ShipmentFromSourceEntry> entries;
    entries.append({amazonOrderId, &sale, QDate(), false, false});
    orderManager.recordShipmentsFromSource(&source, entries);
    QDate pubDate(2026, 1, 31);
    orderManager.publish(pubDate);

    // --- Step 2: Record the correct Amazon FBA invoice under the SHIPMENT root ---
    auto itemRes = LineItem::create("ITEM-SKU", "Product", 100.0, 0.22, 1);
    QVERIFY(itemRes.ok());
    auto saleInfoRes = InvoicingInfo::create(nullptr, {*itemRes.value}, saleInvoice);
    QVERIFY(saleInfoRes.ok());
    orderManager.recordInvoicingInfo(shipmentId, &*saleInfoRes.value);

    // --- Step 3: Simulate the stale entry from a previous broken run ---
    // Previous generateInvoice() called recordInvoicingInfo(orderId, ...) with a
    // wrong generated number.  This entry must NOT poison the lookup.
    auto staleInfoRes = InvoicingInfo::create(nullptr, {*itemRes.value}, staleInvoice);
    QVERIFY(staleInfoRes.ok());
    orderManager.recordInvoicingInfo(amazonOrderId, &*staleInfoRes.value);

    // --- Step 4: Verify getInvoicingInfo returns the CORRECT sale invoice ---
    // Before fix: primary lookup finds the stale entry and returns staleInvoice.
    // After fix:  guard detects amazonOrderId is an order ID, falls through to
    //             order-level fallback, which returns saleInvoice via SHIP-STALE-001.
    auto info = orderManager.getInvoicingInfo(amazonOrderId);
    QVERIFY(!info.isNull());
    QVERIFY(info->getInvoiceNumber().has_value());
    QCOMPARE(*info->getInvoiceNumber(), saleInvoice); // Must be IT600007ENVBFT, NOT staleInvoice

    // --- Step 5: Verify getNextInvoiceNumbers generates correct revision numbers ---
    TaxResolver::TaxContext context;
    context.taxDeclaringCountryCode = "IT";
    context.taxScheme               = TaxScheme::DomesticVat;
    context.taxJurisdictionLevel    = TaxJurisdictionLevel::Country;
    context.countryCodeVatPaidTo    = "IT";

    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates);

    QList<bool>  invoicesToDo = {true, true};
    QStringList  refundIds    = {amazonOrderId, amazonOrderId};

    QStringList invoiceNumbers = generator.getNextInvoiceNumbers(
        saleDate, context, "Amazon", "Amazon EU",
        invoicesToDo, std::nullopt, refundIds, &orderManager);

    QCOMPARE(invoiceNumbers.size(), 2);
    // Must produce -R01 and -R02 relative to the CORRECT sale invoice,
    // NOT -R05/-R06 relative to the stale wrong number.
    QCOMPARE(invoiceNumbers[0], saleInvoice + "-R01");
    QCOMPARE(invoiceNumbers[1], saleInvoice + "-R02");
}

// ===========================================================================
// test_serviceSale_getInvoicingInfo_beforeInvoiceGeneration
//
// Regression: for a service sale the shipment id equals the order id (both are
// the orderId).  The guard inside getInvoicingInfo used to ask
//   SELECT 1 FROM shipments WHERE order_id = <rootId>
// which found the service sale's own row (because its order_id = id = orderId)
// and mistakenly concluded that rootId was an "Amazon order ID", falling
// through to the fallback that only returns info when an invoice number
// already exists.  Result: getInvoicingInfo() returned nullptr for a brand-new
// service sale → "Missing invoicing info for …" during invoice generation.
//
// After the fix the guard additionally checks id != rootId, so the service
// sale's own row no longer triggers the false-positive and the invoicing info
// is returned correctly.
// ===========================================================================
void TestInvoicing::test_serviceSale_getInvoicingInfo_beforeInvoiceGeneration()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("GRDF", "Energy consulting", "FR", "FR12345", "EUR");

    VatResolver vatResolver(tempDir.path());
    vatResolver.addRate(QDate(2026, 1, 1), "FR", SaleType::Service, 0.20);

    TaxResolver taxResolver(tempDir.path());

    ServiceSalesBooksTable serviceTable(nullptr, &orderManager, tempDir.path());

    const QDate date(2026, 1, 1);
    const QString orderId = "2620045-ICINGER-GRDF-2026-01";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    serviceTable.createSale(&clientManager, 0, date, "EUR", orderId, "706000",
                            {Item{"Energy consulting", 1200.0, 1.0}},
                            vatResolver, taxResolver);

    // Before fix: getInvoicingInfo returned nullptr because the guard query
    // found the service sale's own shipment and mistook the orderId for an
    // Amazon order ID, then fell through to the fallback that requires an
    // invoice number to already exist.
    auto info = orderManager.getInvoicingInfo(orderId);
    QVERIFY2(!info.isNull(),
        "getInvoicingInfo must return the InvoicingInfo recorded by createSale "
        "even before an invoice number has been assigned");

    // The info must carry the line item recorded by createSale
    QVERIFY(!info->getItems().isEmpty());

    // The invoice number must not be set yet (it is assigned later by generateInvoice)
    QVERIFY(!info->getInvoiceNumber().has_value() || info->getInvoiceNumber()->isEmpty());
}

// ===========================================================================
// test_serviceSale_multipleArticles
//
// A sale with two articles must produce an InvoicingInfo that carries both
// line items and whose total matches the sum of the individual amounts.
// ===========================================================================
void TestInvoicing::test_serviceSale_multipleArticles()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("ACME", "Consulting", "FR", "FR99999", "EUR");

    VatResolver vatResolver(tempDir.path());
    vatResolver.addRate(QDate(2026, 1, 1), "FR", SaleType::Service, 0.20);

    TaxResolver taxResolver(tempDir.path());

    ServiceSalesBooksTable serviceTable(nullptr, &orderManager, tempDir.path());

    const QDate date(2026, 3, 1);
    const QString orderId = "MULTI-ARTICLE-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    serviceTable.createSale(&clientManager, 0, date, "EUR", orderId, "706000",
                            {Item{"Consulting day",  1000.0, 2.0},
                             Item{"Travel expenses",  150.0, 1.0}},
                            vatResolver, taxResolver);

    QCOMPARE(serviceTable.rowCount(), 1);

    auto info = orderManager.getInvoicingInfo(orderId);
    QVERIFY2(!info.isNull(), "InvoicingInfo must exist after createSale");

    // Two line items must be stored
    QCOMPARE(info->getItems().size(), 2);
    QCOMPARE(info->getItems()[0].getName(), QString("Consulting day"));
    QCOMPARE(info->getItems()[1].getName(), QString("Travel expenses"));
    QCOMPARE(info->getItems()[0].getQuantity(), 2.0);
    QCOMPARE(info->getItems()[1].getQuantity(), 1.0);

    // Total TTC must equal 2*1000 + 1*150 = 2150
    const double expectedTotal = 2.0 * 1000.0 + 1.0 * 150.0;
    double actualTotal = 0.0;
    for (const auto &item : info->getItems())
        actualTotal += item.getTotalTaxed();
    QVERIFY(qAbs(actualTotal - expectedTotal) < 0.01);
}

// ===========================================================================
// test_serviceSale_fractionalQuantity
//
// A line item with quantity 1.5 must be stored, serialised, and reloaded
// with the same value — ensuring the double round-trip through JSON is correct.
// ===========================================================================
void TestInvoicing::test_serviceSale_fractionalQuantity()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("Beta Corp", "Training", "FR", "FR11111", "EUR");

    VatResolver vatResolver(tempDir.path());
    vatResolver.addRate(QDate(2026, 1, 1), "FR", SaleType::Service, 0.20);

    TaxResolver taxResolver(tempDir.path());

    ServiceSalesBooksTable serviceTable(nullptr, &orderManager, tempDir.path());

    const QDate date(2026, 4, 1);
    const QString orderId = "FRAC-QTY-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    serviceTable.createSale(&clientManager, 0, date, "EUR", orderId, "706000",
                            {Item{"Training session", 800.0, 1.5}},
                            vatResolver, taxResolver);

    // Quantity must survive the round-trip through OrderManager (JSON)
    auto info = orderManager.getInvoicingInfo(orderId);
    QVERIFY(!info.isNull());
    QVERIFY(!info->getItems().isEmpty());
    QCOMPARE(info->getItems().first().getQuantity(), 1.5);

    // JSON round-trip for the LineItem itself
    auto lineItemRes = LineItem::create("SKU", "Test", 800.0, 0.20, 1.5);
    QVERIFY(lineItemRes.ok());
    const LineItem &original = *lineItemRes.value;
    QCOMPARE(original.getQuantity(), 1.5);

    QJsonObject json = original.toJson();
    LineItem reloaded = LineItem::fromJson(json);
    QCOMPARE(reloaded.getQuantity(), 1.5);
    QVERIFY(qAbs(reloaded.getTotalTaxed() - original.getTotalTaxed()) < 0.001);
}

QTEST_MAIN(TestInvoicing)
#include "test_invoicing.moc"
