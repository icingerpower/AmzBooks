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
    void test_FileImportersTable();
    void test_ApiImportersTable();
    void test_ParamsTable();
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

#include "orders/FileImportersTable.h"

void TestRecorders::test_FileImportersTable()
{
    FileImportersTable model;
    
    // Verify row count matches the number of registered importers
    QCOMPARE(model.rowCount(), AbstractImporterFile::ALL_IMPORTERS().size());
    
    // Verify column count
    QCOMPARE(model.columnCount(), FileImportersTable::ColCount);
    
    // Verify headers
    QCOMPARE(model.headerData(FileImportersTable::ColName, Qt::Horizontal).toString(), QString("Name"));
    QCOMPARE(model.headerData(FileImportersTable::ColChannel, Qt::Horizontal).toString(), QString("Channel"));
    
    // Verify data logic (at least for the first row if available)
    if (model.rowCount() > 0) {
        QModelIndex index = model.index(0, FileImportersTable::ColName);
        QVERIFY(index.isValid());
        
        // Get the importer pointer
        AbstractImporterFile* importer = model.getImporter(index);
        QVERIFY(importer != nullptr);
        
        // Check data matches
        QCOMPARE(model.data(index).toString(), importer->getLabel());
        
        QModelIndex indexChannel = model.index(0, FileImportersTable::ColChannel);
        QCOMPARE(model.data(indexChannel).toString(), importer->getActivitySource().channel);
    }
    

    
    // Verify sorting (Name -> Channel -> Subchannel -> Report)
    for (int i = 1; i < model.rowCount(); ++i) {
        QModelIndex prevIdx = model.index(i - 1, 0);
        QModelIndex currIdx = model.index(i, 0);
        AbstractImporterFile* prev = model.getImporter(prevIdx);
        AbstractImporterFile* curr = model.getImporter(currIdx);
        
        QVERIFY(prev != nullptr);
        QVERIFY(curr != nullptr);
        
        int cmp = prev->getLabel().compare(curr->getLabel(), Qt::CaseInsensitive);
        bool sorted = false;
        if (cmp < 0) sorted = true;
        else if (cmp == 0) {
            cmp = prev->getActivitySource().channel.compare(curr->getActivitySource().channel, Qt::CaseInsensitive);
            if (cmp < 0) sorted = true;
            else if (cmp == 0) {
                cmp = prev->getActivitySource().subchannel.compare(curr->getActivitySource().subchannel, Qt::CaseInsensitive);
                if (cmp < 0) sorted = true;
                else if (cmp == 0) {
                    cmp = prev->getActivitySource().reportOrMethode.compare(curr->getActivitySource().reportOrMethode, Qt::CaseInsensitive);
                    if (cmp <= 0) sorted = true;
                }
            }
        }
        
        QVERIFY2(sorted, qPrintable(QString("Not sorted correctly at row %1 and %2").arg(i-1).arg(i)));
    }
}

#include "orders/ApiImportersTable.h"

void TestRecorders::test_ApiImportersTable()
{
    ApiImportersTable model;
    
    // Verify row count matches the number of registered importers
    QCOMPARE(model.rowCount(), AbstractImporterApi::ALL_IMPORTERS().size());
    
    // Verify column count
    QCOMPARE(model.columnCount(), ApiImportersTable::ColCount);
    
    // Verify headers
    QCOMPARE(model.headerData(ApiImportersTable::ColName, Qt::Horizontal).toString(), QString("Name"));
    QCOMPARE(model.headerData(ApiImportersTable::ColChannel, Qt::Horizontal).toString(), QString("Channel"));
    
    // Verify data logic (at least for the first row if available)
    if (model.rowCount() > 0) {
        QModelIndex index = model.index(0, ApiImportersTable::ColName);
        QVERIFY(index.isValid());
        
        // Get the importer pointer
        AbstractImporterApi* importer = model.getImporter(index);
        QVERIFY(importer != nullptr);
        
        // Check data matches
        QCOMPARE(model.data(index).toString(), importer->getLabel());
        
        QModelIndex indexChannel = model.index(0, ApiImportersTable::ColChannel);
        QCOMPARE(model.data(indexChannel).toString(), importer->getActivitySource().channel);
    }
    
    // Verify sorting (Name -> Channel -> Subchannel -> Report)
    for (int i = 1; i < model.rowCount(); ++i) {
        QModelIndex prevIdx = model.index(i - 1, 0);
        QModelIndex currIdx = model.index(i, 0);
        AbstractImporterApi* prev = model.getImporter(prevIdx);
        AbstractImporterApi* curr = model.getImporter(currIdx);
        
        QVERIFY(prev != nullptr);
        QVERIFY(curr != nullptr);
        
        int cmp = prev->getLabel().compare(curr->getLabel(), Qt::CaseInsensitive);
        bool sorted = false;
        if (cmp < 0) sorted = true;
        else if (cmp == 0) {
            cmp = prev->getActivitySource().channel.compare(curr->getActivitySource().channel, Qt::CaseInsensitive);
            if (cmp < 0) sorted = true;
            else if (cmp == 0) {
                cmp = prev->getActivitySource().subchannel.compare(curr->getActivitySource().subchannel, Qt::CaseInsensitive);
                if (cmp < 0) sorted = true;
                else if (cmp == 0) {
                    cmp = prev->getActivitySource().reportOrMethode.compare(curr->getActivitySource().reportOrMethode, Qt::CaseInsensitive);
                    if (cmp <= 0) sorted = true;
                }
            }
        }
        
        QVERIFY2(sorted, qPrintable(QString("Not sorted correctly at row %1 and %2").arg(i-1).arg(i)));
    }
}


