#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QCoroTask>

#include "orders/ImporterFileAmazonVatEu.h"
#include "orders/AbstractImporter.h"

// VatFixerTaxually is compiled directly into this test target (see CMakeLists).
#include "gui/vatfixer/VatFixerTaxually.h"

#include <xlsxdocument.h>

// ── helpers ───────────────────────────────────────────────────────────────────

static QString dataDir()
{
    return QCoreApplication::applicationDirPath() + "/data/fixtaxually";
}

// ── test class ────────────────────────────────────────────────────────────────

class TestFixVatTaxually : public QObject
{
    Q_OBJECT

private slots:
    // Verify exactly the 10 known DE orders (Amazon shows 0 VAT, taxually
    // declares VAT) are returned by findVatDiscrepancies — no more, no less.
    void test_findDiscrepancies_DE();

    // Verify exactly 1 known ES order (402-0867102-9157132) is found —
    // no more, no less.  The order appears in the ES ReturnAnalytics xlsx.
    void test_findDiscrepancies_ES();

    // Verify that ImporterFileAmazonVatEu skips the DE orders that have
    // TOTAL_ACTIVITY_VALUE_VAT_AMT = 0 in the vat-eu CSV files.
    void test_importerVatEu_zeroVatOrdersAbsent();

    // Verify that _writefixedTxt produces a file where the discrepancy orders
    // have their VAT fields updated (Fix action) and the rest are unchanged.
    void test_writefixedTxt_fixAction();

    // Verify that fixInventoryValue handles the "full-inventory" Type 2 xlsx
    // (Inventory sheet only, no "Missing Sku list").  When all prices are already
    // present the result must report zero fixed and zero missing SKUs, and the
    // output file must contain the expected SKU/price pairs.
    void test_fixInventoryValue_fullInventoryType();
};

// ── DE discrepancies ──────────────────────────────────────────────────────────

void TestFixVatTaxually::test_findDiscrepancies_DE()
{
    const QString dir = dataDir();
    const QString summaryXlsx = dir + "/ReturnAnalytics_Icinger_Power_DE_2026_01.xlsx";
    const QStringList txtFiles = {
        dir + "/2026-01-automated_download_19032_A4ULFPTF5ZHJH_20260101_20260131_20260206-110408.3243.txt",
        dir + "/2026-02-automated_download_19032_A4ULFPTF5ZHJH_20260201_20260228_20260303-042003.2358.txt",
        dir + "/2026-03-automated_download_19032_A4ULFPTF5ZHJH_20260301_20260331_20260403-044630.4793.txt",
    };

    VatFixerTaxually fixer;
    const QList<VatOrderEntry> discrepancies = fixer.findVatDiscrepancies(summaryXlsx, txtFiles);

    // Build a map for easy lookup and collect found IDs.
    QHash<QString, VatOrderEntry> foundById;
    QSet<QString> foundIds;
    for (const VatOrderEntry &e : discrepancies) {
        foundById.insert(e.orderId, e);
        foundIds.insert(e.orderId);

        // No entry should have a date before year 2000 — that indicates an
        // Excel date cell being mis-read as the serial-number epoch.
        QVERIFY2(e.date.isValid() && e.date.year() >= 2000,
                 qPrintable(QString("Entry %1 has invalid/pre-2000 date: %2")
                            .arg(e.orderId, e.date.toString(Qt::ISODate))));
    }

    const QSet<QString> expectedIds = {
        QStringLiteral("305-2990746-2525156"),
        QStringLiteral("303-4657426-6725927"),
        QStringLiteral("305-0973078-1989937"),
        QStringLiteral("305-3383714-3261124"),
        QStringLiteral("028-7577964-2445114"),
        QStringLiteral("304-7829022-0903521"),
        QStringLiteral("302-0296521-9390778"),
        QStringLiteral("302-9577202-9233123"),
        QStringLiteral("305-4836730-9584360"),
        QStringLiteral("305-4302845-2102729"),
    };

    // Exact match — every expected order must be present …
    for (const QString &orderId : std::as_const(expectedIds)) {
        QVERIFY2(foundById.contains(orderId),
                 qPrintable(QString("DE order not found in discrepancies: %1").arg(orderId)));
        const VatOrderEntry &e = foundById.value(orderId);
        QVERIFY2(e.taxuallyVat > 0.0,
                 qPrintable(QString("taxuallyVat should be > 0 for %1").arg(orderId)));
        QVERIFY2(qAbs(e.amazonVat) < 0.01,
                 qPrintable(QString("amazonVat should be 0 for %1 (got %2)")
                            .arg(orderId).arg(e.amazonVat)));
    }

    // … and no unexpected order may appear.
    const QSet<QString> unexpected = foundIds - expectedIds;
    QVERIFY2(unexpected.isEmpty(),
             qPrintable(QString("Unexpected DE discrepancies: %1")
                        .arg(QStringList(unexpected.values()).join(", "))));
}

