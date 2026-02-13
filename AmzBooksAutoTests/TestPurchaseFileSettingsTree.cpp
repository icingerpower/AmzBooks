#include <QtTest>
#include "profit/PurchaseFileSettingsTree.h"
#include "profit/PurchaseFileSettingsTreeItem.h"
#include "orders/ExceptionParamValue.h"
#include <QSignalSpy>
#include <QDir>
#include <QStandardPaths>
#include <QFile>

class TestPurchaseFileSettingsTree : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testGetColPos();
    void testGetColPosWithCandidates();
    void testPersistence();
    void testAddDuplicateCandidate();

private:
    QDir m_testDir;
};

void TestPurchaseFileSettingsTree::initTestCase()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/TestPurchaseFileSettingsTree";
    m_testDir = QDir(path);
    if (m_testDir.exists()) m_testDir.removeRecursively();
    m_testDir.mkpath(".");
}

void TestPurchaseFileSettingsTree::cleanupTestCase()
{
    m_testDir.removeRecursively();
}

void TestPurchaseFileSettingsTree::testGetColPos()
{
    PurchaseFileSettingsTree tree(m_testDir);
    
    QStringList headers = {"Date", "Title", "Ignore", "Order ID", "Amount"};
    
    // Test exact match with fixed row name/ID
    int idx = tree.getColPos(headers, PurchaseFileSettingsTree::COL_ORDER_ID);
    QCOMPARE(idx, 3);
    
    idx = tree.getColPos(headers, PurchaseFileSettingsTree::COL_TITLE);
    QCOMPARE(idx, 1);
    
    // Test not found
    idx = tree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
    QCOMPARE(idx, -1);
}

void TestPurchaseFileSettingsTree::testGetColPosWithCandidates()
{
    PurchaseFileSettingsTree tree(m_testDir);
    
    // Add a candidate for SKU
    QModelIndex skuIndex;
    for (int i=0; i<tree.rowCount(); ++i) {
        QModelIndex idx = tree.index(i, 0);
        if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_SKU) {
            skuIndex = idx;
            break;
        }
    }
    QVERIFY(skuIndex.isValid());
    
    tree.addCandidate(skuIndex, "Custom SKU Column");
    
    QStringList headers = {"Date", "Custom SKU Column", "Amount"};
    
    int idx = tree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
    QCOMPARE(idx, 1);
}

void TestPurchaseFileSettingsTree::testPersistence()
{
    {
        PurchaseFileSettingsTree tree(m_testDir);
        // Add candidate to Order ID
        QModelIndex orderIdIndex;
        for (int i=0; i<tree.rowCount(); ++i) {
            QModelIndex idx = tree.index(i, 0);
            if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_ORDER_ID) {
                orderIdIndex = idx;
                break;
            }
        }
        tree.addCandidate(orderIdIndex, "Bestseller Order");
    }
    
    // Verify file content uses ID
    QFile file(m_testDir.filePath("purchaseFileSettings.csv"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    
    // Should contain "Order ID;Bestseller Order"
    // "Order ID" is the ID for COL_ORDER_ID
    QVERIFY(content.contains("Order ID;Bestseller Order"));
    
    // Reload
    PurchaseFileSettingsTree tree2(m_testDir);
    QStringList headers = {"Bestseller Order", "Other"};
    int idx = tree2.getColPos(headers, PurchaseFileSettingsTree::COL_ORDER_ID);
    QCOMPARE(idx, 0);
}

void TestPurchaseFileSettingsTree::testAddDuplicateCandidate()
{
    PurchaseFileSettingsTree tree(m_testDir);
    
    // Get SKU index
    QModelIndex skuIdx;
    for (int i=0; i<tree.rowCount(); ++i) {
        QModelIndex idx = tree.index(i, 0);
        if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_SKU) {
            skuIdx = idx;
            break;
        }
    }
    QVERIFY(skuIdx.isValid());
    
    // Add "My SKU"
    tree.addCandidate(skuIdx, "My SKU");
    
    // 1. Try adding same "My SKU" again -> should throw
    bool exceptionThrown = false;
    try {
        tree.addCandidate(skuIdx, "My SKU");
    } catch (const ExceptionParamValue&) {
        exceptionThrown = true;
    }
    QVERIFY(exceptionThrown);
    
    // 2. Try adding "Title" (which is a fixed row name) -> should throw
    // Note: Is "Title" translated? FIXED_ROW_NAMES uses tr().
    // If the test environment language is English, "Title" should match.
    // Let's check fixed row name directly from tree to be sure what we are clashing with.
    QModelIndex titleIdx;
     for (int i=0; i<tree.rowCount(); ++i) {
        QModelIndex idx = tree.index(i, 0);
        if (tree.data(idx, Qt::UserRole).toString() == PurchaseFileSettingsTree::COL_TITLE) {
            titleIdx = idx;
            break;
        }
    }
    QString titleName = tree.data(titleIdx, Qt::DisplayRole).toString();
    
    exceptionThrown = false;
    try {
        tree.addCandidate(skuIdx, titleName);
    } catch (const ExceptionParamValue&) {
        exceptionThrown = true;
    }
    QVERIFY(exceptionThrown);
}

QTEST_MAIN(TestPurchaseFileSettingsTree)
#include "TestPurchaseFileSettingsTree.moc"
