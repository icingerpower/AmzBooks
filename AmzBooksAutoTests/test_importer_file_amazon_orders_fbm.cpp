#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QDebug>
#include <QCoroTask>

#include "orders/ImporterFileAmazonOrdersFBM.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Builds a tab-separated header line for the FBM report.
static QString fbmHeader()
{
    return QString("order-id\torder-item-id\tpurchase-date\tpayments-date\t"
                   "buyer-email\tbuyer-name\tbuyer-phone-number\tsku\tproduct-name\t"
                   "quantity-purchased\tcurrency\titem-price\titem-tax\t"
                   "shipping-price\tshipping-tax\tship-service-level\t"
                   "recipient-name\tship-address-1\tship-address-2\tship-address-3\t"
                   "ship-city\tship-state\tship-postal-code\tship-country\t"
                   "ship-phone-number\titem-promotion-discount\titem-promotion-id\t"
                   "ship-promotion-discount\tship-promotion-id\t"
                   "delivery-start-date\tdelivery-end-date\tdelivery-time-zone\t"
                   "delivery-Instructions\tsales-channel\torder-channel\t"
                   "order-channel-instance\texternal-order-id\tis-business-order\t"
                   "purchase-order-number\tprice-designation\tbuyer-company-name\t"
                   "signature-confirmation-recommended\tbuyer-identification-number\t"
                   "buyer-identification-type\n");
}

// Writes one FBM data row.  Fields that are rarely varied in a test can be
// left at their defaults; callers override only what matters for the assertion.
struct FbmRow {
    QString orderId         = "111-0000001-0000001";
    QString orderItemId     = "100000000000001";
    QString purchaseDate    = "2026-02-05T22:39:16-08:00";
    QString buyerEmail      = "test@marketplace.amazon.com";
    QString buyerName       = "Test Buyer";
    QString sku             = "SKU-001";
    QString productName     = "Test Product";
    int     qty             = 1;
    QString currency        = "USD";
    double  itemPrice       = 39.99;
    double  itemTax         = 2.40;
    double  shippingPrice   = 2.91;
    double  shippingTax     = 0.18;
    QString recipientName   = "Test Recipient";
    QString addr1           = "123 Main St";
    QString addr2;
    QString addr3;
    QString city            = "Rockville";
    QString state           = "MD";
    QString postalCode      = "20853";
    QString country         = "US";
    QString deliveryInstructions;
    QString salesChannel    = "Amazon.com";
    bool    isBusinessOrder = false;
};

static QString fbmRow(const FbmRow &r)
{
    return QString("%1\t%2\t%3\t%3\t%4\t%5\t\t%6\t%7\t%8\t%9\t%10\t%11\t%12\t%13\t"
                   "Standard\t%14\t%15\t%16\t%17\t%18\t%19\t%20\t%21\t\t"
                   "0.00\t\t0.00\t\t\t\t\t%22\t%23\tWebsiteOrderChannel\t\t\t%24\t\t\tfalse\t\t\n")
        .arg(r.orderId, r.orderItemId, r.purchaseDate)
        .arg(r.buyerEmail, r.buyerName, r.sku, r.productName)
        .arg(r.qty)
        .arg(r.currency)
        .arg(r.itemPrice, 0, 'f', 2)
        .arg(r.itemTax, 0, 'f', 2)
        .arg(r.shippingPrice, 0, 'f', 2)
        .arg(r.shippingTax, 0, 'f', 2)
        .arg(r.recipientName, r.addr1, r.addr2, r.addr3)
        .arg(r.city, r.state, r.postalCode, r.country)
        .arg(r.deliveryInstructions)
        .arg(r.salesChannel)
        .arg(r.isBusinessOrder ? "true" : "false");
}

