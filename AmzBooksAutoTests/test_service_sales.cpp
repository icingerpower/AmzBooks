#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QDirIterator>
#include <QCoroTask>
#include <QCoroFuture>

#include "books/ServiceClientManager.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/TaxResolver.h"
#include "books/VatResolver.h"
#include "books/JournalEntryFactory.h"
#include "books/JournalEntry.h"
#include "books/CompanyInfosTable.h"
#include "books/BooksAccountsSalesTable.h"
#include "books/BookAccountPurchaseTable.h"
#include "books/JournalTable.h"
#include "books/BookSaverFull.h"
#include "CurrencyRateManager.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/ActivitySource.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "ExceptionWithTitleText.h"

// Helper to synchronously run a QCoro::Task in tests
template <typename T>
T syncWait(QCoro::Task<T> &&task) {
    return QCoro::waitFor<T>(std::move(task));
}

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
        manager.addClient("Acme Corp", "Consulting", "US", "12345", "USD");
        QCOMPARE(manager.rowCount(), 1);
        QCOMPARE(manager.getClientName(0), "Acme Corp");
        QCOMPARE(manager.getServiceLabel(0), "Consulting");
        QCOMPARE(manager.getCountry(0), "US");
        QCOMPARE(manager.getVatNumber(0), "12345");
        QCOMPARE(manager.getCurrency(0), "USD");

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
         clientManager.addClient("ClientA", "Service A", "FR", "FR123", "EUR");
         
         ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
         VatResolver vatResolver(tempDir.path());
         TaxResolver taxResolver(tempDir.path());

         QDate date(2023, 1, 15);
         QString expectedOrderId = "Service-20230115-ClientA";

         // 1. Create Sale
         table.createSale(&clientManager, 0, date, 600.0, "EUR", expectedOrderId, "Service A", 1, "", vatResolver, taxResolver);

         QCOMPARE(table.rowCount(), 1);
         QCOMPARE(orderManager.containsOrder(expectedOrderId), true);

         // Verify Data in Table
         // Columns: Date, Amount, Currency
         QCOMPARE(table.data(table.index(0, 0)).toDate(), date);
         QCOMPARE(table.data(table.index(0, 1)).toDouble(), 600.0); // grossAmount (TTC): 600
         QCOMPARE(table.data(table.index(0, 2)).toString(), "EUR");
         QCOMPARE(table.data(table.index(0, 3)).toString(), "Service A");

         // 2. Duplicate Check
         bool exceptionCaught = false;
         try {
             table.createSale(&clientManager, 0, date, 600.0, "EUR", expectedOrderId, "Service A", 1, "", vatResolver, taxResolver);
         } catch (const ExceptionWithTitleText &e) {
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
    
    // ========== NEW TESTS FOR PAYMENT DATE FEATURE ==========
    void test_InvoicingInfo_paymentDate();
    void test_ServiceClientManager_paymentTypes();
    void test_createSale_instantPayment();
    void test_createSale_afterXDays();
    void test_createSale_endOfNextMonth();
    void test_paymentDate_edgeCases();
    void test_createSale_withBookKeeping();
    void test_lineitem_and_quantity();
};

