#include <QtTest>
#include <QTemporaryDir>
#include "books/PurchaseAmzPaymentsManager.h"
#include "ExceptionWithTitleText.h"

// ─────────────────────────────────────────────────────────────────────────────
// TestAmazonPaymentReports
//
// Tests the decode() / encode() logic of PurchaseAmzPaymentsManager by
// exercising every structural variation that can appear in the payment
// filename format:
//
//   payment_{marketplace}_{YYYY}_{MM}_{DD}
//     __to__{YYYY}_{MM}_{DD}
//     __balance-begin-{amount}{CUR}
//     __balance-end-{amount}{CUR}
//     [__expenses-{amount}{CUR}]           (optional if < 200 EUR proxy)
//     [__refunded-expenses-{amount}{CUR}]  (always optional)
//     __{paid}{CUR}
//
// Part 1: 20 filename test scenarios (exception raised iff proxy > 200 EUR)
// Part 2: 20+ focused VERIFY statements for maximum line coverage
// ─────────────────────────────────────────────────────────────────────────────

// Helper: decode and fail test on unexpected exception
static AmzPaymentInfo mustDecode(QObject *ctx, const QString &fname)
{
    Q_UNUSED(ctx);
    try {
        return PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &e) {
        QTest::qFail(
            qPrintable(QString("Unexpected exception for '%1': %2")
                           .arg(fname, QString::fromUtf8(e.what()))),
            __FILE__, __LINE__);
        return {}; // unreachable, but satisfies the compiler
    }
}

// Helper: decode and assert exception is thrown
static void mustThrow(QObject *ctx, const QString &fname)
{
    Q_UNUSED(ctx);
    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    if (!threw) {
        QFAIL(qPrintable(QString("Expected exception was NOT thrown for '%1'").arg(fname)));
    }
}

class TestAmazonPaymentReports : public QObject
{
    Q_OBJECT

private slots:
    // ── Part 1: 20 filename scenarios ─────────────────────────────────────────

    // 1.  com/USD – all fields present
    void test_01_com_usd_full();
    // 2.  de/EUR – all fields present
    void test_02_de_eur_full();
    // 3.  co_uk/GBP – expenses + refunded, no exception
    void test_03_co_uk_gbp_full();
    // 4.  ca/CAD – expenses present
    void test_04_ca_cad_with_expenses();
    // 5.  co_jp/JPY – expenses present (large JPY but large EUR equivalent)
    void test_05_co_jp_jpy_with_expenses();
    // 6.  com_au/AUD – expenses present
    void test_06_com_au_aud_with_expenses();
    // 7.  com_mx/MXN – expenses present
    void test_07_com_mx_mxn_with_expenses();
    // 8.  se/SEK – expenses present
    void test_08_se_sek_with_expenses();
    // 9.  pl/PLN – expenses present
    void test_09_pl_pln_with_expenses();
    // 10. com_tr/TRY – expenses present
    void test_10_com_tr_try_with_expenses();
    // 11. ae/AED – all fields present
    void test_11_ae_aed_full();
    // 12. sa/SAR – all fields present
    void test_12_sa_sar_full();
    // 13. sg/SGD – all fields present
    void test_13_sg_sgd_full();
    // 14. com_br/BRL – all fields present
    void test_14_com_br_brl_full();
    // 15. in/INR – all fields present
    void test_15_in_inr_full();
    // 16. Missing expenses + small proxy (USD) → no exception
    void test_16_missing_expenses_small_proxy_no_exception();
    // 17. Missing expenses + large proxy (USD) → exception
    void test_17_missing_expenses_large_proxy_exception();
    // 18. Missing expenses + large proxy (GBP) → exception
    void test_18_missing_expenses_large_gbp_exception();
    // 19. Missing expenses, just-below-threshold (JPY) → no exception
    void test_19_missing_expenses_jpy_just_below_threshold();
    // 20. Paid in a different currency from balance → parses correctly
    void test_20_paid_different_currency_eur_on_usd_balance();