// Writes a complete FBM file with header + rows and returns the file path.
static QString writeFbmFile(const QTemporaryDir &dir, const QString &name, const QString &content)
{
    QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream out(&f);
    out << content;
    return path;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestImporterFileAmazonOrdersFBM : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_variedSituations();
    void test_multipleItemsPerOrder();
    void test_taxHandling_marketplaceFacilitator();
    void test_businessOrder();
    void test_salesChannelToOrigin();
    void test_invalidDate();
    void test_invalidCsv_missingOrderId();
    void test_invalidCsv_missingShipCountry();
    void test_invalidCsv_missingItemPrice();
    void test_invalidCsv_missingItemTax();
    void test_invalidCsv_missingCurrency();
    void test_invalidCsv_missingPurchaseDate();
    void test_invalidCsv_missingSalesChannel();
    void test_realData();

private:
    QString m_dataDir;

    // Runs the importer synchronously, asserts no error, returns result.
    AbstractImporter::ReturnOrderInfos runImport(const QString &filePath, const QString &workingDir);
};

// ---------------------------------------------------------------------------
// initTestCase / cleanupTestCase
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::initTestCase()
{
    // Search for data/amazon-orders-fbm relative to the executable
    QDir appDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        QDir candidate = appDir;
        if (candidate.cd("data/amazon-orders-fbm")) {
            m_dataDir = candidate.absolutePath();
            break;
        }
        if (!appDir.cdUp() || appDir.isRoot()) {
            break;
        }
    }
    if (m_dataDir.isEmpty()) {
        qDebug() << "data/amazon-orders-fbm not found — test_realData will be skipped";
    } else {
        qDebug() << "Data dir:" << m_dataDir;
    }
}

void TestImporterFileAmazonOrdersFBM::cleanupTestCase() {}

// ---------------------------------------------------------------------------
// Helper used by several tests
// ---------------------------------------------------------------------------

AbstractImporter::ReturnOrderInfos TestImporterFileAmazonOrdersFBM::runImport(
    const QString &filePath, const QString &workingDir)
{
    ImporterFileAmazonOrdersFBM importer(workingDir);
    auto task = importer.loadReport(filePath);
    return QCoro::waitFor(task);
}

