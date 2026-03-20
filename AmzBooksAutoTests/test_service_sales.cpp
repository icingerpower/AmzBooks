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
#include "books/BookAccountSelfVatTable.h"
#include "books/AmzPaymentSettings.h"
#include "books/BookAccountAmzBalanceTable.h"
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

         using Item = ServiceSalesBooksTable::SaleLineItemInput;

         // 1. Create Sale
         table.createSale(&clientManager, 0, date, "EUR", expectedOrderId, "",
                          {Item{"Service A", 600.0, 1.0}}, vatResolver, taxResolver);

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
             table.createSale(&clientManager, 0, date, "EUR", expectedOrderId, "",
                              {Item{"Service A", 600.0, 1.0}}, vatResolver, taxResolver);
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
    void test_load_dec31_included();

    // replacePublishedSale tests
    void test_replacePublishedSale_leadsToTwoNewEntries();
    void test_replacePublishedSale_throwsIfNotPublished();
    void test_replacePublishedSale_throwsIfCreditAlreadyExists();
    void test_replacePublishedSale_persistsAllEntries();

    // replaceSale tests
    void test_replaceSale_basicReplace();
    void test_replaceSale_sameOrderId();
    void test_replaceSale_throwsWhenPublished();
    void test_replaceSale_withInvoiceCleanup();
    void test_replaceSale_multipleLineItemsReplaced();
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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    table.createSale(&clientManager, 0, date, "EUR", expectedOrderId, "",
                     {Item{"Consulting", 500.0, 1.0}}, vatResolver, taxResolver);

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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    table.createSale(&clientManager, 0, date, "EUR", expectedOrderId, "",
                     {Item{"Development", 1000.0, 1.0}}, vatResolver, taxResolver);

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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    table.createSale(&clientManager, 0, date, "EUR", expectedOrderId, "",
                     {Item{"Support", 750.0, 1.0}}, vatResolver, taxResolver);

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

         using Item = ServiceSalesBooksTable::SaleLineItemInput;

         // Add Service Sale
         table.createSale(&clientManager, 0, QDate(2023, 5, 20), "EUR", "Service-20230520-ClientB", "",
                          {Item{"Service B", 1190.0, 1.0}}, vatResolver, taxResolver);

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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    const QDate saleDate(2025, 3, 15);
    serviceTable.createSale(
        &clientManager, 0,
        saleDate, "EUR",
        "INV-SERVICE-001", "7060",
        {Item{"Consulting", 1000.0, 1.0}},
        vatResolver, taxResolver
    );

    QCOMPARE(serviceTable.rowCount(), 1);
    QVERIFY(orderManager.containsOrder("INV-SERVICE-001"));

    // --- 3. Replicate generateBookKeepingAsync (sales portion) ---
    CompanyInfosTable companyInfos(workingDir);
    BooksAccountsSalesTable salesAccountTable(workingDir);
    BookAccountPurchaseTable purchaseAccountTable(workingDir, companyInfos.getCompanyCountryCode());
    JournalTable journalTable(workingDir);
    BookAccountSelfVatTable selfVatAccountTable(workingDir, companyInfos.getCompanyCountryCode());
    AmzPaymentSettings amzPaymentSettings(workingDir);
    BookAccountAmzBalanceTable amzBalanceTable(workingDir);
    CurrencyRateManager currencyRateManager(workingDir, "");

    JournalEntryFactory factory(
        &currencyRateManager, &companyInfos,
        &salesAccountTable, &purchaseAccountTable,
        &journalTable, &selfVatAccountTable, &amzPaymentSettings, &amzBalanceTable
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
    const double  qty      = 3.0;
    const double  unitTTC  = 120.0;          // per unit gross
    const double  totalTTC = unitTTC * qty;  // 360.0

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    table.createSale(&clientManager, 0, date, "EUR",
                     orderId, "",
                     {Item{title, unitTTC, qty}}, vatResolver, taxResolver);

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
    const double  qty      = 2.0;
    const double  totalTTC = 600.0;   // 2 × 300 TTC

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    serviceTable.createSale(&clientManager, 0, saleDate, "EUR",
                            orderId, "",
                            {Item{title, totalTTC / qty, qty}}, vatResolver, taxResolver,
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
    const double   qty        = 4.0;
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

        using Item = ServiceSalesBooksTable::SaleLineItemInput;
        table.createSale(&clientManager, 0, saleDate, currency,
                         orderId, account1,
                         {Item{title, totalTTC / qty, qty}}, vatResolver, taxResolver);

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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    // -------------------------------------------------------------------
    // Step 1: Create the sale
    // -------------------------------------------------------------------
    table.createSale(&clientManager, 0, date, "EUR",
                     orderId, "706000",
                     {Item{"Software Dev", 600.0, 1.0}}, vatResolver, taxResolver);

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
    generator.generateInvoice(inv1, "", pdfPath1, addr, invInfo, orderId, orderManager, date);

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
    table.createSale(&clientManager, 0, date, "EUR",
                     orderId, "706000",
                     {Item{"Software Dev", 600.0, 1.0}}, vatResolver, taxResolver);

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
    generator.generateInvoice(inv2, "", pdfPath2, addr, invInfo2, orderId, orderManager, date);

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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    table.createSale(&clientManager, 0, date, "EUR",
                     orderId, "706000",
                     {Item{"Audit", 600.0, 1.0}}, vatResolver, taxResolver,
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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    table.createSale(&clientManager, 0, date, "EUR",
                     orderId, "706000",
                     {Item{"Audit", 600.0, 1.0}}, vatResolver, taxResolver,
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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    // --- 1. Instant: payment date equals the order date → no separate date stored ---
    {
        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
        const QString orderId = "TERM-INSTANT-001";
        table.createSale(&clientManager, 0, date, "EUR",
                         orderId, "706000",
                         {Item{"Dev", 600.0, 1.0}}, vatResolver, taxResolver,
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
        table.createSale(&clientManager, 0, date, "EUR",
                         orderId, "706000",
                         {Item{"Dev", 600.0, 1.0}}, vatResolver, taxResolver,
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
        table.createSale(&clientManager, 0, date, "EUR",
                         orderId, "706000",
                         {Item{"Dev", 600.0, 1.0}}, vatResolver, taxResolver,
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

    using Item = ServiceSalesBooksTable::SaleLineItemInput;
    table.createSale(&clientManager, 0, date, "EUR",
                     orderId, "706000",
                     {Item{"My Service Title", 1200.0, 1.0}},
                     vatResolver, taxResolver,
                     /*paymentType=*/PaymentType::EndOfNextMonth,
                     /*paymentDays=*/0,
                     /*vatOnPayment=*/true);

    QCOMPARE(table.rowCount(), 1);

    // VERIFY 1: column count includes the 4 extra columns (9 base + 4)
    QCOMPARE(table.columnCount(), 13);

    // VERIFY 1b: header and data for Reference column
    QCOMPARE(table.headerData(ServiceSalesBooksTable::IND_REFERENCE,
                              Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Reference"));
    {
        const QModelIndex idxRef = table.index(0, ServiceSalesBooksTable::IND_REFERENCE);
        QCOMPARE(table.data(idxRef, Qt::DisplayRole).toString(), orderId);
        QVERIFY(table.flags(idxRef) & Qt::ItemIsEditable);

        // Edit reference
        QVERIFY(table.setData(idxRef, QVariant("NEW-REF"), Qt::EditRole));
        QCOMPARE(table.data(idxRef, Qt::DisplayRole).toString(), QString("NEW-REF"));
        auto infoRef = orderManager.getInvoicingInfo(orderId);
        QVERIFY(!infoRef.isNull());
        QCOMPARE(infoRef->getReference(), QString("NEW-REF"));
    }

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
        const QModelIndex t2Ref   = table2.index(0, ServiceSalesBooksTable::IND_REFERENCE);
        const QModelIndex t2Title = table2.index(0, ServiceSalesBooksTable::IND_TITLE);
        const QModelIndex t2Term  = table2.index(0, ServiceSalesBooksTable::IND_PAYMENT_TERM);
        const QModelIndex t2Vop   = table2.index(0, ServiceSalesBooksTable::IND_VAT_ON_PAYMENT);
        QCOMPARE(table2.data(t2Ref,    Qt::DisplayRole).toString(), QString("NEW-REF"));
        QCOMPARE(table2.data(t2Title,  Qt::DisplayRole).toString(), QString("Updated Title"));
        QCOMPARE(table2.data(t2Term,   Qt::DisplayRole).toString(), QString("After 30 days"));
        QCOMPARE(table2.data(t2Vop,    Qt::EditRole).toBool(), false);
    }
}

void TestServiceSales::test_load_dec31_included()
{
    // Regression test: a sale on Dec 31 must appear when loading that year.
    // The SQL filter "event_date <= '2025-12-31'" fails for stored ISO datetimes
    // like "2025-12-31T00:00:00" because 'T' > end-of-string lexicographically.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("TestClient", "Service X", "FR", "FR123", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());

    const QDate dec31(2025, 12, 31);
    const QString orderId = "Service-20251231-TestClient";

    {
        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
        using Item = ServiceSalesBooksTable::SaleLineItemInput;
        table.createSale(&clientManager, 0, dec31, "EUR", orderId, "",
                         {Item{"Service X", 100.0, 1.0}}, vatResolver, taxResolver);
    }

    // Reload and load year 2025 — Dec 31 entry must be included
    ServiceSalesBooksTable table2(nullptr, &orderManager, tempDir.path());
    table2.load(2025);
    QCOMPARE(table2.rowCount(), 1);
    QCOMPARE(table2.data(table2.index(0, 0)).toDate(), dec31);
}

// ===========================================================================
// test_replacePublishedSale_leadsToTwoNewEntries
// replacePublishedSale on a published sale must produce exactly 2 new rows:
// a credit note (same data, negated amounts) and a new corrected sale.
// The original published entry is preserved, so the table grows by 2.
// ===========================================================================
void TestServiceSales::test_replacePublishedSale_leadsToTwoNewEntries()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("PubClient", "Consulting", "FR", "FR1001", "EUR",
                            PaymentType::Instant, 0,
                            QString(), QString(), QString(), QString(),
                            QString(), QString(), "706000", /*vatOnPayment=*/true);

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   origDate = QDate(2025, 2, 10);
    const QString origId   = "PUB-ORIG-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    // Create and publish the original sale
    table.createSale(&clientManager, 0, origDate, "EUR", origId, "706000",
                     {Item{"Consulting", 600.0, 1.0}},
                     vatResolver, taxResolver,
                     PaymentType::Instant, 0, /*vatOnPayment=*/true);

    QCOMPARE(table.rowCount(), 1);
    QVERIFY(!orderManager.isOrderPublished(origId));

    QDate publishDate(2025, 12, 31);
    orderManager.publish(publishDate);
    QVERIFY(orderManager.isOrderPublished(origId));

    // Re-invoice: corrected date, new orderId, different amount
    const QDate   newDate  = QDate(2025, 3, 1);
    const QString newId    = "PUB-NEW-001";

    table.replacePublishedSale(origId, &clientManager, 0, newDate, "EUR", newId, "706000",
                               {Item{"Consulting v2", 800.0, 1.0}},
                               vatResolver, taxResolver,
                               PaymentType::EndOfNextMonth, 0, /*vatOnPayment=*/true);

    // VERIFY 1: exactly 2 new rows added (original stays + credit note + new sale)
    QCOMPARE(table.rowCount(), 3);

    // VERIFY 2: original orderId still in OrderManager (published, cannot be removed)
    QVERIFY(orderManager.containsOrder(origId));

    // VERIFY 3: credit note orderId registered
    const QString refundId = origId + "-CREDIT";
    QVERIFY(orderManager.containsOrder(refundId));

    // VERIFY 4: new sale orderId registered
    QVERIFY(orderManager.containsOrder(newId));

    // VERIFY 5: credit note has negative amount (find its row by scanning)
    double refundAmt = 0.0;
    double newAmt    = 0.0;
    for (int r = 0; r < table.rowCount(); ++r) {
        const QString id = table.getRowId(table.index(r, 0));
        if (id == refundId) {
            refundAmt = table.data(table.index(r, AbstractBooksTable::IND_AMOUNT)).toDouble();
        } else if (id == newId) {
            newAmt = table.data(table.index(r, AbstractBooksTable::IND_AMOUNT)).toDouble();
        }
    }
    QVERIFY(refundAmt < 0.0);
    QCOMPARE(refundAmt, -600.0);

    // VERIFY 6: new sale has the updated amount
    QCOMPARE(newAmt, 800.0);

    // VERIFY 7: credit note InvoicingInfo has negated line items
    {
        const auto info = orderManager.getInvoicingInfo(refundId);
        QVERIFY(!info.isNull());
        QCOMPARE(info->getItems().size(), 1);
        QCOMPARE(info->getItems().first().getName(), QString("Consulting"));
        QVERIFY(info->getItems().first().getAmountTaxed() < 0.0);
    }

    // VERIFY 8: new sale InvoicingInfo has updated line items
    {
        const auto info = orderManager.getInvoicingInfo(newId);
        QVERIFY(!info.isNull());
        QCOMPARE(info->getItems().size(), 1);
        QCOMPARE(info->getItems().first().getName(), QString("Consulting v2"));
        QCOMPARE(info->getItems().first().getAmountTaxed(), 800.0);
    }

    // VERIFY 9: credit note extra columns carry the original title
    for (int r = 0; r < table.rowCount(); ++r) {
        if (table.getRowId(table.index(r, 0)) == refundId) {
            QCOMPARE(table.data(table.index(r, ServiceSalesBooksTable::IND_TITLE),
                                Qt::DisplayRole).toString(),
                     QString("Consulting"));
            break;
        }
    }

    // VERIFY 10: new sale extra columns reflect the new title
    for (int r = 0; r < table.rowCount(); ++r) {
        if (table.getRowId(table.index(r, 0)) == newId) {
            QCOMPARE(table.data(table.index(r, ServiceSalesBooksTable::IND_TITLE),
                                Qt::DisplayRole).toString(),
                     QString("Consulting v2"));
            break;
        }
    }
}

// ===========================================================================
// test_replacePublishedSale_throwsIfNotPublished
// replacePublishedSale must throw ExceptionWithTitleText when the target sale
// is still a draft, leaving the table with only the original row.
// ===========================================================================
void TestServiceSales::test_replacePublishedSale_throwsIfNotPublished()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("DraftClient", "Service", "FR", "FR0001", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   date    = QDate(2025, 5, 1);
    const QString orderId = "DRAFT-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    table.createSale(&clientManager, 0, date, "EUR", orderId, "",
                     {Item{"Draft Service", 300.0, 1.0}},
                     vatResolver, taxResolver);

    QCOMPARE(table.rowCount(), 1);
    QVERIFY(!orderManager.isOrderPublished(orderId));

    // VERIFY 1: replacePublishedSale throws when sale is unpublished
    bool threw = false;
    try {
        table.replacePublishedSale(orderId, &clientManager, 0, date, "EUR", "NEW-DRAFT-001", "",
                                   {Item{"Should Not Appear", 500.0, 1.0}},
                                   vatResolver, taxResolver);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY(threw);

    // VERIFY 2: table still has only the original row
    QCOMPARE(table.rowCount(), 1);

    // VERIFY 3: no credit note was created
    QVERIFY(!orderManager.containsOrder(orderId + "-CREDIT"));

    // VERIFY 4: the replacement orderId was never created
    QVERIFY(!orderManager.containsOrder("NEW-DRAFT-001"));
}

// ===========================================================================
// test_replacePublishedSale_throwsIfCreditAlreadyExists
// Calling replacePublishedSale a second time on the same published sale must
// throw, because the credit note orderId already exists.
// ===========================================================================
void TestServiceSales::test_replacePublishedSale_throwsIfCreditAlreadyExists()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("CreditClient", "Service", "FR", "FR0002", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   date    = QDate(2025, 4, 1);
    const QString origId  = "CREDIT-ORIG-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    table.createSale(&clientManager, 0, date, "EUR", origId, "",
                     {Item{"Original Service", 500.0, 1.0}},
                     vatResolver, taxResolver);

    QDate publishDate(2025, 12, 31);
    orderManager.publish(publishDate);
    QVERIFY(orderManager.isOrderPublished(origId));

    // First re-invoice succeeds → table grows to 3
    table.replacePublishedSale(origId, &clientManager, 0, date, "EUR", "CREDIT-NEW-001", "",
                               {Item{"Corrected Service", 550.0, 1.0}},
                               vatResolver, taxResolver);
    QCOMPARE(table.rowCount(), 3);

    // VERIFY 1: second call throws (credit note orderId already exists)
    bool threw = false;
    try {
        table.replacePublishedSale(origId, &clientManager, 0, date, "EUR", "CREDIT-NEW-002", "",
                                   {Item{"Should Not Appear", 600.0, 1.0}},
                                   vatResolver, taxResolver);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY(threw);

    // VERIFY 2: table is unchanged (still 3 rows, not 5)
    QCOMPARE(table.rowCount(), 3);

    // VERIFY 3: the second replacement orderId was never created
    QVERIFY(!orderManager.containsOrder("CREDIT-NEW-002"));
}

// ===========================================================================
// test_replacePublishedSale_persistsAllEntries
// After reload, ServiceSalesBooksTable::load() must return all three entries:
// the original published sale, its credit note, and the new corrected sale.
// ===========================================================================
void TestServiceSales::test_replacePublishedSale_persistsAllEntries()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("PersistClient", "Dev", "FR", "FR0003", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    const QDate   origDate = QDate(2025, 6, 15);
    const QString origId   = "PERSIST-ORIG-001";
    const QString newId    = "PERSIST-NEW-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    {
        ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());
        table.createSale(&clientManager, 0, origDate, "EUR", origId, "",
                         {Item{"Dev Work", 400.0, 1.0}},
                         vatResolver, taxResolver);

        QDate publishDate(2025, 12, 31);
        orderManager.publish(publishDate);

        table.replacePublishedSale(origId, &clientManager, 0, origDate, "EUR", newId, "",
                                   {Item{"Dev Work v2", 450.0, 1.0}},
                                   vatResolver, taxResolver);
    }

    // Reload and verify all three entries survive a load()
    ServiceSalesBooksTable table2(nullptr, &orderManager, tempDir.path());
    table2.load(2025);

    // VERIFY 1: all 3 entries are loaded
    QCOMPARE(table2.rowCount(), 3);

    // VERIFY 2: identify each entry and check amounts
    double origAmt   = 0.0;
    double refundAmt = 0.0;
    double newAmt    = 0.0;
    const QString refundId = origId + "-CREDIT";

    for (int r = 0; r < table2.rowCount(); ++r) {
        const QString id = table2.getRowId(table2.index(r, 0));
        const double  a  = table2.data(table2.index(r, AbstractBooksTable::IND_AMOUNT)).toDouble();
        if (id == origId)   origAmt   = a;
        if (id == refundId) refundAmt = a;
        if (id == newId)    newAmt    = a;
    }

    QCOMPARE(origAmt,   400.0);
    QCOMPARE(refundAmt, -400.0);
    QCOMPARE(newAmt,    450.0);

    // VERIFY 3: credit note InvoicingInfo persisted correctly
    {
        const auto info = orderManager.getInvoicingInfo(refundId);
        QVERIFY(!info.isNull());
        QVERIFY(!info->getItems().isEmpty());
        QVERIFY(info->getItems().first().getAmountTaxed() < 0.0);
        QCOMPARE(info->getItems().first().getName(), QString("Dev Work"));
    }

    // VERIFY 4: new sale InvoicingInfo persisted correctly
    {
        const auto info = orderManager.getInvoicingInfo(newId);
        QVERIFY(!info.isNull());
        QVERIFY(!info->getItems().isEmpty());
        QCOMPARE(info->getItems().first().getAmountTaxed(), 450.0);
        QCOMPARE(info->getItems().first().getName(), QString("Dev Work v2"));
    }
}

// ===========================================================================
// test_replaceSale_basicReplace
// Replacing an unpublished sale must remove the original entry and create a
// new one reflecting all changed fields (date, amount, items, payment term,
// vatOnPayment), while leaving exactly one row in the table.
// ===========================================================================
void TestServiceSales::test_replaceSale_basicReplace()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("ReplClient", "Consulting", "FR", "FR9001", "EUR",
                            PaymentType::Instant, 0,
                            QString(), QString(), QString(), QString(),
                            QString(), QString(), "706000", false);

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   origDate = QDate(2025, 3, 10);
    const QString origId   = "REPL-ORIG-001";
    const QDate   newDate  = QDate(2025, 4, 20);
    const QString newId    = "REPL-NEW-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    // Create original sale: 600 EUR, instant payment, vatOnPayment=false
    table.createSale(&clientManager, 0, origDate, "EUR", origId, "706000",
                     {Item{"Old Service", 600.0, 1.0}},
                     vatResolver, taxResolver,
                     PaymentType::Instant, 0, /*vatOnPayment=*/false);

    QCOMPARE(table.rowCount(), 1);
    QVERIFY(orderManager.containsOrder(origId));

    // VERIFY: isOrderPublished is false before any publish call
    QVERIFY(!orderManager.isOrderPublished(origId));

    // Replace: new orderId, new date, 800×2=1600 EUR, end-of-next-month, vatOnPayment=true
    table.replaceSale(origId, &clientManager, 0, newDate, "EUR", newId, "706000",
                      {Item{"New Service", 800.0, 2.0}},
                      vatResolver, taxResolver,
                      PaymentType::EndOfNextMonth, 0, /*vatOnPayment=*/true);

    // VERIFY 1: still exactly one row
    QCOMPARE(table.rowCount(), 1);

    // VERIFY 2: old orderId gone from OrderManager
    QVERIFY(!orderManager.containsOrder(origId));

    // VERIFY 3: new orderId present in OrderManager
    QVERIFY(orderManager.containsOrder(newId));

    // VERIFY 4: table shows updated date
    QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_DATE)).toDate(), newDate);

    // VERIFY 5: table shows updated amount (800 * 2 = 1600 TTC)
    QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), 1600.0);

    // VERIFY 6: getLineItems returns the new item with correct fields
    {
        const auto items = table.getLineItems(newId);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().title, QString("New Service"));
        QCOMPARE(items.first().unitPriceTaxed, 800.0);
        QCOMPARE(items.first().quantity, 2.0);
    }

    // VERIFY 7: InvoicingInfo vatOnPayment is now true
    {
        const auto info = orderManager.getInvoicingInfo(newId);
        QVERIFY(!info.isNull());
        QVERIFY(info->getVatOnPayment());
    }

    // VERIFY 8: payment date is end of May 2025 (end of month after April)
    {
        const auto info = orderManager.getInvoicingInfo(newId);
        QVERIFY(!info.isNull());
        const QDate expectedPayment(2025, 5, 31);
        QCOMPARE(info->getPaymentDate(newDate), expectedPayment);
    }

    // VERIFY 9: extra columns updated
    QCOMPARE(table.data(table.index(0, ServiceSalesBooksTable::IND_REFERENCE),
                        Qt::DisplayRole).toString(), newId);
    QCOMPARE(table.data(table.index(0, ServiceSalesBooksTable::IND_TITLE),
                        Qt::DisplayRole).toString(), QString("New Service"));
    QCOMPARE(table.data(table.index(0, ServiceSalesBooksTable::IND_VAT_ON_PAYMENT),
                        Qt::EditRole).toBool(), true);
    QCOMPARE(table.data(table.index(0, ServiceSalesBooksTable::IND_PAYMENT_TERM),
                        Qt::DisplayRole).toString(),
             ServiceClientManager::paymentTypeLabel(PaymentType::EndOfNextMonth));
}

