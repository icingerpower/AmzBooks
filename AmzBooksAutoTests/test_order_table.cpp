#include <QtTest>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QCoroTask>
#include "books/Activity.h"
#include "orders/Amount.h"
#include "orders/Shipment.h"
#include "orders/TaxSource.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"
#include "orders/SaleType.h"
#include "orders/OrderManager.h"
#include "books/TaxResolver.h"
#include "orders/OrderTable.h"
#include "orders/OrderCompleteTable.h"
#include "orders/ImporterFileAmazonVatEu.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QSharedPointer<Shipment> makeShipment(
    const QString &orderId,
    const QString &activityId,
    const QDate &date,
    double amountTaxed,
    double vatAmount,
    const QString &currency = "EUR",
    const QString &from = "FR",
    const QString &to = "DE",
    bool isCompany = false,
    const QString &vatPaidTo = "DE",
    TaxSource taxSource = TaxSource::MarketplaceProvided,
    TaxScheme taxScheme = TaxScheme::EuOssUnion,
    TaxJurisdictionLevel jurisdictionLevel = TaxJurisdictionLevel::Country,
    SaleType saleType = SaleType::Products,
    const QString &invoiceId = QString{})
{
    auto act = Activity::create(
        orderId, activityId, "",
        QDateTime(date, QTime(12, 0)),
        QDateTime(date, QTime(12, 0)),
        currency, from, to, isCompany, vatPaidTo,
        Amount(amountTaxed, vatAmount),
        taxSource, "DE", taxScheme, jurisdictionLevel, saleType,
        {}, {}, invoiceId);
    return QSharedPointer<Shipment>::create(QList<Activity>{*act.value}, "", true);
}

using CompleteData = QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>;

static QSharedPointer<CompleteData> makeCompleteData()
{
    return QSharedPointer<CompleteData>::create();
}

// ---------------------------------------------------------------------------
// TestOrderTable
// ---------------------------------------------------------------------------

class TestOrderTable : public QObject
{
    Q_OBJECT

private slots:
    void test_basicProperties();
    void test_defaultSortDescendingByDate();
    void test_allColumnData();
    void test_typeColumn_shipmentAndRefund();
    void test_sorting();
    void test_headerData();
    void test_invalidIndex();
    void test_emptyList();
};

void TestOrderTable::test_basicProperties()
{
    auto s1 = makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0);
    auto s2 = makeShipment("ord2", "act2", QDate(2023, 1, 5), 200.0, 40.0);

    OrderTable table({s1, s2});

    QCOMPARE(table.rowCount(), 2);
    QCOMPARE(table.columnCount(), OrderTable::COL_COUNT);
    QCOMPARE(table.columnCount(), 16);
    QVERIFY(table.flags(table.index(0, 0)) & Qt::ItemIsSelectable);
    QVERIFY(table.flags(table.index(0, 0)) & Qt::ItemIsEnabled);
}

void TestOrderTable::test_defaultSortDescendingByDate()
{
    auto s1 = makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0);
    auto s2 = makeShipment("ord2", "act2", QDate(2023, 1, 5), 200.0, 40.0);

    OrderTable table({s1, s2});

    // Default sort: descending by date → newest first
    QCOMPARE(table.data(table.index(0, OrderTable::COL_ORDER_ID)).toString(), QString("ord2"));
    QCOMPARE(table.data(table.index(1, OrderTable::COL_ORDER_ID)).toString(), QString("ord1"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_DATE)).toDate(), QDate(2023, 1, 5));
    QCOMPARE(table.data(table.index(1, OrderTable::COL_DATE)).toDate(), QDate(2023, 1, 1));
}

void TestOrderTable::test_allColumnData()
{
    auto ship = makeShipment(
        "ord42", "actABC", QDate(2024, 6, 15),
        300.0, 60.0,
        "GBP", "GB", "DE",
        /*isCompany=*/true, "DE",
        TaxSource::SelfComputed,
        TaxScheme::DomesticVat,
        TaxJurisdictionLevel::Region,
        SaleType::Service,
        "INV-001");

    OrderTable table({ship});

    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.data(table.index(0, OrderTable::COL_DATE)).toDate(),          QDate(2024, 6, 15));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_ORDER_ID)).toString(),    QString("ord42"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_ACTIVITY_ID)).toString(), QString("actABC"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_SALE_TYPE)).toString(),   QString("Service"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_COUNTRY_FROM)).toString(),QString("GB"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_COUNTRY_TO)).toString(),  QString("DE"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_VAT_PAID_TO)).toString(), QString("DE"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_IS_BUSINESS)).toString(), QString("Yes"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_TAX_SOURCE)).toString(),  QString("SelfComputed"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_TAX_SCHEME)).toString(),  QString("DomesticVat"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_TAX_JURISDICTION)).toString(), QString("Region"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_CURRENCY)).toString(),    QString("GBP"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_AMOUNT_TAXED)).toDouble(), 300.0);
    QCOMPARE(table.data(table.index(0, OrderTable::COL_VAT_AMOUNT)).toDouble(),  60.0);
    QCOMPARE(table.data(table.index(0, OrderTable::COL_INVOICE_ID)).toString(),  QString("INV-001"));

    // isCompany = false → "No"
    auto s2 = makeShipment("ord2", "act2", QDate(2024, 1, 1), 50.0, 10.0,
                            "EUR", "FR", "DE", /*isCompany=*/false);
    OrderTable t2({s2});
    QCOMPARE(t2.data(t2.index(0, OrderTable::COL_IS_BUSINESS)).toString(), QString("No"));
}