// ── ES discrepancies ──────────────────────────────────────────────────────────

void TestFixVatTaxually::test_findDiscrepancies_ES()
{
    const QString dir = dataDir();
    // The ES ReturnAnalytics covers all three months.
    const QString summaryXlsx = dir + "/ReturnAnalytics_Icinger_Power_ES_2026_01.xlsx";
    const QStringList txtFiles = {
        dir + "/2026-01-automated_download_19032_A4ULFPTF5ZHJH_20260101_20260131_20260206-110408.3243.txt",
        dir + "/2026-02-automated_download_19032_A4ULFPTF5ZHJH_20260201_20260228_20260303-042003.2358.txt",
        dir + "/2026-03-automated_download_19032_A4ULFPTF5ZHJH_20260301_20260331_20260403-044630.4793.txt",
    };

    VatFixerTaxually fixer;
    const QList<VatOrderEntry> discrepancies = fixer.findVatDiscrepancies(summaryXlsx, txtFiles);

    QSet<QString> foundIds;
    for (const VatOrderEntry &e : discrepancies) {
        foundIds.insert(e.orderId);

        // No entry should have a date before year 2000.
        QVERIFY2(e.date.isValid() && e.date.year() >= 2000,
                 qPrintable(QString("Entry %1 has invalid/pre-2000 date: %2")
                            .arg(e.orderId, e.date.toString(Qt::ISODate))));
    }

    const QSet<QString> expectedIds = {
        QStringLiteral("402-0867102-9157132"),
    };

    // Exact match — must be present …
    QVERIFY2(foundIds.contains(QStringLiteral("402-0867102-9157132")),
             "ES order 402-0867102-9157132 not found in discrepancies");

    for (const VatOrderEntry &e : discrepancies) {
        if (e.orderId != QLatin1String("402-0867102-9157132")) {
            continue;
        }
        QVERIFY2(e.taxuallyVat > 0.0, "taxuallyVat should be > 0 for ES order");
        QVERIFY2(qAbs(e.amazonVat) < 0.01, "amazonVat should be 0 for ES order");
    }

    // … and no unexpected order may appear.
    const QSet<QString> unexpected = foundIds - expectedIds;
    QVERIFY2(unexpected.isEmpty(),
             qPrintable(QString("Unexpected ES discrepancies: %1")
                        .arg(QStringList(unexpected.values()).join(", "))));
}

// ── ImporterFileAmazonVatEu skips zero-VAT orders ─────────────────────────────

