#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSqlQuery>
#include "model/OrderManager.h"
#include "model/Shipment.h"
#include "model/Refund.h"
#include "model/Activity.h"
#include "model/ActivitySource.h"
#include "model/LineItem.h"
#include "model/ActivityUpdate.h"
#include "model/Address.h"
#include "model/InvoicingInfo.h"

class TestOrderManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_init();
    void test_recordShipmentDraft();
    void test_publish();
    void test_record_publish_update();
    void test_activity_update_model();
    void test_record_with_refund();
    void test_getShipments();
    void test_invoicingInfos();
};

void TestOrderManager::initTestCase()
{
}

void TestOrderManager::cleanupTestCase()
{
}

void TestOrderManager::test_init()
{
    QTemporaryDir tempDir;
    OrderManager manager(tempDir.path());
    
    // Check if DB exists
    QFile dbFile(tempDir.filePath("Orders.db"));
    QVERIFY(dbFile.exists());
}

void TestOrderManager::test_recordShipmentDraft()
{
    QTemporaryDir tempDir;
    OrderManager manager(tempDir.path());
    
    ActivitySource source{ActivitySourceType::Report, "Amazon", "amazon.fr", "Report1"};
    
    // Create Activity with subId
    auto actRes = Activity::create("evt1", "act1", "sub1", QDateTime::currentDateTime(), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    QVERIFY(actRes.errors.isEmpty());
    
    Shipment shipment({*actRes.value});
    
    manager.recordShipmentFromSource("ord1", &source, &shipment, QDate());
    
    QDateTime dt = manager.getLastDateTime(&source);
    QVERIFY(dt.isValid());
}

void TestOrderManager::test_publish()
{
    QTemporaryDir tempDir;
    OrderManager manager(tempDir.path());
    
    ActivitySource source{ActivitySourceType::Report, "Amazon", "amazon.fr", "Report1"};
    auto actRes = Activity::create("evt1", "act1", "", QDateTime::currentDateTime(), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    
    Shipment shipment({*actRes.value});
    manager.recordShipmentFromSource("ord1", &source, &shipment, QDate());
    
    QDate tomorrow = QDate::currentDate().addDays(1);
    manager.publish(tomorrow);
    
    // Let's modify shipment source and record again
    auto actRes2 = Activity::create("evt1", "act1", "", QDateTime::currentDateTime(), "EUR", "FR", "DE", "DE",
         Amount(120.0, 24.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment2({*actRes2.value});
    
    manager.recordShipmentFromSource("ord1", &source, &shipment2, QDate());
}

void TestOrderManager::test_record_publish_update()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) QSKIP("Temp dir invalid");
    
    OrderManager manager(tempDir.path());
    QString orderId = "ord1";
    Address addr("John Doe", "Street", "", "", "City", "12345", "DE", "", "", "", "", "");
    ActivitySource source{ActivitySourceType::Report, "FR", "Amazon.fr", "GET_ORDERS_DATA"};

    // 1. Create a shipment
    auto actRes = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment({*actRes.value});
    manager.recordShipmentFromSource(orderId, &source, &shipment, QDate());
    
    // 1.b Update again with modified hour (same day) -> Should NOT create double entry
    {
        auto actResMod = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(12, 0)), "EUR", "FR", "DE", "DE",
             Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
        Shipment shipmentMod({*actResMod.value});
        manager.recordShipmentFromSource(orderId, &source, &shipmentMod, QDate());
        
        // Check still 1 shipment
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1); 
        
        // Verify Content updated
        q.exec("SELECT current_json FROM shipments");
        QVERIFY(q.next());
        // Simple check that it's updated (e.g. time)
        QVERIFY(q.value(0).toString().contains("12:00:00"));
    }
    
    // Check we have 1 shipment (Draft)
    {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1); 
    }

    // 2. Publish it
    QDate publishUntil = QDate(2023, 2, 1);
    manager.publish(publishUntil);

    // 3. Update the shipment with same values => Check no change
    manager.recordShipmentFromSource(orderId, &source, &shipment, QDate());
    {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1); 
    }

    // 4. Update the shipment with CONFLICT (new amount) => Check double entry created
    auto actRes2 = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(200.0, 40.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment2({*actRes2.value});
    
    QDate dateIfConflict(2023, 2, 15);
    manager.recordShipmentFromSource(orderId, &source, &shipment2, dateIfConflict);

    // Now we should have 3 shipments:
    // 1 Published (Original)
    // 1 Draft (Reversal)
    // 1 Draft (New version)
    {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 3); 
    }
    
    // 5. Publish again
    QDate publishUntil2 = QDate(2023, 3, 1);
    manager.publish(publishUntil2);

    // All should be published
    {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT count(*) FROM shipments WHERE status='Published'");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 3); 
    }

    // 6. Update the shipment again ...
    auto actRes3 = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(300.0, 60.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment3({*actRes3.value});
    manager.recordShipmentFromSource(orderId, &source, &shipment3, dateIfConflict);

    // Still 3 shipments (Published: Original, Reversal, v2)
    // + 2 New Drafts (Reversal of v2, v3) -> Total 5
    {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 5); 
    }
    
    // 7. publish the shipment
    QDate publishDate3 = QDate(2023, 5, 1);
    manager.publish(publishDate3);
    
    {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments WHERE status='Published'");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 5);
    }
    
    // 8. Update the shipment again without conflict
    manager.recordShipmentFromSource(orderId, &source, &shipment3, dateIfConflict);
    
     {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 5);
    }

    // 9. Update the shipment again with conflict -> +2 records
     auto actRes4 = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(400.0, 80.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment4({*actRes4.value});
    
    manager.recordShipmentFromSource(orderId, &source, &shipment4, dateIfConflict);
    
    // Should increase by 2 again => 7
    {
        QSqlQuery q(manager.m_db);
        q.exec("SELECT COUNT(*) FROM shipments");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 7);
    }
}

