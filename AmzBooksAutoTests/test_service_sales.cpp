#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "books/ServiceClientManager.h"
#include "books/ServiceSalesBooksTable.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/ExceptionParamValue.h"

class TestServiceSales : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Setup temporary directory structure
    }

    void test_ServiceClientManager()
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) QFAIL("Could not create temp dir");

        ServiceClientManager manager(tempDir.path());
        QCOMPARE(manager.rowCount(), 0);

        // Add Client
        manager.addClient("Acme Corp", "Consulting", "US", "12345", "USD", 1000.0);
        QCOMPARE(manager.rowCount(), 1);
        QCOMPARE(manager.getClientName(0), "Acme Corp");
        QCOMPARE(manager.getServiceLabel(0), "Consulting");
        QCOMPARE(manager.getCountry(0), "US");
        QCOMPARE(manager.getVatNumber(0), "12345");
        QCOMPARE(manager.getCurrency(0), "USD");
        QCOMPARE(manager.getDefaultAmount(0), 1000.0);

        // Persistence
        ServiceClientManager manager2(tempDir.path());
        QCOMPARE(manager2.rowCount(), 1);
        QCOMPARE(manager2.getClientName(0), "Acme Corp");

        // Remove
        manager.removeClient(0);
        QCOMPARE(manager.rowCount(), 0);
        
        ServiceClientManager manager3(tempDir.path());
        QCOMPARE(manager3.rowCount(), 0);
    }

    void test_ServiceSalesBooksTable()
    {
         QTemporaryDir tempDir;
         if (!tempDir.isValid()) QFAIL("Could not create temp dir");
         
         OrderManager orderManager(tempDir.path());
         orderManager.deleteDatabase(); // Ensure clean
         
         ServiceClientManager clientManager(tempDir.path());
         clientManager.addClient("ClientA", "Service A", "FR", "FR123", "EUR", 500.0);
         
         ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
         
         QDate date(2023, 1, 15);
         QString expectedOrderId = "Service-20230115-ClientA";
         
         // 1. Create Sale
         table.createSale(&clientManager, 0, date, 500.0, "EUR", "INV-123");
         
         QCOMPARE(table.rowCount(), 1);
         QCOMPARE(orderManager.containsOrder(expectedOrderId), true);
         
         // Verify Data in Table
         // Columns: Date, Amount, Currency
         QCOMPARE(table.data(table.index(0, 0)).toDate(), date);
         QCOMPARE(table.data(table.index(0, 1)).toDouble(), 500.0);
         QCOMPARE(table.data(table.index(0, 2)).toString(), "EUR");
         QCOMPARE(table.data(table.index(0, 3)).toString(), "Service A");
         
         // 2. Duplicate Check
         bool exceptionCaught = false;
         try {
             table.createSale(&clientManager, 0, date, 500.0, "EUR", "INV-124");
         } catch (const ExceptionParamValue &e) {
             exceptionCaught = true;
         }
         QVERIFY(exceptionCaught);
         
         // 3. Remove
         bool removed = table.remove(expectedOrderId);
         QVERIFY(removed);
         QCOMPARE(table.rowCount(), 0);
         QCOMPARE(orderManager.containsOrder(expectedOrderId), false);
    }
    
    void test_persistence();
};

void TestServiceSales::test_persistence()
{
     QTemporaryDir tempDir;
     if (!tempDir.isValid()) QFAIL("Could not create temp dir");
     
     // 1. Setup Data
     {
         OrderManager orderManager(tempDir.path());
         orderManager.deleteDatabase();
         
         ServiceClientManager clientManager(tempDir.path());
         clientManager.addClient("ClientB", "Service B", "DE", "DE123", "EUR", 1000.0);
         
         ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
         
         // Add Service Sale
         // invoiceId="INV-999"
         table.createSale(&clientManager, 0, QDate(2023, 5, 20), 1000.0, "EUR", "INV-999");
         
         // Add Random Order (Amazon)
         ActivitySource sourceAmazon(ActivitySourceType::Report, "Amazon", "Report1");
         Activity::create("AmazonOrder1", "Act1", "", QDateTime(QDate(2023, 5, 21), QTime(0,0)), "EUR", "FR", "DE", "DE", 
                          Amount(50.0, 0.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
         
         auto actRes = Activity::create("AmazonOrder1", "Act1", "", QDateTime(QDate(2023, 5, 21), QTime(0,0)), "EUR", "FR", "DE", "DE", 
                          Amount(50.0, 0.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
         
         if (actRes.value) {
             QList<Activity> acts;
             acts.append(*actRes.value);
             Shipment shipment(acts);
             orderManager.recordShipmentFromSource("AmazonOrder1", &sourceAmazon, &shipment, QDate(2023, 5, 21));
         }
     }
     
     // 2. Reload and Verify
     {
         OrderManager orderManager(tempDir.path()); // Should load from DB
         ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
         
         QCOMPARE(table.rowCount(), 0); // Initially empty until load() called
         
         table.load(2023);
         
         // Expecting ONLY the Service Sale (1 item)
         QCOMPARE(table.rowCount(), 1);
         
         QModelIndex idx = table.index(0, 0);
         // Verify Invoice ID (bookId column 1?) - checking RowID
         QCOMPARE(table.getRowId(idx), "Service-20230520-ClientB");
         
         // Check Label (Stored in subActivityId -> passed as Label to add())
         QCOMPARE(table.data(table.index(0, 3)).toString(), "Service B");
         
         // Check Amount
         QCOMPARE(table.data(table.index(0, 1)).toDouble(), 1000.0);
     }
}

QTEST_MAIN(TestServiceSales)
#include "test_service_sales.moc"
