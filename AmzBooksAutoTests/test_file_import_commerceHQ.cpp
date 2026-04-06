#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QCoroTask>
#include <QTemporaryDir>

#include "orders/ImporterFileCommerceHQ.h"
#include "books/TaxScheme.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"

// For integration test (test_ungroupedVsGroupedFiltering)
#include "orders/OrderManager.h"
#include "orders/Amount.h"
#include "orders/TaxSource.h"
#include "orders/SaleType.h"
#include "books/UngroupedOrderTable.h"
#include "books/TaxJurisdictionLevel.h"

class TestFileImportCommerceHQ : public QObject
{
    Q_OBJECT

public:
    TestFileImportCommerceHQ();
    ~TestFileImportCommerceHQ();

private slots:
    void test_basicImport();
    void test_columnOrderChanged();
    void test_missingRequiredColumn();
    void test_variedSituations();
    void test_dateTimeFormats();
    void test_refundClueVsRefund();
    void test_ungroupedVsGroupedFiltering();

private:
    QString createTempCsv(const QString &content, QTemporaryDir &tempDir,
                          const QString &fileName = "test.csv");

    // Full CommerceHQ CSV header matching the real export format
    static const QString FULL_HEADER;
};

const QString TestFileImportCommerceHQ::FULL_HEADER =
    "order-number,order-date,order-time,sku,product-id,product-title,quantity,"
    "product-type,size,product-vendor,price,subtotal-paid,shipping-paid,"
    "order-total,refunded-amount,refund-date,referring-website,full-name,email,"
    "phone,street-address,address-line-2,city,zip-code,state,country,"
    "country-code,weight,tax,payment-method,payment-type\n";

TestFileImportCommerceHQ::TestFileImportCommerceHQ() {}
TestFileImportCommerceHQ::~TestFileImportCommerceHQ() {}

QString TestFileImportCommerceHQ::createTempCsv(const QString &content,
                                                 QTemporaryDir &tempDir,
                                                 const QString &fileName)
{
    QString file = tempDir.filePath(fileName);
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&f);
        out << content;
        f.close();
    }
    return file;
}