void TestOrderTable::test_typeColumn_shipmentAndRefund()
{
    auto shipPos  = makeShipment("ordPos",  "actPos",  QDate(2023, 3, 1),  100.0,  20.0);
    auto shipNeg  = makeShipment("ordNeg",  "actNeg",  QDate(2023, 3, 2), -100.0, -20.0);
    auto shipZero = makeShipment("ordZero", "actZero", QDate(2023, 3, 3),    0.0,   0.0);

    OrderTable table({shipPos, shipNeg, shipZero});
    table.sort(OrderTable::COL_DATE, Qt::AscendingOrder);

    QCOMPARE(table.data(table.index(0, OrderTable::COL_ORDER_ID)).toString(), QString("ordPos"));
    QCOMPARE(table.data(table.index(0, OrderTable::COL_TYPE)).toString(),     QString("Shipment"));

    QCOMPARE(table.data(table.index(1, OrderTable::COL_ORDER_ID)).toString(), QString("ordNeg"));
    QCOMPARE(table.data(table.index(1, OrderTable::COL_TYPE)).toString(),     QString("Refund"));

    // Zero amountTaxed is treated as Shipment (>= 0)
    QCOMPARE(table.data(table.index(2, OrderTable::COL_ORDER_ID)).toString(), QString("ordZero"));
    QCOMPARE(table.data(table.index(2, OrderTable::COL_TYPE)).toString(),     QString("Shipment"));
}

void TestOrderTable::test_sorting()
{
    auto s1 = makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0);
    auto s2 = makeShipment("ord2", "act2", QDate(2023, 6, 1), 200.0, 40.0);
    auto s3 = makeShipment("ord3", "act3", QDate(2023, 3, 1),  50.0, 10.0);

    OrderTable table({s1, s2, s3});

    // Ascending by date
    table.sort(OrderTable::COL_DATE, Qt::AscendingOrder);
    QCOMPARE(table.data(table.index(0, OrderTable::COL_ORDER_ID)).toString(), QString("ord1"));
    QCOMPARE(table.data(table.index(1, OrderTable::COL_ORDER_ID)).toString(), QString("ord3"));
    QCOMPARE(table.data(table.index(2, OrderTable::COL_ORDER_ID)).toString(), QString("ord2"));

    // Descending by date
    table.sort(OrderTable::COL_DATE, Qt::DescendingOrder);
    QCOMPARE(table.data(table.index(0, OrderTable::COL_ORDER_ID)).toString(), QString("ord2"));
    QCOMPARE(table.data(table.index(2, OrderTable::COL_ORDER_ID)).toString(), QString("ord1"));

    // Ascending by amount
    table.sort(OrderTable::COL_AMOUNT_TAXED, Qt::AscendingOrder);
    QCOMPARE(table.data(table.index(0, OrderTable::COL_AMOUNT_TAXED)).toDouble(),  50.0);
    QCOMPARE(table.data(table.index(1, OrderTable::COL_AMOUNT_TAXED)).toDouble(), 100.0);
    QCOMPARE(table.data(table.index(2, OrderTable::COL_AMOUNT_TAXED)).toDouble(), 200.0);

    // Ascending by orderId
    table.sort(OrderTable::COL_ORDER_ID, Qt::AscendingOrder);
    QCOMPARE(table.data(table.index(0, OrderTable::COL_ORDER_ID)).toString(), QString("ord1"));
    QCOMPARE(table.data(table.index(2, OrderTable::COL_ORDER_ID)).toString(), QString("ord3"));
}

