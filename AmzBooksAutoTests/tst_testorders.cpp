#include <QtTest>
#include <QCoreApplication>
#include "model/Activity.h"
#include "model/Amount.h"
#include "model/Shipment.h"
#include "model/LineItem.h"
#include "model/TaxSource.h"
#include "model/TaxScheme.h"
#include "model/TaxJurisdictionLevel.h"
#include "model/Order.h"
#include "model/Refund.h"

class TestOrders : public QObject
{
    Q_OBJECT

public:
    TestOrders();
    ~TestOrders();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_Activity_getters();
    void test_shipments();
    void test_refunds();
    void test_orders();
};

TestOrders::TestOrders()
{

}

TestOrders::~TestOrders()
{

}

void TestOrders::initTestCase()
{

}

void TestOrders::cleanupTestCase()
{

}

void TestOrders::test_Activity_getters()
{
    QDateTime dt = QDateTime::currentDateTime();
    QString eventId = "event123";
    QString activityId = "act-456";
    QString currency = "EUR";
    QString fr = "FR";
    QString de = "DE";
    double amountTaxed = 100.0;
    double amountTaxes = 20.0;
    QString taxDeclaring = "DE";

    // Case 1: MarketplaceProvided
    {
        Activity activity(
            eventId, activityId, dt, currency, fr, de, Amount(amountTaxed, amountTaxes),
            TaxSource::MarketplaceProvided, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
        );

        QCOMPARE(activity.getEventId(), eventId);
        QCOMPARE(activity.getActivityId(), activityId);
        QCOMPARE(activity.getDateTime(), dt);
        QCOMPARE(activity.getCurrency(), currency);
        QCOMPARE(activity.getCountryCodeFrom(), fr);
        QCOMPARE(activity.getCountryCodeTo(), de);
        QCOMPARE(activity.getAmountTaxed(), amountTaxed);
        QCOMPARE(activity.getAmountTaxesSource(), amountTaxes);
        QCOMPARE(activity.getTaxSource(), TaxSource::MarketplaceProvided);
        QCOMPARE(activity.getTaxDeclaringCountryCode(), taxDeclaring);
        QCOMPARE(activity.getTaxScheme(), TaxScheme::EuOssUnion);
        QCOMPARE(activity.getTaxJurisdictionLevel(), TaxJurisdictionLevel::Country);

        // Check getAmountTaxes logic (should return Source)
        QCOMPARE(activity.getAmountTaxes(), amountTaxes);
        // Check getAmountUntaxed
        QCOMPARE(activity.getAmountUntaxed(), amountTaxed - amountTaxes);

        // Check getVatRate checks (20 / 100 = 0.2)
        QCOMPARE(activity.getVatRate(), 0.2);
        QCOMPARE(activity.getVatRate_4digits(), QString("0.2000"));
        QCOMPARE(activity.getVatRate_2digits(), QString("0.20"));

        // Test setTaxes logic for MarketplaceProvided -> ManualOverride
        double newTaxes = 25.0;
        activity.setTaxes(newTaxes);
        QCOMPARE(activity.getTaxSource(), TaxSource::ManualOverride);
        QCOMPARE(activity.getAmountTaxesComputed(), newTaxes);
        QCOMPARE(activity.getAmountTaxes(), newTaxes);
        
        // Rate should update with new taxes (25 / 100 = 0.25)
        QCOMPARE(activity.getVatRate(), 0.25);
        QCOMPARE(activity.getVatRate_4digits(), QString("0.2500"));
        QCOMPARE(activity.getVatRate_2digits(), QString("0.25"));
    }

    // Case 2: SelfComputed
    {
        Activity activity(
            eventId, activityId, dt, currency, fr, de, Amount(amountTaxed, amountTaxes),
            TaxSource::SelfComputed, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
        );

        // Check getAmountTaxes logic (should return Computed, currently 0.0)
        QCOMPARE(activity.getAmountTaxes(), 0.0);
        QCOMPARE(activity.getAmountUntaxed(), amountTaxed - 0.0);

        // Test setTaxes logic for Other -> SelfComputed
        double newTaxes = 30.0;
        activity.setTaxes(newTaxes);
        QCOMPARE(activity.getTaxSource(), TaxSource::SelfComputed); // Remains SelfComputed
        QCOMPARE(activity.getAmountTaxesComputed(), newTaxes);
        QCOMPARE(activity.getAmountTaxes(), newTaxes);
    }

    // Case 3: ManualOverride
    {
        Activity activity(
            eventId, activityId, dt, currency, fr, de, Amount(amountTaxed, amountTaxes),
            TaxSource::ManualOverride, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
        );

        // Check getAmountTaxes logic (should return Computed, currently 0.0)
        QCOMPARE(activity.getAmountTaxes(), 0.0);

        // Test setTaxes logic for Other -> SelfComputed (if it was ManualOverride, strictly per logic it becomes SelfComputed unless it was MP Provided)
        // Wait, the logic says:
        // if (m_taxSource == TaxSource::MarketplaceProvided) -> ManualOverride
        // else -> SelfComputed
        // So ManualOverride -> SelfComputed. Let's verify this.
        activity.setTaxes(5.0);
        QCOMPARE(activity.getTaxSource(), TaxSource::SelfComputed);
        QCOMPARE(activity.getAmountTaxesComputed(), 5.0);
    }
    
    // Case 4: Unknown -> SelfComputed
    {
        Activity activity(
            eventId, activityId, dt, currency, fr, de, Amount(amountTaxed, amountTaxes),
            TaxSource::Unknown, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
        );

        activity.setTaxes(10.0);
        QCOMPARE(activity.getTaxSource(), TaxSource::SelfComputed);
        QCOMPARE(activity.getAmountTaxesComputed(), 10.0);
    }
}