void TestServiceSales::test_InvoicingInfo_paymentDate()
{
    // VERIFY 1: InvoicingInfo without payment date returns order date
    {
        QDate orderDate(2025, 3, 15);
        auto res = InvoicingInfo::create(nullptr, {}, "INV-001", std::nullopt, std::nullopt);
        QVERIFY(res.ok());
        InvoicingInfo info = *res.value;
        QCOMPARE(info.getPaymentDate(orderDate), orderDate);
    }
    
    // VERIFY 2: InvoicingInfo with explicit payment date returns that date
    {
        QDate orderDate(2025, 3, 15);
        QDate paymentDate(2025, 4, 30);
        auto res = InvoicingInfo::create(nullptr, {}, "INV-002", std::nullopt, paymentDate);
        QVERIFY(res.ok());
        InvoicingInfo info = *res.value;
        QCOMPARE(info.getPaymentDate(orderDate), paymentDate);
    }
    
    // VERIFY 3: JSON round-trip without paymentDate
    {
        auto res = InvoicingInfo::create(nullptr, {}, "INV-003", "http://link.com", std::nullopt);
        QVERIFY(res.ok());
        InvoicingInfo original = *res.value;
        QJsonObject json = original.toJson();
        QVERIFY(!json.contains("paymentDate"));
        InvoicingInfo loaded = InvoicingInfo::fromJson(json);
        QDate orderDate(2025, 1, 1);
        QCOMPARE(loaded.getPaymentDate(orderDate), orderDate);
        QCOMPARE(loaded.getInvoiceNumber().value(), "INV-003");
    }
    
    // VERIFY 4: JSON round-trip with paymentDate
    {
        QDate paymentDate(2025, 5, 31);
        auto res = InvoicingInfo::create(nullptr, {}, "INV-004", std::nullopt, paymentDate);
        QVERIFY(res.ok());
        InvoicingInfo original = *res.value;
        QJsonObject json = original.toJson();
        QVERIFY(json.contains("paymentDate"));
        QCOMPARE(json["paymentDate"].toString(), "2025-05-31");
        InvoicingInfo loaded = InvoicingInfo::fromJson(json);
        QCOMPARE(loaded.getPaymentDate(QDate(2025, 1, 1)), paymentDate);
    }
}

void TestServiceSales::test_ServiceClientManager_paymentTypes()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) QFAIL("Could not create temp dir");

    ServiceClientManager manager(tempDir.path());
    
    // VERIFY 5: Add client with Instant payment (default)
    manager.addClient("Client1", "ServiceA", "FR", "FR111", "EUR");
    QCOMPARE(manager.getPaymentType(0), PaymentType::Instant);
    QCOMPARE(manager.getPaymentDays(0), 0);
    
    // VERIFY 6: Add client with AfterXDays (30 days)
    manager.addClient("Client2", "ServiceB", "DE", "DE222", "EUR",
                      PaymentType::AfterXDays, 30);
    QCOMPARE(manager.getPaymentType(1), PaymentType::AfterXDays);
    QCOMPARE(manager.getPaymentDays(1), 30);
    
    // VERIFY 7: Add client with EndOfNextMonth
    manager.addClient("Client3", "ServiceC", "US", "US333", "USD",
                      PaymentType::EndOfNextMonth, 0);
    QCOMPARE(manager.getPaymentType(2), PaymentType::EndOfNextMonth);
    
    // VERIFY 8: calculatePaymentDate for Instant returns order date
    {
        QDate orderDate(2025, 6, 15);
        QCOMPARE(manager.calculatePaymentDate(0, orderDate), orderDate);
    }
    
    // VERIFY 9: calculatePaymentDate for AfterXDays adds correct days
    {
        QDate orderDate(2025, 6, 15);
        QDate expected(2025, 7, 15); // + 30 days
        QCOMPARE(manager.calculatePaymentDate(1, orderDate), expected);
    }
    
    // VERIFY 10: calculatePaymentDate for EndOfNextMonth (mid-month order)
    {
        QDate orderDate(2025, 6, 15);
        QDate expected(2025, 7, 31); // End of July
        QCOMPARE(manager.calculatePaymentDate(2, orderDate), expected);
    }
    
    // VERIFY 11: calculatePaymentDate for EndOfNextMonth (end of month order)
    {
        QDate orderDate(2025, 1, 31);
        QDate expected(2025, 2, 28); // End of February (non-leap year 2025)
        QCOMPARE(manager.calculatePaymentDate(2, orderDate), expected);
    }
    
    // VERIFY 12: Persistence - reload and verify payment types
    {
        ServiceClientManager manager2(tempDir.path());
        QCOMPARE(manager2.rowCount(), 3);
        QCOMPARE(manager2.getPaymentType(0), PaymentType::Instant);
        QCOMPARE(manager2.getPaymentType(1), PaymentType::AfterXDays);
        QCOMPARE(manager2.getPaymentDays(1), 30);
        QCOMPARE(manager2.getPaymentType(2), PaymentType::EndOfNextMonth);
    }
}

