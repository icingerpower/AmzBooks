#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QCoroTask>
#include <QTemporaryDir>

#include "orders/ImporterFileTemuVatEu.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"

// ---------------------------------------------------------------------------
// CSV header matching the real Temu EU VAT report format.
// Note: "PAYS DE DÉPART DE LA TRANSACTION " is quoted in the CSV (trailing space).
// ---------------------------------------------------------------------------
static const QString CSV_HEADER =
    "N° DE COMMANDE,MARKETPLACE,NOM DU CENTRE COMMERCIAL,DATE DE PAIEMENT DE LA COMMANDE,"
    "TYPE DE VENTE,SKU DU VENDEUR,TAUX DE TVA DES ARTICLES,QUANTITÉ,"
    "\"PAYS DE DÉPART DE LA TRANSACTION \",PAYS D'ARRIVÉE DE LA TRANSACTION,"
    "PRIX DES ARTICLES (HORS TVA),PRIX DE LA SUBVENTION (HORS TVA),"
    "MONTANT DE LA SUBVENTION POUR L'EXPÉDITION (TVA non incluse),"
    "PRIX D'EXPÉDITION (HORS TVA),"
    "Achat d'étiquettes d'expédition pour les retours pris en charge par la plateforme (TVA incluse le cas échéant),"
    "Ajustement de l'achat d'étiquettes d'expédition pour les retours pris en charge par la plateforme (TVA incluse le cas échéant),"
    "MONTANT DE LA TVA DES ARTICLES,MONTANT DE LA TVA DE LA SUBVENTION,"
    "MONTANT DE LA TVA SUR LA SUBVENTION POUR L'EXPÉDITION,MONTANT DE LA TVA DE L'EXPÉDITION,"
    "TAXE TOTALE,DEVISE,ID DE FACTURE,ID DE FACTURE DE SUBVENTION\n";

// ---------------------------------------------------------------------------
// Convenience macro: build one data row matching the CSV_HEADER column order.
// Arguments:
//   orderId, marketplace, shop, date, type, sku, vatRate, qty,
//   depart, arrival,
//   itemExcl, subsidyExcl, shipSubsidy, shippingExcl,
//   retLabelBuy, retLabelAdj,
//   itemVat, subsidyVat, shipSubsidyVat, shippingVat,
//   totalTax, currency, invoiceId, subsidyInvoiceId
// ---------------------------------------------------------------------------
#define CSV_ROW(orderId, marketplace, shop, date, type, sku, vatRate, qty, \
                depart, arrival, \
                itemExcl, subsidyExcl, shipSubsidy, shippingExcl, \
                retLabelBuy, retLabelAdj, \
                itemVat, subsidyVat, shipSubsidyVat, shippingVat, \
                totalTax, currency, invoiceId, subsidyInvoiceId) \
    QString(orderId) + "," + (marketplace) + "," + (shop) + "," + (date) + "," \
    + (type) + "," + (sku) + "," + (vatRate) + "," + (qty) + "," \
    + (depart) + "," + (arrival) + "," \
    + "\"" + (itemExcl) + "\",\"" + (subsidyExcl) + "\",\"" + (shipSubsidy) + "\",\"" + (shippingExcl) + "\"," \
    + "\"" + (retLabelBuy) + "\",\"" + (retLabelAdj) + "\"," \
    + "\"" + (itemVat) + "\",\"" + (subsidyVat) + "\",\"" + (shipSubsidyVat) + "\",\"" + (shippingVat) + "\"," \
    + "\"" + (totalTax) + "\"," + (currency) + "," + (invoiceId) + "," + (subsidyInvoiceId) + "\n"

// ---------------------------------------------------------------------------

class TestImporterFileTemuVatEu : public QObject
{
    Q_OBJECT

public:
    TestImporterFileTemuVatEu();
    ~TestImporterFileTemuVatEu();

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Real data
    void test_realData();

    // Functional tests
    void test_simpleSale();
    void test_simpleReturn();
    void test_subsidyAmounts();
    void test_shippingSubsidyAmounts();
    void test_multipleItemsPerOrder();
    void test_orderWithSaleAndReturn();
    void test_frenchDateFormats();
    void test_isodateFormat();
    void test_dateRange();
    void test_invoiceIdAssignment();
    void test_creditNoteIdOnReturn();
    void test_columnOrderChanged();
    void test_emptyData();

    // MARKETPLACE → orderId_store
    void test_marketplace_populatesOrderIdStore();
    void test_marketplace_columnAbsent();