    // ── Part 2: 20+ focused VERIFY statements ─────────────────────────────────
    void test_verify_countrycode_various_marketplaces();
    void test_verify_dates_parsed_correctly();
    void test_verify_balance_amounts_and_currencies();
    void test_verify_expenses_parsed_and_flags();
    void test_verify_refunded_expenses_parsed_and_flags();
    void test_verify_paid_amount_and_currency();
    void test_verify_optional_tokens_absent_flags();
    void test_verify_toeur_conversion_rates();
    void test_verify_threshold_boundary_usd();
    void test_verify_threshold_boundary_gbp();
    void test_verify_threshold_boundary_cad();
    void test_verify_threshold_boundary_jpy();
    void test_verify_threshold_boundary_aud();
    void test_verify_threshold_boundary_mxn();
    void test_verify_threshold_boundary_sek();
    void test_verify_threshold_boundary_pln();
    void test_verify_threshold_boundary_try();
    void test_verify_threshold_boundary_aed();
    void test_verify_threshold_boundary_sar();
    void test_verify_threshold_boundary_sgd();
    void test_verify_encode_format();
    void test_verify_encode_decode_several_currencies();
    void test_verify_model_rowcount_and_header();
    void test_verify_invalid_filenames_throw();
    void test_verify_relative_path_uses_dateto_year();
    void test_verify_negative_paid_amount_parses();
};

// ─────────────────────────────────────────────────────────────────────────────
// Part 1 implementations
// ─────────────────────────────────────────────────────────────────────────────

void TestAmazonPaymentReports::test_01_com_usd_full()
{
    QString f = "payment_com_2026_01_07__to__2026_01_21"
                "__balance-begin-1311.19USD"
                "__balance-end-1135.55USD"
                "__expenses-2627.38USD"
                "__refunded-expenses-153.17USD"
                "__177.90USD";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("com"));
    QCOMPARE(info.dateFrom,    QDate(2026,  1,  7));
    QCOMPARE(info.dateTo,      QDate(2026,  1, 21));
    QVERIFY(qAbs(info.balanceStart -  1311.19) < 0.001);
    QCOMPARE(info.balanceStartCurrency, QString("USD"));
    QVERIFY(qAbs(info.balanceEnd - 1135.55) < 0.001);
    QVERIFY(info.hasExpenses);
    QVERIFY(qAbs(info.expenses - 2627.38) < 0.001);
    QCOMPARE(info.expensesCurrency, QString("USD"));
    QVERIFY(info.hasRefundedExpenses);
    QVERIFY(qAbs(info.refundedExpenses - 153.17) < 0.001);
    QCOMPARE(info.refundedExpensesCurrency, QString("USD"));
    QVERIFY(qAbs(info.paid - 177.90) < 0.001);
    QCOMPARE(info.paidCurrency, QString("USD"));
}

void TestAmazonPaymentReports::test_02_de_eur_full()
{
    QString f = "payment_de_2026_02_01__to__2026_02_14"
                "__balance-begin-900.00EUR"
                "__balance-end-700.00EUR"
                "__expenses-450.00EUR"
                "__refunded-expenses-35.97EUR"
                "__185.97EUR";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("de"));
    QCOMPARE(info.balanceStartCurrency, QString("EUR"));
    QVERIFY(info.hasExpenses);
    QVERIFY(qAbs(info.expenses - 450.00) < 0.001);
    QVERIFY(info.hasRefundedExpenses);
    QVERIFY(qAbs(info.refundedExpenses - 35.97) < 0.001);
    QCOMPARE(info.refundedExpensesCurrency, QString("EUR"));
}

void TestAmazonPaymentReports::test_03_co_uk_gbp_full()
{
    QString f = "payment_co_uk_2026_03_01__to__2026_03_14"
                "__balance-begin-800.00GBP"
                "__balance-end-620.00GBP"
                "__expenses-300.00GBP"
                "__refunded-expenses-20.00GBP"
                "__160.00GBP";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("co_uk"));
    QCOMPARE(info.balanceStartCurrency, QString("GBP"));
    QVERIFY(info.hasExpenses);
    QVERIFY(info.hasRefundedExpenses);
    QVERIFY(qAbs(info.paid - 160.00) < 0.001);
}

void TestAmazonPaymentReports::test_04_ca_cad_with_expenses()
{
    QString f = "payment_ca_2026_04_01__to__2026_04_14"
                "__balance-begin-1200.00CAD"
                "__balance-end-900.00CAD"
                "__expenses-400.00CAD"
                "__100.00CAD";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("ca"));
    QCOMPARE(info.expensesCurrency, QString("CAD"));
    QVERIFY(qAbs(info.expenses - 400.00) < 0.001);
    QVERIFY(!info.hasRefundedExpenses);
}

void TestAmazonPaymentReports::test_05_co_jp_jpy_with_expenses()
{
    // 50000 JPY expenses → 310 EUR
    QString f = "payment_co_jp_2026_05_01__to__2026_05_14"
                "__balance-begin-200000.00JPY"
                "__balance-end-140000.00JPY"
                "__expenses-50000.00JPY"
                "__10000.00JPY";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("co_jp"));
    QCOMPARE(info.expensesCurrency, QString("JPY"));
    QVERIFY(qAbs(info.expenses - 50000.00) < 0.001);
}

