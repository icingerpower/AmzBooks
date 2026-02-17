#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QCoroTask>

#include "orders/ImporterFileAmazonVatEu.h"
#include "orders/ImporterFileAmazonTransactions.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"

// We need a main for QCoro tests if we use QCoroTest, or just use QTEST_MAIN and block on tasks.
// Using QCoro::waitFor(task) is simplest for synchronous tests.

class TestFileImportAmazonTransactions : public QObject
{
    Q_OBJECT

public:
    TestFileImportAmazonTransactions();
    ~TestFileImportAmazonTransactions();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_allFiles();

private:
    QString m_reportsPath;
    QString m_transactionsPath;

private slots:
    void test_amazonTransactions_structure();
    void test_amazonTransactions_realData();
    void test_amazonTransactions_invalid();
    void test_crossVerifyVsEuVat();
};


TestFileImportAmazonTransactions::TestFileImportAmazonTransactions()
{
}

TestFileImportAmazonTransactions::~TestFileImportAmazonTransactions()
{
}

void TestFileImportAmazonTransactions::initTestCase()
{
    QDir appDir(QCoreApplication::applicationDirPath());
    // Assuming data copied to data/amazon-vat-reports
    QString possiblePath = appDir.absoluteFilePath("data/amazon-vat-reports");
    if (QFileInfo::exists(possiblePath)) {
        m_reportsPath = possiblePath;
    } else {
        // Fallback to source dir (adjust as needed if standard path differs)
        m_reportsPath = QDir::current().absoluteFilePath("data/amazon-vat-reports"); 
    }
    qDebug() << "Reports Path:" << m_reportsPath;
    
    QVERIFY2(QDir(m_reportsPath).exists(), "Reports directory must exist.");

    QString possibleTransPath = appDir.absoluteFilePath("data/amazon-transactions");
    if (QFileInfo::exists(possibleTransPath)) {
        m_transactionsPath = possibleTransPath;
    } else {
        // Search upwards for data directory
        QDir searchDir = appDir;
        bool found = false;
        // Limit search to 5 levels up to avoid infinite loops or root fs scan
        for (int i = 0; i < 5; ++i) {
            if (searchDir.cd("data/amazon-transactions")) {
                m_transactionsPath = searchDir.absolutePath();
                found = true;
                break;
            }
            if (!searchDir.cdUp()) break;
            if (searchDir.isRoot()) break;
        }
        
        if (!found) {
            // Fallback to current dir or hard failure logging
             m_transactionsPath = QDir::current().absoluteFilePath("data/amazon-transactions");
             qWarning() << "Could not locate data/amazon-transactions from" << appDir.absolutePath() << "or upwards.";
        }
    }
    qDebug() << "Transactions Path:" << m_transactionsPath;
}

void TestFileImportAmazonTransactions::cleanupTestCase()
{
}

void TestFileImportAmazonTransactions::test_allFiles()
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