void TestFixVatTaxually::test_importerVatEu_zeroVatOrdersAbsent()
{
    const QString dir = dataDir();

    // The DE orders with 0 VAT should be absent from the imported shipments
    // because ImporterFileAmazonVatEu skips REGULAR SALE rows where
    // TOTAL_ACTIVITY_VALUE_VAT_AMT = 0 and no invoice number is present.
    const QStringList csvFiles = {
        dir + "/vat-eu-2026-01.csv",
        dir + "/vat-eu-2026-02.csv",
        dir + "/vat-eu-2026-03.csv",
    };

    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "Failed to create temporary directory for importer");
    ImporterFileAmazonVatEu importer(QDir(tmpDir.path()));

    QSet<QString> importedOrderIds;
    for (const QString &csvPath : csvFiles) {
        const auto result = QCoro::waitFor(importer.loadReport(csvPath));
        QVERIFY2(result.errorReturned.isEmpty(),
                 qPrintable(QString("Import error for %1: %2")
                            .arg(csvPath, result.errorReturned)));

        const auto &infos = result.orderInfos;
        for (auto it = infos->orderId_infos.cbegin();
             it != infos->orderId_infos.cend(); ++it) {
            importedOrderIds.insert(it.key());
        }
    }

    const QStringList zeroVatOrders = {
        QStringLiteral("305-2990746-2525156"),
        QStringLiteral("303-4657426-6725927"),
        QStringLiteral("305-0973078-1989937"),
        QStringLiteral("305-3383714-3261124"),
        QStringLiteral("028-7577964-2445114"),
        QStringLiteral("304-7829022-0903521"),
        QStringLiteral("302-0296521-9390778"),
        QStringLiteral("302-9577202-9233123"),
        QStringLiteral("305-4836730-9584360"),
        QStringLiteral("305-4302845-2102729"),
    };

    for (const QString &orderId : zeroVatOrders) {
        QVERIFY2(!importedOrderIds.contains(orderId),
                 qPrintable(QString("Zero-VAT order should be absent from import: %1")
                            .arg(orderId)));
    }
}

// ── writefixedTxt fix action ──────────────────────────────────────────────────

void TestFixVatTaxually::test_writefixedTxt_fixAction()
{
    const QString dir = dataDir();
    const QString summaryXlsx = dir + "/ReturnAnalytics_Icinger_Power_DE_2026_01.xlsx";
    const QString txtPath =
        dir + "/2026-01-automated_download_19032_A4ULFPTF5ZHJH_20260101_20260131_20260206-110408.3243.txt";

    VatFixerTaxually fixer;
    const QList<VatOrderEntry> discrepancies =
        fixer.findVatDiscrepancies(summaryXlsx, {txtPath});

    // Every discrepancy must carry a non-empty sourceFile and a positive taxRate
    // — these fields are required for the write step to function correctly.
    for (const VatOrderEntry &e : discrepancies) {
        QVERIFY2(!e.sourceFile.isEmpty(),
                 qPrintable(QString("sourceFile is empty for order %1").arg(e.orderId)));
        QVERIFY2(e.taxRate > 0.0,
                 qPrintable(QString("taxRate is 0 for order %1").arg(e.orderId)));
    }

    // At least one discrepancy must come from the Jan TXT file.
    QHash<QString, double> fixRates;
    for (const VatOrderEntry &e : discrepancies) {
        if (e.sourceFile == txtPath) {
            fixRates.insert(e.orderId, e.taxRate);
        }
    }
    QVERIFY2(!fixRates.isEmpty(), "No discrepancies matched the Jan TXT file");

    // Write the fixed file directly (bypasses the dialog).
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QString fixedPath = tmpDir.filePath("fixed.txt");
    fixer.writefixedTxt(txtPath, fixedPath, {}, fixRates);

    QVERIFY2(QFile::exists(fixedPath), "writefixedTxt did not create the output file");

    // Verify every discrepancy order now has VAT > 0 in the fixed file.
    QFile f(fixedPath);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&f);
    const QStringList hdrs = in.readLine().split('\t');
    const int idxId  = hdrs.indexOf(QStringLiteral("TRANSACTION_EVENT_ID"));
    const int idxVat = hdrs.indexOf(QStringLiteral("TOTAL_ACTIVITY_VALUE_VAT_AMT"));
    QVERIFY(idxId  >= 0);
    QVERIFY(idxVat >= 0);

    while (!in.atEnd()) {
        const QStringList fields = in.readLine().split('\t');
        if (fields.size() <= idxVat) { continue; }
        const QString orderId = fields.value(idxId).trimmed();
        if (!fixRates.contains(orderId)) { continue; }
        const double vat = fields.value(idxVat).replace(',', '.').toDouble();
        QVERIFY2(vat > 0.0,
                 qPrintable(QString("Fixed VAT should be > 0 for %1, got %2")
                            .arg(orderId).arg(vat)));
    }
}

