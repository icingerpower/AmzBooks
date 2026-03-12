#include <QTest>
#include <QTemporaryDir>

#include "books/UngroupedOrderTable.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/ServiceClientManager.h"
#include "books/VatResolver.h"
#include "books/TaxResolver.h"
#include "books/Activity.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/ActivitySource.h"
#include "orders/ActivitySourceType.h"
#include "orders/Amount.h"
#include "orders/TaxSource.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"
#include "orders/SaleType.h"

class TestUngroupedOrderTable : public QObject
{
    Q_OBJECT

private:
    static QSharedPointer<Shipment> makeShipment(
            const QString &eventId,
            const QDate &date,
            double amountTaxed,
            const QString &currency,
            const QString &customerAccount,
            bool isGrouped)
    {
        QDateTime dt(date, QTime(0, 0));
        auto res = Activity::create(
            eventId, eventId, eventId,
            dt, dt,
            currency, "FR", "DE", false, "DE",
            Amount(amountTaxed, 0.0),
            TaxSource::MarketplaceProvided,
            "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country,
            SaleType::Products);
        if (!res.value) return {};
        QList<Activity> acts;
        acts.append(*res.value);
        return QSharedPointer<Shipment>::create(Shipment(acts, customerAccount, isGrouped));
    }

private slots:
    void test_loadsOnlyUngroupedNonServiceOrders()
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) QFAIL("Could not create temp dir");

        OrderManager orderManager(tempDir.path());
        orderManager.deleteDatabase();

        ActivitySource sourceAmazon{ActivitySourceType::Report, "Amazon", "EU", "report-2024"};

        // 1. Ungrouped order (should appear)
        auto shipUngrouped = makeShipment("Order-Ungrouped-1", QDate(2024, 3, 10), 50.0, "EUR", "acc1", false);
        QVERIFY(shipUngrouped);
        orderManager.recordShipmentFromSource("Order-Ungrouped-1", &sourceAmazon, shipUngrouped.data(), QDate(2024, 3, 10));
        orderManager.recordOrders({{"Order-Ungrouped-1", OrderManager::OrderInfo{QString(), false, "acc1"}}});

        // 2. Grouped order (should NOT appear)
        auto shipGrouped = makeShipment("Order-Grouped-1", QDate(2024, 3, 11), 120.0, "EUR", "acc1", true);
        QVERIFY(shipGrouped);
        orderManager.recordShipmentFromSource("Order-Grouped-1", &sourceAmazon, shipGrouped.data(), QDate(2024, 3, 11));
        orderManager.recordOrders({{"Order-Grouped-1", OrderManager::OrderInfo{QString(), true, "acc1"}}});

        // 3. Ungrouped service sale (should NOT appear)
        ServiceClientManager clientManager(tempDir.path());
        clientManager.addClient("ClientA", "Service A", "FR", "FR123", "EUR");
        ServiceSalesBooksTable serviceTable(nullptr, &orderManager, tempDir.path());
        VatResolver vatResolver(tempDir.path());
        TaxResolver taxResolver(tempDir.path());
        using Item = ServiceSalesBooksTable::SaleLineItemInput;
        serviceTable.createSale(&clientManager, 0, QDate(2024, 3, 12), "EUR",
                                "Service-20240312-ClientA", "",
                                {Item{"Service A", 600.0, 1.0}},
                                vatResolver, taxResolver);

        // Load UngroupedOrderTable
        UngroupedOrderTable table(nullptr, &orderManager, tempDir.path());
        table.load(2024);

        QCOMPARE(table.rowCount(), 1);
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_DATE)).toDate(), QDate(2024, 3, 10));
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_AMOUNT)).toDouble(), 50.0);
        QCOMPARE(table.data(table.index(0, AbstractBooksTable::IND_CURRENCY)).toString(), QString("EUR"));
    }

    void test_emptyWhenNoUngroupedOrders()
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) QFAIL("Could not create temp dir");

        OrderManager orderManager(tempDir.path());
        orderManager.deleteDatabase();

        ActivitySource sourceAmazon{ActivitySourceType::Report, "Amazon", "EU", "report-2024"};

        // Only grouped orders
        auto shipGrouped = makeShipment("Order-Grouped-2", QDate(2024, 5, 1), 200.0, "EUR", "acc2", true);
        QVERIFY(shipGrouped);
        orderManager.recordShipmentFromSource("Order-Grouped-2", &sourceAmazon, shipGrouped.data(), QDate(2024, 5, 1));
        orderManager.recordOrders({{"Order-Grouped-2", OrderManager::OrderInfo{QString(), true, "acc2"}}});

        UngroupedOrderTable table(nullptr, &orderManager, tempDir.path());
        table.load(2024);

        QCOMPARE(table.rowCount(), 0);
    }

    void test_doesNotLoadOtherYears()
    {
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) QFAIL("Could not create temp dir");

        OrderManager orderManager(tempDir.path());
        orderManager.deleteDatabase();

        ActivitySource sourceAmazon{ActivitySourceType::Report, "Amazon", "EU", "report-2023"};

        // Order in 2023
        auto ship2023 = makeShipment("Order-2023-1", QDate(2023, 6, 15), 75.0, "EUR", "acc3", false);
        QVERIFY(ship2023);
        orderManager.recordShipmentFromSource("Order-2023-1", &sourceAmazon, ship2023.data(), QDate(2023, 6, 15));
        orderManager.recordOrders({{"Order-2023-1", OrderManager::OrderInfo{QString(), false, "acc3"}}});

        UngroupedOrderTable table(nullptr, &orderManager, tempDir.path());
        table.load(2024);

        QCOMPARE(table.rowCount(), 0);
    }
};

QTEST_MAIN(TestUngroupedOrderTable)
#include "test_ungrouped_order_table.moc"