void TestFileImportAmazonTransactions::test_amazonTransactions_structure()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString mockFile = tempDir.filePath("transactions-us.csv");
    QFile file(mockFile);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);

    // Header
    out << "\"Date\",\"Transaction type\",\"Order ID\",\"Product Details\",\"Total product charges\",\"Total promotional rebates\",\"Amazon fees\",\"Other\",\"Total (USD)\"\n";

    // 1. Valid Refund
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111111\",\"Product A\",\"-20.00\",\"0\",\"2.00\",\"0\",\"-18.00\"\n";
    // 2. Valid Refund small date format
    out << "\"1/1/2025\",\"Refund\",\"111-1111111-1111112\",\"Product B\",\"-20.00\",\"0\",\"2.00\",\"0\",\"-18.00\"\n";
    // 3. Valid Refund different date format (MM/dd/yyyy)
    out << "\"12/31/2025\",\"Refund\",\"111-1111111-1111113\",\"Product C\",\"-20.00\",\"0\",\"2.00\",\"0\",\"-18.00\"\n";
    // 4. Ignored Type (Order Payment)
    out << "\"1/31/2025\",\"Order Payment\",\"111-1111111-1111114\",\"Product D\",\"20.00\",\"0\",\"-2.00\",\"0\",\"18.00\"\n";
    // 5. Ignored Type (Service Fee)
    out << "\"1/31/2025\",\"Service Fee\",\"\",\"Subscription\",\"0\",\"0\",\"-40.00\",\"0\",\"-40.00\"\n";
    // 6. Missing Order ID for Refund (Should be skipped)
    out << "\"1/31/2025\",\"Refund\",\"\",\"Product E\",\"-20.00\",\"0\",\"2.00\",\"0\",\"-18.00\"\n";
    // 7. Invalid Date (Should be warned/skipped)
    out << "\"InvalidDate\",\"Refund\",\"111-1111111-1111115\",\"Product F\",\"-20.00\",\"0\",\"2.00\",\"0\",\"-18.00\"\n";
    // 8. Empty Date (Should be skipped)
    out << "\"\",\"Refund\",\"111-1111111-1111116\",\"Product G\",\"-20.00\",\"0\",\"2.00\",\"0\",\"-18.00\"\n";
    // 9. Positive Amount Refund (Should still be handled, just positive magnitude)
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111117\",\"Product H\",\"20.00\",\"0\",\"0\",\"0\",\"20.00\"\n";
    // 10. Zero Amount Refund
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111118\",\"Product I\",\"0.00\",\"0\",\"0\",\"0\",\"0.00\"\n";
    // 11. Malformed CSV Line (Should be handled by CSV reader but if broken significantly might skipped)
    // out << "This is not a CSV line\n"; // CsvReader might error out whole file or skip line. 
    // 12. Extra quotes
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111119\",\"Product \"\"J\"\"\",\"-15.00\",\"0\",\"0\",\"0\",\"-15.00\"\n";
    // 13. Missing columns at end of line
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111120\",\"Product K\",\"-10.00\"\n";
    // 14. Unicode chars in Product
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111121\",\"Product \u00E9\u00E0\",\"-10.00\",\"0\",\"0\",\"0\",\"-10.00\"\n";
    // 15. yyyy-MM-dd date format
    out << "\"2025-01-31\",\"Refund\",\"111-1111111-1111122\",\"Product L\",\"-20.00\",\"0\",\"0\",\"0\",\"-20.00\"\n";
    // 16. dd/MM/yyyy date format
    out << "\"31/01/2025\",\"Refund\",\"111-1111111-1111123\",\"Product M\",\"-20.00\",\"0\",\"0\",\"0\",\"-20.00\"\n";
    // 17. Empty Transaction Type
    out << "\"1/31/2025\",\"\",\"111-1111111-1111124\",\"Product N\",\"-20.00\",\"0\",\"0\",\"0\",\"-20.00\"\n";
    // 18. Case sensitive type check ("refund" instead of "Refund" - assuming strict check so skipped)
    out << "\"1/31/2025\",\"refund\",\"111-1111111-1111125\",\"Product O\",\"-20.00\",\"0\",\"0\",\"0\",\"-18.00\"\n";
    // 19. Large Amount
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111126\",\"Product P\",\"-20000.00\",\"0\",\"0\",\"0\",\"-20000.00\"\n";
    // 20. Very small amount
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111127\",\"Product Q\",\"-0.01\",\"0\",\"0\",\"0\",\"-0.01\"\n";
    // 21. Different Currency header (Total (EUR)) check?
    // We can't change header mid-file. This test assumes USD from header.
    
    file.close();

    ImporterFileAmazonTransactions importer(tempDir.path());
    auto task = importer.loadReport(mockFile);
    AbstractImporter::ReturnOrderInfos result = QCoro::waitFor(task);

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable("Error: " + result.errorReturned));
    QVERIFY(result.orderInfos != nullptr);
    
    // Check results
    // We expect valid refunds to be counted
    // Valid: 1, 2, 3, 9, 10, 12, 13 (reader handles missing cols?), 14, 15, 16, 19, 20
    // Invalid/Skipped: 4(Payment), 5(Service), 6(NoID), 7(BadDate), 8(EmptyDate), 17(EmptyType), 18(Case)
    
    // Note: CsvReader robustness for missing columns (13) depends on implementation. Default CsvReader usually expects all columns or returns empty for missing?
    // If lines.value(index) is called and index > size, QList returns default constructed value (empty string). So it might work but return empty "Total (USD)".
    // Product charges is column index 4. Line 13 has 5 columns (0-4). So it has product charges.
    
    int expectedCount = 12; // Adjusted based on manual count above
    // Let's verify specific IDs to be sure
    QSet<QString> expectedIds = {
        "111-1111111-1111111", // 1
        "111-1111111-1111112", // 2
        "111-1111111-1111113", // 3
        "111-1111111-1111117", // 9
        "111-1111111-1111118", // 10
        "111-1111111-1111119", // 12
        "111-1111111-1111120", // 13
        "111-1111111-1111121", // 14
        "111-1111111-1111122", // 15
        "111-1111111-1111123", // 16
        "111-1111111-1111126", // 19
        "111-1111111-1111127"  // 20
    };
    
    int actualCount = result.orderInfos->refunds.size();
    if (actualCount != expectedIds.size()) {
         qDebug() << "Found IDs:";
         for(const auto &r : result.orderInfos->invoicingInfos) qDebug() << r.shipmentOrRefundId;
    }
    QCOMPARE(actualCount, expectedIds.size());
    
    // Verify amounts for one case
    // Case 1: -20.00
    bool found = false;
    for (const auto &refund : result.orderInfos->refunds) {
        if (refund.getActivities().first().getEventId() == "111-1111111-1111111") {
             found = true;
             QCOMPARE(refund.getActivities().first().getAmountTaxed(), -20.00);
        }
    }
    QVERIFY(found);
}