// ===========================================================================
// test_replaceSale_sameOrderId
// When the new orderId equals the old rowId the sale must still be replaced
// correctly: remove + recreate under the same ID.
// ===========================================================================
void TestServiceSales::test_replaceSale_sameOrderId()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("SameClient", "Service", "FR", "FR0042", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   date    = QDate(2025, 5, 1);
    const QString orderId = "SAME-ID-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    table.createSale(&clientManager, 0, date, "EUR", orderId, "",
                     {Item{"Initial Title", 300.0, 1.0}},
                     vatResolver, taxResolver);

    QCOMPARE(table.rowCount(), 1);

    // Replace keeping the same orderId — remove + recreate with new data
    table.replaceSale(orderId, &clientManager, 0, date, "EUR", orderId, "",
                      {Item{"Updated Title", 500.0, 1.0}},
                      vatResolver, taxResolver);

    // VERIFY 1: still one row
    QCOMPARE(table.rowCount(), 1);

    // VERIFY 2: orderId still registered
    QVERIFY(orderManager.containsOrder(orderId));

    // VERIFY 3: amount reflects the updated item
    QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), 500.0);

    // VERIFY 4: line items carry the new title
    {
        const auto items = table.getLineItems(orderId);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().title, QString("Updated Title"));
        QCOMPARE(items.first().unitPriceTaxed, 500.0);
    }
}

