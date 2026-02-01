#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QCoroTask>
#include <QTemporaryDir>

#include "orders/ImporterFileTemuOrders.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"

class TestFileImportTemuOrders : public QObject
{
    Q_OBJECT

public:
    TestFileImportTemuOrders();
    ~TestFileImportTemuOrders();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_realData();
    void test_columnOrderChanged();
    void test_missingRequiredColumn();
    void test_variedSituations();

private:
    QString m_dataDir;
    QString createTempCsv(const QString &content, QTemporaryDir &tempDir, const QString &fileName = "test.csv");
};

TestFileImportTemuOrders::TestFileImportTemuOrders() {}
TestFileImportTemuOrders::~TestFileImportTemuOrders() {}

void TestFileImportTemuOrders::initTestCase()
{
    QDir appDir(QCoreApplication::applicationDirPath());
    QString possiblePath = appDir.absoluteFilePath("data/temu-orders");
    if (QFileInfo::exists(possiblePath))
    {
        m_dataDir = possiblePath;
    }
    else
    {
        // Search upwards
        QDir searchDir = appDir;
        bool found = false;
        for (int i = 0; i < 5; ++i)
        {
            if (searchDir.cd("data/temu-orders"))
            {
                m_dataDir = searchDir.absolutePath();
                found = true;
                break;
            }
            if (!searchDir.cdUp())
            {
                break;
            }
            if (searchDir.isRoot())
            {
                break;
            }
        }
        if (!found)
        {
            m_dataDir = QDir::current().absoluteFilePath("data/temu-orders");
            qWarning() << "Could not locate data/temu-orders, using:" << m_dataDir;
        }
    }
    qDebug() << "Temu Orders Data Dir:" << m_dataDir;
}

void TestFileImportTemuOrders::cleanupTestCase() {}

QString TestFileImportTemuOrders::createTempCsv(const QString &content, QTemporaryDir &tempDir, const QString &fileName)
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

void TestFileImportTemuOrders::test_realData()
{
    if (!QFileInfo::exists(m_dataDir))
    {
        QSKIP("Temu orders data directory not found");
    }
    
    QDirIterator it(m_dataDir, QStringList() << "*.csv", QDir::Files, QDirIterator::Subdirectories);
    
    QTemporaryDir tempDir;
    ImporterFileTemuOrders importer(tempDir.path());
    
    bool filesFound = false;
    int totalShipments = 0;
    
    while (it.hasNext())
    {
        QString filePath = it.next();
        filesFound = true;
        qDebug() << "Testing file:" << filePath;
        
        AbstractImporter::ReturnOrderInfos result;
        try
        {
            auto task = importer.loadReport(filePath);
            result = QCoro::waitFor(task);
        }
        catch (const CsvHeaderException &e)
        {
            qWarning() << "CsvHeaderException for file:" << filePath;
            QFAIL("CsvHeaderException thrown for real data file");
        }
        catch (const std::exception &e)
        {
            qWarning() << "Exception for file:" << filePath << e.what();
            QFAIL(qPrintable(QString("Exception: ") + e.what()));
        }
        
        if (!result.errorReturned.isEmpty())
        {
            qWarning() << "Error importing:" << filePath << result.errorReturned;
            QFAIL(qPrintable(result.errorReturned));
        }
        
        QVERIFY(result.orderInfos);
        totalShipments += result.orderInfos->shipments.size();
        qDebug() << "Imported" << result.orderInfos->shipments.size() << "shipments from" << filePath;
    }
    
    QVERIFY2(filesFound, "No CSV files found in temu-orders directory");
    QVERIFY2(totalShipments > 0, "Expected at least some shipments from real data");
    qDebug() << "Total shipments imported:" << totalShipments;
}

void TestFileImportTemuOrders::test_columnOrderChanged()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    // Create CSV with columns in different order than the original
    // Use unquoted format - CsvReader with guillemets="" parses unquoted columns
    // Note: "Base price total  after discount" has TWO spaces
    QString content = 
        "order status,Order item ID,purchase date,ship country,Order ID,Base price total  after discount,Product Tax,contribution sku,quantity purchased\n"
        "Delivered,069-12345678901234567,\"Jan 15, 2026, 10:00 am CET(UTC+1)\",France,PO-069-12345678901234567,\"9,99€\",\"2,00€\",TEST-SKU-001,1\n"
        "Delivered,069-23456789012345678,\"Jan 16, 2026, 2:30 pm CET(UTC+1)\",France,PO-069-23456789012345678,\"19,99€\",\"4,00€\",TEST-SKU-002,2\n"
        "Canceled,069-34567890123456789,\"Jan 17, 2026, 9:00 am CET(UTC+1)\",France,PO-069-34567890123456789,\"5,99€\",\"1,20€\",TEST-SKU-003,1\n";
    
    QString file = createTempCsv(content, tempDir);
    
    ImporterFileTemuOrders importer(tempDir.path());
    
    AbstractImporter::ReturnOrderInfos result;
    try
    {
        auto task = importer.loadReport(file);
        result = QCoro::waitFor(task);
    }
    catch (const CsvHeaderException &e)
    {
        QFAIL("Unexpected CsvHeaderException - columns should be found regardless of order");
    }
    catch (const std::exception &e)
    {
        QFAIL(qPrintable(QString("Unexpected exception: ") + e.what()));
    }
    
    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    
    // Should have 2 shipments (1 canceled is skipped)
    QCOMPARE(result.orderInfos->shipments.size(), 2);
    
    // Verify first order details
    auto s1 = result.orderInfos->shipments.first();
    QCOMPARE(s1.getActivities().size(), 1);
    auto a1 = s1.getActivities().first();
    QCOMPARE(a1.getActivityId(), QString("069-12345678901234567"));
    QCOMPARE(a1.getCountryCodeTo(), QString("FR")); // France -> FR
    QVERIFY(qAbs(a1.getAmountTaxed() - 11.99) < 0.01); // 9.99 + 2.00
    
    qDebug() << "Column order change test passed - successfully parsed shuffled columns";
}