void TestServiceSales::test_createSale_instantPayment()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) QFAIL("Could not create temp dir");
    
    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();
    
    ServiceClientManager clientManager(tempDir.path());
    // Client with Instant payment (default)
    clientManager.addClient("InstantClient", "Consulting", "FR", "FR001", "EUR");
    
    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());

    QDate date(2025, 3, 15);
    QString expectedOrderId = QString("Service-%1-InstantClient").arg(date.toString("yyyyMMdd"));

    table.createSale(&clientManager, 0, date, 500.0, "EUR", expectedOrderId, "Consulting", 1, "", vatResolver, taxResolver);
    
    // VERIFY 13: Sale created successfully
    QCOMPARE(table.rowCount(), 1);
    
    // VERIFY 14: Order is recorded in OrderManager
    QVERIFY(orderManager.containsOrder(expectedOrderId));
}

void TestServiceSales::test_createSale_afterXDays()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) QFAIL("Could not create temp dir");
    
    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();
    
    ServiceClientManager clientManager(tempDir.path());
    // Client with 30 days payment
    clientManager.addClient("Net30Client", "Development", "DE", "DE001", "EUR",
                            PaymentType::AfterXDays, 30);
    
    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());

    QDate date(2025, 3, 1);
    QString expectedOrderId = QString("Service-%1-Net30Client").arg(date.toString("yyyyMMdd"));

    table.createSale(&clientManager, 0, date, 1000.0, "EUR", expectedOrderId, "Development", 1, "", vatResolver, taxResolver);
    
    // VERIFY 15: Sale created successfully
    QCOMPARE(table.rowCount(), 1);
    
    // VERIFY 16: Order is registered correctly
    QVERIFY(orderManager.containsOrder(expectedOrderId));
    
    // VERIFY extra: Payment type is correctly set for client
    QCOMPARE(clientManager.getPaymentType(0), PaymentType::AfterXDays);
    QDate expectedPaymentDate = date.addDays(30); // March 31
    QCOMPARE(clientManager.calculatePaymentDate(0, date), expectedPaymentDate);
}

void TestServiceSales::test_createSale_endOfNextMonth()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) QFAIL("Could not create temp dir");
    
    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();
    
    ServiceClientManager clientManager(tempDir.path());
    // Client with EndOfNextMonth payment
    clientManager.addClient("EOMClient", "Support", "US", "US001", "EUR",
                            PaymentType::EndOfNextMonth, 0);
    
    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());

    QDate date(2025, 3, 15); // Mid March
    QString expectedOrderId = QString("Service-%1-EOMClient").arg(date.toString("yyyyMMdd"));

    table.createSale(&clientManager, 0, date, 750.0, "EUR", expectedOrderId, "Support", 1, "", vatResolver, taxResolver);
    
    // VERIFY 17: Sale created successfully
    QCOMPARE(table.rowCount(), 1);
    
    // VERIFY 18: Order is registered correctly
    QVERIFY(orderManager.containsOrder(expectedOrderId));
    
    // VERIFY extra: Payment type is correctly set and calculated
    QCOMPARE(clientManager.getPaymentType(0), PaymentType::EndOfNextMonth);
    QDate expectedPaymentDate(2025, 4, 30); // End of April
    QCOMPARE(clientManager.calculatePaymentDate(0, date), expectedPaymentDate);
}

void TestServiceSales::test_paymentDate_edgeCases()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) QFAIL("Could not create temp dir");
    
    ServiceClientManager manager(tempDir.path());
    
    // Client with AfterXDays = 0 (should be same as instant)
    manager.addClient("ZeroDays", "Service", "FR", "FR000", "EUR",
                      PaymentType::AfterXDays, 0);
    
    // VERIFY 19: AfterXDays with 0 days = instant (same date)
    {
        QDate orderDate(2025, 5, 10);
        QCOMPARE(manager.calculatePaymentDate(0, orderDate), orderDate);
    }
    
    // Client with EndOfNextMonth - order at end of December
    manager.addClient("DecClient", "Service", "FR", "FR001", "EUR",
                      PaymentType::EndOfNextMonth, 0);
    
    // VERIFY 20: EndOfNextMonth from December -> end of January next year
    {
        QDate orderDate(2025, 12, 31);
        QDate expected(2026, 1, 31); // End of January next year
        QCOMPARE(manager.calculatePaymentDate(1, orderDate), expected);
    }
}

