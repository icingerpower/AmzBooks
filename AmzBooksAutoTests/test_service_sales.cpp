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
#include "books/CompanyAddressTable.h"
#include "books/InvoiceGenerator.h"
#include "books/VatNumbersTable.h"
#include "books/BooksAccountsSalesTable.h"
#include "books/BookAccountPurchaseTable.h"
#include "books/JournalTable.h"
#include "books/BookSaverFull.h"
#include "CurrencyRateManager.h"
#include "orders/OrderManager.h"
#include "orders/Address.h"
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
    void test_noInvoices_and_recordInfo();
    void test_persistence_all_columns();
    void test_deleteWithInvoice();
    void test_vatOnPayment_true();
    void test_vatOnPayment_false();
    void test_createSale_paymentTerm();
    void test_serviceSalesTable_extraColumns();
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

        auto entries = syncWait(factory.createEntryGrouped(&source, shipments, nullptr));
        QVERIFY(!entries.isEmpty());

        // Service sales belong to the "VTSERVICE" journal
        const QString journalId = journalTable.getJournalServiceSale().code;
        for (const auto &entry : std::as_const(entries)) {
            journal_date_entries[journalId].insert(entry->getDate(), entry);
        }
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

// ===========================================================================
// test_noInvoices_and_recordInfo
// Verifies the full workflow:
//   1. Create a service sale → appears in BOTH getShipmentAndRefundsNoInvoices
//      AND get_channel_site_ShipmentAndRefundsNoInvoices because no invoice number is set yet.
//   2. Retrieve the InvoicingInfo, add an invoice number, record it again.
//   3. Line item details (title, quantity, payment date) are still present.
//   4. Entry no longer appears in either NoInvoices query.
// ===========================================================================
void TestServiceSales::test_noInvoices_and_recordInfo()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    // FR client — 30-day payment terms
    clientManager.addClient("Dupont SAS", "IT Consulting", "FR", "FR98765", "EUR",
                            PaymentType::AfterXDays, 30);

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable serviceTable(nullptr, &orderManager, tempDir.path());

    const QDate saleDate(2025, 5, 10);
    const QString orderId  = "SVC-NOINV-001";
    const QString title    = "IT Consulting";
    const int     qty      = 2;
    const double  totalTTC = 600.0;   // 2 × 300 TTC

    serviceTable.createSale(&clientManager, 0, saleDate, totalTTC, "EUR",
                            orderId, title, qty, "", vatResolver, taxResolver,
                            PaymentType::AfterXDays, 30);

    QCOMPARE(serviceTable.rowCount(), 1);
    QVERIFY(orderManager.containsOrder(orderId));

    // -----------------------------------------------------------------------
    // Step 1: Sale without invoice number → must appear in BOTH NoInvoices queries
    // -----------------------------------------------------------------------
    const QDate from(2025, 1, 1);
    const QDate to(2025, 12, 31);

    // --- Flat list variant ---
    auto noInvList = orderManager.getShipmentAndRefundsNoInvoices(from, to);
    QVERIFY(!noInvList.isNull());
    QCOMPARE(noInvList->size(), 1);

    // Verify address was recorded by createSale
    QVERIFY(!noInvList->first().addressTo.isNull());
    QCOMPARE(noInvList->first().addressTo->getFullName(), QString("Dupont SAS"));
    QCOMPARE(noInvList->first().addressTo->getCountryCode(), QString("FR"));
    QCOMPARE(noInvList->first().addressTo->getTaxId(), QString("FR98765"));

    // --- Channel/site map variant ---
    auto noInvMap = orderManager.get_channel_site_ShipmentAndRefundsNoInvoices(from, to);
    QVERIFY(!noInvMap.isNull());
    // The service sale uses channel = ServiceSalesBooksTable::CHANNEL_SALE, subchannel = "Unknown"
    const QString expectedChannel = QString(ServiceSalesBooksTable::CHANNEL_SALE);
    QVERIFY(noInvMap->contains(expectedChannel));
    // At least one channel entry present
    int totalShipmentsInMap = 0;
    for (auto &subMap : *noInvMap) {
        for (auto &ctxMap : subMap) {
            for (auto &entry : ctxMap) {
                totalShipmentsInMap += entry.shipmentsRefundsSameActivity.size();
            }
        }
    }
    QCOMPARE(totalShipmentsInMap, 1);

    // -----------------------------------------------------------------------
    // Step 2: Retrieve InvoicingInfo, add invoice number, record again
    // -----------------------------------------------------------------------
    auto info = orderManager.getInvoicingInfo(orderId);
    QVERIFY(!info.isNull());

    // Sanity: no invoice number yet
    QVERIFY(!info->getInvoiceNumber().has_value());

    // Add the invoice number
    info->setInvoiceNumber("INV-2025-042");
    orderManager.recordInvoicingInfo(orderId, info.get());

    // -----------------------------------------------------------------------
    // Step 3: Retrieve again and verify line item details are preserved
    // -----------------------------------------------------------------------
    auto updatedInfo = orderManager.getInvoicingInfo(orderId);
    QVERIFY(!updatedInfo.isNull());

    // Invoice number must be present
    QVERIFY(updatedInfo->getInvoiceNumber().has_value());
    QCOMPARE(updatedInfo->getInvoiceNumber().value(), QString("INV-2025-042"));

    // Line items must still be there
    QCOMPARE(updatedInfo->getItems().size(), 1);
    const LineItem &item = updatedInfo->getItems().first();

    // Service title preserved
    QCOMPARE(item.getName(), title);

    // Quantity preserved
    QCOMPARE(item.getQuantity(), qty);

    // Total gross preserved
    QCOMPARE(item.getTotalTaxed(), totalTTC);

    // Payment date (30 days after sale date) preserved
    const QDate expectedPayment = saleDate.addDays(30); // 2025-06-09
    QCOMPARE(updatedInfo->getPaymentDate(saleDate), expectedPayment);

    // -----------------------------------------------------------------------
    // Step 4: BOTH NoInvoices queries must now be empty
    // -----------------------------------------------------------------------

    // --- Flat list variant ---
    auto noInvListAfter = orderManager.getShipmentAndRefundsNoInvoices(from, to);
    QVERIFY(!noInvListAfter.isNull());
    QCOMPARE(noInvListAfter->size(), 0);

    // --- Channel/site map variant ---
    auto noInvMapAfter = orderManager.get_channel_site_ShipmentAndRefundsNoInvoices(from, to);
    QVERIFY(!noInvMapAfter.isNull());
    int totalAfter = 0;
    for (auto &subMap : *noInvMapAfter) {
        for (auto &ctxMap : subMap) {
            for (auto &entry : ctxMap) {
                totalAfter += entry.shipmentsRefundsSameActivity.size();
            }
        }
    }
    QCOMPARE(totalAfter, 0);
}