void TestFileImportTemuOrders::test_missingRequiredColumn()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    // Test 1: Missing "Order ID" column
    {
        QString content = 
            "\"order status\",\"Order item ID\",\"purchase date\",\"ship country\",\"Base price total  after discount\",\"Product Tax\"\n"
            "\"Delivered\",\"069-12345\",\"Jan 15, 2026, 10:00 am CET(UTC+1)\",\"France\",\"9,99€\",\"2,00€\"\n";
        
        QString file = createTempCsv(content, tempDir, "missing_orderid.csv");
        ImporterFileTemuOrders importer(tempDir.path());
        
        bool exceptionCaught = false;
        try
        {
            auto task = importer.loadReport(file);
            auto result = QCoro::waitFor(task);
            qDebug() << "No exception thrown, error:" << result.errorReturned;
        }
        catch (const CsvHeaderException &e)
        {
            exceptionCaught = true;
            qDebug() << "Caught expected CsvHeaderException for missing Order ID";
        }
        catch (const std::exception &e)
        {
            qDebug() << "Caught std::exception:" << e.what();
        }
        
        QVERIFY2(exceptionCaught, "Expected CsvHeaderException for missing Order ID column");
    }
    
    // Test 2: Missing "Product Tax" column
    {
        QString content = 
            "\"Order ID\",\"order status\",\"Order item ID\",\"purchase date\",\"ship country\",\"Base price total  after discount\"\n"
            "\"PO-069-12345\",\"Delivered\",\"069-12345\",\"Jan 15, 2026, 10:00 am CET(UTC+1)\",\"France\",\"9,99€\"\n";
        
        QString file = createTempCsv(content, tempDir, "missing_tax.csv");
        ImporterFileTemuOrders importer(tempDir.path());
        
        bool exceptionCaught = false;
        try
        {
            auto task = importer.loadReport(file);
            auto result = QCoro::waitFor(task);
        }
        catch (const CsvHeaderException &e)
        {
            exceptionCaught = true;
            qDebug() << "Caught expected CsvHeaderException for missing Product Tax";
        }
        
        QVERIFY2(exceptionCaught, "Expected CsvHeaderException for missing Product Tax column");
    }
    
    // Test 3: Missing "purchase date" column
    {
        QString content = 
            "\"Order ID\",\"order status\",\"Order item ID\",\"ship country\",\"Base price total  after discount\",\"Product Tax\"\n"
            "\"PO-069-12345\",\"Delivered\",\"069-12345\",\"France\",\"9,99€\",\"2,00€\"\n";
        
        QString file = createTempCsv(content, tempDir, "missing_date.csv");
        ImporterFileTemuOrders importer(tempDir.path());
        
        bool exceptionCaught = false;
        try
        {
            auto task = importer.loadReport(file);
            auto result = QCoro::waitFor(task);
        }
        catch (const CsvHeaderException &e)
        {
            exceptionCaught = true;
            qDebug() << "Caught expected CsvHeaderException for missing purchase date";
        }
        
        QVERIFY2(exceptionCaught, "Expected CsvHeaderException for missing purchase date column");
    }
    
    qDebug() << "Missing column exception tests passed";
}