void TestOrders::test_shipments()
{
    // Create Activity
    Activity activity(
        "evt-ship-001", "act-ship-001", QDateTime::currentDateTime(), "EUR", "FR", "DE",
        Amount(100.0, 20.0), // Taxed: 100, Taxes: 20
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
    );

    // Create LineItems
    // Item 1: 50.0 taxed, 20% VAT = 10.0 tax. Qty 1.
    LineItem item1("SKU1", "Item 1", 50.0, 0.20, 1);
    
    // Item 2: 50.0 taxed, 20% VAT = 10.0 tax. Qty 1.
    LineItem item2("SKU2", "Item 2", 50.0, 0.20, 1);

    QList<LineItem> items;
    items << item1 << item2;

    // Shipment with matching taxes (20.0 total vs 10+10)
    Shipment shipment(activity, items);

    QCOMPARE(shipment.getActivity().getEventId(), "evt-ship-001");
    QCOMPARE(shipment.getItems().size(), 2);
    QCOMPARE(shipment.getItems()[0].getSku(), "SKU1");
    QCOMPARE(shipment.getItems()[0].getTotalTaxes(), 10.0);
    QCOMPARE(shipment.getItems()[1].getTotalTaxes(), 10.0);

    // Case with delta adjustment (small delta < 0.015 per unit)
    // Activity says 20.01 taxes, items say 20.0 (10+10). Delta 0.01.
    // 0.01 / 1 (first item qty) = 0.01 <= 0.015.
    // Should dump 0.01 on the first item.
    Activity activityDelta(
        "evt-ship-002", "act-ship-002", QDateTime::currentDateTime(), "EUR", "FR", "DE",
        Amount(100.0, 20.01), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
    );

    Shipment shipmentDelta(activityDelta, items);
    // adjustItemTexes should add 0.01 to the first item only.
    // First item taxes was 10.0. New should be 10.01.
    QCOMPARE(shipmentDelta.getItems()[0].getTotalTaxes(), 10.01);
    QCOMPARE(shipmentDelta.getItems()[1].getTotalTaxes(), 10.0);

    // Case with large delta adjustment (spread evenly)
    // Activity says 20.20 (+0.20 delta). 0.20 / 1 (first item qty) = 0.20 > 0.015.
    // Should spread 0.20 across 2 items (total qty 2). 0.10 per item.
    Activity activityLargeDelta(
        "evt-ship-003", "act-ship-003", QDateTime::currentDateTime(), "EUR", "FR", "DE",
        Amount(100.0, 20.20),
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
    );

    Shipment shipmentLargeDelta(activityLargeDelta, items);
    
    // Each item should increase by 0.10. Original 10.0 --> 10.10.
    QCOMPARE(shipmentLargeDelta.getItems()[0].getTotalTaxes(), 10.10);
    QCOMPARE(shipmentLargeDelta.getItems()[1].getTotalTaxes(), 10.10);
}

// test_refunds implementation
void TestOrders::test_refunds()
{
    // Activity for refund
    Activity activity(
        "evt-refund-001", "act-refund-001", QDateTime::currentDateTime(), "EUR", "FR", "DE",
        Amount(50.0, 10.0), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
    );

    QList<LineItem> items;
    items << LineItem("SKU1", "Item 1", 50.0, 0.20, 1);

    Refund refund(activity, items);

    QCOMPARE(refund.getActivity().getEventId(), "evt-refund-001");
    // Verify it behaves like a shipment (inherits)
    QCOMPARE(refund.getItems().size(), 1);
}

void TestOrders::test_orders()
{
    // 1. Create Order
    QString orderId = "ORDER-001";
    Order order(orderId);
    QCOMPARE(order.id(), orderId);

    // 2. Set Address
    Address addr(
        "John Doe", "123 Main St", "Apt 4", "Building B", "Paris", "75001", "FR", "Ile-de-France",
        "john@example.com", "+33123456789", "Doe Corp", "FR12345678901"
    );
    order.setAddressTo(addr);

    QVERIFY(order.addressTo().has_value());
    QCOMPARE(order.addressTo()->getFullName(), QString("John Doe"));
    QCOMPARE(order.addressTo()->getAddressLine3(), QString("Building B"));

    // 3. Add Activity (Shipment)
    Activity actShip(
        "evt-ship-001", "act-ship-001", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE",
        Amount(100.0, 20.0), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
    );
    QList<LineItem> shipItems;
    shipItems << LineItem("SKU1", "Item 1", 100.0, 0.20, 1);
    Shipment shipment(actShip, shipItems);

    order.addShipment(&shipment);

    // 4. Add Activity (Refund)
    Activity actRefund(
        "evt-ref-001", "act-ref-001", QDateTime(QDate(2023, 1, 2), QTime(15, 0)), "EUR", "FR", "DE",
        Amount(50.0, 10.0), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
    );
    QList<LineItem> refItems;
    refItems << LineItem("SKU1", "Item 1", 50.0, 0.20, 1);
    Refund refund(actRefund, refItems);

    order.addRefund(&refund);

    // 5. Verify Activities
    const auto &activities = order.activities();
    QCOMPARE(activities.size(), 2);

    // Check mapping by date order
    auto it = activities.begin();
    QCOMPARE(it.key(), QDateTime(QDate(2023, 1, 1), QTime(10, 0)));
    QCOMPARE(it.value()->getId(), QString("act-ship-001"));

    ++it;
    QCOMPARE(it.key(), QDateTime(QDate(2023, 1, 2), QTime(15, 0)));
    QCOMPARE(it.value()->getId(), QString("act-ref-001"));
}




QTEST_MAIN(TestOrders)

#include "tst_testorders.moc"
