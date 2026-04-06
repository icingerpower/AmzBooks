#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QCoroTask>

#include "orders/ImporterFileAmazonTransactions.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"

class TestFileImportAmazonTransactions : public QObject
{
    Q_OBJECT

public:
    TestFileImportAmazonTransactions();
    ~TestFileImportAmazonTransactions();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_amazonTransactions_structure();
    void test_amazonTransactions_realData();
    void test_amazonTransactions_invalid();
    void test_amazonTransactions_multiLineRefund();
};


TestFileImportAmazonTransactions::TestFileImportAmazonTransactions()
{
}

TestFileImportAmazonTransactions::~TestFileImportAmazonTransactions()
{
}

void TestFileImportAmazonTransactions::initTestCase()
{
}

void TestFileImportAmazonTransactions::cleanupTestCase()
{
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
    // 9. Positive Amount Refund
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111117\",\"Product H\",\"20.00\",\"0\",\"0\",\"0\",\"20.00\"\n";
    // 10. Zero Amount Refund
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111118\",\"Product I\",\"0.00\",\"0\",\"0\",\"0\",\"0.00\"\n";
    // 11. Extra quotes
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111119\",\"Product \"\"J\"\"\",\"-15.00\",\"0\",\"0\",\"0\",\"-15.00\"\n";
    // 12. Missing columns at end of line
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111120\",\"Product K\",\"-10.00\"\n";
    // 13. Unicode chars in Product
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111121\",\"Product éà\",\"-10.00\",\"0\",\"0\",\"0\",\"-10.00\"\n";
    // 14. yyyy-MM-dd date format
    out << "\"2025-01-31\",\"Refund\",\"111-1111111-1111122\",\"Product L\",\"-20.00\",\"0\",\"0\",\"0\",\"-20.00\"\n";
    // 15. dd/MM/yyyy date format
    out << "\"31/01/2025\",\"Refund\",\"111-1111111-1111123\",\"Product M\",\"-20.00\",\"0\",\"0\",\"0\",\"-20.00\"\n";
    // 16. Empty Transaction Type
    out << "\"1/31/2025\",\"\",\"111-1111111-1111124\",\"Product N\",\"-20.00\",\"0\",\"0\",\"0\",\"-20.00\"\n";
    // 17. Case sensitive type check ("refund" instead of "Refund")
    out << "\"1/31/2025\",\"refund\",\"111-1111111-1111125\",\"Product O\",\"-20.00\",\"0\",\"0\",\"0\",\"-18.00\"\n";
    // 18. Large Amount
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111126\",\"Product P\",\"-20000.00\",\"0\",\"0\",\"0\",\"-20000.00\"\n";
    // 19. Very small amount
    out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111127\",\"Product Q\",\"-0.01\",\"0\",\"0\",\"0\",\"-0.01\"\n";

    file.close();

    ImporterFileAmazonTransactions importer(tempDir.path());
    auto task = importer.loadReport(mockFile);
    AbstractImporter::ReturnOrderInfos result = QCoro::waitFor(task);

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable("Error: " + result.errorReturned));
    QVERIFY(result.orderInfos != nullptr);

    // Now we expect orderId_refundClues to be populated, not refunds
    // No refunds or shipments should be created
    QCOMPARE(result.orderInfos->refunds.size(), 0);
    QCOMPARE(result.orderInfos->shipments.size(), 0);
    QCOMPARE(result.orderInfos->invoicingInfos.size(), 0);

    // Expected valid refund clues (valid date + non-empty orderId + non-zero amount):
    // 1 (1111111), 2 (1111112), 3 (1111113), 9 (1111117),
    // 11 (1111119), 12 (1111120), 13 (1111121), 14 (1111122), 15 (1111123),
    // 18 (1111126), 19 (1111127)
    // Skipped: 4(Payment), 5(Service), 6(NoID), 7(BadDate), 8(EmptyDate), 10(ZeroAmount), 16(EmptyType), 17(lowercase)
    QSet<QString> expectedIds = {
        "111-1111111-1111111",
        "111-1111111-1111112",
        "111-1111111-1111113",
        "111-1111111-1111117",
        "111-1111111-1111119",
        "111-1111111-1111120",
        "111-1111111-1111121",
        "111-1111111-1111122",
        "111-1111111-1111123",
        "111-1111111-1111126",
        "111-1111111-1111127"
    };

    const auto &clues = result.orderInfos->orderId_refundClues;

    if (clues.size() != expectedIds.size()) {
        qDebug() << "Found clue IDs:";
        for (auto it = clues.constBegin(); it != clues.constEnd(); ++it) {
            qDebug() << "  " << it.key() << " -> value:" << it.value().first().value << " currency:" << it.value().first().currency;
        }
    }
    QCOMPARE(clues.size(), expectedIds.size());

    for (const QString &id : expectedIds) {
        QVERIFY2(clues.contains(id), qPrintable("Missing clue for: " + id));
        QCOMPARE(clues[id].first().currency, QString("USD"));
    }

    // Verify specific amounts (all test lines have Other=0, so product charges == total)
    QCOMPARE(clues["111-1111111-1111111"].first().value, -20.00);
    QCOMPARE(clues["111-1111111-1111117"].first().value, 20.00);    // Positive amount is kept
    QVERIFY(!clues.contains("111-1111111-1111118"));                // Zero amount must be skipped
    QCOMPARE(clues["111-1111111-1111126"].first().value, -20000.00); // Large
    QCOMPARE(clues["111-1111111-1111127"].first().value, -0.01);    // Small

    // Dates
    QVERIFY(result.orderInfos->dateMin.isValid());
    QVERIFY(result.orderInfos->dateMax.isValid());
    QVERIFY(result.orderInfos->dateMin <= result.orderInfos->dateMax);
    QCOMPARE(result.orderInfos->dateMin, QDate(2025, 1, 1));
    QCOMPARE(result.orderInfos->dateMax, QDate(2025, 12, 31));
}