#include "orders/ParamsTable.h"
#include "orders/ImporterApiAmazonEu.h"

void TestRecorders::test_ParamsTable()
{
    // Iterate over all registered API importers
    const auto& importers = AbstractImporterApi::ALL_IMPORTERS();
    
    // Ensure we have importers to test
    QVERIFY(importers.size() > 0);

    for (auto it = importers.constBegin(); it != importers.constEnd(); ++it) {
        // Cast away constness for testing purposes (simulating the editor usage)
        AbstractImporter* importer = const_cast<AbstractImporterApi*>(it.value());
        QVERIFY(importer != nullptr);
        
        qDebug() << "Testing ParamsTable for importer:" << it.key();

        // Initialize parameters (load defaults)
        importer->load();
        
        ParamsTable model(importer);
        
        // Check structural integrity
        QCOMPARE(model.columnCount(), ParamsTable::ColCount);
        
        // 1. Verify getLoadedParamValues returns values (via model)
        const auto& params = importer->getLoadedParamValues();
        QCOMPARE(model.rowCount(), params.size());
        
        if (model.rowCount() > 0) {
            // 2. Test Editing
            // Target the last parameter to avoid issues if 0 is special, but 0 should be fine.
            int rowToEdit = 0;
            QModelIndex valIdx = model.index(rowToEdit, ParamsTable::ColParamValue);
            QVERIFY(valIdx.isValid());

            QString testValue = "TestValue_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + it.key();
            
            // Special handling for CommerceHQ "stores" param which requires valid JSON
            // We check the key of the row we are editing
            QString paramKey = model.data(model.index(rowToEdit, ParamsTable::ColParamName)).toString();
            // Actually col 0 returns label or key. Let's get the key from the importer directly to be sure or check label.
            // But we don't have easy access to key from row index in public API without iterating.
            // So let's just make the value generic valid JSON array, which is also a valid string, usually.
            // Or specific check:
            if (it.key().contains("CommerceHQ", Qt::CaseInsensitive) || it.key().contains("Temu", Qt::CaseInsensitive)) {
                 testValue = "[]";
            }

            // Before edit
            QVariant originalValue = model.data(valIdx);
            
            // Perform Edit
            bool success = model.setData(valIdx, testValue);
            if (!success) {
                qWarning() << "ParamsTable::setData failed for" <<it.key() << "with value" << testValue;
            }
            QVERIFY(success);
            
            // Check model updated
            QCOMPARE(model.data(valIdx).toString(), testValue);
            
            // 3. Verify params are really saved in the importer
            const auto& currentParams = importer->getLoadedParamValues();
            
            bool valueFound = false;
            for (auto paramIt = currentParams.constBegin(); paramIt != currentParams.constEnd(); ++paramIt) {
                 if (paramIt.value().value.toString() == testValue) {
                     valueFound = true;
                     break;
                 }
            }
            QVERIFY2(valueFound, qPrintable("Value not saved in importer for " + it.key()));
        } else {
            qWarning() << "Importer" << it.key() << "has no parameters to test.";
        }
    }
}

QTEST_MAIN(TestRecorders)

#include "test_recorders.moc"
