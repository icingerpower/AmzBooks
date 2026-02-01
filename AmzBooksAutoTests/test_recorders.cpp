#include <QtTest>
#include <QCoreApplication>
#include "orders/AbstractImporterFile.h"
#include "orders/AbstractImporterApi.h"

class TestRecorders : public QObject
{
    Q_OBJECT

public:
    TestRecorders();
    ~TestRecorders();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_AbstractImporterFile_recorders();
    void test_AbstractImporterApi_recorders();
};

TestRecorders::TestRecorders()
{

}

TestRecorders::~TestRecorders()
{

}

void TestRecorders::initTestCase()
{

}

void TestRecorders::cleanupTestCase()
{

}

void TestRecorders::test_AbstractImporterFile_recorders()
{
    // Get all registered importers
    const auto& importers = AbstractImporterFile::ALL_IMPORTERS();
    
    // Verify that at least one importer is registered
    QVERIFY(importers.size() > 0);
    
    // Verify that we have the expected importers
    // Based on grep results, we should have:
    // - ImporterFileAmazonVatEu
    // - ImporterFileAmazonFbaInvoicing  
    // - ImporterFileAmazonTransactions
    QVERIFY(importers.size() >= 3);
    
    // Verify each registered importer has a valid label
    for (auto it = importers.begin(); it != importers.end(); ++it) {
        QVERIFY(!it.key().isEmpty());
        QVERIFY(it.value() != nullptr);
        QCOMPARE(it.value()->getLabel(), it.key());
    }
    
    qDebug() << "AbstractImporterFile registered importers:" << importers.size();
    for (auto it = importers.begin(); it != importers.end(); ++it) {
        qDebug() << "  -" << it.key();
    }
}

void TestRecorders::test_AbstractImporterApi_recorders()
{
    // Get all registered API importers
    const auto& importers = AbstractImporterApi::ALL_IMPORTERS();
    
    // Verify that at least one importer is registered
    QVERIFY(importers.size() > 0);
    
    // Based on grep results, we should have:
    // - ImporterApiAmazonEu
    // - ImporterApiAmazonAmerica
    // - ImporterApiTemuEu
    // - ImporterApiCommerceHQ
    QVERIFY(importers.size() >= 4);
    
    // Verify each registered importer has a valid label
    for (auto it = importers.begin(); it != importers.end(); ++it) {
        QVERIFY(!it.key().isEmpty());
        QVERIFY(it.value() != nullptr);
        QCOMPARE(it.value()->getLabel(), it.key());
    }
    
    qDebug() << "AbstractImporterApi registered importers:" << importers.size();
    for (auto it = importers.begin(); it != importers.end(); ++it) {
        qDebug() << "  -" << it.key();
    }
}

QTEST_MAIN(TestRecorders)

#include "test_recorders.moc"