// ── Full-inventory (Type 2) fix ───────────────────────────────────────────────

void TestFixVatTaxually::test_fixInventoryValue_fullInventoryType()
{
    const QString xlsxPath = dataDir() + "/2026-03-full-inventory.xlsx";

    // Confirm the file is detected as inventory by the fixer.
    VatFixerTaxually fixer;
    QVERIFY2(fixer.isInventoryFile(xlsxPath),
             "isInventoryFile should return true for full-inventory xlsx");

    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QString fixedPath = tmpDir.filePath("2026-03-full-inventory-FIXED.xlsx");

    // Pass no purchase CSVs: no prices can be overwritten, so fixedSkus must be
    // empty.  All existing prices are non-zero, so skusNotFound must also be
    // empty (only truly blank cells would be flagged).
    const AbstractVatFixer::InventoryFixResult result =
        fixer.fixInventoryValue(xlsxPath, fixedPath, {}, QDir());

    QVERIFY2(result.fixedSkus.isEmpty(),
             qPrintable(QString("Expected no fixed SKUs (no purchase CSVs), got %1")
                        .arg(result.fixedSkus.size())));
    QVERIFY2(result.skusNotFound.isEmpty(),
             qPrintable(QString("Expected no missing SKUs (all have prices), got: %1")
                        .arg(result.skusNotFound.join(", "))));

    // The output file must have been created.
    QVERIFY2(QFile::exists(fixedPath),
             "fixInventoryValue did not create the output file");

    // Read back the fixed file and spot-check known SKU → price pairs.
    QXlsx::Document fixedDoc(fixedPath);
    QVERIFY2(fixedDoc.isLoadPackage(), "Cannot open the fixed output xlsx");
    QVERIFY2(fixedDoc.selectSheet("Inventory"),
             "Inventory sheet not found in output xlsx");

    QHash<QString, QPair<double, QString>> priceMap; // SKU → {price, currency}
    const QXlsx::CellRange dim = fixedDoc.dimension();
    for (int row = 2; row <= dim.lastRow(); ++row) {
        const QString sku = fixedDoc.read(row, 1).toString().trimmed(); // col A
        const QVariant pv  = fixedDoc.read(row, 6);                     // col F
        const QString curr = fixedDoc.read(row, 7).toString().trimmed();// col G
        if (!sku.isEmpty() && !pv.isNull()) {
            priceMap[sku] = {pv.toDouble(), curr};
        }
    }

    // Known rows from the fixture file.
    struct Expected { const char *sku; double price; const char *currency; };
    const Expected expected[] = {
        {"084-CRYOGEX-EYES",          0.48,    "EUR"},
        {"085-NEO-KNEE",             11.994,   "EUR"},
        {"085-NEO-KNEE-X2",          16.776,   "EUR"},
        {"087-ICE-CAPS",             10.434,   "GBP"},
        {"088-LARGE-NECK-ELASTIC-PACK", 16.794, "EUR"},
    };

    for (const Expected &e : expected) {
        QVERIFY2(priceMap.contains(e.sku),
                 qPrintable(QString("SKU %1 not found in output Inventory sheet")
                            .arg(e.sku)));
        const auto &[price, currency] = priceMap.value(e.sku);
        QVERIFY2(qAbs(price - e.price) < 0.001,
                 qPrintable(QString("Price mismatch for %1: expected %2, got %3")
                            .arg(e.sku).arg(e.price).arg(price)));
        QCOMPARE(currency, QString(e.currency));
    }
}

QTEST_MAIN(TestFixVatTaxually)
#include "test_fix_vat_taxually.moc"