void TestOrderTable::test_headerData()
{
    OrderTable table({});

    QCOMPARE(table.headerData(OrderTable::COL_DATE,             Qt::Horizontal).toString(), QString("Date"));
    QCOMPARE(table.headerData(OrderTable::COL_ORDER_ID,         Qt::Horizontal).toString(), QString("Order ID"));
    QCOMPARE(table.headerData(OrderTable::COL_ACTIVITY_ID,      Qt::Horizontal).toString(), QString("Activity ID"));
    QCOMPARE(table.headerData(OrderTable::COL_SALE_TYPE,        Qt::Horizontal).toString(), QString("Sale Type"));
    QCOMPARE(table.headerData(OrderTable::COL_TYPE,             Qt::Horizontal).toString(), QString("Type"));
    QCOMPARE(table.headerData(OrderTable::COL_COUNTRY_FROM,     Qt::Horizontal).toString(), QString("From"));
    QCOMPARE(table.headerData(OrderTable::COL_COUNTRY_TO,       Qt::Horizontal).toString(), QString("To"));
    QCOMPARE(table.headerData(OrderTable::COL_VAT_PAID_TO,      Qt::Horizontal).toString(), QString("VAT Paid To"));
    QCOMPARE(table.headerData(OrderTable::COL_IS_BUSINESS,      Qt::Horizontal).toString(), QString("Is Business"));
    QCOMPARE(table.headerData(OrderTable::COL_TAX_SOURCE,       Qt::Horizontal).toString(), QString("Tax Source"));
    QCOMPARE(table.headerData(OrderTable::COL_TAX_SCHEME,       Qt::Horizontal).toString(), QString("Tax Scheme"));
    QCOMPARE(table.headerData(OrderTable::COL_TAX_JURISDICTION, Qt::Horizontal).toString(), QString("Jurisdiction"));
    QCOMPARE(table.headerData(OrderTable::COL_CURRENCY,         Qt::Horizontal).toString(), QString("Currency"));
    QCOMPARE(table.headerData(OrderTable::COL_AMOUNT_TAXED,     Qt::Horizontal).toString(), QString("Amount Taxed"));
    QCOMPARE(table.headerData(OrderTable::COL_VAT_AMOUNT,       Qt::Horizontal).toString(), QString("VAT Amount"));
    QCOMPARE(table.headerData(OrderTable::COL_INVOICE_ID,       Qt::Horizontal).toString(), QString("Invoice ID"));

    // Vertical headers and out-of-range return nothing
    QVERIFY(!table.headerData(0, Qt::Vertical).isValid());
    QVERIFY(!table.headerData(OrderTable::COL_COUNT, Qt::Horizontal).isValid());
}

void TestOrderTable::test_invalidIndex()
{
    auto ship = makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0);
    OrderTable table({ship});

    QVERIFY(!table.data(table.index(100, 0)).isValid());
    QVERIFY(!table.data(table.index(-1,  0)).isValid());
    QVERIFY(!table.data(QModelIndex()).isValid());
}

void TestOrderTable::test_emptyList()
{
    OrderTable table({});
    QCOMPARE(table.rowCount(), 0);
    QCOMPARE(table.columnCount(), OrderTable::COL_COUNT);
}

// ---------------------------------------------------------------------------
// TestOrderCompleteTable
// ---------------------------------------------------------------------------

class TestOrderCompleteTable : public QObject
{
    Q_OBJECT

private slots:
    void test_basicProperties();
    void test_channelAndStore();
    void test_activityIdOrig_emptyWhenSingleShipment();
    void test_activityIdOrig_filledWhenRefund();
    void test_allCommonColumns();
    void test_typeColumn_shipmentAndRefund();
    void test_headerData();
    void test_sorting_byChannel();
    void test_sorting_byStore();
    void test_sorting_byOrigActivityId();
    void test_sorting_byDate();
    void test_sorting_bySite();
    void test_invalidIndex();
    void test_nullData();
    void test_defaultSortDescendingByDate();
    void test_activityIdOrig_twoShipmentsAndTwoRefunds();
    void test_multipleGroupsMultipleShipments();
    void test_site_column();
};

void TestOrderCompleteTable::test_basicProperties()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;
    OrderManager::ShipmentRefundsWithUpdates group;
    group.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0));
    (*data)["Amazon"]["Europe"][ctx] = group;

    OrderCompleteTable table(data, {});

    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.columnCount(), OrderCompleteTable::COL_COUNT);
    QCOMPARE(table.columnCount(), 20);
    QVERIFY(table.flags(table.index(0, 0)) & Qt::ItemIsSelectable);
    QVERIFY(table.flags(table.index(0, 0)) & Qt::ItemIsEnabled);
}

void TestOrderCompleteTable::test_channelAndStore()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    OrderManager::ShipmentRefundsWithUpdates g1;
    g1.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0));
    (*data)["Amazon"]["Europe"][ctx] = g1;

    OrderManager::ShipmentRefundsWithUpdates g2;
    g2.shipmentsRefundsSameActivity.append(
        makeShipment("ord2", "act2", QDate(2023, 1, 5), 200.0, 40.0));
    (*data)["Temu"]["Asia"][ctx] = g2;

    OrderCompleteTable table(data, {});
    QCOMPARE(table.rowCount(), 2);

    // Sort by channel for deterministic order
    table.sort(OrderCompleteTable::COL_CHANNEL, Qt::AscendingOrder);

    // "Amazon" < "Temu" alphabetically
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_CHANNEL)).toString(),  QString("Amazon"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_STORE)).toString(),    QString("Europe"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ORDER_ID)).toString(), QString("ord1"));

    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_CHANNEL)).toString(),  QString("Temu"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_STORE)).toString(),    QString("Asia"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ORDER_ID)).toString(), QString("ord2"));
}

void TestOrderCompleteTable::test_activityIdOrig_emptyWhenSingleShipment()
{
    // A group with a single shipment: activityIdOrig must be empty (it IS the original).
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;
    OrderManager::ShipmentRefundsWithUpdates group;
    group.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "originalAct", QDate(2023, 5, 10), 150.0, 30.0));
    (*data)["Chan"]["Store"][ctx] = group;

    OrderCompleteTable table(data, {});
    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),      QString("originalAct"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString(""));
}