// ===========================================================================
// test_replaceSale_throwsWhenPublished
// replaceSale must throw ExceptionWithTitleText when the target sale has been
// published, leaving the original entry completely intact.
// ===========================================================================
void TestServiceSales::test_replaceSale_throwsWhenPublished()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("PubClient", "Service", "FR", "FR0099", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   date    = QDate(2025, 1, 15);
    const QString orderId = "PUB-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    table.createSale(&clientManager, 0, date, "EUR", orderId, "",
                     {Item{"Published Service", 400.0, 1.0}},
                     vatResolver, taxResolver);

    QVERIFY(orderManager.containsOrder(orderId));

    // VERIFY 1: not published yet
    QVERIFY(!orderManager.isOrderPublished(orderId));

    // Publish all drafts up to end of year
    QDate publishDate(2025, 12, 31);
    orderManager.publish(publishDate);

    // VERIFY 2: now reported as published
    QVERIFY(orderManager.isOrderPublished(orderId));

    // VERIFY 3: replaceSale throws ExceptionWithTitleText
    bool threw = false;
    try {
        table.replaceSale(orderId, &clientManager, 0, date, "EUR", "NEW-PUB-001", "",
                          {Item{"Should Not Appear", 500.0, 1.0}},
                          vatResolver, taxResolver);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY(threw);

    // VERIFY 4: original orderId still in OrderManager (nothing was removed)
    QVERIFY(orderManager.containsOrder(orderId));

    // VERIFY 5: the "replacement" orderId was never created
    QVERIFY(!orderManager.containsOrder("NEW-PUB-001"));

    // VERIFY 6: table in-memory model still has one row
    QCOMPARE(table.rowCount(), 1);

    // VERIFY 7: table still shows the original amount (row is untouched)
    QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), 400.0);
}

