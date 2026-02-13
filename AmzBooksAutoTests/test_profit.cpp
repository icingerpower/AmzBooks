#include <QtTest>
#include "profit/PurchaseFileSettingsTree.h"
#include "profit/PurchaseFileSettingsTreeItem.h"
#include "orders/ExceptionParamValue.h"
#include <QDir>
#include <QStandardPaths>
#include <QFile>

class TestProfit : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testAddDuplicate();
    void testSaveLoad();
    void testSaveReorderLoad();
    void testSaveRemoveLoad();
    void testAdditionalChecks();
    void testGetColPosOriginalAndChild();

private:
    QDir m_testDir;
};

void TestProfit::initTestCase()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/TestProfit";
    m_testDir = QDir(path);
    if (m_testDir.exists()) m_testDir.removeRecursively();
    m_testDir.mkpath(".");
}

void TestProfit::cleanupTestCase()
{
    m_testDir.removeRecursively();
}

void TestProfit::testAddDuplicate()
{
    PurchaseFileSettingsTree tree(m_testDir);
    
    // Find a fixed row (e.g. Order ID)
    QModelIndex orderIdIdx;
    for (int i=0; i<tree.rowCount(); ++i) {
        QModelIndex idx = tree.index(i, 0);
        if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_ORDER_ID) {
            orderIdIdx = idx;
            break;
        }
    }
    QVERIFY(orderIdIdx.isValid());
    
    // Add 3 labels
    tree.addCandidate(orderIdIdx, "Label 1");
    tree.addCandidate(orderIdIdx, "Label 2");
    tree.addCandidate(orderIdIdx, "Label 3");
    
    // Verify count
    QCOMPARE(tree.rowCount(orderIdIdx), 3);
    
    // Add "Label 3" again -> should throw
    bool exceptionThrown = false;
    try {
        tree.addCandidate(orderIdIdx, "Label 3");
    } catch (const ExceptionParamValue&) {
        exceptionThrown = true;
    }
    
    QVERIFY(exceptionThrown);
    QCOMPARE(tree.rowCount(orderIdIdx), 3); // Count shouldn't change
}

void TestProfit::testSaveLoad()
{
    {
        PurchaseFileSettingsTree tree(m_testDir);
        // Add candidate to Title
        QModelIndex titleIdx;
         for (int i=0; i<tree.rowCount(); ++i) {
            QModelIndex idx = tree.index(i, 0);
            if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_TITLE) {
                titleIdx = idx;
                break;
            }
        }
        tree.addCandidate(titleIdx, "Book Title");
    }
    
    // Reload
    PurchaseFileSettingsTree tree2(m_testDir);
    QStringList headers = {"Book Title", "Order ID"};
    int idx = tree2.getColPos(headers, PurchaseFileSettingsTree::COL_TITLE);
    QCOMPARE(idx, 0);
}

void TestProfit::testSaveReorderLoad()
{
    // Setup initial data
    {
        PurchaseFileSettingsTree tree(m_testDir);
        // Clear previous generic additions for cleaner test state if needed, but append is fine.
        // Let's add distinctive candidates
        
        QModelIndex skuIdx;
         for (int i=0; i<tree.rowCount(); ++i) {
            QModelIndex idx = tree.index(i, 0);
            if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_SKU) {
                skuIdx = idx;
                break;
            }
        }
        tree.addCandidate(skuIdx, "SKU_A");
        tree.addCandidate(skuIdx, "SKU_B");
    }
    
    // Manually Edit CSV to reorder lines
    QFile file(m_testDir.filePath("purchaseFileSettings.csv"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QStringList lines;
    QTextStream in(&file);
    while(!in.atEnd()) lines << in.readLine();
    file.close();
    
    // Find lines
    int idxA = -1, idxB = -1;
    for(int i=0; i<lines.size(); ++i) {
        if(lines[i].contains("SKU_A")) idxA = i;
        if(lines[i].contains("SKU_B")) idxB = i;
    }
    
    QVERIFY(idxA != -1 && idxB != -1);
    // Swap them if they are in order, to test reordering
    // Actually the prompt says "reorder the csv". Let's reverse list.
    std::reverse(lines.begin(), lines.end());
    
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QTextStream out(&file);
    for(const QString &line : lines) out << line << "\n";
    file.close();
    
    // Reload
    PurchaseFileSettingsTree tree2(m_testDir);
    // Verify both are present and working
    QStringList headers = {"SKU_A", "SKU_B", "Other"};
    QCOMPARE(tree2.getColPos(headers, PurchaseFileSettingsTree::COL_SKU), 0);
    
    QStringList headers2 = {"Other", "SKU_B"};
    QCOMPARE(tree2.getColPos(headers2, PurchaseFileSettingsTree::COL_SKU), 1);
}

void TestProfit::testSaveRemoveLoad()
{
     {
        PurchaseFileSettingsTree tree(m_testDir);
        QModelIndex priceIdx;
         for (int i=0; i<tree.rowCount(); ++i) {
            QModelIndex idx = tree.index(i, 0);
            if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_UNIT_PRICE) {
                priceIdx = idx;
                break;
            }
        }
        tree.addCandidate(priceIdx, "Price_Remove");
        tree.addCandidate(priceIdx, "Price_Keep");
        
        // Remove "Price_Remove"
        // Need to find index of child
        QModelIndex removeIdx = tree.index(0, 0, priceIdx); // Assuming it's first
        if (tree.data(removeIdx).toString() != "Price_Remove") {
            // Find it
            for(int i=0; i<tree.rowCount(priceIdx); ++i) {
                if (tree.data(tree.index(i, 0, priceIdx)).toString() == "Price_Remove") {
                    removeIdx = tree.index(i, 0, priceIdx);
                    break;
                }
            }
        }
        
        QVERIFY(removeIdx.isValid());
        tree.removeRow(removeIdx.row(), priceIdx);
    }
    
    // Reload
    PurchaseFileSettingsTree tree2(m_testDir);
    QStringList headers = {"Price_Remove", "Price_Keep"};
    // "Price_Remove" should NOT match Price ID anymore
    int idx = tree2.getColPos(headers, PurchaseFileSettingsTree::COL_UNIT_PRICE);
    QCOMPARE(idx, 1); // Should match "Price_Keep" at index 1
}