// ---------------------------------------------------------------------------
// Test 1 — single standard order, verify all key fields
// ---------------------------------------------------------------------------
void TestFileImportCommerceHQ::test_basicImport()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // One order: US customer, no refund, no tax (typical US export scenario)
    QString content = FULL_HEADER;
    content += "3101,01/18/2026,10:40pm,CJNSWTQB00154-S-Blue,235,"
               "\"Light Blue Floral Winter Jacket With White Fur\","
               "1,Coat,,,79.70,79.70,0.00,79.70,,,www.google.com,"
               "\"Mitzie Hennessey\",jer7of9@gmail.com,\"+1 910 429 6914\","
               "\"4618 Dow Ct\",,Fayetteville,28314,\"North Carolina\","
               "\"United States\",US,460,0.00,Visa,Stripe\n";

    QString file = createTempCsv(content, tempDir);
    ImporterFileCommerceHQ importer(tempDir.path());

    AbstractImporter::ReturnOrderInfos result;
    try
    {
        result = QCoro::waitFor(importer.loadReport(file));
    }
    catch (const std::exception &e)
    {
        QFAIL(qPrintable(QString("Unexpected exception: ") + e.what()));
    }

    // Basic sanity
    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    QCOMPARE(result.orderInfos->shipments.size(), 1);

    const auto &s = result.orderInfos->shipments.first();
    QCOMPARE(s.getActivities().size(), 1);
    const auto &a = s.getActivities().first();

    // Order identity — one shipment per order; activityId == orderNumber
    QCOMPARE(a.getEventId(),    QString("3101"));
    QCOMPARE(a.getActivityId(), QString("3101"));

    // Geography
    QCOMPARE(a.getCountryCodeFrom(), QString("FR"));
    QCOMPARE(a.getCountryCodeTo(), QString("US"));

    // Currency
    QCOMPARE(a.getCurrency(), QString("USD"));

    // Amounts: subtotal=79.70, tax=0.00 → gross=79.70, taxes=0.00, net=79.70
    QVERIFY(qAbs(a.getAmountTaxed()   - 79.70) < 0.01);
    QVERIFY(qAbs(a.getAmountTaxes()   - 0.00)  < 0.01);
    QVERIFY(qAbs(a.getAmountUntaxed() - 79.70) < 0.01);

    // Date and time
    QCOMPARE(a.getDateTime().date(), QDate(2026, 1, 18));
    QCOMPARE(a.getDateTime().time(), QTime(22, 40)); // 10:40pm → 22:40

    // Tax scheme: FR → US = Exempt (export outside EU)
    QCOMPARE(a.getTaxScheme(), TaxScheme::Exempt);

    // Sale type
    QVERIFY(a.getSaleType() == SaleType::Products);

    // No refund on this row
    QCOMPARE(result.orderInfos->refunds.size(), 0);

    // InvoicingInfo — must be present so PaneBookKeeping::generateInvoices() can produce a PDF
    QCOMPARE(result.orderInfos->invoicingInfos.size(), 1);
    const auto &inv = result.orderInfos->invoicingInfos.first();
    QCOMPARE(inv.shipmentOrRefundId, QString("3101"));
    QVERIFY(!inv.invoicingInfo.getItems().isEmpty());
    // Single line item: whole order; taxed total == 79.70, no VAT
    const auto &item = inv.invoicingInfo.getItems().first();
    QVERIFY(qAbs(item.getTotalTaxed() - 79.70) < 0.01);
    QVERIFY(qAbs(item.getTotalTaxes() - 0.00)  < 0.01);

    // Address populated
    QCOMPARE(result.orderInfos->orderAddresses.size(), 1);
    const auto &addr = result.orderInfos->orderAddresses.first().address;
    QCOMPARE(result.orderInfos->orderAddresses.first().orderId, QString("3101"));
    QCOMPARE(addr.getFullName(),     QString("Mitzie Hennessey"));
    QCOMPARE(addr.getAddressLine1(), QString("4618 Dow Ct"));
    QCOMPARE(addr.getCity(),         QString("Fayetteville"));
    QCOMPARE(addr.getPostalCode(),   QString("28314"));
    QCOMPARE(addr.getCountryCode(),  QString("US"));
    QCOMPARE(addr.getStateOrRegion(),QString("North Carolina"));
    QCOMPARE(addr.getEmail(),        QString("jer7of9@gmail.com"));

    // Date range
    QCOMPARE(result.orderInfos->dateMin, QDate(2026, 1, 18));
    QCOMPARE(result.orderInfos->dateMax, QDate(2026, 1, 18));
}

// ---------------------------------------------------------------------------
// Test 2 — column order changed (resilience to shuffled header)
// ---------------------------------------------------------------------------
void TestFileImportCommerceHQ::test_columnOrderChanged()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Minimal header with columns in different order
    QString content =
        "country-code,subtotal-paid,tax,order-number,order-date,order-time,sku\n"
        "DE,45.00,3.60,5001,02/05/2026,8:30am,SKU-DE-01\n"
        "FR,30.00,6.00,5002,02/06/2026,12:00pm,SKU-FR-01\n"
        "US,20.00,0.00,5003,02/07/2026,3:15pm,SKU-US-01\n";

    QString file = createTempCsv(content, tempDir);
    ImporterFileCommerceHQ importer(tempDir.path());

    AbstractImporter::ReturnOrderInfos result;
    try
    {
        result = QCoro::waitFor(importer.loadReport(file));
    }
    catch (const CsvHeaderException &)
    {
        QFAIL("CsvHeaderException — columns must be found regardless of order");
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    QCOMPARE(result.orderInfos->shipments.size(), 3);

    // First row: DE order → EU OSS (FR → DE)
    const auto &a0 = result.orderInfos->shipments[0].getActivities().first();
    QCOMPARE(a0.getCountryCodeTo(), QString("DE"));
    QCOMPARE(a0.getTaxScheme(), TaxScheme::EuOssUnion);
    QVERIFY(qAbs(a0.getAmountTaxed() - 48.60) < 0.01); // 45.00 + 3.60

    // Third row: US → Exempt
    const auto &a2 = result.orderInfos->shipments[2].getActivities().first();
    QCOMPARE(a2.getTaxScheme(), TaxScheme::Exempt);

    // Date range
    QCOMPARE(result.orderInfos->dateMin, QDate(2026, 2, 5));
    QCOMPARE(result.orderInfos->dateMax, QDate(2026, 2, 7));
}