void TestFileImportAmazonTransactions::test_amazonTransactions_realData()
{
    // Locate transactions data directory
    QDir appDir(QCoreApplication::applicationDirPath());
    QString transactionsPath;

    QString possibleTransPath = appDir.absoluteFilePath("data/amazon-transactions");
    if (QFileInfo::exists(possibleTransPath)) {
        transactionsPath = possibleTransPath;
    } else {
        QDir searchDir = appDir;
        for (int i = 0; i < 5; ++i) {
            if (searchDir.cd("data/amazon-transactions")) {
                transactionsPath = searchDir.absolutePath();
                break;
            }
            if (!searchDir.cdUp()) break;
            if (searchDir.isRoot()) break;
        }
        if (transactionsPath.isEmpty()) {
            transactionsPath = QDir::current().absoluteFilePath("data/amazon-transactions");
        }
    }

    if (!QDir(transactionsPath).exists()) {
        QSKIP("Transactions data path not found.");
    }

    QDirIterator it(transactionsPath, QStringList() << "*.csv", QDir::Files, QDirIterator::Subdirectories);

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

        // No refunds/shipments should be created — only orderId_refundClues
        QCOMPARE(result.orderInfos->refunds.size(), 0);
        QCOMPARE(result.orderInfos->shipments.size(), 0);

        // Manual scan: mirror the importer logic (product charges + other, append per line)
        CsvReader reader(fileInfo.absoluteFilePath(), ",", "\"", true, "\n", 0, "UTF-8");
        reader.readAll();
        const auto *data = reader.dataRode();
        int indType    = data->header.pos("Transaction type");
        int indId      = data->header.pos("Order ID");
        int indCharges = data->header.pos("Total product charges");
        int indOther   = data->header.pos("Other");

        QHash<QString, QList<double>> expectedClues;
        if (indType != -1 && indId != -1) {
            for (const auto &line : data->lines) {
                if (line.value(indType) != "Refund") continue;
                const QString orderId = line.value(indId);
                if (orderId.isEmpty()) continue;
                double charges = (indCharges >= 0) ? line.value(indCharges).toDouble() : 0.0;
                double other   = (indOther   >= 0) ? line.value(indOther).toDouble()   : 0.0;
                double total   = charges + other;
                if (qFuzzyIsNull(total)) continue;
                expectedClues[orderId].append(total);
            }
        }

        const auto &clues = result.orderInfos->orderId_refundClues;
        // Unique order IDs should match (modulo lines with invalid dates, at most 2)
        QVERIFY(clues.size() <= expectedClues.size());
        QVERIFY(clues.size() >= expectedClues.size() - 2);

        // For each order in the result, the list of clue amounts must match
        for (auto it = clues.constBegin(); it != clues.constEnd(); ++it) {
            QVERIFY2(expectedClues.contains(it.key()),
                     qPrintable("Unexpected clue orderId: " + it.key()));
            const auto &expected = expectedClues[it.key()];
            const auto &actual   = it.value();
            QCOMPARE(actual.size(), expected.size());
            for (int i = 0; i < actual.size(); ++i) {
                QVERIFY(qAbs(actual[i].value - expected[i]) < 0.01);
            }
        }

        int totalClues = 0;
        for (const auto &list : std::as_const(clues)) { totalClues += list.size(); }
        qDebug() << "File:" << fileInfo.fileName()
                 << "Refund clue entries:" << totalClues << "for" << clues.size() << "orders";
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
        out << "\"Date\",\"Order ID\",\"Product Details\",\"Total product charges\",\"Total (USD)\"\n";
        file.close();

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

    // Case 5: Valid content with any filename (no longer needs country extraction)
    {
        QString validFile = tempDir.filePath("badname.csv");
        QFile file(validFile);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "\"Date\",\"Transaction type\",\"Order ID\",\"Product Details\",\"Total product charges\",\"Total (USD)\"\n";
        out << "\"1/31/2025\",\"Refund\",\"111-1111111-1111111\",\"Product A\",\"-20.00\",\"-18.00\"\n";
        file.close();

        ImporterFileAmazonTransactions importer5(tempDir.path());
        auto task5 = importer5.loadReport(validFile);
        AbstractImporter::ReturnOrderInfos result = QCoro::waitFor(task5);

        // Should succeed — no country extraction needed anymore
        QVERIFY2(result.errorReturned.isEmpty(), qPrintable("Error: " + result.errorReturned));
        QCOMPARE(result.orderInfos->orderId_refundClues.size(), 1);
        QVERIFY(result.orderInfos->orderId_refundClues.contains("111-1111111-1111111"));
    }
}

