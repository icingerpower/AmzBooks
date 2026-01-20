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
#include "model/VatTerritoryResolver.h"
#include "model/TaxResolver.h"
#include "model/SaleType.h"
#include "model/InvoicingInfo.h"

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
    void test_vatTerritoryResolver();
    void test_getTaxContext();
    void test_isEuMember();
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
        auto result = Activity::create(
            eventId, activityId, dt, currency, fr, de, de, Amount(amountTaxed, amountTaxes),
            TaxSource::MarketplaceProvided, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
        );
        QVERIFY(result.ok());
        Activity activity = *result.value;

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
        auto result = Activity::create(
            eventId, activityId, dt, currency, fr, de, de, Amount(amountTaxed, amountTaxes),
            TaxSource::SelfComputed, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
        );
        QVERIFY(result.ok());
        Activity activity = *result.value;

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
        auto result = Activity::create(
            eventId, activityId, dt, currency, fr, de, de, Amount(amountTaxed, amountTaxes),
            TaxSource::ManualOverride, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
        );
        QVERIFY(result.ok());
        Activity activity = *result.value;

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
        auto result = Activity::create(
            eventId, activityId, dt, currency, fr, de, de, Amount(amountTaxed, amountTaxes),
            TaxSource::Unknown, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
        );
        QVERIFY(result.ok());
        Activity activity = *result.value;

        activity.setTaxes(10.0);
        QCOMPARE(activity.getTaxSource(), TaxSource::SelfComputed);
        QCOMPARE(activity.getAmountTaxesComputed(), 10.0);
    }
}

void TestOrders::test_shipments()
{
    // Create Activity
    auto result = Activity::create(
        "evt-ship-001", "act-ship-001", QDateTime::currentDateTime(), "EUR", "FR", "DE", "DE",
        Amount(100.0, 20.0), // Taxed: 100, Taxes: 20
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    );
    QVERIFY(result.ok());
    Activity activity = *result.value;

    // Create LineItems
    // Item 1: 50.0 taxed, 20% VAT = 10.0 tax. Qty 1.
    // Item 1: 50.0 taxed, 20% VAT = 10.0 tax. Qty 1.
    auto resItem1 = LineItem::create("SKU1", "Item 1", 50.0, 0.20, 1);
    QVERIFY(resItem1.ok());
    LineItem item1 = *resItem1.value;
    
    // Item 2: 50.0 taxed, 20% VAT = 10.0 tax. Qty 1.
    auto resItem2 = LineItem::create("SKU2", "Item 2", 50.0, 0.20, 1);
    QVERIFY(resItem2.ok());
    LineItem item2 = *resItem2.value;

    QList<LineItem> items;
    items << item1 << item2;

    // Shipment with matching taxes (20.0 total vs 10+10)
    // Shipment no longer holds items.
    Shipment shipment(activity);

    QCOMPARE(shipment.getActivity().getEventId(), "evt-ship-001");
    
    // Verify InvoicingInfo handles items
    InvoicingInfo invInfo(&shipment, items);
    QCOMPARE(invInfo.getItems().size(), 2);
    QCOMPARE(invInfo.getItems()[0].getSku(), "SKU1");
    QCOMPARE(invInfo.getItems()[0].getTotalTaxes(), 10.0);
    QCOMPARE(invInfo.getItems()[1].getTotalTaxes(), 10.0);

    // Case with delta adjustment (small delta < 0.015 per unit)
    // Activity says 20.01 taxes, items say 20.0 (10+10). Delta 0.01.
    // 0.01 / 1 (first item qty) = 0.01 <= 0.015.
    // Should dump 0.01 on the first item.
    auto resultDelta = Activity::create(
        "evt-ship-002", "act-ship-002", QDateTime::currentDateTime(), "EUR", "FR", "DE", "DE",
        Amount(100.0, 20.01), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    );
    QVERIFY(resultDelta.ok());
    Activity activityDelta = *resultDelta.value;

    Shipment shipmentDelta(activityDelta);
    
    // Create InvoicingInfo to trigger tax adjustment, passing items explicitly
    InvoicingInfo invInfoDelta(&shipmentDelta, items);
    
    // adjustItemTaxes should add 0.01 to the first item only.
    // First item taxes was 10.0. New should be 10.01.
    QCOMPARE(invInfoDelta.getItems()[0].getTotalTaxes(), 10.01);
    QCOMPARE(invInfoDelta.getItems()[1].getTotalTaxes(), 10.0);

    // Case with large delta adjustment (spread evenly)
    // Activity says 20.20 (+0.20 delta). 0.20 / 1 (first item qty) = 0.20 > 0.015.
    // Should spread 0.20 across 2 items (total qty 2). 0.10 per item.
    auto resultLargeDelta = Activity::create(
        "evt-ship-003", "act-ship-003", QDateTime::currentDateTime(), "EUR", "FR", "DE", "DE",
        Amount(100.0, 20.20),
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    );
    QVERIFY(resultLargeDelta.ok());
    Activity activityLargeDelta = *resultLargeDelta.value;

    Shipment shipmentLargeDelta(activityLargeDelta);
    InvoicingInfo invInfoLargeDelta(&shipmentLargeDelta, items);
    
    // Each item should increase by 0.10. Original 10.0 --> 10.10.
    QCOMPARE(invInfoLargeDelta.getItems()[0].getTotalTaxes(), 10.10);
    QCOMPARE(invInfoLargeDelta.getItems()[1].getTotalTaxes(), 10.10);
}