// ---------------------------------------------------------------------------
// test_variedSituations
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::test_variedSituations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString content = fbmHeader();

    // Row 1 — standard US→US order, non-zero shipping tax
    FbmRow r1;
    r1.orderId      = "111-0000001-0000001";
    r1.orderItemId  = "100000000000001";
    r1.purchaseDate = "2026-02-05T22:39:16-08:00";
    r1.itemPrice    = 39.99;
    r1.itemTax      = 2.40;
    r1.shippingPrice = 2.91;
    r1.shippingTax  = 0.18;
    r1.country      = "US";
    r1.salesChannel = "Amazon.com";
    content += fbmRow(r1);

    // Row 2 — US→US, shipping tax = 0
    FbmRow r2;
    r2.orderId      = "111-0000002-0000002";
    r2.orderItemId  = "100000000000002";
    r2.purchaseDate = "2026-02-14T19:15:14-08:00";
    r2.itemPrice    = 37.99;
    r2.itemTax      = 2.47;
    r2.shippingPrice = 3.04;
    r2.shippingTax  = 0.00;
    r2.country      = "US";
    r2.salesChannel = "Amazon.com";
    content += fbmRow(r2);

    // Row 3 — empty order-id → must be skipped
    FbmRow r3;
    r3.orderId     = "";
    r3.orderItemId = "100000000000003";
    r3.country     = "US";
    r3.salesChannel = "Amazon.com";
    content += fbmRow(r3);

    // Row 4 — Amazon.co.uk (origin = GB)
    FbmRow r4;
    r4.orderId      = "202-0000004-0000004";
    r4.orderItemId  = "100000000000004";
    r4.currency     = "GBP";
    r4.itemPrice    = 24.99;
    r4.itemTax      = 0.00;
    r4.shippingPrice = 0.00;
    r4.shippingTax  = 0.00;
    r4.country      = "GB";
    r4.salesChannel = "Amazon.co.uk";
    content += fbmRow(r4);

    // Row 5 — delivery instructions with special characters
    FbmRow r5;
    r5.orderId      = "111-0000005-0000005";
    r5.orderItemId  = "100000000000005";
    r5.purchaseDate = "2026-03-01T10:00:00+00:00";
    r5.itemPrice    = 15.50;
    r5.itemTax      = 1.00;
    r5.shippingPrice = 5.00;
    r5.shippingTax  = 0.30;
    r5.country      = "US";
    r5.salesChannel = "Amazon.com";
    r5.deliveryInstructions = "Leave at door — no bell";
    content += fbmRow(r5);

    const QString filePath = writeFbmFile(dir, "orders.txt", content);
    QVERIFY(!filePath.isEmpty());

    AbstractImporter::ReturnOrderInfos result;
    try {
        result = runImport(filePath, dir.path());
    } catch (const CsvHeaderException &e) {
        QFAIL(qPrintable(e.getErrorColumns("Missing columns in " + e.getFileName())));
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);

    // Row 3 (empty orderId) is skipped, row 4 has 0 total (24.99+0=24.99, non-zero)
    // so we expect 4 shipments (rows 1, 2, 4, 5 — row 3 skipped)
    // BUT: for US rows, taxes are zeroed.  For row 4 (GB), item-tax=0 already.
    // Row 4 total = 24.99 (non-zero) → kept.
    QCOMPARE(result.orderInfos->shipments.size(), 4);

    // Row 1 — US marketplace facilitator: taxes zeroed
    // Expected amount = 39.99 + 0 + 2.91 = 42.90 (taxes zeroed), taxSource = 0
    {
        const auto &s = result.orderInfos->shipments[0];
        QCOMPARE(s.getActivities().size(), 1);
        const auto &a = s.getActivities().first();
        QCOMPARE(a.getCountryCodeFrom(), "US");
        QCOMPARE(a.getCountryCodeTo(),   "US");
        QVERIFY(qAbs(a.getAmountTaxed()       - 42.90) < 0.001);
        QVERIFY(qAbs(a.getAmountTaxesSource() -  0.00) < 0.001);
    }

    // Row 2 — US, shippingTax already 0; result same (taxes zeroed)
    {
        const auto &s = result.orderInfos->shipments[1];
        const auto &a = s.getActivities().first();
        QVERIFY(qAbs(a.getAmountTaxed()       - (37.99 + 3.04)) < 0.001);
        QVERIFY(qAbs(a.getAmountTaxesSource() -  0.00) < 0.001);
    }

    // Row 4 — Amazon.co.uk: origin = GB
    {
        const auto &s = result.orderInfos->shipments[2];
        const auto &a = s.getActivities().first();
        QCOMPARE(a.getCountryCodeFrom(), "GB");
        QCOMPARE(a.getCountryCodeTo(),   "GB");
        QCOMPARE(a.getCurrency(),        "GBP");
        QVERIFY(qAbs(a.getAmountTaxed() - 24.99) < 0.001);
    }

    // Addresses — 4 valid orders, each stored once
    QCOMPARE(result.orderInfos->orderAddresses.size(), 4);

    // orderId_infos populated
    QVERIFY(result.orderInfos->orderId_infos.contains("111-0000001-0000001"));
    QCOMPARE(result.orderInfos->orderId_infos["111-0000001-0000001"].store, "Amazon.com");

    // Date range
    QVERIFY(result.orderInfos->dateMin.isValid());
    QVERIFY(result.orderInfos->dateMax.isValid());
    QVERIFY(result.orderInfos->dateMin <= result.orderInfos->dateMax);
}