void TestOrderCompleteTable::test_activityIdOrig_filledWhenRefund()
{
    // Group: original shipment + refund. The refund row must show the original activityId.
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    auto origShipment   = makeShipment("ord1", "origAct",   QDate(2023, 5, 10),  150.0,  30.0);
    auto refundShipment = makeShipment("ord1", "refundAct", QDate(2023, 5, 15), -150.0, -30.0);
    OrderManager::ShipmentRefundsWithUpdates group;
    group.shipmentsRefundsSameActivity.append(origShipment);
    group.shipmentsRefundsSameActivity.append(refundShipment);
    (*data)["Chan"]["Store"][ctx] = group;

    OrderCompleteTable table(data, {});
    QCOMPARE(table.rowCount(), 2);

    table.sort(OrderCompleteTable::COL_DATE, Qt::AscendingOrder);

    // Original row: activityIdOrig is empty
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),      QString("origAct"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString(""));

    // Refund row: activityIdOrig = "origAct"
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),      QString("refundAct"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString("origAct"));
}

void TestOrderCompleteTable::test_allCommonColumns()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;
    OrderManager::ShipmentRefundsWithUpdates group;
    group.shipmentsRefundsSameActivity.append(makeShipment(
        "ord99", "actXYZ", QDate(2024, 3, 20),
        250.0, 50.0,
        "USD", "US", "CA",
        /*isCompany=*/true, "CA",
        TaxSource::ManualOverride,
        TaxScheme::ImportVat,
        TaxJurisdictionLevel::City,
        SaleType::InventoryMove,
        "INV-999"));
    (*data)["MyChannel"]["MyStore"][ctx] = group;

    OrderCompleteTable table(data, {});
    QCOMPARE(table.rowCount(), 1);

    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_DATE)).toDate(),              QDate(2024, 3, 20));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ORDER_ID)).toString(),        QString("ord99"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),     QString("actXYZ"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(),QString(""));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_CHANNEL)).toString(),         QString("MyChannel"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_STORE)).toString(),           QString("MyStore"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_SITE)).toString(),            QString(""));  // no orderIdToSite provided
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_SALE_TYPE)).toString(),       QString("InventoryMove"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_TYPE)).toString(),            QString("Shipment"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_COUNTRY_FROM)).toString(),    QString("US"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_COUNTRY_TO)).toString(),      QString("CA"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_VAT_PAID_TO)).toString(),     QString("CA"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_IS_BUSINESS)).toString(),     QString("Yes"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_TAX_SOURCE)).toString(),      QString("ManualOverride"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_TAX_SCHEME)).toString(),      QString("ImportVat"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_TAX_JURISDICTION)).toString(),QString("City"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_CURRENCY)).toString(),        QString("USD"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_AMOUNT_TAXED)).toDouble(),    250.0);
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_VAT_AMOUNT)).toDouble(),      50.0);
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_INVOICE_ID)).toString(),      QString("INV-999"));
}

void TestOrderCompleteTable::test_typeColumn_shipmentAndRefund()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;
    OrderManager::ShipmentRefundsWithUpdates group;
    group.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "act1", QDate(2023, 1, 1),  100.0,  20.0));
    group.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "act2", QDate(2023, 1, 5), -100.0, -20.0));
    (*data)["Chan"]["Store"][ctx] = group;

    OrderCompleteTable table(data, {});
    table.sort(OrderCompleteTable::COL_DATE, Qt::AscendingOrder);

    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_TYPE)).toString(), QString("Shipment"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_TYPE)).toString(), QString("Refund"));
}

void TestOrderCompleteTable::test_headerData()
{
    OrderCompleteTable table(makeCompleteData(), {});

    QCOMPARE(table.headerData(OrderCompleteTable::COL_DATE,             Qt::Horizontal).toString(), QString("Date"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_ORDER_ID,         Qt::Horizontal).toString(), QString("Order ID"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_ACTIVITY_ID,      Qt::Horizontal).toString(), QString("Activity ID"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_ACTIVITY_ID_ORIG, Qt::Horizontal).toString(), QString("Orig. Activity ID"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_CHANNEL,          Qt::Horizontal).toString(), QString("Channel"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_STORE,            Qt::Horizontal).toString(), QString("Store"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_SITE,             Qt::Horizontal).toString(), QString("Site"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_SALE_TYPE,        Qt::Horizontal).toString(), QString("Sale Type"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_TYPE,             Qt::Horizontal).toString(), QString("Type"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_COUNTRY_FROM,     Qt::Horizontal).toString(), QString("From"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_COUNTRY_TO,       Qt::Horizontal).toString(), QString("To"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_VAT_PAID_TO,      Qt::Horizontal).toString(), QString("VAT Paid To"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_IS_BUSINESS,      Qt::Horizontal).toString(), QString("Is Business"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_TAX_SOURCE,       Qt::Horizontal).toString(), QString("Tax Source"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_TAX_SCHEME,       Qt::Horizontal).toString(), QString("Tax Scheme"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_TAX_JURISDICTION, Qt::Horizontal).toString(), QString("Jurisdiction"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_CURRENCY,         Qt::Horizontal).toString(), QString("Currency"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_AMOUNT_TAXED,     Qt::Horizontal).toString(), QString("Amount Taxed"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_VAT_AMOUNT,       Qt::Horizontal).toString(), QString("VAT Amount"));
    QCOMPARE(table.headerData(OrderCompleteTable::COL_INVOICE_ID,       Qt::Horizontal).toString(), QString("Invoice ID"));

    QVERIFY(!table.headerData(0, Qt::Vertical).isValid());
    QVERIFY(!table.headerData(OrderCompleteTable::COL_COUNT, Qt::Horizontal).isValid());
}