    // Error / edge-case tests
    void test_missingRequiredColumn();
    void test_invalidDateRowSkipped();
    void test_unknownTypeRowSkipped();
    void test_emptyOrderIdSkipped();
    void test_emptySkuSkipped();

private:
    QString m_dataDir;

    QString createTempCsv(const QString &content, QTemporaryDir &tempDir,
                          const QString &fileName = "test.csv");
};

// ---------------------------------------------------------------------------
TestImporterFileTemuVatEu::TestImporterFileTemuVatEu() {}
TestImporterFileTemuVatEu::~TestImporterFileTemuVatEu() {}

void TestImporterFileTemuVatEu::initTestCase()
{
    // Locate the real data directory (build system copies it next to the executable)
    QDir appDir(QCoreApplication::applicationDirPath());
    QString candidate = appDir.absoluteFilePath("data/temu-vat-reports");
    if (QFileInfo::exists(candidate))
    {
        m_dataDir = candidate;
    }
    else
    {
        // Walk up a few levels to find the source tree
        QDir searchDir = appDir;
        for (int i = 0; i < 5; ++i)
        {
            QString p = searchDir.absoluteFilePath("data/temu-vat-reports");
            if (QFileInfo::exists(p)) { m_dataDir = p; break; }
            if (!searchDir.cdUp() || searchDir.isRoot()) break;
        }
        if (m_dataDir.isEmpty())
        {
            m_dataDir = QDir::current().absoluteFilePath("data/temu-vat-reports");
            qWarning() << "Could not locate data/temu-vat-reports, using:" << m_dataDir;
        }
    }
    qDebug() << "Temu VAT data dir:" << m_dataDir;
}

void TestImporterFileTemuVatEu::cleanupTestCase() {}

QString TestImporterFileTemuVatEu::createTempCsv(const QString &content,
                                                  QTemporaryDir &tempDir,
                                                  const QString &fileName)
{
    QString path = tempDir.filePath(fileName);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    f.close();
    return path;
}

// ===========================================================================
// test_realData
// Load every CSV file found under data/temu-vat-reports and expect no errors.
// ===========================================================================
void TestImporterFileTemuVatEu::test_realData()
{
    if (!QFileInfo::exists(m_dataDir))
        QSKIP("Real data directory not found, skipping test_realData");

    QDirIterator it(m_dataDir, QStringList() << "*.csv", QDir::Files,
                    QDirIterator::Subdirectories);

    QTemporaryDir workDir;
    QVERIFY(workDir.isValid());
    ImporterFileTemuVatEu importer(workDir.path());

    bool filesFound = false;
    int totalShipments = 0;
    int totalRefunds   = 0;

    while (it.hasNext())
    {
        QString filePath = it.next();
        filesFound = true;
        qDebug() << "Testing real file:" << filePath;

        auto result = QCoro::waitFor(importer.loadReport(filePath));

        QVERIFY2(result.errorReturned.isEmpty(),
                 qPrintable("Error in " + filePath + ": " + result.errorReturned));
        QVERIFY(result.orderInfos);

        totalShipments += result.orderInfos->shipments.size();
        totalRefunds   += result.orderInfos->refunds.size();
        qDebug() << "  shipments:" << result.orderInfos->shipments.size()
                 << "  refunds:"   << result.orderInfos->refunds.size()
                 << "  dateMin:"   << result.orderInfos->dateMin
                 << "  dateMax:"   << result.orderInfos->dateMax;
    }

    QVERIFY2(filesFound, "No CSV files found in temu-vat-reports directory");
    QVERIFY2(totalShipments > 0, "Expected at least some shipments from real data");
    QVERIFY2(totalRefunds   > 0, "Expected at least some refunds from real data");
    qDebug() << "Total shipments:" << totalShipments << "  refunds:" << totalRefunds;
}