void TestAmazonPaymentReports::test_06_com_au_aud_with_expenses()
{
    QString f = "payment_com_au_2026_06_01__to__2026_06_14"
                "__balance-begin-1500.00AUD"
                "__balance-end-1100.00AUD"
                "__expenses-500.00AUD"
                "__refunded-expenses-100.00AUD"
                "__300.00AUD";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("com_au"));
    QVERIFY(qAbs(info.refundedExpenses - 100.00) < 0.001);
    QCOMPARE(info.refundedExpensesCurrency, QString("AUD"));
}

void TestAmazonPaymentReports::test_07_com_mx_mxn_with_expenses()
{
    QString f = "payment_com_mx_2026_07_01__to__2026_07_14"
                "__balance-begin-20000.00MXN"
                "__balance-end-14000.00MXN"
                "__expenses-5500.00MXN"
                "__500.00MXN";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("com_mx"));
    QCOMPARE(info.expensesCurrency, QString("MXN"));
    QVERIFY(qAbs(info.expenses - 5500.00) < 0.001);
}

void TestAmazonPaymentReports::test_08_se_sek_with_expenses()
{
    QString f = "payment_se_2026_08_01__to__2026_08_14"
                "__balance-begin-10000.00SEK"
                "__balance-end-7000.00SEK"
                "__expenses-2500.00SEK"
                "__refunded-expenses-300.00SEK"
                "__800.00SEK";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("se"));
    QCOMPARE(info.expensesCurrency, QString("SEK"));
    QVERIFY(qAbs(info.expenses - 2500.00) < 0.001);
    QVERIFY(qAbs(info.refundedExpenses - 300.00) < 0.001);
}

void TestAmazonPaymentReports::test_09_pl_pln_with_expenses()
{
    QString f = "payment_pl_2026_09_01__to__2026_09_14"
                "__balance-begin-5000.00PLN"
                "__balance-end-3500.00PLN"
                "__expenses-1000.00PLN"
                "__500.00PLN";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("pl"));
    QCOMPARE(info.expensesCurrency, QString("PLN"));
}

void TestAmazonPaymentReports::test_10_com_tr_try_with_expenses()
{
    QString f = "payment_com_tr_2026_10_01__to__2026_10_14"
                "__balance-begin-50000.00TRY"
                "__balance-end-35000.00TRY"
                "__expenses-10000.00TRY"
                "__5000.00TRY";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("com_tr"));
    QCOMPARE(info.expensesCurrency, QString("TRY"));
}

void TestAmazonPaymentReports::test_11_ae_aed_full()
{
    QString f = "payment_ae_2026_11_01__to__2026_11_14"
                "__balance-begin-4000.00AED"
                "__balance-end-3000.00AED"
                "__expenses-900.00AED"
                "__refunded-expenses-100.00AED"
                "__300.00AED";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("ae"));
    QCOMPARE(info.expensesCurrency, QString("AED"));
    QVERIFY(qAbs(info.expenses - 900.00) < 0.001);
}

void TestAmazonPaymentReports::test_12_sa_sar_full()
{
    QString f = "payment_sa_2026_11_15__to__2026_11_28"
                "__balance-begin-5000.00SAR"
                "__balance-end-3800.00SAR"
                "__expenses-1000.00SAR"
                "__200.00SAR";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("sa"));
    QCOMPARE(info.expensesCurrency, QString("SAR"));
    QVERIFY(qAbs(info.expenses - 1000.00) < 0.001);
    QVERIFY(!info.hasRefundedExpenses);
}

void TestAmazonPaymentReports::test_13_sg_sgd_full()
{
    QString f = "payment_sg_2026_12_01__to__2026_12_14"
                "__balance-begin-2000.00SGD"
                "__balance-end-1500.00SGD"
                "__expenses-400.00SGD"
                "__refunded-expenses-50.00SGD"
                "__150.00SGD";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("sg"));
    QCOMPARE(info.expensesCurrency, QString("SGD"));
    QVERIFY(qAbs(info.paid - 150.00) < 0.001);
}

void TestAmazonPaymentReports::test_14_com_br_brl_full()
{
    QString f = "payment_com_br_2026_12_15__to__2026_12_28"
                "__balance-begin-8000.00BRL"
                "__balance-end-5500.00BRL"
                "__expenses-1500.00BRL"
                "__refunded-expenses-200.00BRL"
                "__1200.00BRL";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("com_br"));
    QCOMPARE(info.expensesCurrency, QString("BRL"));
    QVERIFY(qAbs(info.refundedExpenses - 200.00) < 0.001);
}