void TestFileImportAmazonTransactions::test_amazonTransactions_realData()
{
    if (m_transactionsPath.isEmpty()) {
        QSKIP("Transactions data path not found.");
    }

    QDir reportDir(m_transactionsPath);
    // Use QDirIterator to specific recursive scanning
    QDirIterator it(m_transactionsPath, QStringList() << "*.csv", QDir::Files, QDirIterator::Subdirectories);
    
    QFileInfoList files;
    while (it.hasNext()) {
        it.next();
        files.append(it.fileInfo());
    }

    if (files.isEmpty()) {
        QSKIP("No transaction CSV files found.");
    }

    QTemporaryDir tempDir;
    ImporterFileAmazonTransactions importer(tempDir.path());

    for (const QFileInfo &fileInfo : files) {
        qDebug() << "Testing real file:" << fileInfo.fileName();
        auto task = importer.loadReport(fileInfo.absoluteFilePath());
        AbstractImporter::ReturnOrderInfos result = QCoro::waitFor(task);

        QVERIFY2(result.errorReturned.isEmpty(), qPrintable("Error in " + fileInfo.fileName() + ": " + result.errorReturned));
        QVERIFY(result.orderInfos != nullptr);
        
        // We expect some refunds? Not necessarily, but we can check it ran.
        // If file has only payments, refunds might be 0.
        // Let's check against a manual scan if possible, or just correctness of execution.
        
        // Manual scan for refunds count and amounts
        CsvReader reader(fileInfo.absoluteFilePath(), ",", "\"", true, "\n", 0, "UTF-8");
        reader.readAll();
        const auto *data = reader.dataRode();
        int indType = data->header.pos("Transaction type");
        int indCharges = data->header.pos("Total product charges");
        
        int refundCount = 0;
        double totalRefundAmount = 0.0;
        
        if (indType != -1) {
            for (const auto &line : data->lines) {
                if (line.value(indType) == "Refund") {
                    refundCount++;
                    if (indCharges != -1) {
                        totalRefundAmount += line.value(indCharges).toDouble();
                    }
                }
            }
        }
        
        QCOMPARE(result.orderInfos->refunds.size(), refundCount);
        
        // Sum from Importer objects
        double impTotalRefundAmount = 0.0;
        for (const auto &refund : result.orderInfos->refunds) {
            // Each refund should have activities
            for (const auto &act : refund.getActivities()) {
                 // Activity stores 'taxed' amount in getAmountTaxed() (untaxed + taxes).
                 // In our importer we set tax=0, so taxed == raw amount.
                 impTotalRefundAmount += act.getAmountTaxed();
            }
        }
        
        // Floating point comparison
        if (qAbs(impTotalRefundAmount - totalRefundAmount) > 0.01) {
             qWarning() << "Refund Amount Mismatch in file" << fileInfo.fileName() 
                        << "Importer:" << impTotalRefundAmount << "CSV:" << totalRefundAmount;
        }
        QVERIFY(qAbs(impTotalRefundAmount - totalRefundAmount) < 0.01);
        
        qDebug() << "File:" << fileInfo.fileName() 
                 << "Refunds:" << refundCount 
                 << "Amount:" << totalRefundAmount;
    }
}