// ===========================================================================
// test_simpleSale
// One sales row with ISO date and no subsidy.
// Amounts: item=11.09, shipping=2.49, totalTax=2.72 → totalIncl=16.30
// ===========================================================================
void TestImporterFileTemuVatEu::test_simpleSale()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    content += CSV_ROW("PO-069-00000000000000001","FR","TestShop","2025-11-15",
                       "sales","SKU001","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-FR-TEST-001","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    QCOMPARE(result.orderInfos->shipments.size(), 1);
    QCOMPARE(result.orderInfos->refunds.size(),   0);

    const auto &shipment = result.orderInfos->shipments.first();
    QCOMPARE(shipment.getActivities().size(), 1);

    const auto &act = shipment.getActivities().first();
    QCOMPARE(act.getEventId(),       QString("PO-069-00000000000000001"));
    QCOMPARE(act.getActivityId(),    QString("PO-069-00000000000000001__FR")); // orderId__departure
    QCOMPARE(act.getSubActivityId(), QString("SKU001"));                        // sku as sub-ID
    QCOMPARE(act.getCountryCodeFrom(), QString("FR"));
    QCOMPARE(act.getCountryCodeTo(),   QString("FR"));
    QVERIFY(qAbs(act.getAmountTaxed() - 16.30) < 0.01);
    QVERIFY(qAbs(act.getAmountTaxes()  -  2.72) < 0.01);

    QCOMPARE(result.orderInfos->dateMin, QDate(2025, 11, 15));
    QCOMPARE(result.orderInfos->dateMax, QDate(2025, 11, 15));

    qDebug() << "test_simpleSale: amountTaxed=" << act.getAmountTaxed()
             << " tax=" << act.getAmountTaxes();
}

// ===========================================================================
// test_simpleReturn
// One return row: all amounts are negative.
// Amounts: item=-11.09, shipping=-2.49, totalTax=-2.72 → totalIncl=-16.30
// ===========================================================================
void TestImporterFileTemuVatEu::test_simpleReturn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    content += CSV_ROW("PO-069-00000000000000002","FR","TestShop","2025-12-05",
                       "return","SKU002","20%","1","FR","FR",
                       "-11,09","0,00","0,00","-2,49","0,00","0,00",
                       "-2,22","0,00","0,00","-0,50",
                       "-2,72","EUR","CN-FR-TEST-001","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    QCOMPARE(result.orderInfos->shipments.size(), 0);
    QCOMPARE(result.orderInfos->refunds.size(),   1);

    const auto &refund = result.orderInfos->refunds.first();
    QCOMPARE(refund.getActivities().size(), 1);

    const auto &act = refund.getActivities().first();
    QCOMPARE(act.getActivityId(),    QString("PO-069-00000000000000002__FR")); // orderId__departure
    QCOMPARE(act.getSubActivityId(), QString("SKU002"));
    QVERIFY(qAbs(act.getAmountTaxed() - (-16.30)) < 0.01);
    QVERIFY(qAbs(act.getAmountTaxes()  - (- 2.72)) < 0.01);

    QCOMPARE(result.orderInfos->dateMin, QDate(2025, 12, 5));
    QCOMPARE(result.orderInfos->dateMax, QDate(2025, 12, 5));
}

// ===========================================================================
// test_subsidyAmounts
// Row where the marketplace subsidises the item price (non-zero subsidy column).
// item=6.92, subsidy=3.07, shipping=2.49, totalTax=2.50 → totalIncl=14.98
// ===========================================================================
void TestImporterFileTemuVatEu::test_subsidyAmounts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    content += CSV_ROW("PO-069-00000000000000003","FR","TestShop","2025-11-29",
                       "sales","SKU003","20%","1","FR","FR",
                       "6,92","3,07","0,00","2,49","0,00","0,00",
                       "1,39","0,61","0,00","0,50",
                       "2,50","EUR","INV-FR-TEST-003","INV-FR-TEST-003-S");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);

    const auto &act = result.orderInfos->shipments.first().getActivities().first();
    // totalExcl = 6.92+3.07+0+2.49 = 12.48; totalIncl = 12.48+2.50 = 14.98
    QVERIFY(qAbs(act.getAmountTaxed() - 14.98) < 0.01);
    QVERIFY(qAbs(act.getAmountTaxes()  -  2.50) < 0.01);
}

// ===========================================================================
// test_shippingSubsidyAmounts
// Row where the shipping subsidy column (optional) has a non-zero value.
// item=10.00, subsidy=0, ship_subsidy=1.00, shipping=2.00, totalTax=2.60
// totalExcl=13.00, totalIncl=15.60
// ===========================================================================
void TestImporterFileTemuVatEu::test_shippingSubsidyAmounts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    content += CSV_ROW("PO-069-00000000000000010","FR","TestShop","2025-11-20",
                       "sales","SKU010","20%","1","FR","FR",
                       "10,00","0,00","1,00","2,00","0,00","0,00",
                       "2,00","0,00","0,20","0,40",
                       "2,60","EUR","INV-FR-TEST-010","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);

    const auto &act = result.orderInfos->shipments.first().getActivities().first();
    // totalExcl = 10+0+1+2=13; totalIncl = 13+2.60=15.60
    QVERIFY(qAbs(act.getAmountTaxed() - 15.60) < 0.01);
    QVERIFY(qAbs(act.getAmountTaxes()   -  2.60) < 0.01);
}