void TestOrderCompleteTable::test_sorting_byChannel()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;
    for (const QString &chan : {"Zebra", "Alpha", "Mango"}) {
        OrderManager::ShipmentRefundsWithUpdates g;
        g.shipmentsRefundsSameActivity.append(
            makeShipment("ord", "act", QDate(2023, 1, 1), 10.0, 2.0));
        (*data)[chan]["Store"][ctx] = g;
    }

    OrderCompleteTable table(data, {});
    table.sort(OrderCompleteTable::COL_CHANNEL, Qt::AscendingOrder);

    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_CHANNEL)).toString(), QString("Alpha"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_CHANNEL)).toString(), QString("Mango"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_CHANNEL)).toString(), QString("Zebra"));

    table.sort(OrderCompleteTable::COL_CHANNEL, Qt::DescendingOrder);
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_CHANNEL)).toString(), QString("Zebra"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_CHANNEL)).toString(), QString("Alpha"));
}

void TestOrderCompleteTable::test_sorting_byStore()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;
    for (const QString &store : {"UK", "EU", "US"}) {
        OrderManager::ShipmentRefundsWithUpdates g;
        g.shipmentsRefundsSameActivity.append(
            makeShipment("ord", "act", QDate(2023, 1, 1), 10.0, 2.0));
        (*data)["Amazon"][store][ctx] = g;
    }

    OrderCompleteTable table(data, {});
    table.sort(OrderCompleteTable::COL_STORE, Qt::AscendingOrder);

    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_STORE)).toString(), QString("EU"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_STORE)).toString(), QString("UK"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_STORE)).toString(), QString("US"));
}

void TestOrderCompleteTable::test_sorting_byOrigActivityId()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    // Group: original "aaa" + refund "zzz" → refund row has activityIdOrig = "aaa"
    OrderManager::ShipmentRefundsWithUpdates g;
    g.shipmentsRefundsSameActivity.append(
        makeShipment("ordA", "aaa", QDate(2023, 1, 1),  50.0, 10.0));
    g.shipmentsRefundsSameActivity.append(
        makeShipment("ordA", "zzz", QDate(2023, 1, 2), -50.0, -10.0));
    (*data)["Chan"]["Store"][ctx] = g;

    OrderCompleteTable table(data, {});
    // Ascending sort: empty string < "aaa"
    table.sort(OrderCompleteTable::COL_ACTIVITY_ID_ORIG, Qt::AscendingOrder);

    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString(""));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString("aaa"));
}

void TestOrderCompleteTable::test_sorting_byDate()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    OrderManager::ShipmentRefundsWithUpdates g1;
    g1.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0));
    (*data)["Chan"]["StoreA"][ctx] = g1;

    OrderManager::ShipmentRefundsWithUpdates g2;
    g2.shipmentsRefundsSameActivity.append(
        makeShipment("ord2", "act2", QDate(2023, 6, 1), 200.0, 40.0));
    (*data)["Chan"]["StoreB"][ctx] = g2;

    OrderCompleteTable table(data, {});
    table.sort(OrderCompleteTable::COL_DATE, Qt::AscendingOrder);
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ORDER_ID)).toString(), QString("ord1"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ORDER_ID)).toString(), QString("ord2"));
}

void TestOrderCompleteTable::test_site_column()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    OrderManager::ShipmentRefundsWithUpdates g1;
    g1.shipmentsRefundsSameActivity.append(
        makeShipment("ord99", "actXYZ", QDate(2024, 3, 20), 100.0, 20.0));
    (*data)["Chan"]["Store"][ctx] = g1;

    // Without orderIdToSite: COL_SITE is empty
    {
        OrderCompleteTable table(data, {});
        QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_SITE)).toString(), QString(""));
    }

    // With orderIdToSite mapping ord99 → "Amazon.fr": COL_SITE shows "Amazon.fr"
    {
        QHash<QString, QString> orderIdToSite;
        orderIdToSite["ord99"] = "Amazon.fr";
        OrderCompleteTable table(data, orderIdToSite);
        QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_SITE)).toString(), QString("Amazon.fr"));
    }

    // With orderIdToSite that does NOT contain ord99: COL_SITE is still empty
    {
        QHash<QString, QString> orderIdToSite;
        orderIdToSite["other-order"] = "Amazon.de";
        OrderCompleteTable table(data, orderIdToSite);
        QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_SITE)).toString(), QString(""));
    }
}