// ---------------------------------------------------------------------------
// test_multipleItemsPerOrder
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::test_multipleItemsPerOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Same order-id, 3 different items
    const QString orderId = "111-0000001-9999999";
    QString content = fbmHeader();

    FbmRow r;
    r.orderId     = orderId;
    r.country     = "US";
    r.salesChannel = "Amazon.com";

    r.orderItemId   = "ITEM-A";
    r.sku           = "SKU-A";
    r.productName   = "Widget A";
    r.itemPrice     = 10.00;
    r.itemTax       = 0.90;
    r.shippingPrice = 2.00;
    r.shippingTax   = 0.10;
    content += fbmRow(r);

    r.orderItemId   = "ITEM-B";
    r.sku           = "SKU-B";
    r.productName   = "Widget B";
    r.itemPrice     = 20.00;
    r.itemTax       = 1.80;
    r.shippingPrice = 0.00;
    r.shippingTax   = 0.00;
    content += fbmRow(r);

    r.orderItemId   = "ITEM-C";
    r.sku           = "SKU-C";
    r.productName   = "Widget C";
    r.itemPrice     = 30.00;
    r.itemTax       = 2.70;
    r.shippingPrice = 3.00;
    r.shippingTax   = 0.20;
    content += fbmRow(r);

    const QString filePath = writeFbmFile(dir, "multi.txt", content);
    QVERIFY(!filePath.isEmpty());

    AbstractImporter::ReturnOrderInfos result;
    try {
        result = runImport(filePath, dir.path());
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);

    // 3 rows share one order-id → exactly 1 Shipment
    QCOMPARE(result.orderInfos->shipments.size(), 1);

    // The Shipment must carry 3 Activities (one per item)
    const auto &s = result.orderInfos->shipments.first();
    QCOMPARE(s.getActivities().size(), 3);

    // US marketplace facilitator: all taxes zeroed
    // Item A: 10.00 + 0 + 2.00 = 12.00
    // Item B: 20.00 + 0 + 0.00 = 20.00
    // Item C: 30.00 + 0 + 3.00 = 33.00
    QVERIFY(qAbs(s.getActivities()[0].getAmountTaxed() - 12.00) < 0.001);
    QVERIFY(qAbs(s.getActivities()[1].getAmountTaxed() - 20.00) < 0.001);
    QVERIFY(qAbs(s.getActivities()[2].getAmountTaxed() - 33.00) < 0.001);

    for (const auto &a : s.getActivities()) {
        QVERIFY(qAbs(a.getAmountTaxesSource()) < 0.001); // all taxes zeroed
    }

    // Address stored exactly once
    QCOMPARE(result.orderInfos->orderAddresses.size(), 1);
    QCOMPARE(result.orderInfos->orderAddresses.first().orderId, orderId);

    // InvoicingInfo created with 3 line items
    QCOMPARE(result.orderInfos->invoicingInfos.size(), 1);
}