void TestServiceSales::test_persistence()
{
     QTemporaryDir tempDir;
     if (!tempDir.isValid()) QFAIL("Could not create temp dir");
     
     // 1. Setup Data
     {
         OrderManager orderManager(tempDir.path());
         orderManager.deleteDatabase();
         
         ServiceClientManager clientManager(tempDir.path());
         clientManager.addClient("ClientB", "Service B", "DE", "DE123", "EUR");
         
         ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
         VatResolver vatResolver(tempDir.path());
         TaxResolver taxResolver(tempDir.path());

         // Add Service Sale
         table.createSale(&clientManager, 0, QDate(2023, 5, 20), 1190.0, "EUR", "Service-20230520-ClientB", "Service B", 1, "", vatResolver, taxResolver);
         
         // Add Random Order (Amazon)
         ActivitySource sourceAmazon(ActivitySourceType::Report, "Amazon", "Report1");
         
         auto actRes = Activity::create("AmazonOrder1", "Act1", "", QDateTime(QDate(2023, 5, 21), QTime(0,0)), QDateTime(QDate(2023, 5, 21), QTime(0,0)), "EUR", "FR", "DE", false, "DE",
                          Amount(50.0, 0.0), TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products);
         
         if (actRes.value) {
             QList<Activity> acts;
             acts.append(*actRes.value);
             Shipment shipment(acts, "", true);
             orderManager.recordShipmentFromSource("AmazonOrder1", &sourceAmazon, &shipment, QDate(2023, 5, 21), false);
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
         
         // Check Amount (grossAmount TTC: 1190)
         QCOMPARE(table.data(table.index(0, 1)).toDouble(), 1190.0);
     }
}

void TestServiceSales::test_createSale_withBookKeeping()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir workingDir(tempDir.path());

    // --- 1. Write minimal company info (FR company, EUR currency) ---
    {
        QFile file(workingDir.filePath("company.csv"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "Id;Parameter;Value\n"
            << "Currency;Currency;EUR\n"
            << "Country;Country Code;FR\n";
    }

    // --- 2. Create service client and sale ---
    OrderManager orderManager(workingDir);
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(workingDir);
    clientManager.addClient("BonClient", "Consulting", "FR", "FR12345", "EUR");

    TaxResolver taxResolver(workingDir);
    VatResolver vatResolver(workingDir);
    // VatResolver._fillIfEmpty only populates Products rates; add FR Service rate explicitly
    // so that createSale calculates non-zero VAT, giving the factory a matchable rate (20).
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable serviceTable(nullptr, &orderManager, workingDir);

    const QDate saleDate(2025, 3, 15);
    serviceTable.createSale(
        &clientManager, 0,
        saleDate, 1000.0, "EUR",
        "INV-SERVICE-001", "Consulting", 1, "7060",
        vatResolver, taxResolver
    );

    QCOMPARE(serviceTable.rowCount(), 1);
    QVERIFY(orderManager.containsOrder("INV-SERVICE-001"));

    // --- 3. Replicate generateBookKeepingAsync (sales portion) ---
    CompanyInfosTable companyInfos(workingDir);
    BooksAccountsSalesTable salesAccountTable(workingDir);
    BookAccountPurchaseTable purchaseAccountTable(workingDir, companyInfos.getCompanyCountryCode());
    JournalTable journalTable(workingDir);
    CurrencyRateManager currencyRateManager(workingDir, "");

    JournalEntryFactory factory(
        &currencyRateManager, &companyInfos,
        &salesAccountTable, &purchaseAccountTable,
        &journalTable
    );

    QHash<QString, QMultiMap<QDate, QSharedPointer<JournalEntry>>> journal_date_entries;

    const QDate from(2025, 1, 1);
    const QDate to(2025, 12, 31);

    auto acceptAll = [](const ActivitySource *, const Shipment *) { return true; };
    auto sourceMap = orderManager.getActivitySource_ShipmentAndRefunds(from, to, acceptAll);

    // Exactly one source group: our service sale
    QCOMPARE(sourceMap.size(), 1);

    for (auto it = sourceMap.begin(); it != sourceMap.end(); ++it) {
        ActivitySource source = it.key();
        const auto &shipments = it.value();

        auto entry = syncWait(factory.createEntryGrouped(&source, shipments, nullptr));
        QVERIFY(!entry.isNull());

        // Service sales belong to the "VTSERVICE" journal
        const QString journalId = journalTable.getJournalServiceSale().code;
        journal_date_entries[journalId].insert(entry->getDate(), entry);
    }

    QVERIFY(!journal_date_entries.isEmpty());

    // Verify the entry is balanced (debits == credits)
    for (const auto &dateMap : std::as_const(journal_date_entries)) {
        for (const auto &entry : dateMap) {
            QVERIFY(!entry.isNull());
            QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
        }
    }

    // --- 4. Save with BookSaverFull ---
    QTemporaryDir outTempDir;
    QVERIFY(outTempDir.isValid());
    QDir outDir(outTempDir.path());

    BookSaverFull saver;
    bool exceptionCaught = false;
    try {
        saver.save(journal_date_entries, outDir);
    } catch (const std::exception &e) {
        qWarning() << "BookSaverFull::save threw:" << e.what();
        exceptionCaught = true;
    }
    QVERIFY(!exceptionCaught);

    // --- 5. Verify output files ---
    // BookSaverFull writes to <year>/<month>/<journal>/<journal>_<year>_<month>.csv
    const QString journalCode = journalTable.getJournalServiceSale().code; // "VTSERVICE"
    const QString expectedJournalCsv = QString("2025/03/%1/%1_2025_03.csv").arg(journalCode);
    const QString expectedAllCsv = "2025/03/all/all_2025_03.csv";

    QVERIFY2(QFile::exists(outDir.filePath(expectedJournalCsv)),
             qPrintable(QString("Missing: %1").arg(outDir.filePath(expectedJournalCsv))));
    QVERIFY2(QFile::exists(outDir.filePath(expectedAllCsv)),
             qPrintable(QString("Missing: %1").arg(outDir.filePath(expectedAllCsv))));
}

void TestServiceSales::test_lineitem_and_quantity()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    // FR client with 20% VAT
    clientManager.addClient("TitleClient", "My Service", "FR", "FR999", "EUR");

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    const QDate date(2025, 6, 1);
    const QString orderId  = "SVC-001";
    const QString title    = "Premium Consulting";
    const int     qty      = 3;
    const double  unitTTC  = 120.0;          // per unit gross
    const double  totalTTC = unitTTC * qty;  // 360.0

    table.createSale(&clientManager, 0, date, totalTTC, "EUR",
                     orderId, title, qty, "", vatResolver, taxResolver);

    // VERIFY 1: sale was recorded in the table
    QCOMPARE(table.rowCount(), 1);

    // VERIFY 2: order is present in OrderManager
    QVERIFY(orderManager.containsOrder(orderId));

    // VERIFY 3: total gross amount stored correctly
    QCOMPARE(table.data(table.index(0, 1)).toDouble(), totalTTC);

    // VERIFY 4: currency stored correctly
    QCOMPARE(table.data(table.index(0, 2)).toString(), QString("EUR"));

    // VERIFY 5: service label (from client manager) stored in table
    QCOMPARE(table.data(table.index(0, 3)).toString(), QString("My Service"));

    // VERIFY 6: InvoicingInfo was created and is retrievable
    auto info = orderManager.getInvoicingInfo(orderId);
    QVERIFY(!info.isNull());

    // VERIFY 7: InvoicingInfo has exactly one line item
    QCOMPARE(info->getItems().size(), 1);

    const LineItem &item = info->getItems().first();

    // VERIFY 8: line item name matches serviceTitle
    QCOMPARE(item.getName(), title);

    // VERIFY 9: line item quantity matches
    QCOMPARE(item.getQuantity(), qty);

    // VERIFY 10: line item total gross matches totalTTC
    QCOMPARE(item.getTotalTaxed(), totalTTC);
}

QTEST_MAIN(TestServiceSales)
#include "test_service_sales.moc"