void TestFileImportTemuOrders::test_variedSituations()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    // Full header matching real data format - using same format as actual Temu export
    QString header = "Order ID,order status,Order item ID,purchase date,ship country,Base price total  after discount,Product Tax,contribution sku,quantity purchased,Retail price total after discounts(tax excl.),Product refund\n";
    
    QString content = header;
    
    // Case 1: Standard delivered order - Base=9.99 (net), Tax=2.00, total=11.99
    // Retail(tax excl) = same as base = 9.99
    content += "PO-069-00000000000000001,Delivered,069-00000000000000001,\"Jan 10, 2026, 10:00 am CET(UTC+1)\",France,\"9,99€\",\"2,00€\",SKU-001,1,\"9,99€\",\n";
    
    // Case 2: Canceled order (should be skipped)
    content += "PO-069-00000000000000002,Canceled,069-00000000000000002,\"Jan 11, 2026, 11:00 am CET(UTC+1)\",France,\"9,99€\",\"2,00€\",SKU-002,1,\"9,99€\",\n";
    
    // Case 3: Zero tax - Base=10.00, Tax=0, total=10.00
    content += "PO-069-00000000000000003,Delivered,069-00000000000000003,\"Jan 12, 2026, 12:00 pm CET(UTC+1)\",France,\"10,00€\",\"0,00€\",SKU-003,1,\"10,00€\",\n";
    
    // Case 4: Large amount - Base=999.99 (net), Tax=200.00, total=1199.99
    content += "PO-069-00000000000000004,Delivered,069-00000000000000004,\"Jan 13, 2026, 1:00 pm CET(UTC+1)\",France,\"999,99€\",\"200,00€\",SKU-004,1,\"999,99€\",\n";
    
    // Case 5: Small amount - Base=0.99, Tax=0.20, total=1.19
    content += "PO-069-00000000000000005,Delivered,069-00000000000000005,\"Jan 14, 2026, 2:00 pm CET(UTC+1)\",France,\"0,99€\",\"0,20€\",SKU-005,1,\"0,99€\",\n";
    
    // Case 6: Different country (Germany) - Base=15.00, Tax=2.85, total=17.85
    content += "PO-069-00000000000000006,Delivered,069-00000000000000006,\"Jan 15, 2026, 3:00 pm CET(UTC+1)\",Germany,\"15,00€\",\"2,85€\",SKU-006,1,\"15,00€\",\n";
    
    // Case 7: Multiple quantity - Base=29.97, Tax=6.00, total=35.97
    content += "PO-069-00000000000000007,Delivered,069-00000000000000007,\"Jan 16, 2026, 4:00 pm CET(UTC+1)\",France,\"29,97€\",\"6,00€\",SKU-007,3,\"29,97€\",\n";
    
    // Case 8: Full refund (should be skipped) - refund = net(11.99) + tax(2.40) = 14.39
    content += "PO-069-00000000000000008,Delivered,069-00000000000000008,\"Jan 17, 2026, 5:00 pm CET(UTC+1)\",France,\"11,99€\",\"2,40€\",SKU-008,1,\"11,99€\",\"14,39€\"\n";
    
    // Case 9: Pending status (should be included) - Base=20.00, Tax=4.00, total=24.00
    content += "PO-069-00000000000000009,Pending,069-00000000000000009,\"Jan 18, 2026, 6:00 pm CET(UTC+1)\",France,\"20,00€\",\"4,00€\",SKU-009,1,\"20,00€\",\n";
    
    // Case 10: Shipped status - Base=25.00, Tax=5.00, total=30.00
    content += "PO-069-00000000000000010,Shipped,069-00000000000000010,\"Jan 19, 2026, 7:00 pm CET(UTC+1)\",France,\"25,00€\",\"5,00€\",SKU-010,1,\"25,00€\",\n";
    
    QString file = createTempCsv(content, tempDir);
    
    ImporterFileTemuOrders importer(tempDir.path());
    
    auto task = importer.loadReport(file);
    auto result = QCoro::waitFor(task);
    
    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    
    // Expected: 10 rows - 1 canceled - 1 full refund = 8 shipments
    QCOMPARE(result.orderInfos->shipments.size(), 8);
    
    // Verify date range
    QCOMPARE(result.orderInfos->dateMin, QDate(2026, 1, 10));
    QCOMPARE(result.orderInfos->dateMax, QDate(2026, 1, 19));
    
    // Find and verify Case 4 (large amount)
    bool foundLargeAmount = false;
    for (const auto &s : result.orderInfos->shipments)
    {
        if (s.getActivities().first().getActivityId() == "069-00000000000000004")
        {
            foundLargeAmount = true;
            QVERIFY(qAbs(s.getActivities().first().getAmountTaxed() - 1199.99) < 0.01);
        }
    }
    QVERIFY(foundLargeAmount);
    
    // Find and verify Case 6 (Germany)
    bool foundGermany = false;
    for (const auto &s : result.orderInfos->shipments)
    {
        if (s.getActivities().first().getActivityId() == "069-00000000000000006")
        {
            foundGermany = true;
            QCOMPARE(s.getActivities().first().getCountryCodeTo(), QString("DE"));
        }
    }
    QVERIFY(foundGermany);
    
    qDebug() << "Varied situations test passed with" << result.orderInfos->shipments.size() << "shipments";
}

QTEST_GUILESS_MAIN(TestFileImportTemuOrders)
#include "test_file_import_temu_orders.moc"