// ---------------------------------------------------------------------------
// test_taxHandling_marketplaceFacilitator
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::test_taxHandling_marketplaceFacilitator()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString content = fbmHeader();

    // US destination — taxes must be zeroed even when non-zero in the file
    FbmRow rUs;
    rUs.orderId      = "US-ORDER";
    rUs.orderItemId  = "US-ITEM";
    rUs.itemPrice    = 20.00;
    rUs.itemTax      = 1.90;
    rUs.shippingPrice = 5.00;
    rUs.shippingTax  = 0.50;
    rUs.country      = "US";
    rUs.salesChannel = "Amazon.com";
    content += fbmRow(rUs);

    // CA destination
    FbmRow rCa;
    rCa.orderId      = "CA-ORDER";
    rCa.orderItemId  = "CA-ITEM";
    rCa.currency     = "CAD";
    rCa.itemPrice    = 30.00;
    rCa.itemTax      = 3.90;
    rCa.shippingPrice = 4.00;
    rCa.shippingTax  = 0.40;
    rCa.country      = "CA";
    rCa.salesChannel = "Amazon.ca";
    content += fbmRow(rCa);

    // MX destination
    FbmRow rMx;
    rMx.orderId      = "MX-ORDER";
    rMx.orderItemId  = "MX-ITEM";
    rMx.currency     = "MXN";
    rMx.itemPrice    = 100.00;
    rMx.itemTax      = 16.00;
    rMx.shippingPrice = 20.00;
    rMx.shippingTax  = 3.20;
    rMx.country      = "MX";
    rMx.salesChannel = "Amazon.com.mx";
    content += fbmRow(rMx);

    // GB destination — taxes must NOT be zeroed
    FbmRow rGb;
    rGb.orderId      = "GB-ORDER";
    rGb.orderItemId  = "GB-ITEM";
    rGb.currency     = "GBP";
    rGb.itemPrice    = 50.00;
    rGb.itemTax      = 10.00;
    rGb.shippingPrice = 6.00;
    rGb.shippingTax  = 1.20;
    rGb.country      = "GB";
    rGb.salesChannel = "Amazon.co.uk";
    content += fbmRow(rGb);

    const QString filePath = writeFbmFile(dir, "tax.txt", content);
    QVERIFY(!filePath.isEmpty());

    AbstractImporter::ReturnOrderInfos result;
    try {
        result = runImport(filePath, dir.path());
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 4);

    const auto actAmount = [&](int idx) {
        return result.orderInfos->shipments[idx].getActivities().first();
    };

    // US: taxes zeroed → amountTaxed = 20+5 = 25, taxSource = 0
    QVERIFY(qAbs(actAmount(0).getAmountTaxed()       - 25.00) < 0.001);
    QVERIFY(qAbs(actAmount(0).getAmountTaxesSource() -  0.00) < 0.001);

    // CA: taxes zeroed → 30+4 = 34
    QVERIFY(qAbs(actAmount(1).getAmountTaxed()       - 34.00) < 0.001);
    QVERIFY(qAbs(actAmount(1).getAmountTaxesSource() -  0.00) < 0.001);

    // MX: taxes zeroed → 100+20 = 120
    QVERIFY(qAbs(actAmount(2).getAmountTaxed()       - 120.00) < 0.001);
    QVERIFY(qAbs(actAmount(2).getAmountTaxesSource() -   0.00) < 0.001);

    // GB: taxes preserved → 50+10+6+1.20 = 67.20, taxSource = 10+1.20 = 11.20
    QVERIFY(qAbs(actAmount(3).getAmountTaxed()       - 67.20) < 0.001);
    QVERIFY(qAbs(actAmount(3).getAmountTaxesSource() - 11.20) < 0.001);
}

// ---------------------------------------------------------------------------
// test_businessOrder
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::test_businessOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString content = fbmHeader();

    FbmRow rB2B;
    rB2B.orderId         = "B2B-ORDER-001";
    rB2B.orderItemId     = "B2B-ITEM-001";
    rB2B.isBusinessOrder = true;
    rB2B.country         = "US";
    rB2B.salesChannel    = "Amazon.com";
    content += fbmRow(rB2B);

    FbmRow rB2C;
    rB2C.orderId         = "B2C-ORDER-001";
    rB2C.orderItemId     = "B2C-ITEM-001";
    rB2C.isBusinessOrder = false;
    rB2C.country         = "US";
    rB2C.salesChannel    = "Amazon.com";
    content += fbmRow(rB2C);

    const QString filePath = writeFbmFile(dir, "b2b.txt", content);
    QVERIFY(!filePath.isEmpty());

    AbstractImporter::ReturnOrderInfos result;
    try {
        result = runImport(filePath, dir.path());
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 2);

    const auto &aB2B = result.orderInfos->shipments[0].getActivities().first();
    const auto &aB2C = result.orderInfos->shipments[1].getActivities().first();

    QVERIFY( aB2B.getIsCompany());
    QVERIFY(!aB2C.getIsCompany());
}