// ===========================================================================
// test_persistence_all_columns
// Verifies that ALL ServiceSalesBooksTable column values survive a
// close/re-open cycle (i.e. createSale followed by load() in a new instance).
// Known regression: Account 1 (bookkeeping account code) was not restored
// by load() — it was hardcoded to "".
// Columns checked: Date, Amount (TTC), Currency, Label, Account1,
//                  VAT amount, VAT Country.
// ===========================================================================
void TestServiceSales::test_persistence_all_columns()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QDate    saleDate   = QDate(2025, 4, 20);
    const double   totalTTC   = 1200.0;
    const QString  currency   = "EUR";
    const QString  orderId    = "SVC-PERSIST-001";
    const QString  title      = "Software Development";
    const int      qty        = 4;
    const QString  account1   = "706000"; // bookkeeping account code

    // -----------------------------------------------------------------------
    // Phase 1: create the sale
    // -----------------------------------------------------------------------
    {
        OrderManager orderManager(tempDir.path());
        orderManager.deleteDatabase();

        ServiceClientManager clientManager(tempDir.path());
        // FR client — instant payment (default)
        clientManager.addClient("ClientX", "Dev", "FR", "FR99999", currency);

        VatResolver vatResolver(tempDir.path());
        TaxResolver taxResolver(tempDir.path());
        vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

        table.createSale(&clientManager, 0, saleDate, totalTTC, currency,
                         orderId, title, qty, account1, vatResolver, taxResolver);

        // Sanity: sale is in table right after creation
        QCOMPARE(table.rowCount(), 1);

        // Account1 is correct immediately after createSale
        QCOMPARE(table.getAccount1(0), account1);
    }

    // -----------------------------------------------------------------------
    // Phase 2: reload — simulates reopening the application
    // -----------------------------------------------------------------------
    {
        OrderManager orderManager(tempDir.path()); // loads from same DB

        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
        // Table starts empty until load() is called (matches existing test_persistence)
        QCOMPARE(table.rowCount(), 0);

        table.load(saleDate.year());

        QCOMPARE(table.rowCount(), 1);

        // --- Date ---
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_DATE)).toDate(), saleDate);

        // --- Amount (TTC gross) ---
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), totalTTC);

        // --- Currency ---
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_CURRENCY)).toString(), currency);

        // --- Label ---
        // The service LABEL comes from clientManager->getServiceLabel(), not the title parameter.
        // In createSale the subActivityId stores clientManager->getServiceLabel(clientRow) = "Dev"
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_LABEL)).toString(), QString("Dev"));

        // --- Account 1 (the bookkeeping account code) ---
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_ACCOUNT1)).toString(), account1);

        // --- VAT amount: 20% of net = totalTTC * 0.20 / 1.20 ---
        const double expectedVat = totalTTC * 0.20 / 1.20;
        QCOMPARE(table.data(table.index(0, 6)).toDouble(), expectedVat); // col 6 = Original VAT

        // --- VAT Country: the client's country ---
        QCOMPARE(table.data(table.index(0, 7)).toString(), QString("FR")); // col 7 = VAT Country

        // --- Row ID must match orderId ---
        QCOMPARE(table.getRowId(table.index(0, 0)), orderId);
    }
}