// ===========================================================================
// test_multipleItemsPerOrder
// Two rows sharing the same orderId and type → one Shipment with two Activities.
// ===========================================================================
void TestImporterFileTemuVatEu::test_multipleItemsPerOrder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    // Row 1: totalIncl = 11.09+2.49+2.72 = 16.30
    content += CSV_ROW("PO-069-00000000000000004","FR","TestShop","2025-12-07",
                       "sales","SKU004A","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-FR-TEST-004","");
    // Row 2: item=5.00, shipping=1.00, totalTax=1.20 → totalExcl=6.00, totalIncl=7.20
    content += CSV_ROW("PO-069-00000000000000004","FR","TestShop","2025-12-07",
                       "sales","SKU004B","20%","1","FR","FR",
                       "5,00","0,00","0,00","1,00","0,00","0,00",
                       "1,00","0,00","0,00","0,20",
                       "1,20","EUR","INV-FR-TEST-004","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    // Both rows belong to same orderId+type → exactly 1 Shipment
    QCOMPARE(result.orderInfos->shipments.size(), 1);
    QCOMPARE(result.orderInfos->refunds.size(),   0);

    const auto &shipment = result.orderInfos->shipments.first();
    QCOMPARE(shipment.getActivities().size(), 2);

    // Verify individual activity amounts
    double totalTaxed = 0.0;
    for (const auto &act : shipment.getActivities())
        totalTaxed += act.getAmountTaxed();
    QVERIFY(qAbs(totalTaxed - (16.30 + 7.20)) < 0.01);

    // With the orderId__departure format, all items in the same order share
    // the same activityId; distinctness is carried by the sub-activity ID (sku).
    QStringList subActIds;
    for (const auto &act : shipment.getActivities())
        subActIds << act.getSubActivityId();
    QCOMPARE(QSet<QString>(subActIds.begin(), subActIds.end()).size(), 2); // two distinct SKUs
}

// ===========================================================================
// test_orderWithSaleAndReturn
// Same orderId → one Shipment AND one Refund.
// ===========================================================================
void TestImporterFileTemuVatEu::test_orderWithSaleAndReturn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    content += CSV_ROW("PO-069-00000000000000005","FR","TestShop","2025-12-10",
                       "sales","SKU005","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-FR-TEST-005","");
    content += CSV_ROW("PO-069-00000000000000005","FR","TestShop","2025-12-15",
                       "return","SKU005","20%","1","FR","FR",
                       "-11,09","0,00","0,00","-2,49","0,00","0,00",
                       "-2,22","0,00","0,00","-0,50",
                       "-2,72","EUR","CN-FR-TEST-005","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);
    QCOMPARE(result.orderInfos->refunds.size(),   1);

    // Sale amount positive, refund amount negative
    const auto &saleAct   = result.orderInfos->shipments.first().getActivities().first();
    const auto &refundAct = result.orderInfos->refunds.first().getActivities().first();
    QVERIFY(saleAct.getAmountTaxed()   > 0);
    QVERIFY(refundAct.getAmountTaxed() < 0);
    QVERIFY(qAbs(saleAct.getAmountTaxed() + refundAct.getAmountTaxed()) < 0.01);
}

// ===========================================================================
// test_frenchDateFormats
// Verify all French abbreviated month names parse to the correct QDate.
// ===========================================================================
void TestImporterFileTemuVatEu::test_frenchDateFormats()
{
    struct DateCase { QString dateStr; QDate expected; };
    const QList<DateCase> cases = {
        {"1 janv. 2026",  QDate(2026,  1,  1)},
        {"28 févr. 2026", QDate(2026,  2, 28)},
        {"3 mars 2026",   QDate(2026,  3,  3)},
        {"15 avr. 2026",  QDate(2026,  4, 15)},
        {"1 mai 2026",    QDate(2026,  5,  1)},
        {"30 juin 2025",  QDate(2025,  6, 30)},
        {"14 juil. 2025", QDate(2025,  7, 14)},
        {"31 août 2025",  QDate(2025,  8, 31)},
        {"12 sept. 2025", QDate(2025,  9, 12)},
        {"20 oct. 2025",  QDate(2025, 10, 20)},
        {"5 nov. 2025",   QDate(2025, 11,  5)},
        {"25 déc. 2025",  QDate(2025, 12, 25)},
    };

    for (const auto &c : cases)
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        QString content = CSV_HEADER;
        content += CSV_ROW("PO-069-00000000000001000","FR","TestShop",c.dateStr,
                           "sales","SKU-DATE","20%","1","FR","FR",
                           "10,00","0,00","0,00","2,00","0,00","0,00",
                           "2,00","0,00","0,00","0,40",
                           "2,40","EUR","INV-DATE","");

        QString safeName = c.dateStr;
        safeName.replace(' ', '_').replace('.', '_');
        QString file = createTempCsv(content, tempDir, "date_" + safeName + ".csv");

        QTemporaryDir workDir;
        ImporterFileTemuVatEu importer(workDir.path());
        auto result = QCoro::waitFor(importer.loadReport(file));

        QVERIFY2(result.errorReturned.isEmpty(),
                 qPrintable("Date '" + c.dateStr + "': " + result.errorReturned));
        QCOMPARE(result.orderInfos->shipments.size(), 1);
        QCOMPARE(result.orderInfos->dateMin, c.expected);
    }
}