void TestAmazonPaymentReports::test_15_in_inr_full()
{
    // 25000 INR expenses → 275 EUR
    QString f = "payment_in_2026_01_01__to__2026_01_14"
                "__balance-begin-100000.00INR"
                "__balance-end-65000.00INR"
                "__expenses-25000.00INR"
                "__10000.00INR";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.countryCode, QString("in"));
    QCOMPARE(info.expensesCurrency, QString("INR"));
    QVERIFY(qAbs(info.expenses - 25000.00) < 0.001);
}

// 16. Missing expenses, proxy = max(0, 500-490-10) = 0 USD → no exception
void TestAmazonPaymentReports::test_16_missing_expenses_small_proxy_no_exception()
{
    QString f = "payment_com_2026_02_01__to__2026_02_14"
                "__balance-begin-500.00USD"
                "__balance-end-490.00USD"
                "__10.00USD";

    mustDecode(this, f); // must not throw
}

// 17. Missing expenses, proxy = 400 USD → 368 EUR > 200 → exception
void TestAmazonPaymentReports::test_17_missing_expenses_large_proxy_exception()
{
    QString f = "payment_com_2026_03_01__to__2026_03_14"
                "__balance-begin-1000.00USD"
                "__balance-end-500.00USD"
                "__100.00USD";

    mustThrow(this, f);
}

// 18. Missing expenses, proxy = 400 GBP → 464 EUR > 200 → exception
void TestAmazonPaymentReports::test_18_missing_expenses_large_gbp_exception()
{
    QString f = "payment_co_uk_2026_04_01__to__2026_04_14"
                "__balance-begin-1000.00GBP"
                "__balance-end-500.00GBP"
                "__100.00GBP";

    mustThrow(this, f);
}

// 19. JPY: proxy = 30000 JPY → 186 EUR < 200 → no exception
void TestAmazonPaymentReports::test_19_missing_expenses_jpy_just_below_threshold()
{
    QString f = "payment_co_jp_2026_05_01__to__2026_05_14"
                "__balance-begin-100000.00JPY"
                "__balance-end-65000.00JPY"
                "__5000.00JPY";

    mustDecode(this, f); // proxy = 30 000 JPY = 186 EUR < 200 → OK
}