// ===========================================================================
// test_deleteWithInvoice
// Full lifecycle: create → generate invoice → delete → recreate → generate
// again and verify the second invoice number is identical to the first.
//
//   1. Create a sale with ServiceSalesBooksTable (generator linked via
//      setInvoiceGenerator so that remove() cleans the CSV registry too).
//   2. Generate the invoice (getBaseInvoiceNumber + generateInvoice).
//   3. Delete the sale — ServiceSalesBooksTable::remove() calls
//      m_invoiceGenerator->removeInvoiceByNumber() before removeOrder().
//   4. Confirm order + InvoicingInfo are gone from OrderManager.
//   5. Recreate the sale with the same orderId.
//   6. Generate the invoice again — must produce the SAME invoice number.
// ===========================================================================
void TestServiceSales::test_deleteWithInvoice()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("Acme Corp", "Software Dev", "FR", "FR12345", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    // Set up InvoiceGenerator and link it to the table
    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    VatNumbersTable vatNumbers(tempDir.path());
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress, &currencyRates, &vatNumbers);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
    table.setInvoiceGenerator(&generator);

    const QDate   date    = QDate(2025, 7, 10);
    const QString orderId = "SVC-DEL-001";

    // -------------------------------------------------------------------
    // Step 1: Create the sale
    // -------------------------------------------------------------------
    table.createSale(&clientManager, 0, date, 600.0, "EUR",
                     orderId, "Software Dev", 1, "706000", vatResolver, taxResolver);

    // VERIFY 1: sale is present in the table
    QCOMPARE(table.rowCount(), 1);

    // VERIFY 2: order is present in OrderManager
    QVERIFY(orderManager.containsOrder(orderId));

    // -------------------------------------------------------------------
    // Step 2: Generate the invoice number and produce the PDF
    // -------------------------------------------------------------------
    TaxResolver::TaxContext taxCtx;
    taxCtx.taxScheme           = TaxScheme::DomesticVat;
    taxCtx.taxDeclaringCountryCode = "FR";
    taxCtx.taxJurisdictionLevel    = TaxJurisdictionLevel::Country;
    taxCtx.countryCodeVatPaidTo    = "FR";

    QString inv1 = generator.getBaseInvoiceNumber(
        date, taxCtx, QString(ServiceSalesBooksTable::CHANNEL_SALE), "", orderId);
    QVERIFY(!inv1.isEmpty());

    // Build a minimal InvoicingInfo with the generated number
    auto lineItemRes = LineItem::create("SVC", "Software Dev", 600.0, 0.20, 1);
    QVERIFY(lineItemRes.ok());
    QList<LineItem> items = {*lineItemRes.value};
    auto resInfo = InvoicingInfo::create(nullptr, items, inv1);
    QVERIFY(resInfo.ok());
    InvoicingInfo invInfo = *resInfo.value;
    Address addr("Acme Corp", "1 Rue de la Paix", "", "", "Paris", "75000", "FR", "", "", "", "", "");

    const QString pdfPath1 = tempDir.filePath("inv1.pdf");
    generator.generateInvoice(inv1, "", pdfPath1, addr, invInfo, orderId, orderManager);

    // VERIFY 3: PDF was created → CSV and OrderManager were updated
    QVERIFY(QFile::exists(pdfPath1));

    // VERIFY 4: invoice number is stored in OrderManager
    {
        auto storedInfo = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!storedInfo.isNull());
        QVERIFY(storedInfo->getInvoiceNumber().has_value());
        QCOMPARE(storedInfo->getInvoiceNumber().value(), inv1);
    }

    // VERIFY 5: generator holds one record
    QCOMPARE(generator.rowCount(), 1);

    // -------------------------------------------------------------------
    // Step 3: Delete the sale
    //   ServiceSalesBooksTable::remove() will:
    //     a) call generator.removeInvoiceByNumber(inv1)  (CSV registry)
    //     b) call orderManager.removeOrder(orderId)        (DB)
    //     c) call AbstractBooksTable::remove()             (table row)
    // -------------------------------------------------------------------
    bool removed = table.remove(orderId);

    // VERIFY 6: remove() reported success
    QVERIFY(removed);

    // VERIFY 7: sale is gone from the table
    QCOMPARE(table.rowCount(), 0);

    // VERIFY 8: order is gone from OrderManager
    QVERIFY(!orderManager.containsOrder(orderId));

    // VERIFY 9: InvoicingInfo (including invoice number) deleted from DB
    {
        auto infoAfterDelete = orderManager.getInvoicingInfo(orderId);
        QVERIFY(infoAfterDelete.isNull());
    }

    // VERIFY 10: invoice record removed from the generator's CSV registry
    QCOMPARE(generator.rowCount(), 0);

    // -------------------------------------------------------------------
    // Step 4: Recreate the sale with the same orderId
    // -------------------------------------------------------------------
    table.createSale(&clientManager, 0, date, 600.0, "EUR",
                     orderId, "Software Dev", 1, "706000", vatResolver, taxResolver);

    // VERIFY 11: sale is back in the table
    QCOMPARE(table.rowCount(), 1);
    QVERIFY(orderManager.containsOrder(orderId));

    // -------------------------------------------------------------------
    // Step 5: Generate the invoice again
    // -------------------------------------------------------------------
    QString inv2 = generator.getBaseInvoiceNumber(
        date, taxCtx, QString(ServiceSalesBooksTable::CHANNEL_SALE), "", orderId);

    // VERIFY 12: the regenerated invoice number is IDENTICAL to the original
    QCOMPARE(inv2, inv1);

    // Generate the second PDF to complete the round-trip
    auto resInfo2 = InvoicingInfo::create(nullptr, items, inv2);
    QVERIFY(resInfo2.ok());
    InvoicingInfo invInfo2 = *resInfo2.value;
    const QString pdfPath2 = tempDir.filePath("inv2.pdf");
    generator.generateInvoice(inv2, "", pdfPath2, addr, invInfo2, orderId, orderManager);

    // VERIFY 13: second PDF was also created
    QVERIFY(QFile::exists(pdfPath2));
}