// ---------------------------------------------------------------------------
// test_salesChannelToOrigin
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::test_salesChannelToOrigin()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Helper to build a single-row file with a given sales-channel and verify origin
    auto checkOrigin = [&](const QString &salesChannel,
                           const QString &shipCountry,
                           const QString &currency,
                           const QString &expectedOrigin)
    {
        QString content = fbmHeader();
        FbmRow r;
        r.orderId      = "ORD-" + salesChannel;
        r.orderItemId  = "ITEM-" + salesChannel;
        r.salesChannel = salesChannel;
        r.country      = shipCountry;
        r.currency     = currency;
        r.itemPrice    = 10.00;
        r.itemTax      = 0.00;
        r.shippingPrice = 0.00;
        r.shippingTax  = 0.00;
        content += fbmRow(r);

        const QString filePath = writeFbmFile(dir, salesChannel + ".txt", content);
        QVERIFY(!filePath.isEmpty());

        AbstractImporter::ReturnOrderInfos result;
        try {
            result = runImport(filePath, dir.path());
        } catch (const std::exception &e) {
            QFAIL(e.what());
        }

        QVERIFY2(result.errorReturned.isEmpty(),
                 qPrintable("Channel " + salesChannel + ": " + result.errorReturned));
        if (result.orderInfos && !result.orderInfos->shipments.isEmpty()) {
            const auto &a = result.orderInfos->shipments.first().getActivities().first();
            QCOMPARE(a.getCountryCodeFrom(), expectedOrigin);
        }
    };

    checkOrigin("Amazon.com",    "US", "USD", "US");
    checkOrigin("Amazon.co.uk",  "GB", "GBP", "GB");
    checkOrigin("Amazon.de",     "DE", "EUR", "DE");
    checkOrigin("Amazon.fr",     "FR", "EUR", "FR");
    checkOrigin("Amazon.it",     "IT", "EUR", "IT");
    checkOrigin("Amazon.es",     "ES", "EUR", "ES");
    checkOrigin("Amazon.nl",     "NL", "EUR", "NL");
    checkOrigin("Amazon.pl",     "PL", "PLN", "PL");
    checkOrigin("Amazon.ca",     "CA", "CAD", "CA");
    checkOrigin("Amazon.com.mx", "MX", "MXN", "MX");
}

// ---------------------------------------------------------------------------
// test_invalidDate
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::test_invalidDate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString content = fbmHeader();
    FbmRow r;
    r.purchaseDate = "not-a-date";
    r.country      = "US";
    r.salesChannel = "Amazon.com";
    content += fbmRow(r);

    const QString filePath = writeFbmFile(dir, "baddate.txt", content);
    QVERIFY(!filePath.isEmpty());

    AbstractImporter::ReturnOrderInfos result;
    try {
        result = runImport(filePath, dir.path());
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }

    // Import must report an error for an invalid date
    QVERIFY(!result.errorReturned.isEmpty());
}

// ---------------------------------------------------------------------------
// Missing-column tests — each mandatory column must trigger CsvHeaderException
// ---------------------------------------------------------------------------

static void runMissingColumnTest(const QString &missingColumn)
{
    // Build a header with all mandatory columns except the one under test
    QStringList allCols = {
        "order-id", "order-item-id", "purchase-date", "payments-date",
        "buyer-email", "buyer-name", "sku", "product-name", "quantity-purchased",
        "currency", "item-price", "item-tax",
        "shipping-price", "shipping-tax", "ship-service-level",
        "recipient-name", "ship-address-1", "ship-city", "ship-state",
        "ship-postal-code", "ship-country", "sales-channel", "is-business-order"
    };
    allCols.removeAll(missingColumn);

    QTemporaryDir dir;
    const QString filePath = dir.filePath("missing.txt");
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QFAIL("Could not create temp file");
    }
    QTextStream out(&f);
    out << allCols.join("\t") << "\n";
    out << "val\tval\tval\tval\tval\tval\tval\tval\t1\t"
           "USD\t10.00\t1.00\t2.00\t0.20\tStandard\t"
           "Rec\tAddr1\tCity\tST\t12345\tUS\tAmazon.com\tfalse\n";
    f.close();

    ImporterFileAmazonOrdersFBM importer(dir.path());
    bool caughtHeader = false;
    try {
        auto result = QCoro::waitFor(importer.loadReport(filePath));
        Q_UNUSED(result)
    } catch (const CsvHeaderException &) {
        caughtHeader = true;
    } catch (const std::exception &e) {
        // Also acceptable: CsvHeaderException is a std::exception subclass
        QString msg = e.what();
        if (msg.contains("Missing") || msg.contains(missingColumn)) {
            caughtHeader = true;
        }
    }
    QVERIFY2(caughtHeader,
             qPrintable("Expected CsvHeaderException for missing column: " + missingColumn));
}

