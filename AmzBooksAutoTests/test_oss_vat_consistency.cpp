// Regression test: the OSS VAT total shown by TaxAmountTable (PaneOrders)
// must match the OSS VAT total produced by JournalEntryFactory::computeGrouping
// (the CSV / bookkeeping generation path).
//
// The two code paths differ in one important detail:
//   - TaxAmountTable::aggregate() converts non-EUR amounts using the
//     *order's own date* as the exchange-rate date.
//   - createEntryOssIoss() converts using the *last day of the month*
//     (entryDate) as the exchange-rate date.
//
// This mismatch means the OSS VAT the user sees in PaneOrders differs from
// what ends up in the accounting CSV, which is confusing and error-prone.
//
// The test loads Q1 2026 from the real working-directory database, computes
// the OSS VAT total both ways, and asserts they are equal.  It is expected
// to FAIL until TaxAmountTable is fixed to use the month-end exchange-rate
// date (like the CSV path does).
//
// Run: ./AmzBooksAutoTests/TestOssVatConsistency
//
// SKIP: if the working-directory database is not available at
//       AMZBOOKS_WORKING_DIR or the hard-coded default path.

#include <QtTest>
#include <QDir>
#include <QDate>
#include <QMultiMap>
#include <QMap>

#include "orders/OrderManager.h"
#include "orders/TaxAmountTable.h"
#include "orders/ActivitySource.h"
#include "orders/Shipment.h"
#include "books/CompanyInfosTable.h"
#include "books/JournalEntryFactory.h"
#include "books/TaxScheme.h"
#include "CurrencyRateManager.h"

static const char *DEFAULT_WORKING_DIR =
    "/home/cedric/Dropbox/freelancers/projects/workingDirectory/AmzBooks";

class TestOssVatConsistency : public QObject
{
    Q_OBJECT

private slots:
    void test_oss_pane_orders_matches_csv_q1_2026();
};

// ---------------------------------------------------------------------------
// Helper: OSS VAT from computeGrouping (CSV path), month-end exchange rates
// ---------------------------------------------------------------------------
static double ossVatCsvMethod(
        OrderManager &orderManager,
        CurrencyRateManager &rateManager,
        const QString &companyCurrency,
        const QDate &dateFrom,
        const QDate &dateTo)
{
    // Collect all shipments (grouped + ungrouped) for the period
    const auto sourceMap = orderManager.getActivitySource_ShipmentAndRefunds(
        dateFrom, dateTo, nullptr);

    // Bucket by calendar month
    QMap<QPair<int,int>, QMultiMap<QDateTime, QSharedPointer<Shipment>>> monthly;
    for (auto it = sourceMap.cbegin(); it != sourceMap.cend(); ++it) {
        for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
            const QDate d = jt.key().date();
            monthly[{d.year(), d.month()}].insert(jt.key(), jt.value());
        }
    }

    double ossTotal = 0.0;
    for (auto it = monthly.cbegin(); it != monthly.cend(); ++it) {
        const int mYear  = it.key().first;
        const int mMonth = it.key().second;
        const QDate entryDate(mYear, mMonth, QDate(mYear, mMonth, 1).daysInMonth());

        const auto groups = JournalEntryFactory::computeGrouping(
            nullptr, it.value(), entryDate);

        double monthOss = 0.0;
        for (const auto &g : groups) {
            if (g.taxScheme != TaxScheme::EuOssUnion
                    && g.taxScheme != TaxScheme::EuOssNonUnion) {
                continue;
            }
            const double cr = (g.currency != companyCurrency)
                ? rateManager.rate(g.currency, companyCurrency, entryDate)
                : 1.0;
            // Mirror the addDebitLeft rounding used in createEntryOssIoss
            const double vatConverted = (g.currency != companyCurrency)
                ? std::round(g.totalVat * cr * 100.0) / 100.0
                : g.totalVat;
            monthOss += vatConverted;
        }
        qDebug() << "  CSV method month" << mYear << "-" << mMonth
                 << ":" << monthOss;
        ossTotal += monthOss;
    }
    return ossTotal;
}

// ---------------------------------------------------------------------------
// Helper: OSS VAT from TaxAmountTable (PaneOrders path), per-order exchange rates
// ---------------------------------------------------------------------------
static double ossVatPaneMethod(
        OrderManager &orderManager,
        CurrencyRateManager &rateManager,
        const QString &companyCurrency,
        const QString &companyCountry,
        const QDate &dateFrom,
        const QDate &dateTo)
{
    const auto data = orderManager.get_channel_site_ShipmentAndRefundsConflicts(
        dateFrom, dateTo);
    TaxAmountTable table(data, &rateManager, companyCurrency, companyCountry);

    // Row layout after prependTotalRows():
    //   index 0 → Total
    //   index 1 → Total OSS   (EuOssUnion + EuOssNonUnion)
    //   index 2 → Total IOSS
    constexpr int OSS_ROW = 1;
    return table.data(table.index(OSS_ROW, TaxAmountTable::COL_AMOUNT_TAXES)).toDouble();
}

// ---------------------------------------------------------------------------
// Test
// ---------------------------------------------------------------------------
void TestOssVatConsistency::test_oss_pane_orders_matches_csv_q1_2026()
{
    // Locate the working directory
    const QString envPath = qEnvironmentVariable("AMZBOOKS_WORKING_DIR");
    const QString wdPath  = envPath.isEmpty() ? QString(DEFAULT_WORKING_DIR) : envPath;
    const QDir    workDir(wdPath);
    if (!workDir.exists()) {
        QSKIP("Working-directory database not found; set AMZBOOKS_WORKING_DIR to enable this test.");
    }

    OrderManager         orderManager(workDir);
    CompanyInfosTable    companyInfo(workDir);
    CurrencyRateManager  rateManager(workDir, companyInfo.getApiKeyFixer());

    const QString companyCurrency = companyInfo.getCurrency();
    const QString companyCountry  = companyInfo.getCompanyCountryCode();

    // Q1 2026
    const QDate dateFrom(2026, 1, 1);
    const QDate dateTo  (2026, 3, 31);

    const double pane = ossVatPaneMethod(
        orderManager, rateManager, companyCurrency, companyCountry, dateFrom, dateTo);
    const double csv  = ossVatCsvMethod(
        orderManager, rateManager, companyCurrency, dateFrom, dateTo);

    qDebug() << "OSS VAT – PaneOrders (per-order rate date):" << pane;
    qDebug() << "OSS VAT – CSV/computeGrouping (month-end rate date):" << csv;
    qDebug() << "Difference (pane - csv):" << (pane - csv);

    // The two paths must agree to within 0.01 EUR (pure floating-point rounding).
    // If this assertion fails it means the exchange-rate dates are inconsistent.
    QVERIFY2(qAbs(pane - csv) < 0.01,
             qPrintable(QString("OSS VAT mismatch: PaneOrders=%1  CSV=%2  diff=%3")
                        .arg(pane,  0, 'f', 2)
                        .arg(csv,   0, 'f', 2)
                        .arg(pane - csv, 0, 'f', 2)));
}

QTEST_MAIN(TestOssVatConsistency)
#include "test_oss_vat_consistency.moc"