// ===========================================================================
// test_vatOnPayment_true
// When createSale is called with vatOnPayment = true, that flag must be
// propagated to the stored InvoicingInfo.
// ===========================================================================
void TestServiceSales::test_vatOnPayment_true()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("VopClient", "Audit", "FR", "FR11111", "EUR",
                            PaymentType::Instant, 0,
                            QString(), QString(), QString(), QString(),
                            QString(), QString(), QString(),
                            /*vatOnPayment=*/false); // client default is false; param must override

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate date(2025, 9, 1);
    const QString orderId = "SVC-VOP-001";
    table.createSale(&clientManager, 0, date, 600.0, "EUR",
                     orderId, "Audit", 1, "706000", vatResolver, taxResolver,
                     /*paymentType=*/PaymentType::EndOfNextMonth, /*paymentDays=*/0,
                     /*vatOnPayment=*/true);

    QCOMPARE(table.rowCount(), 1);

    // InvoicingInfo retrieved from DB must have vatOnPayment = true
    auto info = orderManager.getInvoicingInfo(orderId);
    QVERIFY(!info.isNull());
    QVERIFY(info->getVatOnPayment());
}

// ===========================================================================
// test_vatOnPayment_false
// When createSale is called with vatOnPayment = false, that flag must be
// propagated to the stored InvoicingInfo.
// ===========================================================================
void TestServiceSales::test_vatOnPayment_false()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("VopClient", "Audit", "FR", "FR11111", "EUR",
                            PaymentType::Instant, 0,
                            QString(), QString(), QString(), QString(),
                            QString(), QString(), QString(),
                            /*vatOnPayment=*/true); // client default is true; param must override

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate date(2025, 9, 1);
    const QString orderId = "SVC-VOP-002";
    table.createSale(&clientManager, 0, date, 600.0, "EUR",
                     orderId, "Audit", 1, "706000", vatResolver, taxResolver,
                     /*paymentType=*/PaymentType::EndOfNextMonth, /*paymentDays=*/0,
                     /*vatOnPayment=*/false);

    QCOMPARE(table.rowCount(), 1);

    // InvoicingInfo retrieved from DB must have vatOnPayment = false
    auto info = orderManager.getInvoicingInfo(orderId);
    QVERIFY(!info.isNull());
    QVERIFY(!info->getVatOnPayment());
}