// ===========================================================================
// test_isodateFormat
// ISO date "yyyy-MM-dd" should parse correctly.
// ===========================================================================
void TestImporterFileTemuVatEu::test_isodateFormat()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    content += CSV_ROW("PO-069-00000000000000006","FR","TestShop","2025-11-22",
                       "sales","SKU006","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-FR-TEST-006","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);
    QCOMPARE(result.orderInfos->dateMin, QDate(2025, 11, 22));
}

// ===========================================================================
// test_dateRange
// Multiple rows with different dates; verify dateMin and dateMax are correct.
// ===========================================================================
void TestImporterFileTemuVatEu::test_dateRange()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    // Earliest date
    content += CSV_ROW("PO-069-00000000000000101","FR","TestShop","2025-11-01",
                       "sales","SKU101","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","","");
    // Middle
    content += CSV_ROW("PO-069-00000000000000102","FR","TestShop","1 déc. 2025",
                       "return","SKU102","20%","1","FR","FR",
                       "-11,09","0,00","0,00","-2,49","0,00","0,00",
                       "-2,22","0,00","0,00","-0,50","-2,72","EUR","","");
    // Latest date
    content += CSV_ROW("PO-069-00000000000000103","FR","TestShop","26 janv. 2026",
                       "sales","SKU103","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 2);
    QCOMPARE(result.orderInfos->refunds.size(),   1);
    QCOMPARE(result.orderInfos->dateMin, QDate(2025, 11,  1));
    QCOMPARE(result.orderInfos->dateMax, QDate(2026,  1, 26));
}

// ===========================================================================
// test_invoiceIdAssignment
// Invoice ID from the CSV is stored and accessible via InvoicingInfos.
// ===========================================================================
void TestImporterFileTemuVatEu::test_invoiceIdAssignment()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    // Order with invoice ID
    content += CSV_ROW("PO-069-00000000000000007","FR","TestShop","2025-12-15",
                       "sales","SKU007","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-FR-2437569-2025-1","");
    // Return without invoice ID
    content += CSV_ROW("PO-069-00000000000000008","FR","TestShop","2025-12-20",
                       "return","SKU008","20%","1","FR","FR",
                       "-11,09","0,00","0,00","-2,49","0,00","0,00",
                       "-2,22","0,00","0,00","-0,50",
                       "-2,72","EUR","","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    // The sale with a valid invoice should produce one InvoicingInfo
    bool foundInvoice = false;
    for (const auto &info : result.orderInfos->invoicingInfos)
    {
        auto num = info.invoicingInfo.getInvoiceNumber();
        if (num.has_value() && num.value() == "INV-FR-2437569-2025-1")
            foundInvoice = true;
    }
    QVERIFY2(foundInvoice, "Expected InvoicingInfo with invoice number INV-FR-2437569-2025-1");
}

// ===========================================================================
// test_creditNoteIdOnReturn
// Returns with a credit note ID (CN-FR-...) → InvoicingInfo with that ID.
// ===========================================================================
void TestImporterFileTemuVatEu::test_creditNoteIdOnReturn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    content += CSV_ROW("PO-069-00000000000000009","FR","TestShop","11 déc. 2025",
                       "return","SKU009","20%","0","FR","FR",
                       "-3,87","0,00","0,00","-2,49","0,00","0,00",
                       "-0,78","0,00","0,00","-0,50",
                       "-1,28","EUR","CN-FR-2437569-2025-1","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->refunds.size(), 1);

    bool foundCN = false;
    for (const auto &info : result.orderInfos->invoicingInfos)
    {
        auto num = info.invoicingInfo.getInvoiceNumber();
        if (num.has_value() && num.value() == "CN-FR-2437569-2025-1")
            foundCN = true;
    }
    QVERIFY2(foundCN, "Expected InvoicingInfo with credit note CN-FR-2437569-2025-1");
}