void TestOrderCompleteTable::test_sorting_bySite()
{
    auto data = makeCompleteData();
    // Use distinct TaxContexts (different countryCodeVatPaidTo) to hold each order in a separate bucket
    TaxResolver::TaxContext ctx1, ctx2, ctx3;
    ctx1.countryCodeVatPaidTo = "FR";
    ctx2.countryCodeVatPaidTo = "DE";
    ctx3.countryCodeVatPaidTo = "ES";

    OrderManager::ShipmentRefundsWithUpdates g1;
    g1.shipmentsRefundsSameActivity.append(makeShipment("ord1", "act1", QDate(2023, 1, 1), 10.0, 2.0));
    (*data)["Chan"]["Store"][ctx1] = g1;

    OrderManager::ShipmentRefundsWithUpdates g2;
    g2.shipmentsRefundsSameActivity.append(makeShipment("ord2", "act2", QDate(2023, 1, 2), 20.0, 4.0));
    (*data)["Chan"]["Store"][ctx2] = g2;

    OrderManager::ShipmentRefundsWithUpdates g3;
    g3.shipmentsRefundsSameActivity.append(makeShipment("ord3", "act3", QDate(2023, 1, 3), 30.0, 6.0));
    (*data)["Chan"]["Store"][ctx3] = g3;

    QHash<QString, QString> orderIdToSite;
    orderIdToSite["ord1"] = "Amazon.fr";
    orderIdToSite["ord2"] = "Amazon.de";
    orderIdToSite["ord3"] = "Amazon.es";

    OrderCompleteTable table(data, orderIdToSite);

    table.sort(OrderCompleteTable::COL_SITE, Qt::AscendingOrder);
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_SITE)).toString(), QString("Amazon.de"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_SITE)).toString(), QString("Amazon.es"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_SITE)).toString(), QString("Amazon.fr"));

    table.sort(OrderCompleteTable::COL_SITE, Qt::DescendingOrder);
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_SITE)).toString(), QString("Amazon.fr"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_SITE)).toString(), QString("Amazon.es"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_SITE)).toString(), QString("Amazon.de"));
}

void TestOrderCompleteTable::test_invalidIndex()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;
    OrderManager::ShipmentRefundsWithUpdates g;
    g.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0));
    (*data)["Chan"]["Store"][ctx] = g;

    OrderCompleteTable table(data, {});

    QVERIFY(!table.data(table.index(100, 0)).isValid());
    QVERIFY(!table.data(table.index(-1,  0)).isValid());
    QVERIFY(!table.data(QModelIndex()).isValid());
}

void TestOrderCompleteTable::test_nullData()
{
    QSharedPointer<CompleteData> nullPtr;
    OrderCompleteTable table(nullPtr, {});
    QCOMPARE(table.rowCount(), 0);
    QCOMPARE(table.columnCount(), OrderCompleteTable::COL_COUNT);
}

void TestOrderCompleteTable::test_defaultSortDescendingByDate()
{
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    OrderManager::ShipmentRefundsWithUpdates g1;
    g1.shipmentsRefundsSameActivity.append(
        makeShipment("ord1", "act1", QDate(2023, 1, 1), 100.0, 20.0));
    (*data)["Chan"]["StoreA"][ctx] = g1;

    OrderManager::ShipmentRefundsWithUpdates g2;
    g2.shipmentsRefundsSameActivity.append(
        makeShipment("ord2", "act2", QDate(2023, 9, 1), 200.0, 40.0));
    (*data)["Chan"]["StoreB"][ctx] = g2;

    OrderCompleteTable table(data, {});

    // Default sort is DESC date → newest (ord2) first
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ORDER_ID)).toString(), QString("ord2"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ORDER_ID)).toString(), QString("ord1"));
}