// test_refunds implementation
void TestOrders::test_refunds()
{
    // Activity for refund
    auto result = Activity::create(
        "evt-refund-001", "act-refund-001", QDateTime::currentDateTime(), "EUR", "FR", "DE", "DE",
        Amount(50.0, 10.0), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    );
    QVERIFY(result.ok());
    Activity activity = *result.value;



    QList<LineItem> items;
    auto resItem = LineItem::create("SKU1", "Item 1", 50.0, 0.20, 1);
    QVERIFY(resItem.ok());
    items << *resItem.value;

    Refund refund(activity); // Refund no longer takes items

    QCOMPARE(refund.getActivity().getEventId(), "evt-refund-001");
    // Verify InvoicingInfo with Refund
    InvoicingInfo invInfo(&refund, items);
    QCOMPARE(invInfo.getItems().size(), 1);
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
    auto resultShip = Activity::create(
        "evt-ship-001", "act-ship-001", QDateTime(QDate(2023, 1, 1), QTime(10, 0)), "EUR", "FR", "DE", "DE",
        Amount(100.0, 20.0), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    );
    QVERIFY(resultShip.ok());
    Activity actShip = *resultShip.value;


    // Shipment no longer takes items
    Shipment shipment(actShip);

    order.addShipment(&shipment);

    // 4. Add Activity (Refund)
    auto resultRef = Activity::create(
        "evt-ref-001", "act-ref-001", QDateTime(QDate(2023, 1, 2), QTime(15, 0)), "EUR", "FR", "DE", "DE",
        Amount(50.0, 10.0), 
        TaxSource::MarketplaceProvided, "DE", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    );
    QVERIFY(resultRef.ok());
    Activity actRefund = *resultRef.value;
    
    // Refund no longer takes items
    Refund refund(actRefund);

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

void TestOrders::test_vatTerritoryResolver()
{
    // Temporary dir for test file
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    VatTerritoryResolver resolver(tempDir.path());
    
    // Check initial content (should be populated by _fillIfEmpty)
    QVERIFY(resolver.rowCount() > 0);
    
    // 1. ES Canary 35000 (Positive)
    QVERIFY(!resolver.getTerritoryId("ES", "35000", "Las Palmas").isEmpty());
    // 2. ES Canary 38000 (Positive)
    QVERIFY(!resolver.getTerritoryId("ES", "38000", "Santa Cruz").isEmpty());
    // 3. ES Melilla 52000 (Positive)
    QVERIFY(!resolver.getTerritoryId("ES", "52000", "Melilla").isEmpty());
    // 4. ES Ceuta 51000 (Positive)
    QVERIFY(!resolver.getTerritoryId("ES", "51000", "Ceuta").isEmpty());
    // 5. IT Livigno 23041 (Positive - with city check)
    QVERIFY(!resolver.getTerritoryId("IT", "23041", "Livigno").isEmpty());
    // 6. IT Livigno Match case insensitive
    QVERIFY(!resolver.getTerritoryId("IT", "23041", "LIVIGNO").isEmpty());
    // 7. IT Campione 22061 (Positive - with city check)
    // Actually in my impl I added "Campione", let's check exact string used
    QVERIFY(!resolver.getTerritoryId("IT", "22061", "Campione d'Italia").isEmpty());
    // 8. DE Heligoland 27498
    QVERIFY(!resolver.getTerritoryId("DE", "27498", "Helgoland").isEmpty());
    // 9. DE Busingen 78266
    QVERIFY(!resolver.getTerritoryId("DE", "78266", "Busingen").isEmpty());
    // 10. GR Mount Athos 63086
    QVERIFY(!resolver.getTerritoryId("GR", "63086", "Karyes").isEmpty());
    // 11. FR Guadeloupe 97100
    QVERIFY(!resolver.getTerritoryId("FR", "97100", "Guadeloupe").isEmpty());
    // 12. FR Martinique 97200
    QVERIFY(!resolver.getTerritoryId("FR", "97200", "Martinique").isEmpty());
    // 13. FR Guyane 97300
    QVERIFY(!resolver.getTerritoryId("FR", "97300", "Guyane").isEmpty());
    // 14. FR Reunion 97400
    QVERIFY(!resolver.getTerritoryId("FR", "97400", "Reunion").isEmpty());
    // 15. FR Mayotte 97600
    // 14. FR Mayotte
    QVERIFY(!resolver.getTerritoryId("FR", "97600", "Mayotte").isEmpty());
    // 16. FI Aland 22100
    QVERIFY(!resolver.getTerritoryId("FI", "22100", "Aland").isEmpty());
    // 17. GB Channel Island (Jersey)
    QVERIFY(!resolver.getTerritoryId("GB", "JE1 1AA", "Jersey").isEmpty());
    // 18. GB Channel Island (Guernsey)
    QVERIFY(!resolver.getTerritoryId("GB", "GY1 1AA", "Guernsey").isEmpty());
    // 19. ES extra 35002 (Positive - loop added)
    QVERIFY(!resolver.getTerritoryId("ES", "35002", "Gran Canaria").isEmpty());
    // 20. ES extra 35010 (Positive - loop added)
    QVERIFY(!resolver.getTerritoryId("ES", "35010", "Gran Canaria").isEmpty());
    
    // New additions checks
    // 20a. IT Vatican
    QVERIFY(!resolver.getTerritoryId("IT", "00120", "Citta del Vaticano").isEmpty());
    // 20b. IT San Marino
    QVERIFY(!resolver.getTerritoryId("IT", "47890", "San Marino").isEmpty());
    // 20c. FI Aland range
    QVERIFY(!resolver.getTerritoryId("FI", "22999", "Aland").isEmpty());
    // 20d. ES Canary end of range
    QVERIFY(!resolver.getTerritoryId("ES", "35999", "Las Palmas").isEmpty());
    // 20e. FR Saint Barthelemy
    QVERIFY(!resolver.getTerritoryId("FR", "97133", "Saint-Barthelemy").isEmpty());

    // 21-25 More Positive
    // Add custom ones to test persistence logic
    resolver.addTerritory("XX", "99999", "Wonderland", "T1");
    QVERIFY(!resolver.getTerritoryId("XX", "99999", "Wonderland").isEmpty()); // 21
    
    resolver.addTerritory("YY", "88888", "", "T2");
    QVERIFY(!resolver.getTerritoryId("YY", "88888", "AnyCity").isEmpty()); // 22

    // Check persistence by reloading in a new resolver
    VatTerritoryResolver resolver2(tempDir.path());
    QVERIFY(!resolver2.getTerritoryId("XX", "99999", "Wonderland").isEmpty()); // 23
    QVERIFY(!resolver2.getTerritoryId("YY", "88888", "OtherCity").isEmpty()); // 24
    
    // Check editing via model interface
    // Find row for XX
    int row = -1;
    for(int i=0; i<resolver2.rowCount(); ++i) {
        if (resolver2.data(resolver2.index(i, 0)).toString() == "XX") {
            row = i;
            break;
        }
    }
    QVERIFY(row != -1);
    // Change postal code to 00000
    // Change postal code to 00000
    resolver2.setData(resolver2.index(row, 2), "00000");
    QVERIFY(!resolver2.getTerritoryId("XX", "00000", "Wonderland").isEmpty()); // 25
    

    // Negatives
    // 26. ES Madrid (Not Canary)
    QVERIFY(resolver.getTerritoryId("ES", "28001", "Madrid").isEmpty());
    // 27. FR Paris
    QVERIFY(resolver.getTerritoryId("FR", "75001", "Paris").isEmpty());
    // 28. IT Rome
    QVERIFY(resolver.getTerritoryId("IT", "00100", "Rome").isEmpty());
    // 29. DE Berlin
    QVERIFY(resolver.getTerritoryId("DE", "10115", "Berlin").isEmpty());
    // 30. IT Livigno wrong city
    // In our implementation: m_countryCode_postalCode_cityPattern["IT"]["23041"] == "Livigno"
    // isVatTerritory checks: city.contains("Livigno")
    // If I pass "Milan", contains is false.
    QVERIFY(resolver.getTerritoryId("IT", "23041", "Milan").isEmpty());
}

void TestOrders::test_getTaxContext()
{
    // Temporary dir for TaxResolver
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    TaxResolver resolver(tempDir.path());
    QDateTime now = QDateTime::currentDateTime();

    // --- B2C Cases (isToBusiness = false) ---

    // 1. Domestic: FR -> FR
    {
        auto ctx = resolver.getTaxContext(now, "FR", "FR", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::DomesticVat);
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
        QCOMPARE(ctx.taxJurisdictionLevel, TaxJurisdictionLevel::Country);
    }

    // 2. Intra-EU: FR -> DE
    {
        auto ctx = resolver.getTaxContext(now, "FR", "DE", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::EuOssUnion);
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
        QCOMPARE(ctx.taxJurisdictionLevel, TaxJurisdictionLevel::Country);
    }

    // 3. Export: FR -> US
    {
        auto ctx = resolver.getTaxContext(now, "FR", "US", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::Exempt);
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
        QCOMPARE(ctx.taxJurisdictionLevel, TaxJurisdictionLevel::Country);
    }

    // 4. Import: US -> FR
    {
        auto ctx = resolver.getTaxContext(now, "US", "FR", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::ImportVat);
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
        QCOMPARE(ctx.taxJurisdictionLevel, TaxJurisdictionLevel::Country);
    }

    // 5. Territory Export: FR -> IT-LIVIGNO
    {
        auto ctx = resolver.getTaxContext(now, "FR", "IT", SaleType::Products, false, false, "", "IT-LIVIGNO");
        QCOMPARE(ctx.taxScheme, TaxScheme::Exempt);
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
    }

    // 6. Territory Import: IT-LIVIGNO -> IT
    {
        auto ctx = resolver.getTaxContext(now, "IT", "IT", SaleType::Products, false, false, "IT-LIVIGNO", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::ImportVat);
        QCOMPARE(ctx.taxDeclaringCountryCode, "IT");
    }

    // 7. Territory Domestic: IT-LIVIGNO -> IT-LIVIGNO
    {
        // Livigno -> Livigno is OutOfScope since it is not Origin EU
        auto ctx = resolver.getTaxContext(now, "IT", "IT", SaleType::Products, false, false, "IT-LIVIGNO", "IT-LIVIGNO");
        QCOMPARE(ctx.taxScheme, TaxScheme::OutOfScope);
    }

    // 8. Out of Scope: US -> US
    {
        auto ctx = resolver.getTaxContext(now, "US", "US", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::OutOfScope);
    }

    // --- B2B Cases (isToBusiness = true) ---

    // 9. Intra-EU B2B: FR -> DE
    {
        auto ctx = resolver.getTaxContext(now, "FR", "DE", SaleType::Products, true, false, "", "");
        // Should be Exempt (Intra-Community Supply)
        QCOMPARE(ctx.taxScheme, TaxScheme::Exempt); 
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
    }

    // 10. Import B2B: US -> FR
    {
        auto ctx = resolver.getTaxContext(now, "US", "FR", SaleType::Products, true, false, "", "");
        // Should be ReverseChargeImport (Postponed Accounting)
        QCOMPARE(ctx.taxScheme, TaxScheme::ReverseChargeImport);
        // And declared/accounted in Destination
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
    }

    // 11. Domestic B2B: FR -> FR (Standard, unless specific RC)
    {
        auto ctx = resolver.getTaxContext(now, "FR", "FR", SaleType::Products, true, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::DomesticVat);
    }
}

void TestOrders::test_isEuMember()
{
    // Standard EU
    QVERIFY(TaxResolver::isEuMember("FR", QDate(2020, 1, 1)));
    QVERIFY(TaxResolver::isEuMember("DE", QDate(2025, 1, 1)));
    
    // Non-EU
    QVERIFY(!TaxResolver::isEuMember("US", QDate(2020, 1, 1)));
    QVERIFY(!TaxResolver::isEuMember("CH", QDate(2025, 1, 1)));

    // Brexit GB
    // Member before 2021
    QVERIFY(TaxResolver::isEuMember("GB", QDate(2020, 12, 31)));
    // Non-member from 2021
    QVERIFY(!TaxResolver::isEuMember("GB", QDate(2021, 1, 1)));
}




QTEST_MAIN(TestOrders)

#include "tst_testorders.moc"