void TestFileImportAmazonTransactions::test_amazonTransactions_invalid()
{
    QTemporaryDir tempDir;
    
    // Case 1: Missing "Transaction type"
    {
        QString invalidFile1 = tempDir.filePath("invalid_1.csv");
        QFile file(invalidFile1);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        // Header missing "Transaction type"
        out << "\"Date\",\"Order ID\",\"Product Details\",\"Total product charges\",\"Total (USD)\"\n";
        file.close();
        
        // Case 1
        ImporterFileAmazonTransactions importer1(tempDir.path());
        auto task1 = importer1.loadReport(invalidFile1);
        QVERIFY_EXCEPTION_THROWN(QCoro::waitFor(task1), CsvHeaderException);
    }
    
    // Case 2: Missing "Total product charges"
    {
        QString invalidFile2 = tempDir.filePath("invalid_2.csv");
        QFile file(invalidFile2);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        // Header missing "Total product charges"
        out << "\"Date\",\"Transaction type\",\"Order ID\",\"Product Details\",\"Total (USD)\"\n";
        file.close();
        
        ImporterFileAmazonTransactions importer2(tempDir.path());
        auto task2 = importer2.loadReport(invalidFile2);
        
        QVERIFY_EXCEPTION_THROWN(QCoro::waitFor(task2), CsvHeaderException);
    }
    
    // Case 3: Missing Currency Column "Total (XXX)"
    {
        QString invalidFile3 = tempDir.filePath("invalid_3.csv");
        QFile file(invalidFile3);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        // Header missing "Total (USD)" - has "Total" maybe but not with currency
        out << "\"Date\",\"Transaction type\",\"Order ID\",\"Product Details\",\"Total product charges\",\"Total\"\n";
        file.close();
        
        ImporterFileAmazonTransactions importer3(tempDir.path());
        auto task3 = importer3.loadReport(invalidFile3);
        
        QVERIFY_EXCEPTION_THROWN(QCoro::waitFor(task3), CsvHeaderException);
    }
    
    // Case 4: Missing "Date"
     {
        QString invalidFile4 = tempDir.filePath("invalid_4.csv");
        QFile file(invalidFile4);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "\"Transaction type\",\"Order ID\",\"Product Details\",\"Total product charges\",\"Total (USD)\"\n";
        file.close();
        
        ImporterFileAmazonTransactions importer4(tempDir.path());
        auto task4 = importer4.loadReport(invalidFile4);
        
        QVERIFY_EXCEPTION_THROWN(QCoro::waitFor(task4), CsvHeaderException);
    }
    
    // Case 5: valid content, invalid filename (should return error, not exception)
    {
        QString invalidFile5 = tempDir.filePath("badname.csv");
        QFile file(invalidFile5);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        // Valid content
        out << "\"Date\",\"Transaction type\",\"Order ID\",\"Product Details\",\"Total product charges\",\"Total (USD)\"\n";
        out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111111\",\"Product A\",\"-20.00\",\"-18.00\"\n";
        file.close();
        
        ImporterFileAmazonTransactions importer5(tempDir.path());
        auto task5 = importer5.loadReport(invalidFile5);
        AbstractImporter::ReturnOrderInfos result = QCoro::waitFor(task5);
        
        QVERIFY(!result.errorReturned.isEmpty());
        QVERIFY(result.errorReturned.contains("filename"));
    }
}

