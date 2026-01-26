#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QCoroTask>

#include "orders/ImporterFileAmazonVatEu.h"
#include "utils/CsvReader.h"

// We need a main for QCoro tests if we use QCoroTest, or just use QTEST_MAIN and block on tasks.
// Using QCoro::waitFor(task) is simplest for synchronous tests.

class TestFileImportAmz : public QObject
{
    Q_OBJECT

public:
    TestFileImportAmz();
    ~TestFileImportAmz();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_allFiles();

private:
    QString m_reportsPath;
};

TestFileImportAmz::TestFileImportAmz()
{
}

TestFileImportAmz::~TestFileImportAmz()
{
}

void TestFileImportAmz::initTestCase()
{
    QDir appDir(QCoreApplication::applicationDirPath());
    // Assuming data copied to data/eu-vat-reports
    QString possiblePath = appDir.absoluteFilePath("data/eu-vat-reports");
    if (QFileInfo::exists(possiblePath)) {
        m_reportsPath = possiblePath;
    } else {
        // Fallback to source dir (adjust as needed if standard path differs)
        m_reportsPath = QDir::current().absoluteFilePath("data/eu-vat-reports"); 
    }
    qDebug() << "Reports Path:" << m_reportsPath;
    
    QVERIFY2(QDir(m_reportsPath).exists(), "Reports directory must exist.");
}

void TestFileImportAmz::cleanupTestCase()
{
}

void TestFileImportAmz::test_allFiles()
{
    QDir reportDir(m_reportsPath);
    QStringList filters;
    filters << "*.csv";
    QFileInfoList files = reportDir.entryInfoList(filters, QDir::Files);

    if (files.isEmpty()) {
        QSKIP("No CSV files found in reports directory.");
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QSKIP("Could not create temporary directory.");
    }
    ImporterFileAmazonVatEu importer(tempDir.path());

    foreach(const QFileInfo &fileInfo, files) {
        qDebug() << "Testing file:" << fileInfo.fileName();
        
        // 1. Load via Importer
        auto task = importer.loadReport(fileInfo.absoluteFilePath());
        AbstractImporter::ReturnOrderInfos result = QCoro::waitFor(task);
        
        QVERIFY2(result.errorReturned.isEmpty(), qPrintable("Importer returned error: " + result.errorReturned));
        QVERIFY(result.orderInfos != nullptr);
        
        // Check Store Population
        if (!result.orderInfos->shipments.isEmpty()) {
             // We expect at least some stores to be found if MARKETPLACE column exists
             // In provided sample it exists.
             // Let's check size
             QVERIFY(!result.orderInfos->orderId_store.isEmpty());
             // Check a sample value if possible? 
             // Just verifying it's not empty is a good first step.
        }
        
        // 2. Load via manual CSV check (Strategy: simple independent sum)
        // We will sum up TOTAL_ACTIVITY_VALUE_VAT_AMT and count transactions
        
        CsvReader reader(fileInfo.absoluteFilePath(), ",", "\"", true, "\n", 0, "UTF-8");
        QVERIFY(reader.readAll());
        const auto *data = reader.dataRode();
        
        int indTransType = data->header.pos("TRANSACTION_TYPE");
        int indVatAmt = data->header.pos("TOTAL_ACTIVITY_VALUE_VAT_AMT");
        int indVatExcl = data->header.pos("TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL");
        int indEventId = data->header.pos("TRANSACTION_EVENT_ID");
        if (indEventId == -1) indEventId = data->header.pos("ORDER_ID");

        // We need to double check how lines are aggregated. 
        // Importer aggregates by TRANSACTION_EVENT_ID. 
        // CSV has one line per "Activity" (often one per line item or tax component).
        
        double csvTotalVat = 0.0;
        double csvTotalExcl = 0.0;
        QSet<QString> csvSaleIds;
        QSet<QString> csvRefundIds;
        
        for (const auto &line : data->lines) {
             QString type = line.value(indTransType);
             if (type != "SALE" && type != "REFUND") continue;
             
             double vat = line.value(indVatAmt).toDouble();
             double excl = line.value(indVatExcl).toDouble();
             
             csvTotalVat += vat;
             csvTotalExcl += excl;
             
             QString id = line.value(indEventId);
             if (type == "SALE") csvSaleIds.insert(id);
             else csvRefundIds.insert(id);
        }
        
        qDebug() << "Test Manual: SALE IDs:" << csvSaleIds.size() << "REFUND IDs:" << csvRefundIds.size();
        
        // 3. Compare Results
        
        // A. Counts (Shipments vs Refund objects)
        // Note: Shipments in OrderInfos roughly correspond to unique IDs for SALES.
        // Refunds for REFUNDS.
        int importerShipments = result.orderInfos->shipments.size();
        int importerRefunds = result.orderInfos->refunds.size();
        
        QCOMPARE(importerShipments, csvSaleIds.size());
        QCOMPARE(importerRefunds, csvRefundIds.size());
        
        // B. Totals
        // Sum from Importer objects (Activities)
        double impTotalVat = 0.0;
        double impTotalExcl = 0.0;
        
        for (const auto &s : result.orderInfos->shipments) {
            for (const auto &act : s.getActivities()) {
                impTotalVat += act.getAmountTaxes();
                impTotalExcl += act.getAmountUntaxed();
            }
        }
        for (const auto &r : result.orderInfos->refunds) {
            for (const auto &act : r.getActivities()) {
                impTotalVat += act.getAmountTaxes();
                impTotalExcl += act.getAmountUntaxed();
            }
        }
        
        // Floating point comparison
        if (qAbs(impTotalVat - csvTotalVat) > 0.01) {
             qWarning() << "VAT Mismatch in file" << fileInfo.fileName() 
                        << "Importer:" << impTotalVat << "CSV:" << csvTotalVat;
        }
        QVERIFY(qAbs(impTotalVat - csvTotalVat) < 0.01);
        
        if (qAbs(impTotalExcl - csvTotalExcl) > 0.01) {
             qWarning() << "Excl Mismatch in file" << fileInfo.fileName() 
                        << "Importer:" << impTotalExcl << "CSV:" << csvTotalExcl;
        }
        QVERIFY(qAbs(impTotalExcl - csvTotalExcl) < 0.01);
        
        // Check Dates
        QVERIFY(result.orderInfos->dateMin.isValid());
        QVERIFY(result.orderInfos->dateMax.isValid());
        QVERIFY(result.orderInfos->dateMin <= result.orderInfos->dateMax);
    }
}

QTEST_MAIN(TestFileImportAmz)
#include "test_file_import_amazon.moc"