// ---------------------------------------------------------------------------
// Test 3 — missing required columns throw CsvHeaderException
// ---------------------------------------------------------------------------
void TestFileImportCommerceHQ::test_missingRequiredColumn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Helper: build a header lacking `missingCol` and run the importer
    auto runMissing = [&](const QString &missingCol) -> bool {
        QStringList cols = {"order-number", "order-date", "subtotal-paid", "tax", "country-code"};
        cols.removeAll(missingCol);
        QString content = cols.join(",") + "\n1001,01/01/2026,50.00,5.00,US\n";
        // Remove the matching data value too (keep it simple — fewer fields = mismatched but that's fine)
        QString file = createTempCsv(content, tempDir, "missing_" + missingCol + ".csv");
        ImporterFileCommerceHQ importer(tempDir.path());
        bool threw = false;
        try
        {
            QCoro::waitFor(importer.loadReport(file));
        }
        catch (const CsvHeaderException &)
        {
            threw = true;
        }
        catch (const std::exception &)
        {
            // Some other exception — column still not found
            threw = false;
        }
        return threw;
    };

    QVERIFY2(runMissing("order-number"),  "Expected exception for missing order-number");
    QVERIFY2(runMissing("order-date"),    "Expected exception for missing order-date");
    QVERIFY2(runMissing("subtotal-paid"), "Expected exception for missing subtotal-paid");
    QVERIFY2(runMissing("tax"),           "Expected exception for missing tax");
    QVERIFY2(runMissing("country-code"),  "Expected exception for missing country-code");
}