void TestOrderCompleteTable::test_activityIdOrig_twoShipmentsAndTwoRefunds()
{
    // Scenario: one group contains 2 original shipments and 2 refunds.
    // Each refund must reference its own original, not both the same one.
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    auto ship1   = makeShipment("ord1", "shipAct1", QDate(2023, 1, 1),  100.0,  20.0);
    auto ship2   = makeShipment("ord1", "shipAct2", QDate(2023, 1, 2),   80.0,  16.0);
    auto refund1 = makeShipment("ord1", "refAct1",  QDate(2023, 1, 5), -100.0, -20.0);
    auto refund2 = makeShipment("ord1", "refAct2",  QDate(2023, 1, 6),  -80.0, -16.0);

    OrderManager::ShipmentRefundsWithUpdates group;
    group.shipmentsRefundsSameActivity = {ship1, ship2, refund1, refund2};
    (*data)["Chan"]["Store"][ctx] = group;

    OrderCompleteTable table(data, {});
    QCOMPARE(table.rowCount(), 4);

    table.sort(OrderCompleteTable::COL_DATE, Qt::AscendingOrder);

    // ship1 (Jan 1): original → activityIdOrig must be empty
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),      QString("shipAct1"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString(""));

    // ship2 (Jan 2): also an original → activityIdOrig must be empty
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),      QString("shipAct2"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString(""));  // FAILS with current code

    // refund1 (Jan 5): refunds ship1 → activityIdOrig = "shipAct1"
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),      QString("refAct1"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString("shipAct1"));

    // refund2 (Jan 6): refunds ship2 → activityIdOrig = "shipAct2" (not "shipAct1" again)
    QCOMPARE(table.data(table.index(3, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),      QString("refAct2"));
    QCOMPARE(table.data(table.index(3, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(), QString("shipAct2"));  // FAILS with current code

    // The two refunds must reference different originals
    const QString orig1 = table.data(table.index(2, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString();
    const QString orig2 = table.data(table.index(3, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString();
    QVERIFY(orig1 != orig2);
}

void TestOrderCompleteTable::test_multipleGroupsMultipleShipments()
{
    // Two channels, two stores, one group has a refund.
    auto data = makeCompleteData();
    TaxResolver::TaxContext ctx;

    // Amazon/Europe: single shipment
    OrderManager::ShipmentRefundsWithUpdates gAE;
    gAE.shipmentsRefundsSameActivity.append(
        makeShipment("ordAE", "actAE", QDate(2023, 2, 1), 100.0, 20.0));
    (*data)["Amazon"]["Europe"][ctx] = gAE;

    // Amazon/UK: original + refund
    OrderManager::ShipmentRefundsWithUpdates gAU;
    gAU.shipmentsRefundsSameActivity.append(
        makeShipment("ordAU", "actAU_orig",   QDate(2023, 3, 1),  80.0, 16.0));
    gAU.shipmentsRefundsSameActivity.append(
        makeShipment("ordAU", "actAU_refund", QDate(2023, 3, 5), -80.0, -16.0));
    (*data)["Amazon"]["UK"][ctx] = gAU;

    OrderCompleteTable table(data, {});
    QCOMPARE(table.rowCount(), 3);  // 1 + 2

    table.sort(OrderCompleteTable::COL_DATE, Qt::AscendingOrder);

    // Row 0: ordAE (Feb 1)
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ORDER_ID)).toString(),        QString("ordAE"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_CHANNEL)).toString(),         QString("Amazon"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_STORE)).toString(),           QString("Europe"));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(),QString(""));
    QCOMPARE(table.data(table.index(0, OrderCompleteTable::COL_TYPE)).toString(),            QString("Shipment"));

    // Row 1: origAU (Mar 1)
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),     QString("actAU_orig"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(),QString(""));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_STORE)).toString(),           QString("UK"));
    QCOMPARE(table.data(table.index(1, OrderCompleteTable::COL_AMOUNT_TAXED)).toDouble(),    80.0);

    // Row 2: refundAU (Mar 5)
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_ACTIVITY_ID)).toString(),     QString("actAU_refund"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString(),QString("actAU_orig"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_TYPE)).toString(),            QString("Refund"));
    QCOMPARE(table.data(table.index(2, OrderCompleteTable::COL_AMOUNT_TAXED)).toDouble(),    -80.0);
}

// ---------------------------------------------------------------------------
// TestOrderCompleteTableRealData
// ---------------------------------------------------------------------------

class TestOrderCompleteTableRealData : public QObject
{
    Q_OBJECT

private slots:
    void test_activityIdOrig_neverCrossesOrderBoundary();
    void test_siteColumn_notEmptyAfterRecordingOrders();
};

void TestOrderCompleteTableRealData::test_activityIdOrig_neverCrossesOrderBoundary()
{
    // Collect 2025 VAT report CSV files
    const QString vatDir = "data/amazon-vat-reports/2025";
    QStringList vatFiles;
    {
        QDirIterator it(vatDir, {"*.csv"}, QDir::Files);
        while (it.hasNext())
            vatFiles << it.next();
    }
    vatFiles.sort();
    QVERIFY2(!vatFiles.isEmpty(),
             "No 2025 VAT CSV files found — ensure data/amazon-vat-reports is copied to build dir");

    // Load all CSV files into a temporary OrderManager
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    OrderManager manager(tempDir.path());
    ImporterFileAmazonVatEu vatImporter(tempDir.path());
    ActivitySource source = vatImporter.getActivitySource();

    for (const QString &filePath : vatFiles) {
        AbstractImporter::ReturnOrderInfos result;
        try {
            result = QCoro::waitFor(vatImporter.loadReport(filePath));
        } catch (...) {
            qWarning() << "Exception loading" << filePath << "— skipping";
            continue;
        }
        if (!result.errorReturned.isEmpty() || !result.orderInfos)
            continue;

        manager.m_db.transaction();
        for (const auto &ship : result.orderInfos->shipments)
            manager.recordShipmentFromSource(ship.getId(), &source, &ship, QDate(), false);
        for (const auto &ref : result.orderInfos->refunds)
            manager.recordShipmentFromSource(ref.getId(), &source, &ref, QDate(), false);
        manager.m_db.commit();
    }

    // Build the complete table for the full 2025 year
    auto data = manager.get_channel_site_ShipmentAndRefundsConflicts(
        QDate(2025, 1, 1), QDate(2025, 12, 31));
    OrderCompleteTable table(data, {});
    QVERIFY2(table.rowCount() > 0, "Table must contain rows after loading 2025 data");

    // Build activityId → orderId map from all rows
    QHash<QString, QString> activityIdToOrderId;
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString activityId = table.data(table.index(row, OrderCompleteTable::COL_ACTIVITY_ID)).toString();
        const QString orderId    = table.data(table.index(row, OrderCompleteTable::COL_ORDER_ID)).toString();
        if (!activityId.isEmpty())
            activityIdToOrderId[activityId] = orderId;
    }

    // Verify: for every refund row with a non-empty activityIdOrig,
    // the original activity must belong to the same order as the refund.
    int violations = 0;
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString activityIdOrig = table.data(table.index(row, OrderCompleteTable::COL_ACTIVITY_ID_ORIG)).toString();
        if (activityIdOrig.isEmpty())
            continue;

        const QString rowOrderId  = table.data(table.index(row, OrderCompleteTable::COL_ORDER_ID)).toString();
        const QString origOrderId = activityIdToOrderId.value(activityIdOrig);

        if (origOrderId != rowOrderId) {
            qWarning() << "Cross-order violation: refund activityId="
                       << table.data(table.index(row, OrderCompleteTable::COL_ACTIVITY_ID)).toString()
                       << "orderId=" << rowOrderId
                       << "has activityIdOrig=" << activityIdOrig
                       << "belonging to orderId=" << origOrderId;
            ++violations;
        }
    }
    QCOMPARE(violations, 0);
}