// ===========================================================================
// test_replaceSale_withInvoiceCleanup
// When an invoice number has been generated for the sale being replaced,
// replaceSale must remove it from the InvoiceGenerator CSV registry.
// ===========================================================================
void TestServiceSales::test_replaceSale_withInvoiceCleanup()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("InvClient", "Dev", "FR", "FR12345", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    CompanyInfosTable companyInfos(tempDir.path());
    CompanyAddressTable companyAddress(tempDir.path());
    companyAddress.insertRows(0, 1);
    CurrencyRateManager currencyRates(tempDir.path(), "");
    VatNumbersTable vatNumbers(tempDir.path());
    InvoiceGenerator generator(tempDir.path(), &companyInfos, &companyAddress,
                                &currencyRates, &vatNumbers);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   date   = QDate(2025, 6, 5);
    const QString origId = "INV-ORIG-001";
    const QString newId  = "INV-NEW-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    // Create the original sale
    table.createSale(&clientManager, 0, date, "EUR", origId, "706000",
                     {Item{"Dev Work", 600.0, 1.0}},
                     vatResolver, taxResolver);

    // Generate an invoice for it and store the number in OrderManager
    TaxResolver::TaxContext taxCtx;
    taxCtx.taxScheme               = TaxScheme::DomesticVat;
    taxCtx.taxDeclaringCountryCode = "FR";
    taxCtx.taxJurisdictionLevel    = TaxJurisdictionLevel::Country;
    taxCtx.countryCodeVatPaidTo    = "FR";

    const QString invNumber = generator.getBaseInvoiceNumber(
        date, taxCtx, QString(ServiceSalesBooksTable::CHANNEL_SALE), "", origId);
    QVERIFY(!invNumber.isEmpty());

    auto lineItemRes = LineItem::create("DEV", "Dev Work", 600.0, 0.20, 1.0);
    QVERIFY(lineItemRes.ok());
    auto resInfo = InvoicingInfo::create(nullptr, {*lineItemRes.value}, invNumber);
    QVERIFY(resInfo.ok());
    Address addr("InvClient", "", "", "", "", "", "FR", "", "", "", "", "");
    const QString pdfPath = tempDir.filePath("inv.pdf");
    generator.generateInvoice(invNumber, "", pdfPath, addr, *resInfo.value,
                               origId, orderManager, date);

    QVERIFY(QFile::exists(pdfPath));
    QCOMPARE(generator.rowCount(), 1);

    // VERIFY 1: invoice number is stored for the original sale
    {
        const auto info = orderManager.getInvoicingInfo(origId);
        QVERIFY(!info.isNull());
        QVERIFY(info->getInvoiceNumber().has_value());
        QCOMPARE(info->getInvoiceNumber().value(), invNumber);
    }

    // Replace with the generator set — must clean up the invoice registry
    table.setInvoiceGenerator(&generator);
    table.replaceSale(origId, &clientManager, 0, date, "EUR", newId, "706000",
                      {Item{"Updated Dev", 900.0, 1.0}},
                      vatResolver, taxResolver);
    table.setInvoiceGenerator(nullptr);

    // VERIFY 2: invoice registry is now empty (entry was removed)
    QCOMPARE(generator.rowCount(), 0);

    // VERIFY 3: original sale is gone
    QVERIFY(!orderManager.containsOrder(origId));

    // VERIFY 4: new sale is present
    QVERIFY(orderManager.containsOrder(newId));

    // VERIFY 5: new sale's InvoicingInfo has no invoice number yet
    {
        const auto info = orderManager.getInvoicingInfo(newId);
        QVERIFY(!info.isNull());
        QVERIFY(!info->getInvoiceNumber().has_value());
    }

    // VERIFY 6: table shows the new amount
    QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), 900.0);
}