// ===========================================================================
// test_createSale_paymentTerm
// Verifies that the explicit paymentType/paymentDays parameters control the
// payment date stored in InvoicingInfo, independently of the client's own
// payment type setting.
//   - Instant       → payment date == order date (no stored date)
//   - AfterXDays    → payment date == order date + N days
//   - EndOfNextMonth→ payment date == end of next calendar month
// ===========================================================================
void TestServiceSales::test_createSale_paymentTerm()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    // Client itself has Instant payment — the explicit param must override it
    clientManager.addClient("TermClient", "Dev", "FR", "FR99999", "EUR",
                            PaymentType::Instant, 0,
                            QString(), QString(), QString(), QString(),
                            QString(), QString(), QString(),
                            /*vatOnPayment=*/false);

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    const QDate date(2025, 3, 15);

    // --- 1. Instant: payment date equals the order date → no separate date stored ---
    {
        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
        const QString orderId = "TERM-INSTANT-001";
        table.createSale(&clientManager, 0, date, 600.0, "EUR",
                         orderId, "Dev", 1, "706000", vatResolver, taxResolver,
                         /*paymentType=*/PaymentType::Instant,
                         /*paymentDays=*/0);

        QCOMPARE(table.rowCount(), 1);
        auto info = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!info.isNull());
        // Instant → no explicit payment date stored; getPaymentDate falls back to order date
        QCOMPARE(info->getPaymentDate(date), date);
    }

    // --- 2. AfterXDays (30): payment date == order date + 30 days ---
    {
        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
        const QString orderId = "TERM-30DAYS-001";
        table.createSale(&clientManager, 0, date, 600.0, "EUR",
                         orderId, "Dev", 1, "706000", vatResolver, taxResolver,
                         /*paymentType=*/PaymentType::AfterXDays,
                         /*paymentDays=*/30);

        QCOMPARE(table.rowCount(), 1);
        auto info = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!info.isNull());
        const QDate expected = date.addDays(30); // 2025-04-14
        QCOMPARE(info->getPaymentDate(date), expected);
    }

    // --- 3. EndOfNextMonth: payment date == last day of April 2025 ---
    {
        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
        const QString orderId = "TERM-EOM-001";
        table.createSale(&clientManager, 0, date, 600.0, "EUR",
                         orderId, "Dev", 1, "706000", vatResolver, taxResolver,
                         /*paymentType=*/PaymentType::EndOfNextMonth,
                         /*paymentDays=*/0);

        QCOMPARE(table.rowCount(), 1);
        auto info = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!info.isNull());
        const QDate expected(2025, 4, 30); // end of April
        QCOMPARE(info->getPaymentDate(date), expected);
    }
}