// ---------------------------------------------------------------------------
// Test 4 — varied situations: refunds, domestic, OSS, tax > 0, two SKUs
// ---------------------------------------------------------------------------
void TestFileImportCommerceHQ::test_variedSituations()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Header includes refund-date to test explicit refund date parsing
    QString content =
        "order-number,order-date,order-time,sku,subtotal-paid,order-total,"
        "refunded-amount,refund-date,tax,country-code\n";

    // Row 1: standard US order, no refund → 1 shipment, Exempt
    content += "2001,03/01/2026,9:00am,SKU-A,50.00,50.00,,,0.00,US\n";

    // Row 2: fully refunded US order (refunded-amount == order-total)
    //        → 1 shipment + 1 refund with explicit refund-date 03/10/2026
    content += "2002,03/02/2026,10:00am,SKU-B,30.00,30.00,30.00,03/10/2026,0.00,US\n";

    // Row 3: domestic FR → FR → DomesticVat, no refund
    content += "2003,03/03/2026,11:00am,SKU-C,20.00,23.80,,, 3.80,FR\n";

    // Row 4: EU cross-border FR → DE → EuOssUnion, no refund
    content += "2004,03/04/2026,12:00pm,SKU-D,40.00,47.60,,,7.60,DE\n";

    // Row 5: US order with sales tax, no refund
    content += "2005,03/05/2026,1:00pm,SKU-E,100.00,108.00,,,8.00,US\n";

    // Rows 6a & 6b: same order-number, two different SKUs → two distinct shipments
    content += "2006,03/06/2026,2:00pm,SKU-F1,25.00,25.00,,,0.00,CA\n";
    content += "2006,03/06/2026,2:00pm,SKU-F2,25.00,25.00,,,0.00,CA\n";

    // Row 7: partial refund (refunded < order-total) → 1 shipment; refund stored as orderId_refundClue
    content += "2007,03/07/2026,3:00pm,SKU-G,60.00,60.00,10.00,,0.00,US\n";

    QString file = createTempCsv(content, tempDir);
    ImporterFileCommerceHQ importer(tempDir.path());

    AbstractImporter::ReturnOrderInfos result;
    try
    {
        result = QCoro::waitFor(importer.loadReport(file));
    }
    catch (const std::exception &e)
    {
        QFAIL(qPrintable(QString("Unexpected exception: ") + e.what()));
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);

    // 8 input rows but only 7 unique order numbers (2006 has two SKU rows → one shipment)
    QCOMPARE(result.orderInfos->shipments.size(), 7);
    // Row 2 is fully refunded → 1 Refund; row 7 is partial → orderId_refundClue
    QCOMPARE(result.orderInfos->refunds.size(), 1);

    // Date range — refund date of row 2 is 03/10/2026, extending max beyond 03/07
    QCOMPARE(result.orderInfos->dateMin, QDate(2026, 3, 1));
    QCOMPARE(result.orderInfos->dateMax, QDate(2026, 3, 10));

    // Find and verify row 3 (FR → FR domestic)
    bool foundDomestic = false;
    for (const auto &s : std::as_const(result.orderInfos->shipments))
    {
        const auto &a = s.getActivities().first();
        if (a.getEventId() == "2003")
        {
            foundDomestic = true;
            QCOMPARE(a.getTaxScheme(), TaxScheme::DomesticVat);
            QCOMPARE(a.getCountryCodeTo(), QString("FR"));
            QVERIFY(qAbs(a.getAmountTaxed() - 23.80) < 0.01);
            QVERIFY(qAbs(a.getAmountTaxes() - 3.80)  < 0.01);
        }
    }
    QVERIFY2(foundDomestic, "Domestic FR→FR order not found");

    // Find and verify row 4 (FR → DE OSS)
    bool foundOss = false;
    for (const auto &s : std::as_const(result.orderInfos->shipments))
    {
        const auto &a = s.getActivities().first();
        if (a.getEventId() == "2004")
        {
            foundOss = true;
            QCOMPARE(a.getTaxScheme(), TaxScheme::EuOssUnion);
            QCOMPARE(a.getCountryCodeTo(), QString("DE"));
            QVERIFY(qAbs(a.getAmountTaxed() - 47.60) < 0.01);
        }
    }
    QVERIFY2(foundOss, "EU OSS FR→DE order not found");

    // Order 2006 has two SKU rows — they should be merged into one shipment with summed amount
    int shipCount2006 = 0;
    double amount2006 = 0.0;
    for (const auto &s : std::as_const(result.orderInfos->shipments))
    {
        const auto &a = s.getActivities().first();
        if (a.getEventId() == "2006")
        {
            ++shipCount2006;
            amount2006 = a.getAmountTaxed();
        }
    }
    QCOMPARE(shipCount2006, 1);
    QVERIFY(qAbs(amount2006 - 50.00) < 0.01); // 25.00 + 25.00

    // Verify refund for order 2002: explicit refund-date 03/10/2026, amount -30.00
    bool foundFullRefund = false;
    for (const auto &r : std::as_const(result.orderInfos->refunds))
    {
        const auto &a = r.getActivities().first();
        if (a.getEventId() == "2002")
        {
            foundFullRefund = true;
            QVERIFY(qAbs(a.getAmountTaxed() - (-30.00)) < 0.01);
            QCOMPARE(a.getDateTime().date(), QDate(2026, 3, 10)); // explicit refund-date
            QCOMPARE(a.getActivityId(), QString("2002_refund"));
        }
    }
    QVERIFY2(foundFullRefund, "Full refund for order 2002 not found");

    // Partial refund for order 2007 → stored as orderId_refundClues, not as a Refund
    QVERIFY(result.orderInfos->orderId_refundClues.contains("2007"));
    QVERIFY(qAbs(result.orderInfos->orderId_refundClues["2007"].first().value - 10.00) < 0.01);
    QCOMPARE(result.orderInfos->orderId_refundClues["2007"].first().currency, QString("USD"));

    // Address deduplication: 8 rows but only 7 distinct order-numbers (2006 appears twice)
    QCOMPARE(result.orderInfos->orderAddresses.size(), 7);
    // Confirm order 2006 address is stored only once
    int count2006 = 0;
    for (const auto &addrEntry : std::as_const(result.orderInfos->orderAddresses))
    {
        if (addrEntry.orderId == "2006")
            ++count2006;
    }
    QCOMPARE(count2006, 1);
}

