#include <QtTest>
#include <QTemporaryDir>
#include "books/JournalTable.h"
#include "model/ActivitySource.h"

class TestJournalTable : public QObject
{
    Q_OBJECT

private slots:
    void test_init_and_persistence();
    void test_getters();
};

void TestJournalTable::test_init_and_persistence()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    // 1. First Load - Should create file with "Purchase"
    {
        JournalTable table(dir);
        QCOMPARE(table.rowCount(), 1);
        // Col 0: Code ("AC"), Col 1: Name ("Purchase")
        QCOMPARE(table.data(table.index(0, 0)).toString(), QString("AC"));
        QCOMPARE(table.data(table.index(0, 1)).toString(), QString("Purchase"));
        
        // Check file existence
        QFile file(dir.filePath("journals.csv"));
        QVERIFY(file.exists());
    }
    
    // 2. Add New Row
    {
        JournalTable table(dir);
        QCOMPARE(table.rowCount(), 1);
        
        table.insertRows(1, 1);
        QModelIndex idxCode = table.index(1, 0); // Col 0 = Code
        QModelIndex idxName = table.index(1, 1); // Col 1 = Name
        
        // Code (Col 0) should NOT be editable
        QVERIFY(!(table.flags(idxCode) & Qt::ItemIsEditable));
        QVERIFY(!table.setData(idxCode, "VE", Qt::EditRole));
        
        // Name (Col 1) SHOULD be editable
        QVERIFY(table.flags(idxName) & Qt::ItemIsEditable);
        QVERIFY(table.setData(idxName, "Sales", Qt::EditRole));
        
        // Check update
        QCOMPARE(table.data(idxName).toString(), QString("Sales"));
        // Code remains empty
        QCOMPARE(table.data(idxCode).toString(), QString(""));
    }
    
    // 3. Reload and Verify
    {
        JournalTable table(dir);
        QCOMPARE(table.rowCount(), 2);
        // Row 1: Code="", Name="Sales"
        QCOMPARE(table.data(table.index(1, 0)).toString(), QString(""));
        QCOMPARE(table.data(table.index(1, 1)).toString(), QString("Sales"));
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
    
    // getJournal(ActivitySource) -> "AMZTODO"
    QCOMPARE(table.getJournal(nullptr), QString("AMZTODO"));
}

QTEST_MAIN(TestJournalTable)
#include "tst_journal_table.moc"