// ===========================================================================
// test_columnOrderChanged
// CSV with columns in a different order; parsing must still succeed.
// ===========================================================================
void TestImporterFileTemuVatEu::test_columnOrderChanged()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Shuffled header (only required columns, with departure country quoted)
    QString header =
        "TYPE DE VENTE,TAXE TOTALE,\"PAYS DE DÉPART DE LA TRANSACTION \",SKU DU VENDEUR,"
        "N° DE COMMANDE,DEVISE,PRIX D'EXPÉDITION (HORS TVA),MARKETPLACE,"
        "PAYS D'ARRIVÉE DE LA TRANSACTION,DATE DE PAIEMENT DE LA COMMANDE,"
        "PRIX DES ARTICLES (HORS TVA),PRIX DE LA SUBVENTION (HORS TVA),"
        "MONTANT DE LA SUBVENTION POUR L'EXPÉDITION (TVA non incluse),"
        "ID DE FACTURE,ID DE FACTURE DE SUBVENTION\n";

    // Data row matching the shuffled column order above
    QString row =
        "sales,\"2,72\",FR,SKU-SHUFFLED,"
        "PO-069-00000000000000020,EUR,\"2,49\",FR,"
        "FR,2025-11-15,"
        "\"11,09\",\"0,00\","
        "\"0,00\","
        "INV-SHUFFLE,\n";

    QString content = header + row;
    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);

    const auto &act = result.orderInfos->shipments.first().getActivities().first();
    QCOMPARE(act.getActivityId(),    QString("PO-069-00000000000000020__FR")); // orderId__departure
    QCOMPARE(act.getSubActivityId(), QString("SKU-SHUFFLED"));
    QCOMPARE(act.getCountryCodeTo(), QString("FR"));
    // totalExcl = 11.09+0+0+2.49=13.58; totalIncl=13.58+2.72=16.30
    QVERIFY(qAbs(act.getAmountTaxed() - 16.30) < 0.01);
}

// ===========================================================================
// test_emptyData
// Header only, no data rows → 0 shipments, 0 refunds, no error.
// ===========================================================================
void TestImporterFileTemuVatEu::test_emptyData()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString file = createTempCsv(CSV_HEADER, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 0);
    QCOMPARE(result.orderInfos->refunds.size(),   0);
}

// ===========================================================================
// test_missingRequiredColumn
// A missing required column must produce a non-empty errorReturned.
// ===========================================================================
void TestImporterFileTemuVatEu::test_missingRequiredColumn()
{
    // Columns to individually omit (each run removes just one column)
    // We test a subset of the required ones to keep runtime manageable.
    const QStringList columnsToOmit = {
        "N° DE COMMANDE",
        "DATE DE PAIEMENT DE LA COMMANDE",
        "TYPE DE VENTE",
        "SKU DU VENDEUR",
        "TAXE TOTALE",
        "DEVISE",
        "ID DE FACTURE"
    };

    // Build a minimal but valid header (departure is quoted)
    auto buildHeader = [](const QString &omit) -> QString {
        QStringList cols = {
            "N° DE COMMANDE",
            "MARKETPLACE",
            "DATE DE PAIEMENT DE LA COMMANDE",
            "TYPE DE VENTE",
            "SKU DU VENDEUR",
            "\"PAYS DE DÉPART DE LA TRANSACTION \"",
            "PAYS D'ARRIVÉE DE LA TRANSACTION",
            "PRIX DES ARTICLES (HORS TVA)",
            "PRIX DE LA SUBVENTION (HORS TVA)",
            "MONTANT DE LA SUBVENTION POUR L'EXPÉDITION (TVA non incluse)",
            "PRIX D'EXPÉDITION (HORS TVA)",
            "TAXE TOTALE",
            "DEVISE",
            "ID DE FACTURE",
            "ID DE FACTURE DE SUBVENTION"
        };
        // Remove the column to omit (match by content, ignoring quotes)
        cols.removeIf([&omit](const QString &c){
            return QString(c).remove('"').trimmed() == omit.trimmed();
        });
        return cols.join(",") + "\n";
    };

    for (const QString &missing : columnsToOmit)
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        QString content = buildHeader(missing);
        content += "PO-001,FR,2025-11-15,sales,SKU1,FR,FR,\"11,09\",\"0,00\",\"0,00\",\"2,49\",\"2,72\",EUR,,\n";

        QString file = createTempCsv(content, tempDir,
                                     "missing_" + missing.left(10).replace(' ','_') + ".csv");
        QTemporaryDir workDir;
        ImporterFileTemuVatEu importer(workDir.path());
        auto result = QCoro::waitFor(importer.loadReport(file));

        QVERIFY2(!result.errorReturned.isEmpty(),
                 qPrintable("Expected error when column '" + missing + "' is missing"));
        QVERIFY2(result.errorReturned.contains("Missing column"),
                 qPrintable("Error should mention 'Missing column' for: " + missing
                            + " actual: " + result.errorReturned));
        qDebug() << "Missing column '" << missing << "' → error:" << result.errorReturned;
    }
}