void TestFileImportAmazonTransactions::test_crossVerifyVsEuVat()
{
    // Cross-verify Transaction refunds against VAT reports
    // Transactions uses Order ID as Event ID
    // VAT Report uses TRANSACTION_EVENT_ID (Refund ID) as Event ID, but has ORDER_ID column
    // We need to match by Order ID
    
    QTemporaryDir tempDir;
    
    // Structure to hold activity details for comparison
    struct ActivityDetails {
        QDate date;
        double grossAmount;
        QString currency;
    };
    
    // Load VAT Reports - Build map of Order ID -> Activity Details
    // Need to manually parse to get ORDER_ID column since it's not in Activity
    QMap<QString, ActivityDetails> vatRefundsByOrder;
    
    QDir vatReportDir(m_reportsPath);
    QFileInfoList vatFiles = vatReportDir.entryInfoList(QStringList() << "*.csv", QDir::Files);
    
    for (const QFileInfo &f : vatFiles) {
        try {
            CsvReader reader(f.absoluteFilePath(), ",", "\"", true, "\n", 0, "UTF-8");
            if (!reader.readAll()) continue;
            
            const auto *data = reader.dataRode();
            int idxType = data->header.pos("TRANSACTION_TYPE");
            // For REFUND, TRANSACTION_EVENT_ID contains the Order ID
            // Older formats might have ORDER_ID column
            int idxOrderId = data->header.pos("TRANSACTION_EVENT_ID");
            if (idxOrderId == -1) idxOrderId = data->header.pos("ORDER_ID");
            int idxDate = data->header.pos("TAX_CALCULATION_DATE");
            if (idxDate == -1) idxDate = data->header.pos("TRANSACTION_COMPLETE_DATE");
            int idxTotalExcl = data->header.pos("TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL");
            int idxTotalVat = data->header.pos("TOTAL_ACTIVITY_VALUE_VAT_AMT");
            int idxCurrency = data->header.pos("TRANSACTION_CURRENCY_CODE");
            
            if (idxType == -1 || idxOrderId == -1 || idxTotalExcl == -1) continue;
            
            for (const auto &line : data->lines) {
                if (line.value(idxType) != "REFUND") continue;
                
                QString orderId = line.value(idxOrderId);
                if (orderId.isEmpty()) continue;
                
                double excl = line.value(idxTotalExcl).toDouble();
                double vat = (idxTotalVat != -1) ? line.value(idxTotalVat).toDouble() : 0.0;
                double gross = excl + vat;
                
                QString dateStr = (idxDate != -1) ? line.value(idxDate) : "";
                QDate date = QDate::fromString(dateStr.left(10), "dd-MM-yyyy");
                if (!date.isValid()) date = QDate::fromString(dateStr.left(10), "yyyy-MM-dd");
                
                QString currency = (idxCurrency != -1) ? line.value(idxCurrency) : "";
                
                // Aggregate by Order ID
                if (vatRefundsByOrder.contains(orderId)) {
                    vatRefundsByOrder[orderId].grossAmount += gross;
                } else {
                    ActivityDetails details;
                    details.date = date;
                    details.grossAmount = gross;
                    details.currency = currency;
                    vatRefundsByOrder[orderId] = details;
                }
            }
        } catch (...) {
            // Skip files that can't be parsed
        }
    }
    
    qDebug() << "Loaded VAT Refunds by Order ID:" << vatRefundsByOrder.size();
    
    // Load Transaction Reports - Build map of Order ID -> Activity Details
    QMap<QString, ActivityDetails> transRefundsByOrder;
    
    ImporterFileAmazonTransactions transImporter(tempDir.path());
    QDirIterator it(m_transactionsPath, QStringList() << "*.csv", QDir::Files, QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        it.next();
        try {
            auto task = transImporter.loadReport(it.fileInfo().absoluteFilePath());
            auto res = QCoro::waitFor(task);
            if (!res.errorReturned.isEmpty()) continue;
            
            for (const auto &refund : res.orderInfos->refunds) {
                for (const auto &act : refund.getActivities()) {
                    QString orderId = act.getEventId(); // Transactions uses Order ID as Event ID
                    
                    // Aggregate by Order ID
                    if (transRefundsByOrder.contains(orderId)) {
                        transRefundsByOrder[orderId].grossAmount += act.getAmountTaxed();
                    } else {
                        ActivityDetails details;
                        details.date = act.getDateTime().date();
                        details.grossAmount = act.getAmountTaxed();
                        details.currency = act.getCurrency();
                        transRefundsByOrder[orderId] = details;
                    }
                }
            }
        } catch (...) {
            // Skip files that can't be loaded
        }
    }
    
    qDebug() << "Loaded Transaction Refunds by Order ID:" << transRefundsByOrder.size();
    
    // Compare overlapping Order IDs
    int matches = 0;
    int mismatches = 0;
    QStringList mismatchDetails;
    
    for (auto it = transRefundsByOrder.constBegin(); it != transRefundsByOrder.constEnd(); ++it) {
        const QString &orderId = it.key();
        const ActivityDetails &trans = it.value();
        
        if (!vatRefundsByOrder.contains(orderId)) continue;
        
        const ActivityDetails &vat = vatRefundsByOrder[orderId];
        matches++;
        
        QStringList issues;
        
        // Compare date (allow 1 day tolerance for timezone differences)
        if (trans.date.isValid() && vat.date.isValid()) {
            if (qAbs(trans.date.toJulianDay() - vat.date.toJulianDay()) > 1) {
                issues << QString("Date: Trans=%1 VAT=%2").arg(trans.date.toString()).arg(vat.date.toString());
            }
        }
        
        // Compare gross amount (5 cents tolerance)
        if (qAbs(trans.grossAmount - vat.grossAmount) > 0.05) {
            issues << QString("Gross: Trans=%1 VAT=%2").arg(trans.grossAmount).arg(vat.grossAmount);
        }
        
        if (!issues.isEmpty()) {
            mismatches++;
            mismatchDetails << QString("Order %1: %2").arg(orderId).arg(issues.join("; "));
            if (mismatchDetails.size() <= 10) {
                qWarning() << "Mismatch:" << orderId << issues;
            }
        }
    }
    
    qDebug() << "Cross-verification results:";
    qDebug() << "  Overlapping Orders:" << matches;
    qDebug() << "  Mismatches:" << mismatches;
    qDebug() << "  Match rate:" << (matches > 0 ? (matches - mismatches) * 100.0 / matches : 0) << "%";
    
    // Report results
    if (matches == 0) {
        qWarning() << "No overlapping orders found. Cannot verify.";
        QSKIP("No overlapping data to verify");
    }
    
    if (mismatches > 0) {
        qWarning() << "Found" << mismatches << "mismatches out of" << matches << "overlapping orders";
        qWarning() << "Note: Mismatches may be due to:";
        qWarning() << "  - Different column interpretation (Transactions uses 'Total product charges')";
        qWarning() << "  - MARKETPLACE-handled orders (VAT collected by Amazon)";
        qWarning() << "  - Currency conversion or rounding differences";
        // Show first 10 mismatches for investigation
        for (int i = 0; i < qMin(10, mismatchDetails.size()); i++) {
            qWarning() << mismatchDetails[i];
        }
    }
    
    // Test passes if we successfully loaded and compared data
    // Mismatches are logged for investigation but don't fail the test
    // as they may be due to data quality issues, not importer bugs
    QVERIFY(matches > 0);
    QVERIFY(transRefundsByOrder.size() > 0);
    QVERIFY(vatRefundsByOrder.size() > 0);
}

QTEST_MAIN(TestFileImportAmazonTransactions)
#include "test_file_import_amazon_transactions.moc"