void TestFileImportAmazonTransactions::test_amazonTransactions_multiLineRefund()
{
    // Reproduces the scenario of order 402-6078653-0276335 (2026-03-transactions-be.csv):
    // Two products refunded in the same order, each on its own CSV row, each with a non-zero
    // "Other" amount. The importer must produce one RefundClue per row (not overwrite) and
    // must include "Other" in the amount so each clue matches its corresponding shipment.
    //
    // Order Payment rows (original sales, two shipments):
    //   Caftan:  product=28.82  other=6.05  → shipment total 34.87
    //   Qamis:   product=23.52  other=4.94  → shipment total 28.46
    //
    // Refund rows:
    //   Caftan:  product=-28.82  other=-6.05  → refund total -34.87  (matches 34.87 shipment)
    //   Qamis:   product=-23.52  other=-4.94  → refund total -28.46  (matches 28.46 shipment)

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString mockFile = tempDir.filePath("transactions-be.csv");
    QFile file(mockFile);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);

    out << "\"Date\",\"Transaction status\",\"Transaction type\",\"Order ID\",\"Product Details\","
           "\"Total product charges\",\"Total promotional rebates\",\"Amazon fees\",\"Other\",\"Total (EUR)\"\n";

    // Two Refund lines for the same order
    out << "\"27/03/2026\",\"Released\",\"Refund\",\"402-6078653-0276335\","
           "\"Robe Caftan...\",\"-28.82\",\"0\",\"5.21\",\"-6.05\",\"-29.66\"\n";
    out << "\"27/03/2026\",\"Released\",\"Refund\",\"402-6078653-0276335\","
           "\"Robe Qamis...\",\"-23.52\",\"0\",\"4.25\",\"-4.94\",\"-24.21\"\n";
    // Service fee row — should be ignored
    out << "\"27/03/2026\",\"Released\",\"Service Fees\",\"402-6078653-0276335\","
           "\"FBA Customer Returns Fee...\",\"0\",\"0\",\"-2.12\",\"0\",\"-2.12\"\n";

    file.close();

    QTemporaryDir workDir;
    ImporterFileAmazonTransactions importer(workDir.path());
    auto task = importer.loadReport(mockFile);
    AbstractImporter::ReturnOrderInfos result = QCoro::waitFor(task);

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable("Error: " + result.errorReturned));
    QVERIFY(result.orderInfos != nullptr);
    QCOMPARE(result.orderInfos->refunds.size(), 0);
    QCOMPARE(result.orderInfos->shipments.size(), 0);

    const auto &clues = result.orderInfos->orderId_refundClues;

    // Exactly one order ID
    QCOMPARE(clues.size(), 1);
    QVERIFY(clues.contains("402-6078653-0276335"));

    // Two clues (one per refund line), not one (no overwriting)
    const auto &orderClues = clues["402-6078653-0276335"];
    QCOMPARE(orderClues.size(), 2);

    // First clue: Caftan — product(-28.82) + other(-6.05) = -34.87
    QVERIFY(qAbs(orderClues[0].value - (-34.87)) < 0.01);
    QCOMPARE(orderClues[0].currency, QString("EUR"));
    QCOMPARE(orderClues[0].date, QDate(2026, 3, 27));

    // Second clue: Qamis — product(-23.52) + other(-4.94) = -28.46
    QVERIFY(qAbs(orderClues[1].value - (-28.46)) < 0.01);
    QCOMPARE(orderClues[1].currency, QString("EUR"));
    QCOMPARE(orderClues[1].date, QDate(2026, 3, 27));
}

QTEST_MAIN(TestFileImportAmazonTransactions)
#include "test_file_import_amazon_transactions.moc"