// ===========================================================================
// test_serviceSalesTable_extraColumns
// Verifies that ServiceSalesBooksTable exposes Title, VAT on Payment and
// Payment Term as readable/editable extra columns (indices 9, 10, 11).
// ===========================================================================
void TestServiceSales::test_serviceSalesTable_extraColumns()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("ColClient", "Consulting", "FR", "FR12345", "EUR",
                            PaymentType::Instant, 0,
                            QString(), QString(), QString(), QString(),
                            QString(), QString(), QString(),
                            /*vatOnPayment=*/false);

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate date(2025, 6, 10);
    const QString orderId = "COL-TEST-001";
    table.createSale(&clientManager, 0, date, 1200.0, "EUR",
                     orderId, "My Service Title", 1, "706000",
                     vatResolver, taxResolver,
                     /*paymentType=*/PaymentType::EndOfNextMonth,
                     /*paymentDays=*/0,
                     /*vatOnPayment=*/true);

    QCOMPARE(table.rowCount(), 1);

    // VERIFY 1: column count includes the 3 extra columns (9 base + 3)
    QCOMPARE(table.columnCount(), 12);

    // VERIFY 2: header for Title column
    QCOMPARE(table.headerData(ServiceSalesBooksTable::IND_TITLE,
                              Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Title"));

    // VERIFY 3: header for VAT on Payment column
    QCOMPARE(table.headerData(ServiceSalesBooksTable::IND_VAT_ON_PAYMENT,
                              Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("VAT on Payment"));

    // VERIFY 4: header for Payment Term column
    QCOMPARE(table.headerData(ServiceSalesBooksTable::IND_PAYMENT_TERM,
                              Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Payment Term"));

    const QModelIndex idxTitle   = table.index(0, ServiceSalesBooksTable::IND_TITLE);
    const QModelIndex idxVop     = table.index(0, ServiceSalesBooksTable::IND_VAT_ON_PAYMENT);
    const QModelIndex idxTerm    = table.index(0, ServiceSalesBooksTable::IND_PAYMENT_TERM);

    // VERIFY 5: Title column shows the service title passed to createSale
    QCOMPARE(table.data(idxTitle, Qt::DisplayRole).toString(),
             QString("My Service Title"));

    // VERIFY 6: VAT on Payment column EditRole returns the bool true
    QCOMPARE(table.data(idxVop, Qt::EditRole).toBool(), true);

    // VERIFY 7: VAT on Payment DisplayRole shows "Yes"
    QCOMPARE(table.data(idxVop, Qt::DisplayRole).toString(), QString("Yes"));

    // VERIFY 8: Payment Term shows the canonical "End of Next Month" label
    QCOMPARE(table.data(idxTerm, Qt::DisplayRole).toString(),
             ServiceClientManager::paymentTypeLabel(PaymentType::EndOfNextMonth));

    // VERIFY 9: Title is editable (flag check)
    QVERIFY(table.flags(idxTitle) & Qt::ItemIsEditable);

    // VERIFY 10: setData for Title updates both in-memory value and InvoicingInfo
    QVERIFY(table.setData(idxTitle, QVariant("Updated Title"), Qt::EditRole));
    QCOMPARE(table.data(idxTitle, Qt::DisplayRole).toString(),
             QString("Updated Title"));

    // VERIFY 11: Persisted InvoicingInfo reflects the new title
    {
        auto info = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!info.isNull());
        QVERIFY(!info->getItems().isEmpty());
        QCOMPARE(info->getItems().first().getName(), QString("Updated Title"));
    }

    // VERIFY 12: setData for VAT on Payment toggles the value to false
    QVERIFY(table.setData(idxVop, QVariant(false), Qt::EditRole));
    QCOMPARE(table.data(idxVop, Qt::EditRole).toBool(), false);
    QCOMPARE(table.data(idxVop, Qt::DisplayRole).toString(), QString("No"));

    // VERIFY 13: Persisted InvoicingInfo reflects vatOnPayment = false
    {
        auto info = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!info.isNull());
        QVERIFY(!info->getVatOnPayment());
    }

    // VERIFY 14: setData for Payment Term changes to the canonical "Instant" label
    QVERIFY(table.setData(idxTerm,
                          QVariant(ServiceClientManager::paymentTypeLabel(PaymentType::Instant)),
                          Qt::EditRole));
    QCOMPARE(table.data(idxTerm, Qt::DisplayRole).toString(),
             ServiceClientManager::paymentTypeLabel(PaymentType::Instant));

    // VERIFY 15: Persisted InvoicingInfo now has no deferred payment date
    {
        auto info = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!info.isNull());
        // Instant → getPaymentDate returns the order date itself
        QCOMPARE(info->getPaymentDate(date), date);
    }

    // VERIFY 16: setData for Payment Term to "After 30 days"
    QVERIFY(table.setData(idxTerm, QVariant("After 30 days"), Qt::EditRole));
    QCOMPARE(table.data(idxTerm, Qt::DisplayRole).toString(),
             QString("After 30 days"));

    // VERIFY 17: Persisted payment date is order date + 30 days
    {
        auto info = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!info.isNull());
        QCOMPARE(info->getPaymentDate(date), date.addDays(30));
    }

    // VERIFY 18: After reload, extra columns are restored from persisted InvoicingInfo
    {
        ServiceSalesBooksTable table2(nullptr, &orderManager, tempDir.path());
        table2.load(2025);
        QCOMPARE(table2.rowCount(), 1);
        const QModelIndex t2Title = table2.index(0, ServiceSalesBooksTable::IND_TITLE);
        const QModelIndex t2Term  = table2.index(0, ServiceSalesBooksTable::IND_PAYMENT_TERM);
        const QModelIndex t2Vop   = table2.index(0, ServiceSalesBooksTable::IND_VAT_ON_PAYMENT);
        QCOMPARE(table2.data(t2Title,  Qt::DisplayRole).toString(), QString("Updated Title"));
        QCOMPARE(table2.data(t2Term,   Qt::DisplayRole).toString(), QString("After 30 days"));
        QCOMPARE(table2.data(t2Vop,    Qt::EditRole).toBool(), false);
    }
}

QTEST_MAIN(TestServiceSales)
#include "test_service_sales.moc"