void TestImporterFileAmazonOrdersFBM::test_invalidCsv_missingOrderId()
{
    runMissingColumnTest("order-id");
}

void TestImporterFileAmazonOrdersFBM::test_invalidCsv_missingShipCountry()
{
    runMissingColumnTest("ship-country");
}

void TestImporterFileAmazonOrdersFBM::test_invalidCsv_missingItemPrice()
{
    runMissingColumnTest("item-price");
}

void TestImporterFileAmazonOrdersFBM::test_invalidCsv_missingItemTax()
{
    runMissingColumnTest("item-tax");
}

void TestImporterFileAmazonOrdersFBM::test_invalidCsv_missingCurrency()
{
    runMissingColumnTest("currency");
}

void TestImporterFileAmazonOrdersFBM::test_invalidCsv_missingPurchaseDate()
{
    runMissingColumnTest("purchase-date");
}

void TestImporterFileAmazonOrdersFBM::test_invalidCsv_missingSalesChannel()
{
    runMissingColumnTest("sales-channel");
}

// ---------------------------------------------------------------------------
// test_realData
// ---------------------------------------------------------------------------

void TestImporterFileAmazonOrdersFBM::test_realData()
{
    // Also check the Dropbox path where the user keeps real FBM order exports
    QString dataDir = m_dataDir;
    if (dataDir.isEmpty()) {
        const QString dropbox = QDir::homePath() + "/Dropbox/compta/amazon-orders";
        if (QFileInfo::exists(dropbox)) {
            dataDir = dropbox;
        }
    }

    if (dataDir.isEmpty()) {
        QSKIP("No FBM data directory found");
    }

    QDirIterator it(dataDir,
                    QStringList() << "*.txt" << "*.tsv",
                    QDir::Files,
                    QDirIterator::Subdirectories);

    bool filesFound = false;
    QTemporaryDir workDir;

    while (it.hasNext()) {
        const QString filePath = it.next();

        // Quick sanity check: must look like a tab-separated FBM file
        {
            CsvReader probe(filePath, "\t", "", true, "\n", 0, "UTF-8");
            if (!probe.readAll()) {
                continue;
            }
            const auto *data = probe.dataRode();
            if (!data->header.contains("order-id") || !data->header.contains("ship-country")) {
                qDebug() << "Skipping (not an FBM file):" << filePath;
                continue;
            }
        }

        filesFound = true;
        qDebug() << "Testing real FBM file:" << filePath;

        ImporterFileAmazonOrdersFBM importer(workDir.path());
        AbstractImporter::ReturnOrderInfos result;
        try {
            result = QCoro::waitFor(importer.loadReport(filePath));
        } catch (const CsvHeaderException &e) {
            QFAIL(qPrintable(e.getErrorColumns("Missing columns in " + e.getFileName())));
        } catch (const std::exception &e) {
            QFAIL(e.what());
        }

        if (!result.errorReturned.isEmpty()) {
            qWarning() << "Import error for" << filePath << ":" << result.errorReturned;
            QFAIL(qPrintable(result.errorReturned));
        }

        QVERIFY(result.orderInfos);
        QVERIFY(!result.orderInfos->shipments.isEmpty());
        QVERIFY(result.orderInfos->dateMin.isValid());
        QVERIFY(result.orderInfos->dateMax.isValid());
        QVERIFY(result.orderInfos->dateMin <= result.orderInfos->dateMax);

        qDebug() << "  Shipments:" << result.orderInfos->shipments.size()
                 << "  Date range:" << result.orderInfos->dateMin
                 << "->" << result.orderInfos->dateMax;
    }

    if (!filesFound) {
        QSKIP("No FBM .txt/.tsv files found in data directory");
    }
}

QTEST_GUILESS_MAIN(TestImporterFileAmazonOrdersFBM)
#include "test_importer_file_amazon_orders_fbm.moc"