void TestOrderCompleteTableRealData::test_siteColumn_notEmptyAfterRecordingOrders()
{
    // Verifies that COL_SITE is populated when orderId_infos data from the importer
    // is recorded via recordOrders() before building the table.
    // This is the integration path that PaneOrderFiles.cpp must follow.
    const QString vatDir = "data/amazon-vat-reports/2025";
    QStringList vatFiles;
    {
        QDirIterator it(vatDir, {"*.csv"}, QDir::Files);
        while (it.hasNext())
            vatFiles << it.next();
    }
    vatFiles.sort();
    QVERIFY2(!vatFiles.isEmpty(),
             "No 2025 VAT CSV files found — ensure data/amazon-vat-reports is copied to build dir");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    OrderManager manager(tempDir.path());
    ImporterFileAmazonVatEu vatImporter(tempDir.path());
    ActivitySource source = vatImporter.getActivitySource();

    for (const QString &filePath : vatFiles) {
        AbstractImporter::ReturnOrderInfos result;
        try {
            result = QCoro::waitFor(vatImporter.loadReport(filePath));
        } catch (...) {
            qWarning() << "Exception loading" << filePath << "— skipping";
            continue;
        }
        if (!result.errorReturned.isEmpty() || !result.orderInfos)
            continue;

        manager.m_db.transaction();
        for (const auto &ship : result.orderInfos->shipments)
            manager.recordShipmentFromSource(ship.getId(), &source, &ship, QDate(), false);
        for (const auto &ref : result.orderInfos->refunds)
            manager.recordShipmentFromSource(ref.getId(), &source, &ref, QDate(), false);
        manager.m_db.commit();

        // Record orderId→store mapping so COL_SITE can be populated
        if (!result.orderInfos->orderId_infos.isEmpty())
            manager.recordOrders(result.orderInfos->orderId_infos);
    }

    auto data = manager.get_channel_site_ShipmentAndRefundsConflicts(
        QDate(2025, 1, 1), QDate(2025, 12, 31));

    // Extract all shipments from the data to pass to getStores()
    QList<QSharedPointer<Shipment>> allShipments;
    if (data) {
        for (auto itCh = data->constBegin(); itCh != data->constEnd(); ++itCh)
            for (auto itSt = itCh.value().constBegin(); itSt != itCh.value().constEnd(); ++itSt)
                for (auto itCtx = itSt.value().constBegin(); itCtx != itSt.value().constEnd(); ++itCtx)
                    allShipments.append(itCtx.value().shipmentsRefundsSameActivity);
    }
    auto orderIdToSite = manager.getStores(allShipments);

    // The importer must have extracted at least some store values
    QVERIFY2(!orderIdToSite.isEmpty(),
             "getStores() returned empty — recordOrders() was not called or orderId_infos was empty");

    OrderCompleteTable table(data, orderIdToSite);
    QVERIFY(table.rowCount() > 0);

    // At least some rows must have a non-empty site
    int rowsWithSite = 0;
    for (int row = 0; row < table.rowCount(); ++row) {
        if (!table.data(table.index(row, OrderCompleteTable::COL_SITE)).toString().isEmpty())
            ++rowsWithSite;
    }
    QVERIFY2(rowsWithSite > 0,
             "All COL_SITE values are empty — orderId_infos was not saved to the orders table");
}

// ---------------------------------------------------------------------------
// Main: run both test classes
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        TestOrderTable t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestOrderCompleteTable t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestOrderCompleteTableRealData t;
        status |= QTest::qExec(&t, argc, argv);
    }
    return status;
}

#include "test_order_table.moc"