// ---------------------------------------------------------------------------
// Test 5 — various date+time formats parse correctly
// ---------------------------------------------------------------------------
void TestFileImportCommerceHQ::test_dateTimeFormats()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content =
        "order-number,order-date,order-time,sku,subtotal-paid,tax,country-code\n"
        // 9:15am → 09:15
        "6001,01/05/2026,9:15am,SKU-1,10.00,0.00,US\n"
        // 11:59pm → 23:59
        "6002,12/31/2025,11:59pm,SKU-2,20.00,0.00,US\n"
        // Empty time field → midnight 00:00
        "6003,06/15/2026,,SKU-3,30.00,0.00,US\n"
        // Single-digit month and day: 3/7/2026
        "6004,3/7/2026,8:00am,SKU-4,40.00,0.00,US\n";

    QString file = createTempCsv(content, tempDir);
    ImporterFileCommerceHQ importer(tempDir.path());

    AbstractImporter::ReturnOrderInfos result;
    try
    {
        result = QCoro::waitFor(importer.loadReport(file));
    }
    catch (const std::exception &e)
    {
        QFAIL(qPrintable(QString("Unexpected exception: ") + e.what()));
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    QCOMPARE(result.orderInfos->shipments.size(), 4);

    const auto getAct = [&](int idx) -> const Activity & {
        return result.orderInfos->shipments[idx].getActivities().first();
    };

    // Row 0: 01/05/2026 + 9:15am
    QCOMPARE(getAct(0).getDateTime().date(), QDate(2026, 1, 5));
    QCOMPARE(getAct(0).getDateTime().time(), QTime(9, 15));

    // Row 1: 12/31/2025 + 11:59pm
    QCOMPARE(getAct(1).getDateTime().date(), QDate(2025, 12, 31));
    QCOMPARE(getAct(1).getDateTime().time(), QTime(23, 59));

    // Row 2: 06/15/2026 + empty time → midnight
    QCOMPARE(getAct(2).getDateTime().date(), QDate(2026, 6, 15));
    QCOMPARE(getAct(2).getDateTime().time(), QTime(0, 0));

    // Row 3: 3/7/2026 + 8:00am (single-digit M/D)
    QCOMPARE(getAct(3).getDateTime().date(), QDate(2026, 3, 7));
    QCOMPARE(getAct(3).getDateTime().time(), QTime(8, 0));

    // Date range: 2025-12-31 to 2026-06-15
    QCOMPARE(result.orderInfos->dateMin, QDate(2025, 12, 31));
    QCOMPARE(result.orderInfos->dateMax, QDate(2026, 6, 15));
}