// ===========================================================================
// test_replaceSale_multipleLineItemsReplaced
// A sale with multiple line items can be replaced with a different set of
// items; getLineItems must return only the new items after the replace.
// ===========================================================================
void TestServiceSales::test_replaceSale_multipleLineItemsReplaced()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrderManager orderManager(tempDir.path());
    orderManager.deleteDatabase();

    ServiceClientManager clientManager(tempDir.path());
    clientManager.addClient("MultiClient", "Mixed", "FR", "FR7777", "EUR");

    VatResolver vatResolver(tempDir.path());
    TaxResolver taxResolver(tempDir.path());
    vatResolver.addRate(QDate(2020, 1, 1), "FR", SaleType::Service, 0.20);

    ServiceSalesBooksTable table(nullptr, &orderManager, tempDir.path());

    const QDate   date    = QDate(2025, 7, 1);
    const QString origId  = "MULTI-ORIG-001";
    const QString newId   = "MULTI-NEW-001";

    using Item = ServiceSalesBooksTable::SaleLineItemInput;

    // Create with two items: 200×1 + 300×2 = 800 TTC
    table.createSale(&clientManager, 0, date, "EUR", origId, "",
                     {Item{"Part A", 200.0, 1.0}, Item{"Part B", 300.0, 2.0}},
                     vatResolver, taxResolver);

    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), 800.0);

    {
        const auto items = table.getLineItems(origId);
        QCOMPARE(items.size(), 2);
    }

    // Replace with a single consolidated item: 950×1 = 950 TTC
    table.replaceSale(origId, &clientManager, 0, date, "EUR", newId, "",
                      {Item{"Combined Service", 950.0, 1.0}},
                      vatResolver, taxResolver);

    // VERIFY 1: still one row
    QCOMPARE(table.rowCount(), 1);

    // VERIFY 2: amount reflects single new item
    QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), 950.0);

    // VERIFY 3: getLineItems returns only the one new item (old items fully replaced)
    {
        const auto items = table.getLineItems(newId);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().title, QString("Combined Service"));
        QCOMPARE(items.first().unitPriceTaxed, 950.0);
        QCOMPARE(items.first().quantity, 1.0);
    }

    // VERIFY 4: getLineItems for the old orderId returns nothing (cleaned up)
    QVERIFY(table.getLineItems(origId).isEmpty());
}

QTEST_MAIN(TestServiceSales)
#include "test_service_sales.moc"