// ===========================================================================
// test_invalidDateRowSkipped
// A row with an unrecognisable date is silently skipped; other valid rows still
// produce shipments.
// ===========================================================================
void TestImporterFileTemuVatEu::test_invalidDateRowSkipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    // Valid row
    content += CSV_ROW("PO-069-00000000000000030","FR","TestShop","2025-11-20",
                       "sales","SKU030","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","INV-030","");
    // Row with an invalid date — should be skipped, NOT cause errorReturned
    content += CSV_ROW("PO-069-00000000000000031","FR","TestShop","INVALID-DATE",
                       "sales","SKU031","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","INV-031","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    // Only the valid row should produce a shipment
    QCOMPARE(result.orderInfos->shipments.size(), 1);
    QCOMPARE(result.orderInfos->shipments.first().getActivities().first().getEventId(),
             QString("PO-069-00000000000000030"));
}

// ===========================================================================
// test_unknownTypeRowSkipped
// Rows with an unrecognised TYPE DE VENTE value are silently skipped.
// ===========================================================================
void TestImporterFileTemuVatEu::test_unknownTypeRowSkipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    // Unknown type
    content += CSV_ROW("PO-069-00000000000000040","FR","TestShop","2025-11-10",
                       "adjustment","SKU040","20%","1","FR","FR",
                       "0,00","0,00","0,00","0,00","0,00","0,00",
                       "0,00","0,00","0,00","0,00","0,00","EUR","","");
    // Valid row
    content += CSV_ROW("PO-069-00000000000000041","FR","TestShop","2025-11-10",
                       "sales","SKU041","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","INV-041","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);
    QCOMPARE(result.orderInfos->shipments.first().getActivities().first().getEventId(),
             QString("PO-069-00000000000000041"));
}

// ===========================================================================
// test_emptyOrderIdSkipped
// Rows where N° DE COMMANDE is empty are silently skipped.
// ===========================================================================
void TestImporterFileTemuVatEu::test_emptyOrderIdSkipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    // Row with empty orderId
    content += CSV_ROW("","FR","TestShop","2025-11-10",
                       "sales","SKU050","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","","");
    // Valid row
    content += CSV_ROW("PO-069-00000000000000051","FR","TestShop","2025-11-10",
                       "sales","SKU051","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","INV-051","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);
}

// ===========================================================================
// test_emptySkuSkipped
// Rows where SKU DU VENDEUR is empty are silently skipped.
// ===========================================================================
void TestImporterFileTemuVatEu::test_emptySkuSkipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;
    // Row with empty SKU
    content += CSV_ROW("PO-069-00000000000000060","FR","TestShop","2025-11-10",
                       "sales","","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","","");
    // Valid row
    content += CSV_ROW("PO-069-00000000000000061","FR","TestShop","2025-11-10",
                       "sales","SKU061","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50","2,72","EUR","INV-061","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);
}