// 20. Paid in EUR while balance is in USD
void TestAmazonPaymentReports::test_20_paid_different_currency_eur_on_usd_balance()
{
    QString f = "payment_com_2026_06_01__to__2026_06_14"
                "__balance-begin-1000.00USD"
                "__balance-end-800.00USD"
                "__expenses-400.00USD"
                "__refunded-expenses-50.00USD"
                "__183.80EUR";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.paidCurrency, QString("EUR"));
    QVERIFY(qAbs(info.paid - 183.80) < 0.001);
    QCOMPARE(info.balanceStartCurrency, QString("USD"));
    QCOMPARE(info.expensesCurrency,     QString("USD"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Part 2: focused VERIFY statements
// ─────────────────────────────────────────────────────────────────────────────

void TestAmazonPaymentReports::test_verify_countrycode_various_marketplaces()
{
    // Each marketplace code must be extracted correctly from the first token
    struct Case { const char *fname; const char *code; };
    const Case cases[] = {
        { "payment_com_2026_01_01__to__2026_01_14__balance-begin-100.00USD__balance-end-100.00USD__0.00USD",     "com"    },
        { "payment_de_2026_01_01__to__2026_01_14__balance-begin-100.00EUR__balance-end-100.00EUR__0.00EUR",      "de"     },
        { "payment_fr_2026_01_01__to__2026_01_14__balance-begin-100.00EUR__balance-end-100.00EUR__0.00EUR",      "fr"     },
        { "payment_it_2026_01_01__to__2026_01_14__balance-begin-100.00EUR__balance-end-100.00EUR__0.00EUR",      "it"     },
        { "payment_es_2026_01_01__to__2026_01_14__balance-begin-100.00EUR__balance-end-100.00EUR__0.00EUR",      "es"     },
        { "payment_nl_2026_01_01__to__2026_01_14__balance-begin-100.00EUR__balance-end-100.00EUR__0.00EUR",      "nl"     },
        { "payment_co_uk_2026_01_01__to__2026_01_14__balance-begin-100.00GBP__balance-end-100.00GBP__0.00GBP",  "co_uk"  },
        { "payment_ca_2026_01_01__to__2026_01_14__balance-begin-100.00CAD__balance-end-100.00CAD__0.00CAD",      "ca"     },
        { "payment_co_jp_2026_01_01__to__2026_01_14__balance-begin-100.00JPY__balance-end-100.00JPY__0.00JPY",  "co_jp"  },
        { "payment_com_au_2026_01_01__to__2026_01_14__balance-begin-100.00AUD__balance-end-100.00AUD__0.00AUD","com_au" },
    };

    for (const auto &c : cases) {
        AmzPaymentInfo info = mustDecode(this, QString::fromLatin1(c.fname));
        QVERIFY2(info.countryCode == QString::fromLatin1(c.code),
                 qPrintable(QString("Expected '%1', got '%2' for '%3'")
                                .arg(c.code, info.countryCode, c.fname)));
    }
}

void TestAmazonPaymentReports::test_verify_dates_parsed_correctly()
{
    QString f = "payment_de_2025_12_31__to__2026_01_14"
                "__balance-begin-100.00EUR__balance-end-100.00EUR__0.00EUR";

    AmzPaymentInfo info = mustDecode(this, f);
    QCOMPARE(info.dateFrom, QDate(2025, 12, 31));
    QCOMPARE(info.dateTo,   QDate(2026,  1, 14));
    QVERIFY(info.dateFrom.isValid());
    QVERIFY(info.dateTo.isValid());
}

void TestAmazonPaymentReports::test_verify_balance_amounts_and_currencies()
{
    QString f = "payment_co_uk_2026_07_01__to__2026_07_14"
                "__balance-begin-1234.56GBP"
                "__balance-end-987.65GBP"
                "__expenses-400.00GBP"
                "__0.00GBP";

    AmzPaymentInfo info = mustDecode(this, f);
    QVERIFY(qAbs(info.balanceStart -  1234.56) < 0.001);
    QVERIFY(qAbs(info.balanceEnd   -   987.65) < 0.001);
    QCOMPARE(info.balanceStartCurrency, QString("GBP"));
    QCOMPARE(info.balanceEndCurrency,   QString("GBP"));
}

void TestAmazonPaymentReports::test_verify_expenses_parsed_and_flags()
{
    QString f_with = "payment_fr_2026_08_01__to__2026_08_14"
                     "__balance-begin-500.00EUR__balance-end-400.00EUR"
                     "__expenses-250.00EUR__0.00EUR";
    AmzPaymentInfo with = mustDecode(this, f_with);
    QVERIFY(with.hasExpenses);
    QVERIFY(qAbs(with.expenses - 250.0) < 0.001);
    QCOMPARE(with.expensesCurrency, QString("EUR"));

    QString f_without = "payment_fr_2026_08_01__to__2026_08_14"
                        "__balance-begin-500.00EUR__balance-end-495.00EUR__5.00EUR";
    AmzPaymentInfo without_exp = mustDecode(this, f_without);
    QVERIFY(!without_exp.hasExpenses);
    QCOMPARE(without_exp.expenses, 0.0);
}

void TestAmazonPaymentReports::test_verify_refunded_expenses_parsed_and_flags()
{
    QString f_with = "payment_de_2026_09_01__to__2026_09_14"
                     "__balance-begin-800.00EUR__balance-end-600.00EUR"
                     "__expenses-300.00EUR__refunded-expenses-50.00EUR__0.00EUR";
    AmzPaymentInfo with = mustDecode(this, f_with);
    QVERIFY(with.hasRefundedExpenses);
    QVERIFY(qAbs(with.refundedExpenses - 50.0) < 0.001);
    QCOMPARE(with.refundedExpensesCurrency, QString("EUR"));

    QString f_without = "payment_de_2026_09_01__to__2026_09_14"
                        "__balance-begin-800.00EUR__balance-end-600.00EUR"
                        "__expenses-300.00EUR__0.00EUR";
    AmzPaymentInfo without_ref = mustDecode(this, f_without);
    QVERIFY(!without_ref.hasRefundedExpenses);
    QCOMPARE(without_ref.refundedExpenses, 0.0);
}

void TestAmazonPaymentReports::test_verify_paid_amount_and_currency()
{
    QString f = "payment_com_2026_10_01__to__2026_10_14"
                "__balance-begin-500.00USD__balance-end-400.00USD"
                "__expenses-300.00USD__refunded-expenses-50.00USD"
                "__250.55EUR";

    AmzPaymentInfo info = mustDecode(this, f);
    QVERIFY(qAbs(info.paid - 250.55) < 0.001);
    QCOMPARE(info.paidCurrency, QString("EUR"));
}

void TestAmazonPaymentReports::test_verify_optional_tokens_absent_flags()
{
    // Both absent, trivial balance change
    QString f = "payment_es_2026_11_01__to__2026_11_14"
                "__balance-begin-100.00EUR__balance-end-99.00EUR__1.00EUR";
    AmzPaymentInfo info = mustDecode(this, f);
    QVERIFY(!info.hasExpenses);
    QVERIFY(!info.hasRefundedExpenses);
    QCOMPARE(info.expenses,          0.0);
    QCOMPARE(info.refundedExpenses,  0.0);
    QVERIFY(info.expensesCurrency.isEmpty());
    QVERIFY(info.refundedExpensesCurrency.isEmpty());
}

void TestAmazonPaymentReports::test_verify_toeur_conversion_rates()
{
    // Verify rate values match the constants in the implementation
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "EUR") - 1.00)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "USD") - 0.92)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "GBP") - 1.16)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "CAD") - 0.68)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "AUD") - 0.60)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "MXN") - 0.046) < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "SEK") - 0.087) < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "PLN") - 0.23)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "TRY") - 0.027) < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "AED") - 0.25)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "SAR") - 0.24)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "SGD") - 0.69)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "BRL") - 0.17)  < 0.0001);
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "INR") - 0.011) < 0.0001);
    // Unknown currency defaults to same value
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(1.0, "XXX") - 1.0)  < 0.0001);
}