// ---------------------------------------------------------------------------
// Test 6 — full refund creates Refund; partial refund creates orderId_refundClue
// ---------------------------------------------------------------------------
void TestFileImportCommerceHQ::test_refundClueVsRefund()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content =
        "order-number,order-date,order-time,sku,subtotal-paid,order-total,"
        "refunded-amount,refund-date,tax,country-code\n"
        // Order 7001: fully refunded (refunded == subtotal) → Refund entry
        "7001,02/10/2026,9:00am,SKU-A,60.00,60.00,60.00,02/12/2026,0.00,US\n"
        // Order 7002: half refunded (refunded < subtotal) → orderId_refundClue
        "7002,02/11/2026,10:00am,SKU-B,60.00,60.00,30.00,,0.00,US\n";

    QString file = createTempCsv(content, tempDir, "refund_clue.csv");
    ImporterFileCommerceHQ importer(tempDir.path());

    AbstractImporter::ReturnOrderInfos result;
    try
    {
        result = QCoro::waitFor(importer.loadReport(file));
    }
    catch (const std::exception &e)
    {
        QFAIL(qPrintable(QString("Unexpected exception: ") + e.what()));
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);

    // Both orders produce shipments regardless of refund type
    QCOMPARE(result.orderInfos->shipments.size(), 2);

    // Full refund (7001) → Refund entry with explicit refund-date
    QCOMPARE(result.orderInfos->refunds.size(), 1);
    const auto &refundAct = result.orderInfos->refunds.first().getActivities().first();
    QCOMPARE(refundAct.getEventId(),    QString("7001"));
    QCOMPARE(refundAct.getActivityId(), QString("7001_refund"));
    QVERIFY(qAbs(refundAct.getAmountTaxed() - (-60.00)) < 0.01);
    QCOMPARE(refundAct.getDateTime().date(), QDate(2026, 2, 12)); // explicit refund-date

    // Partial refund (7002) → stored as orderId_refundClues, not a Refund
    QCOMPARE(result.orderInfos->orderId_refundClues.size(), 1);
    QVERIFY(result.orderInfos->orderId_refundClues.contains("7002"));
    const auto &clue = result.orderInfos->orderId_refundClues["7002"].first();
    QVERIFY(qAbs(clue.value - 30.00) < 0.01);
    QCOMPARE(clue.currency, QString("USD"));

    // InvoicingInfo — 2 shipments + 1 full refund = 3 entries
    // Without this, PaneBookKeeping::generateInvoices() fails with
    // "Missing invoicing info for <orderId>" for every CommerceHQ order.
    QCOMPARE(result.orderInfos->invoicingInfos.size(), 3);

    // Verify shipment invoicingInfos have positive amounts
    int shipInvCount = 0;
    int refundInvCount = 0;
    for (const auto &inv : std::as_const(result.orderInfos->invoicingInfos))
    {
        QVERIFY(!inv.invoicingInfo.getItems().isEmpty());
        const double total = inv.invoicingInfo.getItems().first().getTotalTaxed();
        if (total > 0.0)
            ++shipInvCount;
        else if (total < 0.0)
            ++refundInvCount;
    }
    QCOMPARE(shipInvCount,   2); // 7001 and 7002 shipments
    QCOMPARE(refundInvCount, 1); // 7001 refund (−60.00)
}