void TestProfit::testAdditionalChecks()
{
    PurchaseFileSettingsTree tree(m_testDir);
    
    // 1. Verify Root has correct number of children (Fixed rows)
    QCOMPARE(tree.rowCount(), PurchaseFileSettingsTree::FIXED_ROW_IDS.size());
    
    // 2. Verify Fixed Rows are not editable
    QModelIndex firstIdx = tree.index(0, 0);
    QVERIFY(!(tree.flags(firstIdx) & Qt::ItemIsEditable));
    
    // 3. Verify Candidates are editable
    tree.addCandidate(firstIdx, "EditableCandidate");
    QModelIndex candIdx = tree.index(tree.rowCount(firstIdx)-1, 0, firstIdx);
    QVERIFY(tree.flags(candIdx) & Qt::ItemIsEditable);
    
    // 4. Verify Rename Candidate
    tree.setData(candIdx, "RenamedCandidate", Qt::EditRole);
    QCOMPARE(tree.data(candIdx).toString(), QString("RenamedCandidate"));
    
    // 5. Verify ID retrieval via UserRole
    QCOMPARE(tree.data(firstIdx, Qt::UserRole).toString(), PurchaseFileSettingsTree::FIXED_ROW_IDS[0]);
    
    // 6. Verify Candidate has no ID in UserRole
    QVERIFY(tree.data(candIdx, Qt::UserRole).isNull());
    
    // 7. Verify parent of candidate
    QCOMPARE(tree.parent(candIdx), firstIdx);
    
    // 8. Verify parent of fixed row is invalid (root)
    QVERIFY(!tree.parent(firstIdx).isValid());
    
    // 9. Verify getColPos returns -1 for empty headers
    QCOMPARE(tree.getColPos({}, PurchaseFileSettingsTree::COL_ORDER_ID), -1);
    
    // 10. Verify adding candidate to invalid index does nothing
    int countBefore = tree.rowCount(firstIdx);
    tree.addCandidate(QModelIndex(), "InvalidParent");
    QCOMPARE(tree.rowCount(firstIdx), countBefore);
}

void TestProfit::testGetColPosOriginalAndChild()
{
    PurchaseFileSettingsTree tree(m_testDir);
    
    // 1. Setup: Add a candidate to SKU
    QModelIndex skuIdx;
    for (int i=0; i<tree.rowCount(); ++i) {
        QModelIndex idx = tree.index(i, 0);
        if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_SKU) {
            skuIdx = idx;
            break;
        }
    }
    QVERIFY(skuIdx.isValid());
    QString fixedName = tree.data(skuIdx).toString(); // "SKU" (tr)
    
    tree.addCandidate(skuIdx, "My Custom SKU");
    
    // 2. Test getting position using the Fixed Row Name (Original)
    {
        QStringList headers = {"Date", fixedName, "Amount"}; 
        // "SKU" is at index 1
        int idx = tree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
        QCOMPARE(idx, 1);
    }
    
    // 3. Test getting position using the Child Row Name (Candidate)
    {
        QStringList headers = {"Date", "Amount", "My Custom SKU"};
        // "My Custom SKU" is at index 2
        int idx = tree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
        QCOMPARE(idx, 2);
    }
    
    // 4. Test priority (Leftmost wins should apply if both are present)
    {
        QStringList headers = {"My Custom SKU", fixedName};
        // "My Custom SKU" at 0, "SKU" at 1.
        // Leftmost wins -> 0
        int idx = tree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
        QCOMPARE(idx, 0);
        
        QStringList headers2 = {fixedName, "My Custom SKU"};
        // "SKU" at 0, "My Custom SKU" at 1.
        // Leftmost wins -> 0
        int idx2 = tree.getColPos(headers2, PurchaseFileSettingsTree::COL_SKU);
        QCOMPARE(idx2, 0);
    }
}

QTEST_MAIN(TestProfit)
#include "test_profit.moc"