// ── Threshold boundaries per currency ────────────────────────────────────────
// For each currency, we test:
//   - proxy just below threshold → no exception
//   - proxy just above threshold → exception
// proxy = max(0, balStart - balEnd - paid)  (all in same currency)

static void checkThreshold(QObject *ctx, const QString &marketplace,
                            const QString &currency,
                            double justBelow, double justAbove,
                            double balStart = 2000.0)
{
    // just-below: proxy = justBelow (no exception expected)
    double balEnd_below = balStart - justBelow;
    QString f_below = QString(
        "payment_%1_2026_01_01__to__2026_01_14"
        "__balance-begin-%2%3"
        "__balance-end-%4%3"
        "__0.00%3")
        .arg(marketplace)
        .arg(balStart, 0, 'f', 2)
        .arg(currency)
        .arg(balEnd_below, 0, 'f', 2);

    bool threw_below = false;
    try { PurchaseAmzPaymentsManager::decode(f_below); }
    catch (const ExceptionWithTitleText &) { threw_below = true; }

    if (threw_below) {
        QFAIL(qPrintable(QString("%1 just-below: unexpected exception (proxy=%2 %3)")
                             .arg(currency).arg(justBelow).arg(currency)));
    }

    // just-above: proxy = justAbove (exception expected)
    double balEnd_above = balStart - justAbove;
    if (balEnd_above < 0)
        balStart = justAbove * 1.5;
    balEnd_above = balStart - justAbove;

    QString f_above = QString(
        "payment_%1_2026_01_01__to__2026_01_14"
        "__balance-begin-%2%3"
        "__balance-end-%4%3"
        "__0.00%3")
        .arg(marketplace)
        .arg(balStart, 0, 'f', 2)
        .arg(currency)
        .arg(balEnd_above, 0, 'f', 2);

    bool threw_above = false;
    try { PurchaseAmzPaymentsManager::decode(f_above); }
    catch (const ExceptionWithTitleText &) { threw_above = true; }

    if (!threw_above) {
        QFAIL(qPrintable(QString("%1 just-above: exception NOT thrown (proxy=%2 %3)")
                             .arg(currency).arg(justAbove).arg(currency)));
    }
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_usd()
{
    // 200 EUR / 0.92 ≈ 217.39 USD
    // just below: 217 USD → 199.64 EUR < 200 → OK
    // just above: 218 USD → 200.56 EUR > 200 → throw
    checkThreshold(this, "com", "USD", 217.0, 218.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_gbp()
{
    // 200 EUR / 1.16 ≈ 172.41 GBP
    checkThreshold(this, "co_uk", "GBP", 172.0, 173.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_cad()
{
    // 200 EUR / 0.68 ≈ 294.12 CAD
    checkThreshold(this, "ca", "CAD", 294.0, 295.0, 5000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_jpy()
{
    // 200 EUR / 0.0062 ≈ 32258 JPY
    checkThreshold(this, "co_jp", "JPY", 32000.0, 33000.0, 100000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_aud()
{
    // 200 EUR / 0.60 ≈ 333.33 AUD
    checkThreshold(this, "com_au", "AUD", 333.0, 334.0, 5000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_mxn()
{
    // 200 EUR / 0.046 ≈ 4347.8 MXN
    checkThreshold(this, "com_mx", "MXN", 4300.0, 4400.0, 50000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_sek()
{
    // 200 EUR / 0.087 ≈ 2298.9 SEK
    checkThreshold(this, "se", "SEK", 2290.0, 2310.0, 30000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_pln()
{
    // 200 EUR / 0.23 ≈ 869.6 PLN
    checkThreshold(this, "pl", "PLN", 869.0, 870.0, 10000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_try()
{
    // 200 EUR / 0.027 ≈ 7407 TRY
    checkThreshold(this, "com_tr", "TRY", 7400.0, 7410.0, 100000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_aed()
{
    // 200 EUR / 0.25 = 800 AED
    checkThreshold(this, "ae", "AED", 799.0, 801.0, 10000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_sar()
{
    // 200 EUR / 0.24 ≈ 833.3 SAR
    checkThreshold(this, "sa", "SAR", 833.0, 834.0, 10000.0);
}

void TestAmazonPaymentReports::test_verify_threshold_boundary_sgd()
{
    // 200 EUR / 0.69 ≈ 289.9 SGD
    checkThreshold(this, "sg", "SGD", 289.0, 291.0, 5000.0);
}

void TestAmazonPaymentReports::test_verify_encode_format()
{
    AmzPaymentInfo info;
    info.countryCode          = "com";
    info.dateFrom             = QDate(2026, 1, 7);
    info.dateTo               = QDate(2026, 1, 21);
    info.balanceStart         = 1311.19;
    info.balanceStartCurrency = "USD";
    info.hasBalanceStart      = true;
    info.balanceEnd           = 1135.55;
    info.balanceEndCurrency   = "USD";
    info.hasBalanceEnd        = true;
    info.hasExpenses          = true;
    info.expenses             = 2627.38;
    info.expensesCurrency     = "USD";
    info.hasRefundedExpenses  = true;
    info.refundedExpenses     = 153.17;
    info.refundedExpensesCurrency = "USD";
    info.paid                 = 177.90;
    info.paidCurrency         = "USD";

    QString encoded = PurchaseAmzPaymentsManager::encode(info);

    QVERIFY(encoded.startsWith("payment_com_2026_01_07__to__2026_01_21"));
    QVERIFY(encoded.contains("__balance-begin-1311.19USD"));
    QVERIFY(encoded.contains("__balance-end-1135.55USD"));
    QVERIFY(encoded.contains("__expenses-2627.38USD"));
    QVERIFY(encoded.contains("__refunded-expenses-153.17USD"));
    QVERIFY(encoded.endsWith("__177.90USD"));
}

void TestAmazonPaymentReports::test_verify_encode_decode_several_currencies()
{
    struct Case {
        const char *code; const char *cur;
        double bs, be, exp, ref, paid;
    };
    const Case cases[] = {
        { "de",     "EUR", 1000.0, 800.0, 300.0, 50.0, 50.0 },
        { "co_uk",  "GBP",  900.0, 700.0, 250.0, 30.0, 80.0 },
        { "ca",     "CAD", 1200.0, 900.0, 400.0,  0.0, 100.0 },
        { "se",     "SEK", 9000.0, 6000.0, 2500.0, 300.0, 700.0 },
    };

    for (const auto &c : cases) {
        AmzPaymentInfo orig;
        orig.countryCode          = c.code;
        orig.dateFrom             = QDate(2026, 3, 1);
        orig.dateTo               = QDate(2026, 3, 14);
        orig.balanceStart         = c.bs;
        orig.balanceStartCurrency = c.cur;
        orig.hasBalanceStart      = true;
        orig.balanceEnd           = c.be;
        orig.balanceEndCurrency   = c.cur;
        orig.hasBalanceEnd        = true;
        orig.hasExpenses          = true;
        orig.expenses             = c.exp;
        orig.expensesCurrency     = c.cur;
        orig.hasRefundedExpenses  = (c.ref > 0.0);
        orig.refundedExpenses     = c.ref;
        orig.refundedExpensesCurrency = c.cur;
        orig.paid                 = c.paid;
        orig.paidCurrency         = c.cur;

        QString enc = PurchaseAmzPaymentsManager::encode(orig);
        AmzPaymentInfo dec = PurchaseAmzPaymentsManager::decode(enc);

        QCOMPARE(dec.countryCode, orig.countryCode);
        QVERIFY(qAbs(dec.balanceStart - orig.balanceStart) < 0.01);
        QVERIFY(qAbs(dec.expenses     - orig.expenses)     < 0.01);
        QVERIFY(qAbs(dec.paid         - orig.paid)         < 0.01);
        QCOMPARE(dec.paidCurrency, orig.paidCurrency);
        QCOMPARE(dec.hasRefundedExpenses, orig.hasRefundedExpenses);
        if (orig.hasRefundedExpenses)
            QVERIFY(qAbs(dec.refundedExpenses - orig.refundedExpenses) < 0.01);
    }
}

void TestAmazonPaymentReports::test_verify_model_rowcount_and_header()
{
    // Manager on a working dir with no 'amazon-payments' sub-dir → rowCount = 0
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    PurchaseAmzPaymentsManager mgr(QDir(tmpDir.path()));

    QCOMPARE(mgr.rowCount(),    0);
    QCOMPARE(mgr.columnCount(), 8);

    // Header names
    QCOMPARE(mgr.headerData(0, Qt::Horizontal).toString(), QString("Country"));
    QCOMPARE(mgr.headerData(1, Qt::Horizontal).toString(), QString("Date From"));
    QCOMPARE(mgr.headerData(2, Qt::Horizontal).toString(), QString("Date To"));
    QCOMPARE(mgr.headerData(3, Qt::Horizontal).toString(), QString("Balance Start"));
    QCOMPARE(mgr.headerData(4, Qt::Horizontal).toString(), QString("Balance End"));
    QCOMPARE(mgr.headerData(5, Qt::Horizontal).toString(), QString("Expenses"));
    QCOMPARE(mgr.headerData(6, Qt::Horizontal).toString(), QString("Refunded"));
    QCOMPARE(mgr.headerData(7, Qt::Horizontal).toString(), QString("Paid"));
}

void TestAmazonPaymentReports::test_verify_invalid_filenames_throw()
{
    // Too few parts
    auto tryDecode = [](const QString &fname) -> bool {
        bool threw = false;
        try { PurchaseAmzPaymentsManager::decode(fname); }
        catch (const ExceptionWithTitleText &) { threw = true; }
        return threw;
    };

    // Missing "to" keyword
    QVERIFY(tryDecode("payment_com_2026_01_07__XX__2026_01_21"
                      "__balance-begin-100.00USD__balance-end-100.00USD__0.00USD"));

    // Bad first-part format
    QVERIFY(tryDecode("2026_01_07__to__2026_01_21"
                      "__balance-begin-100.00USD__balance-end-100.00USD__0.00USD"));

    // Bad balance-begin (missing prefix)
    QVERIFY(tryDecode("payment_com_2026_01_07__to__2026_01_21"
                      "__begin-100.00USD__balance-end-100.00USD__0.00USD"));

    // Too few parts overall (needs at least 4 parts, so give it 3 parts here to throw)
    QVERIFY(tryDecode("payment_com_2026_01_07__to__2026_01_21"));

    // Bad date-from (30th of Feb)
    QVERIFY(tryDecode("payment_com_2026_02_30__to__2026_02_28"
                      "__balance-begin-100.00USD__balance-end-100.00USD__0.00USD"));
}

void TestAmazonPaymentReports::test_verify_relative_path_uses_dateto_year()
{
    // Same-year payment: folder = dateTo year (2026)
    AmzPaymentInfo sameYear;
    sameYear.dateFrom = QDate(2026, 1, 7);
    sameYear.dateTo   = QDate(2026, 1, 21);
    QCOMPARE(PurchaseAmzPaymentsManager::getRelativePath(sameYear),
             QString("amazon-payments/2026"));

    // Cross-year payment (Dec 2025 → Jan 2026): folder must be 2026, not 2025
    AmzPaymentInfo crossYear;
    crossYear.dateFrom = QDate(2025, 12, 23);
    crossYear.dateTo   = QDate(2026, 1, 20);
    QCOMPARE(PurchaseAmzPaymentsManager::getRelativePath(crossYear),
             QString("amazon-payments/2026"));
}

void TestAmazonPaymentReports::test_verify_negative_paid_amount_parses()
{
    // A negative paid amount (e.g. Amazon claws back money) must decode without throwing.
    // Filename token: "-487.84SEK"
    const QString fname =
        "payment_se_2026_01_07__to__2026_01_21"
        "__balance-begin-1000.00SEK__balance-end-500.00SEK__-487.84SEK";

    AmzPaymentInfo info;
    bool threw = false;
    try {
        info = PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY(!threw);
    QVERIFY(qAbs(info.paid - (-487.84)) < 0.01);
    QCOMPARE(info.paidCurrency, QString("SEK"));
}

QTEST_MAIN(TestAmazonPaymentReports)
#include "test_amz_payment_reports.moc"