// ---------------------------------------------------------------------------
// Test 7 — integration: CommerceHQ (ungrouped) vs Amazon (grouped) in OrderManager
// ---------------------------------------------------------------------------
void TestFileImportCommerceHQ::test_ungroupedVsGroupedFiltering()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // --- Import 2 CommerceHQ orders: CHQ-101 (no refund), CHQ-102 (fully refunded) ---
    QString chqContent =
        "order-number,order-date,order-time,sku,subtotal-paid,order-total,"
        "refunded-amount,refund-date,tax,country-code\n"
        "CHQ-101,02/01/2026,9:00am,SKU-1,50.00,50.00,,,0.00,US\n"
        "CHQ-102,02/02/2026,10:00am,SKU-2,40.00,40.00,40.00,02/03/2026,0.00,US\n";

    QString chqFile = createTempCsv(chqContent, tempDir, "chq.csv");
    ImporterFileCommerceHQ chqImporter(tempDir.path());

    AbstractImporter::ReturnOrderInfos chqResult;
    try
    {
        chqResult = QCoro::waitFor(chqImporter.loadReport(chqFile));
    }
    catch (const std::exception &e)
    {
        QFAIL(qPrintable(QString("CHQ import exception: ") + e.what()));
    }

    QVERIFY2(chqResult.errorReturned.isEmpty(), qPrintable(chqResult.errorReturned));
    QVERIFY(chqResult.orderInfos);
    QCOMPARE(chqResult.orderInfos->shipments.size(), 2);
    QCOMPARE(chqResult.orderInfos->refunds.size(), 1); // CHQ-102 is fully refunded

    // --- Record CommerceHQ entries in OrderManager (isGrouped = false) ---
    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ActivitySource chqSource = chqImporter.getActivitySource();
    QHash<QString, OrderManager::OrderInfo> chqOrders;

    for (const auto &s : std::as_const(chqResult.orderInfos->shipments))
    {
        const QString orderId = s.getActivities().first().getEventId();
        orderManager.recordShipmentFromSource(orderId, &chqSource, &s, QDate(2026, 12, 31));
        chqOrders[orderId] = OrderManager::OrderInfo{"CommerceHQ", false, ""};
    }
    for (const auto &r : std::as_const(chqResult.orderInfos->refunds))
    {
        const QString orderId = r.getActivities().first().getEventId();
        orderManager.recordShipmentFromSource(orderId, &chqSource, &r, QDate(2026, 12, 31));
    }
    orderManager.recordOrders(chqOrders);

    // --- Create 3 Amazon grouped orders; AMZ-002 also has a refund ---
    ActivitySource amzSource{ActivitySourceType::Report, "Amazon", "EU", "amazon-vat-2026"};

    auto makeAmzAct = [](const QString &eventId, const QString &actId,
                         const QDate &date, double amount) -> Activity
    {
        QDateTime dt(date, QTime(0, 0));
        auto res = Activity::create(
            eventId, actId, "",
            dt, dt, "EUR", "FR", "DE", false, "DE",
            ::Amount(amount, amount * 0.2),
            TaxSource::MarketplaceProvided,
            "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country,
            SaleType::Products);
        Q_ASSERT(res.ok());
        return *res.value;
    };

    Shipment amz1({makeAmzAct("AMZ-001", "AMZ-001", QDate(2026, 2, 5), 100.0)}, "", true);
    Shipment amz2({makeAmzAct("AMZ-002", "AMZ-002", QDate(2026, 2, 6),  80.0)}, "", true);
    Shipment amz3({makeAmzAct("AMZ-003", "AMZ-003", QDate(2026, 2, 7),  60.0)}, "", true);
    Refund   amz2Refund({makeAmzAct("AMZ-002", "AMZ-002-refund", QDate(2026, 2, 8), -80.0)}, "", true);

    orderManager.recordShipmentFromSource("AMZ-001", &amzSource, &amz1,      QDate(2026, 12, 31));
    orderManager.recordShipmentFromSource("AMZ-002", &amzSource, &amz2,      QDate(2026, 12, 31));
    orderManager.recordShipmentFromSource("AMZ-003", &amzSource, &amz3,      QDate(2026, 12, 31));
    orderManager.recordShipmentFromSource("AMZ-002", &amzSource, &amz2Refund, QDate(2026, 12, 31));
    orderManager.recordOrders({
        {"AMZ-001", OrderManager::OrderInfo{"Amazon", true, ""}},
        {"AMZ-002", OrderManager::OrderInfo{"Amazon", true, ""}},
        {"AMZ-003", OrderManager::OrderInfo{"Amazon", true, ""}},
    });

    // --- UngroupedOrderTable must contain only CommerceHQ entries ---
    // CHQ-101 shipment + CHQ-102 shipment + CHQ-102 refund = 3 rows
    UngroupedOrderTable ungroupedTable(nullptr, &orderManager, tempDir.path());
    ungroupedTable.load(2026);
    QCOMPARE(ungroupedTable.rowCount(), 3);

    // --- getShipmentAndRefunds with grouped-only callback must return only Amazon entries ---
    // AMZ-001 + AMZ-002 + AMZ-003 + AMZ-002-refund = 4 entries
    const auto grouped = orderManager.getShipmentAndRefunds(
        QDate(2026, 1, 1), QDate(2026, 12, 31),
        [](const ActivitySource *, const Shipment *s) { return s && s->isGrouped(); });

    QCOMPARE(grouped.size(), 4);
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it)
    {
        QVERIFY(it.value()->isGrouped());
    }
}

QTEST_GUILESS_MAIN(TestFileImportCommerceHQ)
#include "test_file_import_commerceHQ.moc"
