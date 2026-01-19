#include <QtTest>
#include <QCoreApplication>
#include "model/Activity.h"
#include "model/TaxSource.h"
#include "model/TaxScheme.h"
#include "model/TaxJurisdictionLevel.h"

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
    QString currency = "EUR";
    QString fr = "FR";
    QString de = "DE";
    double amountTaxed = 100.0;
    double amountTaxes = 20.0;
    QString taxDeclaring = "DE";

    // Case 1: MarketplaceProvided
    {
        Activity activity(
            eventId, dt, currency, fr, de, amountTaxed, amountTaxes,
            TaxSource::MarketplaceProvided, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
        );

        QCOMPARE(activity.getEventId(), eventId);
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

        // Test setTaxes logic for MarketplaceProvided -> ManualOverride
        double newTaxes = 25.0;
        activity.setTaxes(newTaxes);
        QCOMPARE(activity.getTaxSource(), TaxSource::ManualOverride);
        QCOMPARE(activity.getAmountTaxesComputed(), newTaxes);
        QCOMPARE(activity.getAmountTaxes(), newTaxes);
    }

    // Case 2: SelfComputed
    {
        Activity activity(
            eventId, dt, currency, fr, de, amountTaxed, amountTaxes,
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
            eventId, dt, currency, fr, de, amountTaxed, amountTaxes,
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
            eventId, dt, currency, fr, de, amountTaxed, amountTaxes,
            TaxSource::Unknown, taxDeclaring,
            TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country
        );

        activity.setTaxes(10.0);
        QCOMPARE(activity.getTaxSource(), TaxSource::SelfComputed);
        QCOMPARE(activity.getAmountTaxesComputed(), 10.0);
    }
}

QTEST_MAIN(TestOrders)

#include "tst_testorders.moc"