void TestOrderManager::test_activity_update_model()
{
    QTemporaryDir tempDir;
    OrderManager manager(tempDir.path());
    
    QString orderId = "ord1";
    Address addr("John Doe", "Street", "", "", "City", "12345", "DE", "", "", "", "", "");
    ActivitySource source{ActivitySourceType::Report, "FR", "Amazon.fr", "GET_ORDERS_DATA"};
    
    auto actRes = Activity::create("evt1", "act1", "sub1", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
         
    Shipment shipment({*actRes.value});
    
    manager.recordShipmentFromSource(orderId, &source, &shipment, QDate());
    
    QDate publishDate = QDate(2023, 2, 1);
    manager.publish(publishDate);
    
    // Update with Conflict
    auto actResConf = Activity::create("evt1", "act1", "sub1", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(200.0, 40.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipmentConf({*actResConf.value});
    
    manager.recordShipmentFromSource(orderId, &source, &shipmentConf, QDate(2023, 3, 1)); // Conflict date
    
    // Update with extra activity (split)
    // Actually, conflict creates new version (Draft)
    
    QDate publishDate2 = QDate(2023, 4, 1);
    manager.publish(publishDate2);
    
    // Test Model
    ActivityUpdate *model = manager.createActivityUpdateModel("act1");
    // Should have 3 items: Initial, Reversal of Initial, New Version
    QCOMPARE(model->rowCount(), 3);
    
    delete model;
}

void TestOrderManager::test_record_with_refund()
{
    QTemporaryDir tempDir;
    OrderManager manager(tempDir.path());
    
    QString orderId = "ord_ref";
    ActivitySource source{ActivitySourceType::Report, "FR", "Amazon.fr", "GET_REFUND_DATA"};
    
    auto actRes = Activity::create("evt_ref", "act_ref", "sub1", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
         
    Refund refund({*actRes.value}); 
    
    manager.recordShipmentFromSource(orderId, &source, &refund, QDate());
    
    // Update without conflict
    auto actResUpd = Activity::create("evt_ref", "act_ref", "sub1", QDateTime(QDate(2023, 1, 1), QTime(12, 0)), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Refund refundUpd({*actResUpd.value});
    manager.recordShipmentFromSource(orderId, &source, &refundUpd, QDate());
    
    // Check no dual entry (1 total)
    {
         QSqlQuery q(manager.m_db);
         q.exec("SELECT COUNT(*) FROM shipments");
         QVERIFY(q.next());
         QCOMPARE(q.value(0).toInt(), 1);
    }
    
    QDate publishDate = QDate(2023, 4, 1);
    manager.publish(publishDate);
    
    // Update without conflict (after publish)
    // Same amount/taxes, just time changed
    auto actResNoConf = Activity::create("evt_ref", "act_ref", "sub1", QDateTime(QDate(2023, 1, 1), QTime(14, 0)), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Refund refundNoConf({*actResNoConf.value});
    manager.recordShipmentFromSource(orderId, &source, &refundNoConf, QDate());
     
    // Check no dual entry (1 total - updated in place)
    {
         QSqlQuery q(manager.m_db);
         q.exec("SELECT COUNT(*) FROM shipments");
         QVERIFY(q.next());
         QCOMPARE(q.value(0).toInt(), 1);
    }
    
    // Update WITH conflict
    auto actResConf = Activity::create("evt_ref", "act_ref", "sub1", QDateTime(QDate(2023, 1, 1), QTime(14, 0)), "EUR", "FR", "DE", "DE",
         Amount(150.0, 30.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Refund refundConf({*actResConf.value});
    manager.recordShipmentFromSource(orderId, &source, &refundConf, QDate(2023, 3, 1));
    
    // Check dual entry created (1 original published + 1 Reversal + 1 New = 3)
    {
         QSqlQuery q(manager.m_db);
         q.exec("SELECT COUNT(*) FROM shipments");
         QVERIFY(q.next());
         QCOMPARE(q.value(0).toInt(), 3);
    }
    
    // Helper check
    manager.publish(publishDate); // Publish the new changes
    
    // Check model for refund too
    ActivityUpdate *model = manager.createActivityUpdateModel("act_ref");
    QCOMPARE(model->rowCount(), 3);
    delete model;
}

void TestOrderManager::test_getShipments()
{
    QTemporaryDir tempDir;
    OrderManager manager(tempDir.path());
    QString orderId = "ord_get";
    ActivitySource source{ActivitySourceType::Report, "FR", "Amazon.fr", "GET_ORDERS_DATA"};

    // 1. Create a shipment (Jan 1)
    // Amount 100 + 20 tax = 120
    auto actRes = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment({*actRes.value});
    manager.recordShipmentFromSource(orderId, &source, &shipment, QDate());

    // 2. Publish it (Up to Feb 1)
    QDate datePublish = QDate(2023, 2, 1);
    manager.publish(datePublish);

    // 3. Update with conflict (Feb 1)
    // New Amount 200 + 40 = 240
    auto actResConf = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(200.0, 40.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipmentConf({*actResConf.value});
    
    // Conflict Date: Feb 15
    QDate conflictDate(2023, 2, 15);
    manager.recordShipmentFromSource(orderId, &source, &shipmentConf, conflictDate);

    // 4. getShipmentAndRefunds
    // Range Jan 1 to Mar 1
    QDate dateFrom(2023, 1, 1);
    QDate dateTo(2023, 3, 1);
    
    auto results = manager.getShipmentAndRefunds(dateFrom, dateTo, [](const ActivitySource*, const Shipment*){ return true; });
    
    // Check 3 entries
    QCOMPARE(results.size(), 3);
    
    // Check correctness:
    // 1. Original (Published): 120 EUR
    // 2. Reversal (Draft): -120 EUR (reversed from original) - Date should be conflictDate?
    // 3. New (Draft): 240 EUR - Date should be conflictDate?
    
    double sum = 0;
    int count = 0;
    for (auto it = results.begin(); it != results.end(); ++it) {
        double val = 0;
        for (const auto &act : it.value()->getActivities()) {
             val += (act.getAmountTaxed() + act.getAmountTaxes());
        }
        
        sum += val;
        count++;
    }
    
    // Expected Sum: 120 (Original) - 120 (Reversal) + 240 (New) = 240.
    QCOMPARE(sum, 240.0);

    // 5. Add a full refund on the last entry
    // Last entry is the "New Version" (240 EUR).
    // Create a Refund object matching it.
    auto actResRef = Activity::create("evt1_ref", "act1_ref", "", QDateTime(QDate(2023, 2, 20), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(-200.0, -40.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    
    Refund refund({*actResRef.value});
    manager.recordShipmentFromSource("ord_get_ref", &source, &refund, QDate()); 
    
    // 6. Check 4 entries. Sum 0.
    results = manager.getShipmentAndRefunds(dateFrom, dateTo, [](const ActivitySource*, const Shipment*){ return true; });
    QCOMPARE(results.size(), 4);
    
    sum = 0;
    for (auto it = results.begin(); it != results.end(); ++it) {
        double val = 0;
        for (const auto &act : it.value()->getActivities()) {
             val += (act.getAmountTaxed() + act.getAmountTaxes());
        }
        
        sum += val;
    }
    
    QCOMPARE(sum, 0.0);
}

void TestOrderManager::test_invoicingInfos()
{
    QTemporaryDir tempDir;
    OrderManager manager(tempDir.path());
    QString orderId = "ord_inv";
    ActivitySource source{ActivitySourceType::Report, "FR", "Amazon.fr", "GET_ORDERS_DATA"};
    
    // 1. Create Shipment
    auto actRes = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(100.0, 20.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment({*actRes.value});
    manager.recordShipmentFromSource(orderId, &source, &shipment, QDate());
    
    // 2. Add Invoicing Info
    InvoicingInfo info(&shipment, {}, "INV-001");
    manager.recordInvoicingInfo(shipment.getId(), &info);
    
    // Check retrieval
    auto retrieved = manager.getInvoicingInfo(shipment.getId());
    QVERIFY(retrieved);
    QVERIFY(retrieved.data()); // check pointer validity
    QCOMPARE(retrieved->getInvoiceNumber().value(), QString("INV-001"));
    
    // 3. Manual update with conflict (conflict date specified, changing amount)
    // Note: To trigger conflict without publish, we modify the existing draft? No, draft is updated in place.
    // To trigger conflict, we usually need "Published" status OR different taxes if logic dictates.
    // recordShipmentFromSource Logic:
    // If Draft: Update in place.
    // If Published: check diff.
    
    // So update in place first (Draft -> Draft)
    auto actRes2 = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(200.0, 40.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment2({*actRes2.value});
    manager.recordShipmentFromSource(orderId, &source, &shipment2, QDate());
    
    // Check retrieval (should still work on the same ID)
    retrieved = manager.getInvoicingInfo(shipment.getId()); // ID is same "act1" (from getEventId/ActivityId combination usually? Shipment ID comes from activity)
    QVERIFY(retrieved);
    QCOMPARE(retrieved->getInvoiceNumber().value(), QString("INV-001"));
    
    // 4. Publish
    QDate publishDate(2023, 2, 1);
    manager.publish(publishDate);
    
    // Check retrieval after publish
    retrieved = manager.getInvoicingInfo(shipment.getId());
    QVERIFY(retrieved);
    QCOMPARE(retrieved->getInvoiceNumber().value(), QString("INV-001"));
    
    // 5. Update without conflict (e.g. date change, content same/close)
    // Actually, if content differs, it updates the Published revision in place IF no financial impact (taxes same).
    // Let's change time only.
    auto actRes3 = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(12, 0)), "EUR", "FR", "DE", "DE",
         Amount(200.0, 40.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment3({*actRes3.value});
    manager.recordShipmentFromSource(orderId, &source, &shipment3, QDate());
    
    // Check retrieval
    retrieved = manager.getInvoicingInfo(shipment.getId());
    QVERIFY(retrieved);
    QCOMPARE(retrieved->getInvoiceNumber().value(), QString("INV-001"));
    
    // 6. Update with conflict -> Reversal + New Version
    // Change Amount
    auto actRes4 = Activity::create("evt1", "act1", "", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
         Amount(300.0, 60.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
    Shipment shipment4({*actRes4.value});
    manager.recordShipmentFromSource(orderId, &source, &shipment4, QDate(2023, 3, 1));
    
    // Now we have multiple shipments. Use getShipmentAndRefunds to find them.
    auto results = manager.getShipmentAndRefunds(QDate(2023,1,1), QDate(2023,12,31), nullptr);
    QCOMPARE(results.size(), 3); // Original, Reversal, New
    
    /*
      The IDs will be:
      1. Original (act1)
      2. Reversal (act1-rev-TIMESTAMP)
      3. New (act1-v-TIMESTAMP)
    */
    
    for (auto it = results.begin(); it != results.end(); ++it) {
        QString id = it.value()->getId();
        // Check invoking info for EACH
        auto info = manager.getInvoicingInfo(id);
        QVERIFY2(info, qPrintable("Invoicing info missing for " + id));
        QCOMPARE(info->getInvoiceNumber().value(), QString("INV-001"));
    }
}

QTEST_MAIN(TestOrderManager)
#include "tst_testordermanager.moc"
