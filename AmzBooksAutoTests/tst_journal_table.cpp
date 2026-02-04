#include <QtTest>
#include <QTemporaryDir>
#include "books/JournalTable.h"
#include "orders/ActivitySource.h"

class TestJournalTable : public QObject
{
    Q_OBJECT

private slots:
    void test_init_and_persistence();
    void test_auto_population();
    void test_csv_row_shuffling();
    void test_csv_overrides_defaults();
    void test_getters();
};

void TestJournalTable::test_init_and_persistence()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    // 1. First Load - Should create file with auto-populated entries
    int initialRowCount;
    {
        JournalTable table(dir);
        initialRowCount = table.rowCount();
        // Should have at least "purchase" journal
        QVERIFY(initialRowCount >= 1);
        
        // Verify "purchase" exists
        bool foundPurchase = false;
        for (int i = 0; i < table.rowCount(); ++i) {
            QModelIndex idx = table.index(i, 0);
            if (table.data(idx).toString() == "AC" && table.data(table.index(i, 1)).toString() == "Purchase") {
                foundPurchase = true;
                break;
            }
        }
        QVERIFY(foundPurchase);
    }
    
    // 2. Modify Entry
    {
        JournalTable table(dir);
        QCOMPARE(table.rowCount(), initialRowCount);
        
        // Find purchase journal and modify its name
        for (int i = 0; i < table.rowCount(); ++i) {
            QModelIndex idxCode = table.index(i, 0);
            QModelIndex idxName = table.index(i, 1);
            
            // Col 0 = Code ("AC"), Col 1 = Name ("Purchase")
            if (table.data(idxCode).toString() == "AC" && table.data(idxName).toString() == "Purchase") {
                // Name (Col 1) SHOULD be editable
                QVERIFY(table.flags(idxName) & Qt::ItemIsEditable);
                QVERIFY(table.setData(idxName, "Purchase Invoices", Qt::EditRole));
                
                // Code (Col 0) should NOT be editable
                QVERIFY(!(table.flags(idxCode) & Qt::ItemIsEditable));
                
                // Check update
                QCOMPARE(table.data(idxName).toString(), QString("Purchase Invoices"));
                break;
            }
        }

        // Check file existence
        QFile file(dir.filePath("journals.csv"));
        QVERIFY(file.exists());
    }
    
    // 3. Reload and Verify persistence
    {
        JournalTable table(dir);
        QCOMPARE(table.rowCount(), initialRowCount);
        
        // Verify the modified name persisted
        bool foundModified = false;
        for (int i = 0; i < table.rowCount(); ++i) {
            if (table.data(table.index(i, 1)).toString() == "Purchase Invoices" && table.data(table.index(i, 0)).toString() == "AC") {
                foundModified = true;
                break;
            }
        }
        QVERIFY(foundModified);
    }
}

void TestJournalTable::test_auto_population()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());
    JournalTable table(dir);
    
    // Verify that table is auto-populated with at least the purchase journal
    QVERIFY(table.rowCount() >= 1);
    
    // Check that "purchase" journal exists
    bool foundPurchase = false;
    for (int i = 0; i < table.rowCount(); ++i) {
        QModelIndex idxCode = table.index(i, 0);
        QModelIndex idxName = table.index(i, 1);
        
        if (table.data(idxCode).toString() == "AC" &&
            table.data(idxName).toString() == "Purchase") {
            foundPurchase = true;
            break;
        }
    }
    QVERIFY(foundPurchase);
}

void TestJournalTable::test_csv_row_shuffling()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());
    
    // Create initial table and get row count
    int initialRowCount;
    QStringList initialCodes;
    {
        JournalTable table(dir);
        initialRowCount = table.rowCount();
        
        // Collect all codes in initial order
        for (int i = 0; i < table.rowCount(); ++i) {
            initialCodes.append(table.data(table.index(i, 0)).toString());
        }
    }
    
    // Manually shuffle the CSV file
    QString csvPath = dir.filePath("journals.csv");
    QFile file(csvPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    
    QStringList lines;
    QTextStream in(&file);
    QString header = in.readLine(); // Read header
    lines.append(header);
    
    QStringList dataLines;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (!line.trimmed().isEmpty()) {
            dataLines.append(line);
        }
    }
    file.close();
    
    // Shuffle the data lines (reverse order as simple shuffle)
    std::reverse(dataLines.begin(), dataLines.end());
    
    // Write back in shuffled order
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << header << "\n";
    for (const QString &line : dataLines) {
        out << line << "\n";
    }
    file.close();
    
    // Reload table and verify all entries are still present
    {
        JournalTable table(dir);
        QCOMPARE(table.rowCount(), initialRowCount);
        
        // Verify all original codes are still present (regardless of order)
        QSet<QString> currentCodes;
        for (int i = 0; i < table.rowCount(); ++i) {
            currentCodes.insert(table.data(table.index(i, 0)).toString());
        }
        
        QSet<QString> initialCodesSet = QSet<QString>(initialCodes.begin(), initialCodes.end());
        QCOMPARE(currentCodes, initialCodesSet);
    }
}

void TestJournalTable::test_csv_overrides_defaults()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());
    
    // Create initial table
    {
        JournalTable table(dir);
        QVERIFY(table.rowCount() >= 1);
    }
    
    // Manually edit CSV to override "purchase" journal name
    QString csvPath = dir.filePath("journals.csv");
    QFile file(csvPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    
    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        // Replace "Purchase" with "Custom Purchase Name" for purchase journal
        if (line.contains(";purchase")) {
            line.replace("Purchase", "Custom Purchase Name");
        }
        lines.append(line);
    }
    file.close();
    
    // Write modified content back
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    for (const QString &line : lines) {
        out << line << "\n";
    }
    file.close();
    
    // Reload and verify CSV override worked
    {
        JournalTable table(dir);
        
        bool foundCustomName = false;
        for (int i = 0; i < table.rowCount(); ++i) {
            if (table.data(table.index(i, 1)).toString() == "Custom Purchase Name" && table.data(table.index(i, 0)).toString() == "AC") {
                foundCustomName = true;
                break;
            }
        }
        QVERIFY(foundCustomName);
    }
}

void TestJournalTable::test_getters()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());
    JournalTable table(dir);
    
    // getJournalPurchaseInvoice
    auto item = table.getJournalPurchaseInvoice();
    QCOMPARE(item.name, QString("Purchase"));
    QCOMPARE(item.code, QString("AC"));
    QCOMPARE(item.id, QString("purchase"));

    // getJournalServiceSale
    item = table.getJournalServiceSale();
    QCOMPARE(item.name, QString("Service sales"));
    QCOMPARE(item.code, QString("VTSERVICE"));
    QCOMPARE(item.id, QString("service_sales"));

    // getJournalAmzPayment
    item = table.getJournalAmzPayment();
    QCOMPARE(item.name, QString("Amazon Payments"));
    QCOMPARE(item.code, QString("AC"));
    QCOMPARE(item.id, QString("amz_payments"));
    
    // getJournal(ActivitySource) -> ""
    QCOMPARE(table.getJournal(nullptr), QString(""));
}

QTEST_MAIN(TestJournalTable)
#include "tst_journal_table.moc"