// ===========================================================================
// test_marketplace_populatesOrderIdStore
// orderId_store[orderId] must equal "temu." + MARKETPLACE.toLower().
// Covers: uppercase input, already-lowercase input, return rows, empty value.
// ===========================================================================
void TestImporterFileTemuVatEu::test_marketplace_populatesOrderIdStore()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString content = CSV_HEADER;

    // Order 1 — MARKETPLACE = "FR" (uppercase) → "temu.fr"
    content += CSV_ROW("PO-069-00000000000000070","FR","TestShop","2025-11-15",
                       "sales","SKU070","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-070","");

    // Order 2 — MARKETPLACE = "DE" (uppercase) → "temu.de"
    content += CSV_ROW("PO-069-00000000000000071","DE","TestShop","2025-11-20",
                       "sales","SKU071","20%","1","DE","DE",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-071","");

    // Order 3 — MARKETPLACE = "it" (already lowercase) → "temu.it"
    content += CSV_ROW("PO-069-00000000000000072","it","TestShop","2025-11-25",
                       "sales","SKU072","20%","1","IT","IT",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-072","");

    // Order 4 — return row with MARKETPLACE = "ES" → "temu.es"
    // Return rows must also populate orderId_store.
    content += CSV_ROW("PO-069-00000000000000073","ES","TestShop","2025-11-28",
                       "return","SKU073","20%","1","ES","ES",
                       "-11,09","0,00","0,00","-2,49","0,00","0,00",
                       "-2,22","0,00","0,00","-0,50",
                       "-2,72","EUR","CN-073","");

    // Order 5 — MARKETPLACE = "" (empty) → orderId_store must NOT contain this id
    content += CSV_ROW("PO-069-00000000000000074","","TestShop","2025-11-30",
                       "sales","SKU074","20%","1","FR","FR",
                       "11,09","0,00","0,00","2,49","0,00","0,00",
                       "2,22","0,00","0,00","0,50",
                       "2,72","EUR","INV-074","");

    QString file = createTempCsv(content, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 4); // orders 1, 2, 3, 5
    QCOMPARE(result.orderInfos->refunds.size(),   1); // order 4

    const auto &store = result.orderInfos->orderId_store;

    // "FR" uppercase → "temu.fr"
    QVERIFY2(store.contains("PO-069-00000000000000070"),
             "orderId_store must contain order 70");
    QCOMPARE(store.value("PO-069-00000000000000070"), QString("temu.fr"));

    // "DE" uppercase → "temu.de"
    QVERIFY2(store.contains("PO-069-00000000000000071"),
             "orderId_store must contain order 71");
    QCOMPARE(store.value("PO-069-00000000000000071"), QString("temu.de"));

    // "it" already lowercase → "temu.it"
    QVERIFY2(store.contains("PO-069-00000000000000072"),
             "orderId_store must contain order 72");
    QCOMPARE(store.value("PO-069-00000000000000072"), QString("temu.it"));

    // return row with "ES" → "temu.es"
    QVERIFY2(store.contains("PO-069-00000000000000073"),
             "orderId_store must contain return order 73");
    QCOMPARE(store.value("PO-069-00000000000000073"), QString("temu.es"));

    // empty MARKETPLACE → no entry
    QVERIFY2(!store.contains("PO-069-00000000000000074"),
             "orderId_store must NOT contain order 74 (empty marketplace)");
}

// ===========================================================================
// test_marketplace_columnAbsent
// When the MARKETPLACE column is absent the importer must not fail and
// orderId_store must remain empty.
// ===========================================================================
void TestImporterFileTemuVatEu::test_marketplace_columnAbsent()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Build a minimal valid header that intentionally omits MARKETPLACE.
    const QString header =
        "N° DE COMMANDE,"
        "DATE DE PAIEMENT DE LA COMMANDE,"
        "TYPE DE VENTE,"
        "SKU DU VENDEUR,"
        "\"PAYS DE DÉPART DE LA TRANSACTION \","
        "PAYS D'ARRIVÉE DE LA TRANSACTION,"
        "PRIX DES ARTICLES (HORS TVA),"
        "PRIX DE LA SUBVENTION (HORS TVA),"
        "PRIX D'EXPÉDITION (HORS TVA),"
        "TAXE TOTALE,"
        "DEVISE,"
        "ID DE FACTURE\n";

    const QString row =
        "PO-069-00000000000000080,"
        "2025-11-15,"
        "sales,"
        "SKU080,"
        "FR,"
        "FR,"
        "\"11,09\",\"0,00\",\"2,49\","
        "\"2,72\","
        "EUR,"
        "INV-080\n";

    QString file = createTempCsv(header + row, tempDir);

    QTemporaryDir workDir;
    ImporterFileTemuVatEu importer(workDir.path());
    auto result = QCoro::waitFor(importer.loadReport(file));

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QCOMPARE(result.orderInfos->shipments.size(), 1);
    QVERIFY2(result.orderInfos->orderId_store.isEmpty(),
             "orderId_store must be empty when MARKETPLACE column is absent");
}

// ===========================================================================

QTEST_GUILESS_MAIN(TestImporterFileTemuVatEu)
#include "test_importer_file_temu_vat_eu.moc"
