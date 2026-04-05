#include <QtTest>
#include <QCoreApplication>
#include <QFile>
#include "orders/OrderManager.h"
#include "orders/ImporterFileAmazonVatEu.h"
#include "orders/ImporterFileAmazonFbaInvoicing.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "books/JournalEntry.h"
#include "books/PurchaseInvoiceManager.h"
#include "books/PurchaseAmzPaymentsManager.h"
#include "ExceptionWithTitleText.h"
#include "books/JournalEntryFactory.h"
#include "books/CompanyInfosTable.h"
#include "books/BooksAccountsSalesTable.h"
#include "books/BookAccountsGroupedSalesTable.h"
#include "books/BookAccountPurchaseTable.h"
#include "books/BookAccountSelfVatTable.h"
#include "books/JournalTable.h"
#include "CurrencyRateManager.h"
#include "orders/ActivitySource.h"
#include "orders/Shipment.h"
#include "books/Activity.h"
#include <QTemporaryDir>
#include <QCoroTask>
#include <QCoroFuture>
#include "books/BankQontoTable.h"
#include "books/BooksConnections.h"
#include "books/AbstractBooksTable.h"
#include "books/AbstractBooksTableBank.h"
#include "banks/AbstractBankStatement.h"
#include "books/AmzPaymentSettings.h"
#include "books/BookAccountAmzBalanceTable.h"
#include "inventory/InventoryMoveTree.h"

// Helper to synchronously wait for QCoro::Task
template <typename T>
T syncWait(QCoro::Task<T> &&task) {
    return QCoro::waitFor<T>(std::move(task));
}

// Returns a BookAccountPurchaseTable backed by a process-lifetime temp dir.
// The table auto-fills with wildcard entries for standard VAT rates (20 %, 10 %, 5.5 %),
// which is enough for filename-parsing tests.
static const BookAccountPurchaseTable &decodeTestPurchaseTable()
{
    static QTemporaryDir s_tempDir;
    static BookAccountPurchaseTable s_table(QDir(s_tempDir.path()), "FR");
    return s_table;
}

class TestBookEntries : public QObject
{
    Q_OBJECT

private slots:
    void test_journal_entry_simple();
    void test_rounding_within_limit();
    void test_rounding_exceeds_limit();
    void test_currency_conversion();
    void test_currency_conversion_custom_delta();
    void test_get_debit_sum();
    void test_get_credit_sum();
    void test_raise_exception_balanced();
    void test_raise_exception_unbalanced();
    void test_multi_currency_entries();
    void test_invoice_encoding();
    void test_invoice_model_loading();
    void test_invoice_decode_error();
    void test_invoice_add();
    void test_get_invoices();
    void test_invoice_save_load_full_fields();
    void test_invoice_proportion_encode_decode();
    void test_invoice_extra_tokens();
    void test_invoice_negative_amount_refund();

    // JournalEntryFactory tests
    void test_factory_purchase_no_conversion();
    void test_factory_purchase_with_conversion();
    void test_factory_purchase_refund();
    void test_factory_shipment_no_conversion();
    void test_factory_shipment_with_conversion();
    void test_factory_shipment_mixed_rates();
    void test_factory_purchase_with_extra();
    void test_factory_single_shipment();
    void test_factory_bank_entry();
    void test_invoice_decode_no_country_code();
    void test_factory_purchase_multi_vat_rates();
    void test_factory_purchase_missing_vat_rate();
    void test_invoice_label_country_decode();
    void test_invoice_no_country_from_short_supplier();

    // --- Self-VAT (auto-liquidation / reverse charge) tests ---
    void test_factory_selfvat_intracom_eu();
    void test_factory_selfvat_extracom_noneu();
    void test_factory_selfvat_domestic_no_autoliquidation();
    void test_factory_selfvat_thirdparty_no_autoliquidation();
    void test_factory_selfvat_no_amount_no_lines();
    void test_factory_selfvat_has_normal_vat_no_autoliquidation();
    void test_factory_selfvat_custom_accounts();
    void test_factory_selfvat_refund();
    void test_factory_selfvat_invoice_us_fr_label_route();
    void test_invoice_eu_fr_label_route();
    void test_invoice_gb_fr_label_route_noneu();
    void test_invoice_tr_fr_label_route_try_currency();
    void test_invoice_ph_fr_label_route();
    void test_invoice_mixed_currency_vat_eur_total_sek();
    void test_invoice_mixed_currency_vat_sek_total_eur();
    void test_invoice_same_currency_sek_rate_computed();
    void test_invoice_four_currency_variants_same_eur_amounts();
    void test_invoice_gbp_negative_vat_and_total_with_conversion();
    void test_factory_purchase_dual_amount_uses_invoice_rate();
    void test_factory_purchase_amz_account_uses_debit_account();
    void test_abstract_books_table_sort_by_date();

    // ── Amazon Payment filename parsing ──────────────────────────────────────
    // 20 situations that could cause incorrect handling:
    //  1. Full filename with all optional fields present (USD)
    //  2. Missing expenses, small balance drop → no exception
    //  3. Missing expenses, large balance drop → ExceptionWithTitleText
    //  4. Missing refunded-expenses → always OK
    //  5. Both optional tokens absent, trivial amounts
    //  6. Encode then decode roundtrip preserves all fields
    //  7. GBP marketplace (different EUR rate)
    //  8. Paid in different currency from balance (currency conversion)
    //  9. EUR marketplace (de) – expenses in EUR, threshold = 200 EUR exact
    // 10. Threshold edge: just below 200 EUR proxy → no exception
    // 11. Threshold edge: just above 200 EUR proxy → exception
    // 12. CAD marketplace – threshold ~294 CAD
    // 13. JPY marketplace – threshold ~32 258 JPY
    // 14. AUD marketplace – threshold ~333 AUD
    // 15. MXN marketplace – threshold ~4 348 MXN
    // 16. SEK marketplace – threshold ~2 299 SEK
    // 17. PLN marketplace – threshold ~870 PLN
    // 18. TRY marketplace – threshold ~7 407 TRY
    // 19. Expenses present with amount < 200 EUR → always valid, no exception
    // 20. Refunded-expenses before expenses in filename → parsed correctly
    void test_amz_payment_decode_full_usd();
    void test_amz_payment_decode_missing_expenses_small_no_exception();
    void test_amz_payment_decode_missing_expenses_large_exception();
    void test_amz_payment_decode_missing_refunded_ok();
    void test_amz_payment_decode_both_optional_absent_small();
    void test_amz_payment_encode_decode_roundtrip();
    void test_amz_payment_currency_gbp();
    void test_amz_payment_paid_different_currency();
    void test_amz_payment_eur_marketplace_de();
    void test_amz_payment_threshold_just_below_no_exception();
    void test_amz_payment_threshold_just_above_exception();
    void test_amz_payment_currency_cad();
    void test_amz_payment_currency_jpy();
    void test_amz_payment_currency_aud();
    void test_amz_payment_currency_mxn();
    void test_amz_payment_currency_sek();
    void test_amz_payment_currency_pln();
    void test_amz_payment_currency_try();
    void test_amz_payment_expenses_small_present_no_exception();
    void test_amz_payment_refunded_before_expenses_parsed_correctly();

    // ── Amazon Payment createEntry factory tests (30 tests) ──────────────────
    // Group 1: EUR company, EUR payment — no conversion
    void test_factory_amz_entry_eur_all_fields();           // 1
    void test_factory_amz_entry_eur_no_balance();           // 2
    void test_factory_amz_entry_eur_expenses_refund();      // 3
    void test_factory_amz_entry_eur_expenses_only();        // 4
    void test_factory_amz_entry_eur_minimal();              // 5
    // Group 2: USD payment → EUR conversion
    void test_factory_amz_entry_usd_all_fields();           // 6
    void test_factory_amz_entry_usd_no_balance();           // 7
    void test_factory_amz_entry_usd_balance_only();         // 8
    void test_factory_amz_entry_usd_paid_eur();             // 9
    void test_factory_amz_entry_usd_conversion_in_title();  // 10
    // Group 3: other marketplace currencies
    void test_factory_amz_entry_gbp_all_fields();           // 11
    void test_factory_amz_entry_cad_payment();              // 12
    void test_factory_amz_entry_jpy_payment();              // 13
    void test_factory_amz_entry_aud_payment();              // 14
    void test_factory_amz_entry_sek_payment();              // 15
    // Group 4: account routing verification
    void test_factory_amz_entry_debit_account_in_balance(); // 16
    void test_factory_amz_entry_amazon_account_for_paid();  // 17
    void test_factory_amz_entry_custom_accounts();          // 18
    void test_factory_amz_entry_no_settings_null();         // 19
    void test_factory_amz_entry_default_amazon_account();   // 20
    // Group 5: line count and sums
    void test_factory_amz_entry_line_count_all_fields();    // 21
    void test_factory_amz_entry_line_count_no_optionals();  // 22
    void test_factory_amz_entry_line_count_no_balance();    // 23
    void test_factory_amz_entry_debit_sum_all_fields();     // 24
    void test_factory_amz_entry_credit_sum_all_fields();    // 25
    // Group 6: title / label format
    void test_factory_amz_entry_title_paiement_amazon();    // 26
    void test_factory_amz_entry_title_contains_paid_amount(); // 27
    void test_factory_amz_entry_title_contains_currency();  // 28
    void test_factory_amz_entry_title_all_lines_same();     // 29
    void test_factory_amz_entry_date_uses_date_to();        // 30

    // ── createEntry(InventoryMoveTree*) ──────────────────────────────────────
    void test_factory_inventory_null_tree_returns_null();
    void test_factory_inventory_null_selfvat_returns_null();
    void test_factory_inventory_eu_to_france_line_count();
    void test_factory_inventory_eu_to_france_balanced();
    void test_factory_inventory_eu_to_france_accounts();
    void test_factory_inventory_eu_to_france_amounts();
    void test_factory_inventory_eu_to_france_titles();
    void test_factory_inventory_export_skipped();
    void test_factory_inventory_missing_accounts_returns_null();
    void test_factory_inventory_zero_price_skipped();

    // ── createEntryOssIoss ────────────────────────────────────────────────────
    void test_oss_ioss_empty_groups_returns_empty();
    void test_oss_ioss_skips_non_oss_schemes();
    void test_oss_ioss_skips_zero_vat_groups();
    void test_oss_ioss_oss_single_destination_balance_and_structure();
    void test_oss_ioss_oss_two_destinations_two_entries();
    void test_oss_ioss_oss_label_format();
    void test_oss_ioss_ioss_single_group();
    void test_oss_ioss_ioss_two_groups();
    void test_oss_ioss_ioss_label_format();
    void test_oss_ioss_mixed_oss_and_ioss();

    // ── Invoice generation with refunds that have no Amazon invoice number ───
    void test_invoice_generation_refunds_synthetic();

    // ── BooksConnections::tryToConnect same-currency USD association ─────────
    void test_associate_usd_invoice_usd_bank_succeeds();
    void test_associate_eur_invoice_usd_bank_fails_without_rate();
    void test_associate_usd_invoice_usd_bank_with_api_key_blocker();

    // ── Real-world Amazon payment filenames — debit/credit verification ──────
    // All 70 filenames provided by the user.
    // Entries are intentionally partial: debitSum − creditSum = netSales for the
    // period (recorded separately in sales journal entries).
    // BUG: 4 files (SE ×3, TR ×1) have negative paid which the factory ignores
    // instead of generating a credit; see inline comments.
    void test_factory_amz_real_payment_files();
};

void TestBookEntries::test_journal_entry_simple()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Debit 100 EUR
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales Revenue";
    debitLine.account = "707000";
    debitLine.currency_amount["EUR"] = 100.0;
    
    // Credit 100 EUR
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512000";
    creditLine.currency_amount["EUR"] = 100.0;
    
    entry.addDebitLeft(debitLine, "EUR");
    entry.addCreditRight(creditLine, "EUR");
    
    // Should not throw (1.0 conversion rate, implicit or explicit)
    try {
        entry.convertRoundingIfNeeded(1.0);
    } catch (const ExceptionWithTitleText &e) {
        QFAIL(QString("Exception thrown unexpectedly: %1").arg(e.what()).toUtf8().constData());
    }
}

void TestBookEntries::test_rounding_within_limit()
{
    JournalEntry entry(QDate::currentDate(), "EUR");

    // Debit 100.00
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.00;
    entry.addDebitLeft(debitLine, "EUR");

    // Credit 99.99 (0.01 difference < 0.1 limit)
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 99.99;
    entry.addCreditRight(creditLine, "EUR");

    try {
        entry.convertRoundingIfNeeded(1.0); // maxRoundingDelta defaults to 0.1
    } catch (const ExceptionWithTitleText &e) {
        QFAIL(e.what());
    }
    
    // Verify rounding entry
    const auto &credits = entry.getCredits();
    // Expected: 1 original credit + 1 rounding credit
    // But wait, diff is 100.00 - 99.99 = 0.01. Diff > 0 means Debit > Credit.
    // "If diff > 0, Debit > Credit, we need more Credit." -> Added to Credit.
    
    QCOMPARE(credits.size(), 2);
    const auto &lastCredit = credits.last();
    QCOMPARE(lastCredit.title, QString("Rounding difference"));
    QCOMPARE(lastCredit.currency_amount["EUR"], 0.01);
    
    // Verification of rounding entry existence is implied by no exception and successful completion 
    // since the class corrects it internally effectively.
    // Further inspection would require getters on JournalEntry if we wanted to verify the exact entries.
    // Assuming the requirement "Create tst_book_entries.cpp to test this class... including rounding success" 
    // implies detecting that it passed without error.
}

void TestBookEntries::test_rounding_exceeds_limit()
{
    JournalEntry entry(QDate::currentDate(), "EUR");

    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.00;
    entry.addDebitLeft(debitLine, "EUR");

    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 99.00; // 1.00 difference > 0.1
    entry.addCreditRight(creditLine, "EUR");

    bool caught = false;
    try {
        entry.convertRoundingIfNeeded(1.0);
    } catch (const ExceptionWithTitleText &) {
        caught = true;
    }
    QVERIFY2(caught, "ExceptionWithTitleText should have been thrown for large difference");
}

void TestBookEntries::test_currency_conversion()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // USD -> EUR (Rate 0.85)
    // 100 USD = 85.00 EUR
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales USD";
    debitLine.account = "707";
    debitLine.currency_amount["USD"] = 100.00;
    entry.addDebitLeft(debitLine, "USD", 0.85);
    
    // Credit explicitly in EUR matching the conversion
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank EUR";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 85.00;
    entry.addCreditRight(creditLine, "EUR");

    try {
        entry.convertRoundingIfNeeded(0.85);
    } catch (const ExceptionWithTitleText &e) {
        QFAIL(e.what());
    }

    const auto &debits = entry.getDebits();
    QCOMPARE(debits.size(), 1);
    // 100 * 0.85 = 85.00
    QCOMPARE(debits[0].currency_amount["EUR"], 85.00);
    QCOMPARE(debits[0].currency_amount["USD"], 100.00);
    // Check title modification
    // Title should contain " (Conv: 100 USD @ 0.85)"
    QVERIFY(debits[0].title.contains("(Conv: 100 USD @ 0.85)"));
}

void TestBookEntries::test_currency_conversion_custom_delta()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Scenario where rounding diff is larger than default 0.1 but we allow it
    // Debit 100.00 EUR
    JournalEntry::EntryLine debitLine;
    debitLine.title = "A";
    debitLine.account = "1";
    debitLine.currency_amount["EUR"] = 100.00;
    entry.addDebitLeft(debitLine, "EUR");
    
    // Credit 99.50 EUR -> Diff 0.50
    JournalEntry::EntryLine creditLine;
    creditLine.title = "B";
    creditLine.account = "2";
    creditLine.currency_amount["EUR"] = 99.50;
    entry.addCreditRight(creditLine, "EUR");

    // Should fail with default
    bool caught = false;
    try {
        entry.convertRoundingIfNeeded(1.0);
    } catch (const ExceptionWithTitleText &) {
        caught = true;
    }
    QVERIFY(caught);

    // Should pass with custom delta 1.0
    try {
        entry.convertRoundingIfNeeded(1.0, 1.0);
    } catch (const ExceptionWithTitleText &e) {
        QFAIL(e.what());
    }
}

void TestBookEntries::test_get_debit_sum()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Add multiple debit entries
    JournalEntry::EntryLine debit1;
    debit1.title = "Sales 1";
    debit1.account = "707";
    debit1.currency_amount["EUR"] = 100.0;
    entry.addDebitLeft(debit1, "EUR");
    
    JournalEntry::EntryLine debit2;
    debit2.title = "Sales 2";
    debit2.account = "707";
    debit2.currency_amount["EUR"] = 50.0;
    entry.addDebitLeft(debit2, "EUR");
    
    JournalEntry::EntryLine debit3;
    debit3.title = "Sales 3 USD";
    debit3.account = "707";
    debit3.currency_amount["USD"] = 100.0;
    entry.addDebitLeft(debit3, "USD", 0.85); // 100 USD = 85 EUR
    
    // Total should be 100 + 50 + 85 = 235
    QCOMPARE(entry.getDebitSum(), 235.0);
}

void TestBookEntries::test_get_credit_sum()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Add multiple credit entries
    JournalEntry::EntryLine credit1;
    credit1.title = "Bank 1";
    credit1.account = "512";
    credit1.currency_amount["EUR"] = 100.0;
    entry.addCreditRight(credit1, "EUR");
    
    JournalEntry::EntryLine credit2;
    credit2.title = "Bank 2";
    credit2.account = "512";
    credit2.currency_amount["EUR"] = 75.0;
    entry.addCreditRight(credit2, "EUR");
    
    // Total should be 100 + 75 = 175
    QCOMPARE(entry.getCreditSum(), 175.0);
}

void TestBookEntries::test_raise_exception_balanced()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.0;
    entry.addDebitLeft(debitLine, "EUR");
    
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 100.0;
    entry.addCreditRight(creditLine, "EUR");
    
    // Should not throw
    try {
        entry.raiseExceptionIfDebitDifferentCredit();
    } catch (const ExceptionWithTitleText &e) {
        QFAIL(QString("Exception thrown unexpectedly: %1").arg(e.what()).toUtf8().constData());
    }
}

void TestBookEntries::test_raise_exception_unbalanced()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    JournalEntry::EntryLine debitLine;
    debitLine.title = "Sales";
    debitLine.account = "707";
    debitLine.currency_amount["EUR"] = 100.0;
    entry.addDebitLeft(debitLine, "EUR");
    
    JournalEntry::EntryLine creditLine;
    creditLine.title = "Bank";
    creditLine.account = "512";
    creditLine.currency_amount["EUR"] = 95.0;
    entry.addCreditRight(creditLine, "EUR");
    
    // Should throw
    bool caught = false;
    try {
        entry.raiseExceptionIfDebitDifferentCredit();
    } catch (const ExceptionWithTitleText &) {
        caught = true;
    }
    QVERIFY2(caught, "ExceptionWithTitleText should have been thrown for unbalanced entry");
}

void TestBookEntries::test_multi_currency_entries()
{
    JournalEntry entry(QDate::currentDate(), "EUR");
    
    // Debit in USD
    JournalEntry::EntryLine debit1;
    debit1.title = "Sales USD";
    debit1.account = "707";
    debit1.currency_amount["USD"] = 200.0;
    entry.addDebitLeft(debit1, "USD", 0.85); // 200 USD = 170 EUR
    
    // Debit in GBP
    JournalEntry::EntryLine debit2;
    debit2.title = "Sales GBP";
    debit2.account = "707";
    debit2.currency_amount["GBP"] = 50.0;
    entry.addDebitLeft(debit2, "GBP", 1.15); // 50 GBP = 57.5 EUR
    
    // Credit in EUR
    JournalEntry::EntryLine credit1;
    credit1.title = "Bank EUR";
    credit1.account = "512";
    credit1.currency_amount["EUR"] = 227.5;
    entry.addCreditRight(credit1, "EUR");
    
    // Verify sums
    QCOMPARE(entry.getDebitSum(), 227.5);  // 170 + 57.5
    QCOMPARE(entry.getCreditSum(), 227.5);
    
    // Verify entries contain both currencies
    const auto &debits = entry.getDebits();
    QCOMPARE(debits[0].currency_amount["USD"], 200.0);
    QCOMPARE(debits[0].currency_amount["EUR"], 170.0);
    QCOMPARE(debits[1].currency_amount["GBP"], 50.0);
    QCOMPARE(debits[1].currency_amount["EUR"], 57.5);
    
    // Should not throw when checked
    try {
        entry.raiseExceptionIfDebitDifferentCredit();
    } catch (const ExceptionWithTitleText &e) {
        QFAIL(QString("Exception thrown unexpectedly: %1").arg(e.what()).toUtf8().constData());
    }
}

void TestBookEntries::test_invoice_encoding()
{
    // Test Case 1: Standard example
    QString fileName = "2025-02-18__622600__compta__SOCIC-FR__FR-TVA-13.6EUR__81.6EUR.pdf";
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");
    
    QCOMPARE(info.date, QDate(2025, 2, 18));
    QCOMPARE(info.account, QString("622600"));
    QCOMPARE(info.label, QString("compta"));
    QCOMPARE(info.accountSupplier, QString("SOCIC-FR"));
    QCOMPARE(info.vatTokens.size(), 1);
    QCOMPARE(info.vatTokens.first(), QString("FR-TVA-13.6EUR"));
    
    // Check Parsing into country_vatRate_vat
    QCOMPARE(info.country_vatRate_vat.size(), 1);
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    // "TVA" -> Rate Label empty -> Calculated from VAT/Untaxed
    // VAT = 13.6, Total = 81.6 -> Untaxed = 68.0 -> Rate = 13.6 / 68.0 = 20.00% -> 0.2
    QVERIFY(info.country_vatRate_vat["FR"].contains("0.2"));
    QCOMPARE(info.country_vatRate_vat["FR"]["0.2"], 13.6);
    
    QCOMPARE(info.totalAmount, 81.6);
    QCOMPARE(info.currency, QString("EUR"));
    QCOMPARE(info.originalExtension, QString("pdf"));
    
    // Flags
    QCOMPARE(info.isInventory, false);
    QCOMPARE(info.isDDP, false);
    QVERIFY(info.countryCodeFrom.isEmpty());
    QCOMPARE(info.countryCodeTo, QString("FR")); // no route in filename → defaults to company country

    // Roundtrip
    QString encoded = PurchaseInvoiceManager::encode(info);
    QCOMPARE(encoded, fileName);
    
    // Test Case 2: Multiple VATs and different extension + Inventory + Route + DDP
    // Route in supplier: YISHUNCNFR (ends with CN FR)
    // Inventory: "stock" in label or anywhere.
    // DDP: "DDP" in label
    QString fileName2 = "2026-01-24__607000__stock DDP__YISHUNCNFR__FR-TVA5.5-13.6EUR__FR-TVA20-20.0EUR__133.6EUR.jpg";
    PurchaseInformation info2 = PurchaseInvoiceManager::decode(fileName2, &decodeTestPurchaseTable(), "FR");
    
    QCOMPARE(info2.date, QDate(2026, 1, 24));
    QCOMPARE(info2.account, QString("607000"));
    QCOMPARE(info2.vatTokens.size(), 2);
    
    // Check parsed VATs
    // FR, 5.5 -> 13.6
    QVERIFY(info2.country_vatRate_vat.contains("FR"));
    QVERIFY(info2.country_vatRate_vat["FR"].contains("0.055"));
    QCOMPARE(info2.country_vatRate_vat["FR"]["0.055"], 13.6);
    
    // FR, 20 -> 20.0
    QVERIFY(info2.country_vatRate_vat["FR"].contains("0.2")); // formatted proportion
    QCOMPARE(info2.country_vatRate_vat["FR"]["0.2"], 20.0);
    
    QCOMPARE(info2.totalAmount, 133.6);
    
    // Flags
    QCOMPARE(info2.isInventory, true);
    QCOMPARE(info2.isDDP, true);
    QCOMPARE(info2.countryCodeFrom, QString("CN"));
    QCOMPARE(info2.countryCodeTo, QString("FR"));
    
    QString encoded2 = PurchaseInvoiceManager::encode(info2);
    QCOMPARE(encoded2, fileName2);
    
    // Test Case 3: No VAT
    QString fileName3 = "2025-12-31__622600__fees__BANK__10.0USD.pdf";
    PurchaseInformation info3 = PurchaseInvoiceManager::decode(fileName3, &decodeTestPurchaseTable(), "FR");
    QCOMPARE(info3.vatTokens.isEmpty(), true);
    QCOMPARE(info3.totalAmount, 10.0);
    QCOMPARE(info3.currency, QString("USD"));
    
    QString encoded3 = PurchaseInvoiceManager::encode(info3);
    QCOMPARE(encoded3, fileName3);
    
    // Test Folder Structure Path
    QString path = PurchaseInvoiceManager::getRelativePath(info);
    QCOMPARE(path, QString("purchase-invoices/2025/02"));
}

void TestBookEntries::test_invoice_proportion_encode_decode()
{
    QString fileName = "2026-01-29__647700__cheques-CESU-25E__FDOMIS__FR-TVA20-5EUR__411EUR.pdf";
    PurchaseInformation info1 = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");
    
    QVERIFY(info1.country_vatRate_vat.contains("FR"));
    
    // Check that the VAT rate is 0.2 (allowing for formatting variations like "0.20" or "0.2")
    bool hasProportionRate = info1.country_vatRate_vat["FR"].contains("0.20") || info1.country_vatRate_vat["FR"].contains("0.2");
    QVERIFY2(hasProportionRate, "The VAT rate should be 0.2, not 20 or 20.00");
    
    if (info1.country_vatRate_vat["FR"].contains("0.20")) {
        QCOMPARE(info1.country_vatRate_vat["FR"]["0.20"], 5.0);
    } else {
        QCOMPARE(info1.country_vatRate_vat["FR"]["0.2"], 5.0);
    }
    
    // Encode and Decode again
    QString encoded = PurchaseInvoiceManager::encode(info1);
    QCOMPARE(encoded, fileName);
    
    PurchaseInformation info2 = PurchaseInvoiceManager::decode(encoded, &decodeTestPurchaseTable(), "FR");
    
    // Check all information is the same
    QCOMPARE(info1.date, info2.date);
    QCOMPARE(info1.account, info2.account);
    QCOMPARE(info1.label, info2.label);
    QCOMPARE(info1.accountSupplier, info2.accountSupplier);
    QCOMPARE(info1.totalAmount, info2.totalAmount);
    QCOMPARE(info1.vatTokens, info2.vatTokens);
    QCOMPARE(info1.country_vatRate_vat, info2.country_vatRate_vat);
}    

void TestBookEntries::test_invoice_extra_tokens()
{
    // Test Case 4: EXTRA tokens
    QString fileName4 = "2025-05-10__607222__Mix__Supplier__EXTRA-607223-20.1EUR__120.1EUR.pdf";
    PurchaseInformation info4 = PurchaseInvoiceManager::decode(fileName4, &decodeTestPurchaseTable(), "FR");
    QCOMPARE(info4.date, QDate(2025, 5, 10));
    QCOMPARE(info4.account, "607222");
    QCOMPARE(info4.totalAmount, 120.1);
    QVERIFY(info4.subUntaxedAmount.contains("607223"));
    QCOMPARE(info4.subUntaxedAmount["607223"], 20.1);
    QCOMPARE(info4.vatTokens.isEmpty(), true);
    
    // Roundtrip for EXTRA
    QString encoded4 = PurchaseInvoiceManager::encode(info4);
    QCOMPARE(encoded4, fileName4);
    
    // Test Case 5: Double EXTRA tokens
    QString fileName5 = "2025-06-15__607000__MultiMix__Supp__EXTRA-607001-30.0EUR__EXTRA-607002-15.5EUR__245.5EUR.pdf";
    PurchaseInformation info5 = PurchaseInvoiceManager::decode(fileName5, &decodeTestPurchaseTable(), "FR");
    QCOMPARE(info5.date, QDate(2025, 6, 15));
    QCOMPARE(info5.account, "607000");
    QCOMPARE(info5.totalAmount, 245.5);
    QCOMPARE(info5.subUntaxedAmount.size(), 2);
    QVERIFY(info5.subUntaxedAmount.contains("607001"));
    QCOMPARE(info5.subUntaxedAmount["607001"], 30.0);
    QVERIFY(info5.subUntaxedAmount.contains("607002"));
    QCOMPARE(info5.subUntaxedAmount["607002"], 15.5);
    
    // Roundtrip
    QString encoded5 = PurchaseInvoiceManager::encode(info5);
    // Note: Hash map order is not guaranteed, so encoded string might have swapped EXTRA orders.
    // However, for valid roundtrip, it should match if we don't care about order, OR verify parts.
    // encode() iterates over hash.
    // Let's check parts presence instead of exact string match if order is unstable.
    // But since it's a test, maybe just check it contains both.
    QVERIFY(encoded5.contains("EXTRA-607001-30EUR"));
    QVERIFY(encoded5.contains("EXTRA-607002-15.5EUR"));
    // And prefix/suffix
    QVERIFY(encoded5.startsWith("2025-06-15__607000__MultiMix__Supp__"));
    QVERIFY(encoded5.endsWith("__245.5EUR.pdf"));
}

void TestBookEntries::test_invoice_negative_amount_refund()
{
    // Case 1: Decode a filename with a negative total (purchase refund).
    // VAT amounts in tokens are always stored positive; the negative total signals the refund.
    const QString fileName = "2024-03-15__607000__refund-label__Supplier__FR-TVA20-20.0EUR__-120.0EUR.pdf";
    const PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date, QDate(2024, 3, 15));
    QCOMPARE(info.account, QString("607000"));
    QCOMPARE(info.totalAmount, -120.0);
    QCOMPARE(info.rawTotalAmount, QString("-120.0"));
    QCOMPARE(info.currency, QString("EUR"));

    // VAT is positive in country_vatRate_vat even for a refund
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    const auto &frVat = info.country_vatRate_vat["FR"];
    QCOMPARE(frVat.size(), 1);
    const auto frVatKeys = frVat.keys();
    QCOMPARE(frVat[frVatKeys.first()], 20.0);

    // rawVatAmount must also be set (positive)
    QCOMPARE(info.rawVatAmount, QString("20"));

    // Roundtrip encode → same filename
    const QString encoded = PurchaseInvoiceManager::encode(info);
    QCOMPARE(encoded, fileName);

    // Case 2: Negative total with no VAT
    const QString fileNameNoVat = "2024-03-15__607000__refund-fees__Bank__-50.0EUR.pdf";
    const PurchaseInformation infoNoVat = PurchaseInvoiceManager::decode(fileNameNoVat, &decodeTestPurchaseTable(), "FR");
    QCOMPARE(infoNoVat.totalAmount, -50.0);
    QVERIFY(infoNoVat.country_vatRate_vat.isEmpty());
    QCOMPARE(PurchaseInvoiceManager::encode(infoNoVat), fileNameNoVat);

    // Case 3: Factory produces a reversed (credit-side expense) entry for a refund
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    {
        QFile companyFile(dir.filePath("company.csv"));
        QVERIFY(companyFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&companyFile);
        out << "Id;Parameter;Value\n";
        out << "Currency;Currency;EUR\n";
        out << "Country;Country Code;FR\n";
    }

    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts,
                                &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    const auto entry = factory.createEntry(info);
    QVERIFY(!entry.isNull());

    // Expense account (607000) must appear on the credit side for a refund
    bool foundExpenseCredit = false;
    for (const auto &line : entry->getCredits()) {
        if (line.account == "607000") {
            foundExpenseCredit = true;
            QCOMPARE(line.currency_amount["EUR"], 100.0); // HT = 120 - 20
        }
    }
    QVERIFY(foundExpenseCredit);

    // Supplier must appear on the debit side
    bool foundSupplierDebit = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "Supplier") {
            foundSupplierDebit = true;
            QCOMPARE(line.currency_amount["EUR"], 120.0);
        }
    }
    QVERIFY(foundSupplierDebit);

    // Entry must balance
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
}

void TestBookEntries::test_invoice_model_loading()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    QDir dir(tempDir.path());
    // Create structure: purchase-invoices/2025/02
    QVERIFY(dir.mkpath("purchase-invoices/2025/02"));
    QVERIFY(dir.mkpath("purchase-invoices/2026/01"));
    
    // Create files
    QString fileName1 = "2025-02-18__622600__compta__SOCIC__FR-TVA-13.6EUR__81.6EUR.pdf";
    QString fullPath1 = dir.filePath("purchase-invoices/2025/02/" + fileName1);
    QFile file1(fullPath1);
    QVERIFY(file1.open(QIODevice::WriteOnly));
    file1.close();
    
    QString fileName2 = "2026-01-24__607000__stock__YISHUN__FR-TVA5.5-13.6EUR__FR-TVA20-20.0EUR__133.6EUR.jpg";
    QString fullPath2 = dir.filePath("purchase-invoices/2026/01/" + fileName2);
    QFile file2(fullPath2);
    QVERIFY(file2.open(QIODevice::WriteOnly));
    file2.close();
    
    // Initialize Manager (calls _load automatically)
    PurchaseInvoiceManager manager(dir, "FR");
    
    // load() is private now, called in constructor
    
    // Verify Model
    QCOMPARE(manager.rowCount(), 2);
    
    // Check sorting (descending by date)
    // Row 0 should be 2026
    QModelIndex idx0 = manager.index(0, 0); // Date column
    QCOMPARE(manager.data(idx0).toDate(), QDate(2026, 1, 24));
    
    // Row 1 should be 2025
    QModelIndex idx1 = manager.index(1, 0);
    QCOMPARE(manager.data(idx1).toDate(), QDate(2025, 2, 18));
    
    // Check some data
    QModelIndex idxAccount = manager.index(0, 1);
    QCOMPARE(manager.data(idxAccount).toString(), QString("607000"));
    
    QModelIndex idxTotal = manager.index(0, 7);
    QCOMPARE(manager.data(idxTotal).toDouble(), 133.6);
}

void TestBookEntries::test_invoice_decode_error()
{
    // Invalid format (not enough parts)
    QString badFile = "2025-02-18__Info.pdf";
    bool caught = false;
    try {
        PurchaseInvoiceManager::decode(badFile, &decodeTestPurchaseTable(), "FR");
    } catch (const ExceptionWithTitleText &e) {
        caught = true;
        QCOMPARE(e.errorTitle(), QString("Invalid Filename"));
    }
    QVERIFY(caught);
    
    // Invalid date
    QString badDateFile = "2025-99-99__620__Label__Supp__10EUR.pdf";
    caught = false;
    try {
        PurchaseInvoiceManager::decode(badDateFile, &decodeTestPurchaseTable(), "FR");
    } catch (const ExceptionWithTitleText &e) {
        caught = true;
        QCOMPARE(e.errorTitle(), QString("Invalid Date"));
    }
    QVERIFY(caught);
}

void TestBookEntries::test_invoice_add()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    QDir dir(tempDir.path());
    // Create source file
    QString sourceFileName = "source_invoice.pdf";
    QString sourcePath = dir.filePath(sourceFileName);
    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("dummy content");
    sourceFile.close();
    
    // Prepare info
    PurchaseInformation info;
    info.date = QDate(2024, 3, 15);
    info.account = "611";
    info.label = "supplies";
    info.accountSupplier = "OfficeDepot";
    info.totalAmount = 45.50;
    info.currency = "EUR";
    // originalExtension is empty, should be picked up from source
    
    PurchaseInvoiceManager manager(dir, "FR");
    QCOMPARE(manager.rowCount(), 0);
    
    manager.add(sourcePath, info);
    
    QCOMPARE(manager.rowCount(), 1);
    
    // Verify file exists in correct location
    // purchase-invoices/2024/03/2024-03-15__611__supplies__OfficeDepot__45.5EUR.pdf
    QString expectedPath = "purchase-invoices/2024/03/2024-03-15__611__supplies__OfficeDepot__45.5EUR.pdf";
    QVERIFY(dir.exists(expectedPath));
    
    // Verify info updated with extension
    QCOMPARE(info.originalExtension, QString("pdf"));
}

void TestBookEntries::test_get_invoices()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Create 3 invoices
    // 1. Jan 2025
    QString f1 = "2025-01-10__600__L__S__10EUR.pdf";
    QVERIFY(dir.mkpath("purchase-invoices/2025/01"));
    QFile(dir.filePath("purchase-invoices/2025/01/" + f1)).open(QIODevice::WriteOnly);
    
    // 2. Feb 2025
    QString f2 = "2025-02-15__600__L__S__10EUR.pdf";
    QVERIFY(dir.mkpath("purchase-invoices/2025/02"));
    QFile(dir.filePath("purchase-invoices/2025/02/" + f2)).open(QIODevice::WriteOnly);
    
    // 3. Mar 2025
    QString f3 = "2025-03-20__600__L__S__10EUR.pdf";
    QVERIFY(dir.mkpath("purchase-invoices/2025/03"));
    QFile(dir.filePath("purchase-invoices/2025/03/" + f3)).open(QIODevice::WriteOnly);
    
    PurchaseInvoiceManager manager(dir, "FR");
    QCOMPARE(manager.rowCount(), 3);
    
    // Get all
    auto all = manager.getInvoices(QDate(2025, 1, 1), QDate(2025, 12, 31));
    QCOMPARE(all.size(), 3);
    
    // Get Feb only
    auto feb = manager.getInvoices(QDate(2025, 2, 1), QDate(2025, 2, 28));
    QCOMPARE(feb.size(), 1);
    QCOMPARE(feb.first().date, QDate(2025, 2, 15));
    
    // Get Jan and Feb
    auto janfeb = manager.getInvoices(QDate(2025, 1, 1), QDate(2025, 2, 28));
    QCOMPARE(janfeb.size(), 2);
    
    // Get None
    auto none = manager.getInvoices(QDate(2024, 1, 1), QDate(2024, 12, 31));
    QCOMPARE(none.size(), 0);
}

void TestBookEntries::test_factory_purchase_no_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup tables
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    // Add purchase account for FR
    // Account FR 0.2 is created by default
    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts,
                                &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    // Create purchase information (no conversion needed - EUR to EUR)
    PurchaseInformation purchase;
    purchase.date = QDate(2024, 3, 15);
    purchase.account = "607000";
    purchase.label = "Software license";
    purchase.accountSupplier = "SoftCorp";
    purchase.totalAmount = 120.0; // 100 HT + 20 VAT
    purchase.currency = "EUR";
    purchase.country_vatRate_vat["FR"]["0.2"] = 20.0;
    purchase.countryCodeFrom = "FR";
    purchase.countryCodeTo = "FR";
    
    auto entry = factory.createEntry(purchase);
    
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), 120.0);
    QCOMPARE(entry->getCreditSum(), 120.0);
    
    // Verify debit entries (expense + VAT)
    const auto &debits = entry->getDebits();
    QCOMPARE(debits.size(), 2); // Expense + VAT
    
    // Find expense line
    bool foundExpense = false;
    for (const auto &line : debits) {
        if (line.account == "607000") {
            foundExpense = true;
            QCOMPARE(line.currency_amount["EUR"], 100.0);
            QVERIFY(line.title.contains("SoftCorp"));
            QVERIFY(line.title.contains("Software license"));
        }
    }
    QVERIFY(foundExpense);
    
    // Verify that ALL lines share the same title
    // "Achat FR->FR SoftCorp - Software license"
    QString expectedTitle = "Achat FR->FR SoftCorp - Software license";
    const auto &allLines = entry->getDebits() + entry->getCredits();
    for (const auto &line : allLines) {
        QCOMPARE(line.title, expectedTitle);
    }
    
    // Verify credit (supplier)
    const auto &credits = entry->getCredits();
    QCOMPARE(credits.size(), 1); // Supplier only
}

void TestBookEntries::test_factory_purchase_with_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup company info
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);

    // Setup currency rates
    CurrencyRateManager currencyManager(dir, "");
    currencyManager.importRate("2024-03-15", "USD", "EUR", 0.85);
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    // Account FR 0.2 is created by default

    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts,
                                &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    // Create purchase in USD
    PurchaseInformation purchase;
    purchase.date = QDate(2024, 3, 15);
    purchase.account = "607000";
    purchase.label = "Cloud services";
    purchase.accountSupplier = "AWS";
    purchase.totalAmount = 120.0; // USD
    purchase.currency = "USD";
    purchase.country_vatRate_vat["FR"]["0.2"] = 20.0;
    purchase.countryCodeFrom = "FR";
    purchase.countryCodeTo = "FR";
    
    auto entry = factory.createEntry(purchase);
    
    QVERIFY(!entry.isNull());
    
    // Check that amounts include both USD and EUR
    const auto &debits = entry->getDebits();
    bool foundConversion = false;
    // Expected title: "Achat FR->FR AWS - Cloud services (120.00 USD)"
    QString expectedTitle = "Achat FR->FR AWS - Cloud services (120.00 USD)";
    
    for (const auto &line : debits) {
        if (line.currency_amount.contains("USD") && line.currency_amount.contains("EUR")) {
            foundConversion = true;
            // Conversion info handled by JournalEntry appending " (Conv...)"?
            // Wait, JournalEntry append logic:
            // " (Conv: <orig> <curr> @ <rate>)"
            // My Factory added " (120.00 USD)" at the end of BASE title.
            // So full title might be: "Achat ... (120.00 USD) (Conv: 120.00 USD @ 0.85)"
            // Let's check `JournalEntry.cpp` logic? I cannot right now.
            // But `JournalEntry::addDebitLeft` calls `_addEntry` which might append conversion info.
            // Assuming it does if rate != 1.0 or currencies differ.
            // Code in `JournalEntry.cpp` (Step 46):
            // It appends `QString(" (Conv: %1 %2 @ %3)")`.
            
            bool expectedConvFound = line.title.contains("(Conv: 100 USD @ 0.85)") || 
                                     line.title.contains("(Conv: 20 USD @ 0.85)");
            QVERIFY(expectedConvFound);
        }
    }
    QVERIFY(foundConversion);
}

void TestBookEntries::test_factory_purchase_refund()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    // Account FR 0.2 is created by default

    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts,
                                &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    // Create refund (negative amount)
    PurchaseInformation purchase;
    purchase.date = QDate(2024, 3, 15);
    purchase.account = "607000";
    purchase.label = "Returned item";
    purchase.accountSupplier = "RetailCorp";
    purchase.totalAmount = -120.0; // Negative = refund
    purchase.currency = "EUR";
    purchase.country_vatRate_vat["FR"]["0.2"] = 20.0;
    purchase.countryCodeFrom = "FR";
    purchase.countryCodeTo = "FR";
    
    auto entry = factory.createEntry(purchase);
    
    QVERIFY(!entry.isNull());
    
    // For refund, debits and credits should be reversed
    // Expense should be on credit side
    const auto &credits = entry->getCredits();
    bool foundExpenseCredit = false;
    for (const auto &line : credits) {
        if (line.account == "607000") {
            foundExpenseCredit = true;
            QCOMPARE(line.currency_amount["EUR"], 100.0);
        }
    }
    QVERIFY(foundExpenseCredit);
    
    // Supplier should be on debit side
    const auto &debits = entry->getDebits();
    bool foundSupplierDebit = false;
    for (const auto &line : debits) {
        if (line.account == "RetailCorp") {
            foundSupplierDebit = true;
            QCOMPARE(line.currency_amount["EUR"], 120.0);
        }
    }
    QVERIFY(foundSupplierDebit);
    
    // Verify Uniformity
    // Title: "Achat FR->FR RetailCorp - Returned item"
    // Refund amount negative, but currency matches, so no suffix.
    QString expectedTitle = "Achat FR->FR RetailCorp - Returned item";
    const auto &allLines = entry->getDebits() + entry->getCredits();
    for (const auto &line : allLines) {
        QCOMPARE(line.title, expectedTitle);
    }
    
    // Entry should still balance
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
}

void TestBookEntries::test_factory_purchase_with_extra()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup tables
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    // Account FR 0.2 is created by default

    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts,
                                &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    // Case: Purchase 200 Total. 180 Main + 20 Extra.
    
    PurchaseInformation purchase;
    purchase.date = QDate(2024, 6, 1);
    purchase.account = "607000"; // Main Account
    purchase.label = "Mixed Purchase";
    purchase.accountSupplier = "Vendor";
    purchase.totalAmount = 200.0;
    purchase.currency = "EUR";
    purchase.countryCodeFrom = "FR";
    purchase.countryCodeTo = "FR";
    
    // Extra: 20 EUR to account 607999
    purchase.subUntaxedAmount["607999"] = 20.0;
    
    auto entry = factory.createEntry(purchase);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), 200.0);
    QCOMPARE(entry->getCreditSum(), 200.0);
    
    const auto &debits = entry->getDebits();
    // Expected: 
    // 1. Account 607000 Amount 180.0
    // 2. Account 607999 Amount 20.0
    
    bool foundMain = false;
    bool foundExtra = false;
    
    for (const auto &line : debits) {
        if (line.account == "607000") {
            foundMain = true;
            QCOMPARE(line.currency_amount["EUR"], 180.0);
        } else if (line.account == "607999") {
            foundExtra = true;
            QCOMPARE(line.currency_amount["EUR"], 20.0);
        }
    }
    QVERIFY(foundMain);
    QVERIFY(foundExtra);
    
    // Case 2: Refund with Extra
    PurchaseInformation refund = purchase;
    refund.totalAmount = -200.0;
    refund.label = "Refund Mixed";
    
    auto entryRefund = factory.createEntry(refund);
    QVERIFY(!entryRefund.isNull());
    QCOMPARE(entryRefund->getCreditSum(), 200.0); // Expenses on credit side for refund
    
    const auto &credits = entryRefund->getCredits();
    foundMain = false;
    foundExtra = false;
    
    for (const auto &line : credits) {
        if (line.account == "607000") {
            foundMain = true;
            QCOMPARE(line.currency_amount["EUR"], 180.0);
        } else if (line.account == "607999") {
            foundExtra = true;
            QCOMPARE(line.currency_amount["EUR"], 20.0);
        }
    }
    QVERIFY(foundMain);
    QVERIFY(foundExtra);
}

void TestBookEntries::test_factory_shipment_no_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    groupedSaleAccounts.setData(groupedSaleAccounts.index(0, 1), QStringLiteral("411AMZTEST"), Qt::EditRole);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts,
                                &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    // Create activity source
    ActivitySource source;
    source.channel = "Amazon";
    source.subchannel = "amazon.fr";
    source.type = ActivitySourceType::Report;
    
    // Create activity
    auto activityResult = Activity::create(
        "SHIP-001", "ACT-001", "", QDateTime::currentDateTime(), QDateTime::currentDateTime(),
        "EUR", "FR", "FR", false, "FR",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided,
        "FR", TaxScheme::DomesticVat, TaxJurisdictionLevel::Country,
        SaleType::Products
    );
    QVERIFY(activityResult.ok());
    
    QList<Activity> activities;
    activities.append(activityResult.value.value());
    
    auto shipment = QSharedPointer<Shipment>::create(activities, "", true);
    
    QMultiMap<QDateTime, QSharedPointer<Shipment>> shipments;
    shipments.insert(QDateTime::currentDateTime(), shipment);
    
    auto entries = syncWait(factory.createEntryGrouped(&source, shipments));

    QVERIFY(!entries.isEmpty());
    auto entry = entries.first();
    QVERIFY(!entry.isNull());

    // Should have revenue (credit), VAT (credit), and customer (debit)
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    const auto &credits = entry->getCredits();
    QVERIFY(credits.size() >= 1); // At least revenue

    // Verify French labels
    bool foundFrenchLabel = false;
    for (const auto &line : credits) {
        if (line.title.contains("Vente") || line.title.contains("TVA")) {
            foundFrenchLabel = true;
        }
    }
    QVERIFY(foundFrenchLabel);

    // Verify Uniformity
    // All lines should have "Vente Amazon amazon.fr - " ... something
    // Since JournalTable is empty, code might be empty?
    // Let's just check that all titles are identical
    const auto &allShipmentLines = entry->getDebits() + entry->getCredits();
    QVERIFY(!allShipmentLines.isEmpty());
    QString firstTitle = allShipmentLines.first().title;
    for (const auto &line : allShipmentLines) {
        QCOMPARE(line.title, firstTitle);
        QVERIFY(line.title.startsWith("Vente Amazon amazon.fr"));
    }
}

void TestBookEntries::test_factory_shipment_with_conversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    
    // Setup currency rates.
    // Use a fixed past date so entryDate (last day of the shipment month) is always in the past,
    // which is required by CurrencyRateManager::retrieveCurrency.
    const QDate shipmentDate(2025, 1, 15);
    const QDate entryDate(2025, 1, 31); // last day of January 2025
    const QDateTime shipmentDateTime(shipmentDate, QTime(12, 0));

    CurrencyRateManager currencyManager(dir, "");
    // Import the rate for the exact entryDate the factory will use (last day of the month).
    currencyManager.importRate(entryDate.toString("yyyy-MM-dd"), "USD", "EUR", 0.85);
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    groupedSaleAccounts.setData(groupedSaleAccounts.index(0, 1), QStringLiteral("411AMZTEST"), Qt::EditRole);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts, &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    ActivitySource source;
    source.channel = "Amazon";
    source.subchannel = "amazon.com";
    source.type = ActivitySourceType::Report;

    // Create activity in USD
    auto activityResult = Activity::create(
        "SHIP-002", "ACT-002", "", shipmentDateTime, shipmentDateTime,
        "USD", "US", "US", false, "US",
        Amount{120.0, 0.0}, TaxSource::MarketplaceProvided,
        "US", TaxScheme::OutOfScope, TaxJurisdictionLevel::Country,
        SaleType::Products
    );
    QVERIFY(activityResult.ok());

    QList<Activity> activities;
    activities.append(activityResult.value.value());

    auto shipment = QSharedPointer<Shipment>::create(activities, "", true);

    QMultiMap<QDateTime, QSharedPointer<Shipment>> shipments;
    shipments.insert(shipmentDateTime, shipment);
    
    auto entries = syncWait(factory.createEntryGrouped(&source, shipments));

    QVERIFY(!entries.isEmpty());
    auto entry = entries.first();
    QVERIFY(!entry.isNull());

    // Should have conversion info in titles
    const auto &allLines = entry->getDebits() + entry->getCredits();
    bool foundConversion = false;
    for (const auto &line : allLines) {
        if (line.currency_amount.contains("USD") && line.currency_amount.contains("EUR")) {
            foundConversion = true;
        }
    }
    QVERIFY(foundConversion);

    // Entry should balance
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
}
void TestBookEntries::test_factory_shipment_mixed_rates()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup Company
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);

    // Use a fixed past date so entryDate (last day of shipment month) is always in the past,
    // which is required by CurrencyRateManager::retrieveCurrency.
    const QDate shipmentDate(2025, 1, 15);
    const QDate entryDate(2025, 1, 31); // last day of January 2025
    const QDateTime shipmentDateTime(shipmentDate, QTime(12, 0));

    CurrencyRateManager currencyManager(dir, "");
    currencyManager.importRate(entryDate.toString("yyyy-MM-dd"), "USD", "EUR", 0.85); // 1 USD = 0.85 EUR

    BooksAccountsSalesTable saleAccounts(dir); // Will populate defaults (DOM FR 20, OSS DE 19 etc)
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    groupedSaleAccounts.setData(groupedSaleAccounts.index(0, 1), QStringLiteral("411AMZTEST"), Qt::EditRole);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts, &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    ActivitySource source;
    source.channel = "Amazon";
    source.subchannel = "Mixed";
    source.type = ActivitySourceType::Report;

    QList<Activity> activities;
    const QDateTime today = shipmentDateTime;
    
    // 1. Domestic FR 20% EUR
    // Net 100, VAT 20
    activities.append(Activity::create(
        "S1", "A1", "", today, today, "EUR", "FR", "FR", false, "FR",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 2. OSS DE 19% EUR (IT->DE, declared in FR/Company? Or OSS Union)
    // Net 100, VAT 19
    activities.append(Activity::create(
        "S2", "A2", "", today, today, "EUR", "IT", "DE", false, "DE",
        Amount{119.0, 19.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 3. Domestic FR 20% USD
    // Net 100, VAT 20
    activities.append(Activity::create(
        "S3", "A3", "", today, today, "USD", "FR", "FR", false, "FR",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::DomesticVat, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());

    // 4. OSS AT 20% EUR (IT->AT)
    // Net 100, VAT 20
    activities.append(Activity::create(
        "S4", "A4", "", today, today, "EUR", "IT", "AT", false, "AT",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::EuOssUnion, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 5. IOSS ES 21% EUR (CN->ES)
    // Net 100, VAT 21
    activities.append(Activity::create(
        "S5", "A5", "", today, today, "EUR", "CN", "ES", false, "ES",
        Amount{121.0, 21.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::EuIoss, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());
    
    // 6. Exempt CH 0% USD (FR->CH)
    // Net 100, VAT 0
    activities.append(Activity::create(
        "S6", "A6", "", today, today, "USD", "FR", "CH", false, "CH",
        Amount{100.0, 0.0}, TaxSource::MarketplaceProvided, "FR", TaxScheme::Exempt, TaxJurisdictionLevel::Country, SaleType::Products
    ).value.value());

    auto shipment = QSharedPointer<Shipment>::create(activities, "", true);
    QMultiMap<QDateTime, QSharedPointer<Shipment>> shipments;
    shipments.insert(shipmentDateTime, shipment);

    // Execute
    auto entries = syncWait(factory.createEntryGrouped(&source, shipments));
    QVERIFY(!entries.isEmpty());

    // Validate each entry is balanced individually
    for (const auto &e : std::as_const(entries)) {
        QCOMPARE(e->getDebitSum(), e->getCreditSum());
    }

    // Aggregate credits and debits across all entries
    QList<JournalEntry::EntryLine> credits, debits;
    for (const auto &e : std::as_const(entries)) {
        credits += e->getCredits();
        debits  += e->getDebits();
    }
    
    // Credits:
    // 1. FR 20 EUR Revenue
    // 2. FR 20 EUR VAT
    // 3. DE 19 EUR Revenue
    // 4. DE 19 EUR VAT
    // 5. FR 20 USD Revenue
    // 6. FR 20 USD VAT
    // Total 6 lines?
    // Note: getAccounts("FR->FR", 20.0) -> Returns same account for EUR and EUR calls?
    // createEntry loop aggregates by VatKey (includes Currency).
    // So EUR and USD are separate VatKeys.
    // So yes, 6 lines.
    
    int countRevenueFR20EUR = 0;
    int countVatFR20EUR = 0;
    int countRevenueDE19EUR = 0;
    int countVatDE19EUR = 0;
    int countRevenueFR20USD = 0;
    int countVatFR20USD = 0;
    int countRevenueOSSAT = 0;
    int countVatOSSAT = 0;
    int countRevenueIOSSES = 0;
    int countVatIOSSES = 0;
    int countRevenueEXP = 0; // Exempt
    
    for (const auto &line : credits) {
        if (line.currency_amount.contains("USD")) {
            if (line.account.contains("7070DOMFR")) countRevenueFR20USD++;
            else if (line.account.contains("4457DOMFR")) countVatFR20USD++;
            else if (line.account.contains("7073EXPFR")) countRevenueEXP++;
        } else {
            if (line.account.contains("7070DOMFR")) countRevenueFR20EUR++; 
            else if (line.account.contains("4457DOMFR")) countVatFR20EUR++;
            else if (line.account.contains("7070OSSDE") || line.account.contains("7070DOMDE")) countRevenueDE19EUR++;
            else if (line.account.contains("4457OSSDE") || line.account.contains("4457DOMDE")) countVatDE19EUR++;
            else if (line.account.contains("7070OSSAT") || line.account.contains("7070DOMAT")) countRevenueOSSAT++;
            else if (line.account.contains("4457OSSAT") || line.account.contains("4457DOMAT")) countVatOSSAT++;
            else if (line.account.contains("7070IOSSES")) countRevenueIOSSES++;
            else if (line.account.contains("4457IOSSES")) countVatIOSSES++;
        }
    }
    
    // Note: Accounts might be "7070DOMFR20" etc.
    // SaleBookAccountsTable::fillIfEmpty logic: "7070DOM" + cCode + rStr => "7070DOMFR20"
    // "7070OSS" + cCode + rStr => "7070OSSDE19"
    
    // Verify counts (Checking simple existence first)
    // Actually, createEntry sums up amounts. 
    // FR 20 EUR Activity 1 is the only one. So 1 line.
    
    QVERIFY(countRevenueFR20EUR >= 1);
    QVERIFY(countVatFR20EUR >= 1);
    QVERIFY(countRevenueDE19EUR >= 1);
    QVERIFY(countVatDE19EUR >= 1);
    QVERIFY(countRevenueOSSAT >= 1);
    QVERIFY(countVatOSSAT >= 1);
    QVERIFY(countRevenueIOSSES >= 1);
    QVERIFY(countVatIOSSES >= 1);
    
    QVERIFY(countRevenueFR20USD >= 1);
    QVERIFY(countVatFR20USD >= 1);
    QVERIFY(countRevenueEXP >= 1);
    
    // Debits:
    // 1. EUR Customer (Total 239)
    // 2. USD Customer (Total 120)
    double sumCustomerEUR = 0;
    double sumCustomerUSD = 0;
    for (const auto &line : debits) {
        if (line.currency_amount.contains("USD")) {
            sumCustomerUSD += line.currency_amount["USD"];
        }
        else if (line.currency_amount.contains("EUR")) {
            sumCustomerEUR += line.currency_amount["EUR"];
        }
    }
    
    QCOMPARE(sumCustomerEUR, 480.0); // 239 + 120 + 121
    QCOMPARE(sumCustomerUSD, 220.0); // 120 + 100
    
    // Check at least one line exists (implied by non-zero sum, but explicit check good)
    QVERIFY(sumCustomerEUR > 0.0);
    QVERIFY(sumCustomerUSD > 0.0);

    // Verify that no entry title contains the same VAT scheme abbreviation twice.
    // Each JournalGroup is keyed by (scheme, rate), so the same scheme can appear
    // at most once per entry by construction.
    for (const auto &e : std::as_const(entries)) {
        const QList<JournalEntry::EntryLine> allLines = e->getDebits() + e->getCredits();
        if (allLines.isEmpty()) continue;
        const QString &title = allLines.first().title;
        const int sep = title.indexOf(" | ");
        QVERIFY2(sep != -1, qPrintable("Entry title missing ' | ' separator: " + title));
        const QStringList vatParts = title.mid(sep + 3).split(", ");
        QSet<QString> schemesInEntry;
        for (const QString &part : vatParts) {
            const QStringList splitPart = part.split(' ');
            const QString scheme = splitPart.first();
            QVERIFY2(!schemesInEntry.contains(scheme),
                qPrintable(QString("Duplicate VAT scheme '%1' found in entry title: %2")
                    .arg(scheme, title)));
            schemesInEntry.insert(scheme);
        }
    }
}

void TestBookEntries::test_factory_single_shipment()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup company info
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts, &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    // 1. Test null shipment returns nullptr
    auto nullResult = syncWait(factory.createEntry(QSharedPointer<Shipment>(), nullptr));
    QVERIFY(nullResult.isNull());
    
    // 2. Test empty activities returns nullptr
    QList<Activity> emptyActivities;
    auto emptyShipment = QSharedPointer<Shipment>::create(emptyActivities, "", true);
    auto emptyResult = syncWait(factory.createEntry(emptyShipment, nullptr));
    QVERIFY(emptyResult.isNull());
    
    // 3. Create shipment with single activity
    auto activityResult = Activity::create(
        "ORDER-123", "ACT-001", "", QDateTime(QDate(2024, 6, 15), QTime(12, 0)), QDateTime(QDate(2024, 6, 15), QTime(12, 0)),
        "EUR", "FR", "FR", false, "FR",
        Amount{120.0, 20.0}, TaxSource::MarketplaceProvided,
        "FR", TaxScheme::DomesticVat, TaxJurisdictionLevel::Country,
        SaleType::Service
    );
    QVERIFY(activityResult.ok());
    
    QList<Activity> activities;
    activities.append(activityResult.value.value());
    auto shipment = QSharedPointer<Shipment>::create(activities, "", true);
    
    // 4. Verify shipment ID is available
    QVERIFY(!shipment->getId().isEmpty());
    
    // 5. Create entry
    auto entry = syncWait(factory.createEntry(shipment, nullptr));
    QVERIFY(!entry.isNull());
    
    // 6. Verify debit/credit sums are equal
    double debitSum = entry->getDebitSum();
    double creditSum = entry->getCreditSum();
    QCOMPARE(debitSum, creditSum);
    
    // 7. Verify total amount (100 untaxed + 20 tax = 120 TTC)
    QCOMPARE(debitSum, 120.0);
    
    // 8. Verify title contains "Vente Service" 
    const auto &debits = entry->getDebits();
    QVERIFY(!debits.isEmpty());
    QVERIFY(debits.first().title.startsWith("Vente Service"));
    
    // 9. Verify credit has revenue and VAT entries
    const auto &credits = entry->getCredits();
    QVERIFY(credits.size() >= 2); // At least revenue + VAT
    
    // 10. Verify amounts on credit side
    double totalCreditAmount = 0;
    for (const auto &line : credits) {
        totalCreditAmount += line.currency_amount.value("EUR", 0.0);
    }
    QCOMPARE(totalCreditAmount, 120.0);
}

void TestBookEntries::test_factory_bank_entry()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    
    // Setup company info
    QString companyInfoPath = dir.filePath("company.csv");
    QFile companyFile(companyInfoPath);
    companyFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&companyFile);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
    companyFile.close();
    
    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);
    
    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts, &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    // Create BooksConnections for bank table
    BooksConnections booksConnections(dir);
    
    // Create bank table with test data
    BankQontoTable bankTable(&booksConnections, dir);
    bankTable.init();
    
    // Manually add test data using the model's add method
    // add(rowId, bookId, date, amountFullOrig, currencyAmount, label, account1, account2, vatOrig, vatCountry, vatCurrency)
    bankTable.add("ROW-001", "", QDate(2024, 5, 10), -150.0, "EUR", "Office supplies purchase", "", "", 0.0, "", "");
    bankTable.add("ROW-002", "", QDate(2024, 5, 11), 500.0, "EUR", "Client payment", "", "", 0.0, "", "");
    
    // 1. Test null bankTable returns nullptr
    auto nullResult = factory.createEntry(nullptr, "607000", 0);
    QVERIFY(nullResult.isNull());
    
    // 2. Test negative row returns nullptr
    auto negResult = factory.createEntry(&bankTable, "607000", -1);
    QVERIFY(negResult.isNull());
    
    // 3. Create entry for expense (negative amount - money out)
    QString expenseAccount = "607000"; // Purchases account
    auto expenseEntry = factory.createEntry(&bankTable, expenseAccount, 1);
    QVERIFY(!expenseEntry.isNull());
    
    // 4. Verify debit/credit sums are equal
    QCOMPARE(expenseEntry->getDebitSum(), expenseEntry->getCreditSum());
    
    // 5. Verify amount matches bank row
    QCOMPARE(expenseEntry->getDebitSum(), 150.0);
    
    // 6. Verify expense is on debit side (money going out)
    const auto &debits = expenseEntry->getDebits();
    QVERIFY(!debits.isEmpty());
    bool foundExpense = false;
    for (const auto &line : debits) {
        if (line.account == expenseAccount) {
            foundExpense = true;
            QCOMPARE(line.currency_amount["EUR"], 150.0);
        }
    }
    QVERIFY(foundExpense);
    
    // 7. Verify bank account is on credit side (money leaving bank)
    const auto &credits = expenseEntry->getCredits();
    QVERIFY(!credits.isEmpty());
    bool foundBank = false;
    for (const auto &line : credits) {
        if (line.account.startsWith("512")) { // Bank accounts start with 512
            foundBank = true;
            QCOMPARE(line.currency_amount["EUR"], 150.0);
        }
    }
    QVERIFY(foundBank);
    
    // 8. Create entry for revenue (positive amount - money in)
    QString revenueAccount = "706000"; // Service revenue
    auto revenueEntry = factory.createEntry(&bankTable, revenueAccount, 0);
    QVERIFY(!revenueEntry.isNull());
    
    // 9. Verify revenue amount
    QCOMPARE(revenueEntry->getDebitSum(), 500.0);
    
    // 10. Verify bank is on debit side (money coming into bank) and revenue on credit
    const auto &revDebits = revenueEntry->getDebits();
    bool foundBankDebit = false;
    for (const auto &line : revDebits) {
        if (line.account.startsWith("512")) {
            foundBankDebit = true;
        }
    }
    QVERIFY(foundBankDebit);
    
    const auto &revCredits = revenueEntry->getCredits();
    bool foundRevenue = false;
    for (const auto &line : revCredits) {
        if (line.account == revenueAccount) {
            foundRevenue = true;
            QCOMPARE(line.currency_amount["EUR"], 500.0);
        }
    }
    QVERIFY(foundRevenue);
}

void TestBookEntries::test_invoice_save_load_full_fields()
{
    // ── Case 1: full data (two VAT rates + EXTRA + route + flags) ────────────
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    QString sourcePath = dir.filePath("source.pdf");
    { QFile f(sourcePath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }

    PurchaseInformation orig;
    orig.date             = QDate(2025, 7, 14);
    orig.account          = "607000";
    orig.label            = "Computer stock DDP";  // "stock" → isInventory, "DDP" → isDDP
    orig.accountSupplier  = "TechCorpCNFR";        // suffix "CNFR" → countryCodeFrom=CN, countryCodeTo=FR
    orig.vatTokens        = {"FR-TVA20-24.0EUR", "FR-TVA5.5-5.5EUR"};
    orig.totalAmount      = 159.5;
    orig.rawTotalAmount   = "159.5";
    orig.currency         = "EUR";
    orig.subUntaxedAmount["607001"] = 10.0;

    PurchaseInvoiceManager manager(dir, "FR");
    manager.add(sourcePath, orig);
    QCOMPARE(manager.rowCount(), 1);

    const auto savedInvoices = manager.getInvoices(QDate(2025, 1, 1), QDate(2025, 12, 31));
    const PurchaseInformation saved = savedInvoices.first();

    // Basic fields
    QCOMPARE(saved.date,             orig.date);
    QCOMPARE(saved.account,          orig.account);
    QCOMPARE(saved.label,            orig.label);
    QCOMPARE(saved.accountSupplier,  orig.accountSupplier);
    QCOMPARE(saved.totalAmount,      orig.totalAmount);
    QCOMPARE(saved.rawTotalAmount,   orig.rawTotalAmount);
    QCOMPARE(saved.currency,         orig.currency);
    QCOMPARE(saved.originalExtension, QString("pdf"));
    QVERIFY(!saved.filePath.isEmpty());

    // Flags detected from filename content
    QCOMPARE(saved.isInventory, true);
    QCOMPARE(saved.isDDP,       true);

    // Country codes parsed from supplier suffix
    QCOMPARE(saved.countryCodeFrom, QString("CN"));
    QCOMPARE(saved.countryCodeTo,   QString("FR"));

    // VAT tokens preserved verbatim
    QCOMPARE(saved.vatTokens.size(), 2);
    QVERIFY(saved.vatTokens.contains(QString("FR-TVA20-24.0EUR")));
    QVERIFY(saved.vatTokens.contains(QString("FR-TVA5.5-5.5EUR")));

    // country_vatRate_vat: correct country code "FR", no spurious "TVA" key
    QVERIFY( saved.country_vatRate_vat.contains("FR"));
    QVERIFY(!saved.country_vatRate_vat.contains("TVA"));
    QCOMPARE(saved.country_vatRate_vat["FR"].size(), 2);
    // Allow either format like in proportion check
    if (saved.country_vatRate_vat["FR"].contains("0.20")) {
        QCOMPARE(saved.country_vatRate_vat["FR"]["0.20"], 24.0);
    } else {
        QCOMPARE(saved.country_vatRate_vat["FR"]["0.2"], 24.0);
    }
    
    if (saved.country_vatRate_vat["FR"].contains("0.055")) {
        QCOMPARE(saved.country_vatRate_vat["FR"]["0.055"],   5.5);
    } else {
        // Just in case it preserves some formatting differently
        QVERIFY(saved.country_vatRate_vat["FR"].contains("0.055") || saved.country_vatRate_vat["FR"].contains("0.055"));
        QCOMPARE(saved.country_vatRate_vat["FR"]["0.055"],   5.5);
    }

    // rawVatAmount / vatCurrency / vatCountry derived by decode (24.0 + 5.5 = 29.5)
    QCOMPARE(saved.rawVatAmount.toDouble(), 29.5);
    QCOMPARE(saved.vatCurrency, QString("EUR"));
    QCOMPARE(saved.vatCountry,  QString("FR"));

    // subUntaxedAmount round-trip
    QCOMPARE(saved.subUntaxedAmount.size(), 1);
    QVERIFY(saved.subUntaxedAmount.contains("607001"));
    QCOMPARE(saved.subUntaxedAmount["607001"], 10.0);

    // ── Case 2: no VAT, no extras ─────────────────────────────────────────────
    QString src2Path = dir.filePath("source2.pdf");
    { QFile f(src2Path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("y"); }

    PurchaseInformation plain;
    plain.date            = QDate(2025, 8, 1);
    plain.account         = "622600";
    plain.label           = "fees";
    plain.accountSupplier = "MyBank";
    plain.totalAmount     = 50.0;
    plain.rawTotalAmount  = "50.0";
    plain.currency        = "USD";

    manager.add(src2Path, plain);
    QCOMPARE(manager.rowCount(), 2);

    const auto plainInvoices = manager.getInvoices(QDate(2025, 8, 1), QDate(2025, 8, 31));
    const PurchaseInformation savedPlain = plainInvoices.first();

    QCOMPARE(savedPlain.date,            plain.date);
    QCOMPARE(savedPlain.account,         plain.account);
    QCOMPARE(savedPlain.label,           plain.label);
    QCOMPARE(savedPlain.accountSupplier, plain.accountSupplier);
    QCOMPARE(savedPlain.totalAmount,     plain.totalAmount);
    QCOMPARE(savedPlain.currency,        plain.currency);
    QCOMPARE(savedPlain.isInventory,     false);
    QCOMPARE(savedPlain.isDDP,           false);
    QVERIFY(savedPlain.vatTokens.isEmpty());
    QVERIFY(savedPlain.country_vatRate_vat.isEmpty());
    QVERIFY(savedPlain.rawVatAmount.isEmpty());
    QVERIFY(savedPlain.vatCurrency.isEmpty());
    QVERIFY(savedPlain.subUntaxedAmount.isEmpty());
    QVERIFY(savedPlain.vatCountry.isEmpty());
    QVERIFY(savedPlain.countryCodeFrom.isEmpty());
    QCOMPARE(savedPlain.countryCodeTo, QString("FR")); // no route in filename → defaults to company country

    // ── Case 3: simple 2-part VAT token generated by dialogs ─────────────────
    // "TVA-{amount}{currency}" has 2 parts when split by '-', so decode must NOT
    // create a country_vatRate_vat entry (country would otherwise be "TVA").
    QString fileSimpleVat =
        "2025-09-10__607000__Office__Supplier__TVA-15.0EUR__115.0EUR.pdf";
    PurchaseInformation infoSV = PurchaseInvoiceManager::decode(fileSimpleVat, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(infoSV.vatTokens.size(), 1);
    QCOMPARE(infoSV.vatTokens.first(), QString("TVA-15.0EUR"));
    QVERIFY(infoSV.country_vatRate_vat.isEmpty());  // no "TVA" country
    QCOMPARE(infoSV.rawVatAmount.toDouble(), 15.0);
    QCOMPARE(infoSV.vatCurrency, QString("EUR"));
    QVERIFY(infoSV.vatCountry.isEmpty());  // 2-part token has no country
    QCOMPARE(PurchaseInvoiceManager::encode(infoSV), fileSimpleVat); // round-trip
}

void TestBookEntries::test_invoice_decode_no_country_code()
{
    // "FUBER" has no two-letter country-code suffix pair, so countryCodeFrom is empty
    // and countryCodeTo defaults to the company country.
    QString fileName = "2026-01-05__625100__frais-deplacement__FUBER__FR-TVA-1.81EUR__19.91EUR.pdf";
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QVERIFY(info.countryCodeFrom.isEmpty());
    QCOMPARE(info.countryCodeTo, QString("FR"));
}

void TestBookEntries::test_invoice_label_country_decode()
{
    QString fileName = "2026-01-09__604000__photographie-PH-FR__FCIPID__39GBP.pdf";
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");
    
    QCOMPARE(info.date.toString(Qt::ISODate), QString("2026-01-09"));
    QCOMPARE(info.account, QString("604000"));
    QCOMPARE(info.label, QString("photographie-PH-FR"));
    QCOMPARE(info.accountSupplier, QString("FCIPID"));
    QCOMPARE(info.countryCodeFrom, QString("PH"));
    QCOMPARE(info.countryCodeTo, QString("FR"));
    QCOMPARE(info.totalAmount, 39.0);
    QCOMPARE(info.currency, QString("GBP"));
    
    // Encode back
    QString encoded = PurchaseInvoiceManager::encode(info);
    QCOMPARE(encoded, fileName);
    
    // Decode again
    PurchaseInformation info2 = PurchaseInvoiceManager::decode(encoded, &decodeTestPurchaseTable(), "FR");
    QCOMPARE(info2.countryCodeFrom, QString("PH"));
    QCOMPARE(info2.countryCodeTo, QString("FR"));
}

void TestBookEntries::test_invoice_no_country_from_short_supplier()
{
    // "FNEEDE" ends with "EE" (Estonia) + "DE" (Germany) which are both valid
    // ISO 3166 country codes, but the supplier name is too short (6 chars) to
    // carry a meaningful 3-char prefix + 4-char route suffix.
    // The decoder must NOT extract country codes in this situation.
    const QString fileName =
        "2026-01-02__622600__compta__FNEEDE__FR-TVA-50EUR__300EUR.pdf";

    // ── First decode ──────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date, QDate(2026, 1, 2));
    QCOMPARE(info.account, QString("622600"));
    QCOMPARE(info.label, QString("compta"));
    QCOMPARE(info.accountSupplier, QString("FNEEDE"));
    QVERIFY(info.countryCodeFrom.isEmpty());
    QCOMPARE(info.countryCodeTo, QString("FR")); // no route → defaults to company country

    QCOMPARE(info.totalAmount, 300.0);
    QCOMPARE(info.currency, QString("EUR"));

    // VAT token preserved verbatim, amount 50 EUR, rate computed to 0.2
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    QVERIFY(info.country_vatRate_vat["FR"].contains("0.2"));
    QCOMPARE(info.country_vatRate_vat["FR"]["0.2"], 50.0);

    // ── Encode → same filename ────────────────────────────────────────────────
    const QString encoded = PurchaseInvoiceManager::encode(info);
    QCOMPARE(encoded, fileName);

    // ── Second decode → countryCodeFrom still absent, countryCodeTo defaults to company ──
    PurchaseInformation info2 = PurchaseInvoiceManager::decode(encoded, &decodeTestPurchaseTable(), "FR");
    QVERIFY(info2.countryCodeFrom.isEmpty());
    QCOMPARE(info2.countryCodeTo, QString("FR"));
    QCOMPARE(info2.totalAmount, 300.0);
    QCOMPARE(info2.accountSupplier, QString("FNEEDE"));
}

// ── helpers shared by the two multi-rate tests ────────────────────────────────
static void setupCompanyInfoFr(const QDir &dir)
{
    QString companyInfoPath = dir.filePath("company.csv");
    QFile f(companyInfoPath);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&f);
    out << "Id;Parameter;Value\n";
    out << "Currency;Currency;EUR\n";
    out << "Country;Country Code;FR\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: two VAT rates on the same invoice (FR 20 % + FR 5.5 %) both configured
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_purchase_multi_vat_rates()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);

    CompanyInfosTable companyInfos(dir);
    CurrencyRateManager currencyManager(dir, "");
    BooksAccountsSalesTable saleAccounts(dir);
    BookAccountPurchaseTable purchaseAccounts(dir, "FR");
    JournalTable journalTable(dir);

    // Accounts for both rates that appear in the filename
    // Account FR 0.2 is created by default
    purchaseAccounts.addAccount("FR", 0.055,  "445661", "445711");
    // Explicitly add DE so it doesn't fail lookup during factory creation
    purchaseAccounts.addAccount("DE", 0.19, "44566DE", "44571DE");
    BookAccountSelfVatTable selfVatAccounts(dir, "FR");
    AmzPaymentSettings amzPaymentSettings(dir);
    BookAccountAmzBalanceTable amzBalanceTable(dir);
    BookAccountsGroupedSalesTable groupedSaleAccounts(dir);
    JournalEntryFactory factory(&currencyManager, &companyInfos, &saleAccounts, &groupedSaleAccounts, &purchaseAccounts, &journalTable,
                                &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

    PurchaseInformation info = PurchaseInvoiceManager::decode(
        "2026-01-05__625100__frais-deplacement__FUBER__FR-TVA20-1.81EUR__FR-TVA5.5-1EUR__19.91EUR.pdf", &decodeTestPurchaseTable(), "FR");

    // Both FR|20 and FR|5.5 accounts exist → must not throw
    QSharedPointer<JournalEntry> entry;
    try {
        entry = factory.createEntry(info);
    } catch (const ExceptionWithTitleText &e) {
        QFAIL(QString("Unexpected exception: %1 – %2")
                  .arg(e.errorTitle(), e.errorText()).toUtf8().constData());
    }

    QVERIFY(!entry.isNull());

    // Both VAT debit lines must be present
    const auto &debits = entry->getDebits();
    bool found20 = false, found55 = false;
    for (const auto &line : debits) {
        if (line.account == "445660") { found20 = true; QCOMPARE(line.currency_amount["EUR"], 1.81); }
        if (line.account == "445661") { found55 = true; QCOMPARE(line.currency_amount["EUR"], 1.0);  }
    }
    QVERIFY(found20);
    QVERIFY(found55);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: FR 6 % rate has no configured account → ExceptionWithTitleText expected
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_purchase_missing_vat_rate()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    BookAccountPurchaseTable purchaseAccounts(dir, "FR");

    // Only FR 20 % is configured by default – FR 6 % deliberately omitted.
    // decode() validates VAT accounts, so it must throw "Account Missing"
    bool caught = false;
    try {
        PurchaseInvoiceManager::decode(
            "2026-01-05__625100__frais-deplacement__FUBER__FR-TVA20-1.81EUR__FR-TVA6-1EUR__19.91EUR.pdf",
            &purchaseAccounts, "FR");
    } catch (const ExceptionWithTitleText &e) {
        caught = true;
        QCOMPARE(e.errorTitle(), QString("Account Missing"));
    }
    QVERIFY2(caught, "ExceptionWithTitleText should have been thrown for missing FR 6% VAT account");
}

// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: intracom EU supplier (DE→FR) with rawVatAmount → 2 extra lines
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_intracom_eu()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2025, 6, 1);
    p.account         = "607000";
    p.label           = "goods";
    p.accountSupplier = "GMBHDE";
    p.totalAmount     = 100.0;
    p.currency        = "EUR";
    p.rawVatAmount    = "20.0";   // buyer-declared self-VAT
    p.vatCurrency     = "EUR";
    p.countryCodeFrom = "DE";     // EU supplier
    p.countryCodeTo   = "FR";     // company country
    // country_vatRate_vat intentionally empty → triggers self-VAT path

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());

    // Entry must balance
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    // Debits: expense(100) + self-VAT deductible(20) = 2 lines
    const auto &debits = entry->getDebits();
    QCOMPARE(debits.size(), 2);

    // Credits: supplier(100) + self-VAT due(20) = 2 lines
    const auto &credits = entry->getCredits();
    QCOMPARE(credits.size(), 2);

    // Find each specific line
    bool foundExpense    = false;
    bool foundDeductible = false;
    for (const auto &line : debits) {
        if (line.account == "607000") {
            foundExpense = true;
            QCOMPARE(line.currency_amount["EUR"], 100.0);
        }
        if (line.account == "445662") {   // default EU deductible
            foundDeductible = true;
            QCOMPARE(line.currency_amount["EUR"], 20.0);
        }
    }
    QVERIFY(foundExpense);
    QVERIFY(foundDeductible);

    bool foundSupplier = false;
    bool foundDue      = false;
    for (const auto &line : credits) {
        if (line.account == "GMBHDE") {
            foundSupplier = true;
            QCOMPARE(line.currency_amount["EUR"], 100.0);
        }
        if (line.account == "445200") {   // default EU due
            foundDue = true;
            QCOMPARE(line.currency_amount["EUR"], 20.0);
        }
    }
    QVERIFY(foundSupplier);
    QVERIFY(foundDue);

    // Total sums
    QCOMPARE(entry->getDebitSum(),  120.0);
    QCOMPARE(entry->getCreditSum(), 120.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: extracom non-EU supplier (CN→FR) → same default accounts, non-EU row
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_extracom_noneu()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2025, 7, 1);
    p.account         = "607000";
    p.label           = "import";
    p.accountSupplier = "SUPPLCN";
    p.totalAmount     = 200.0;
    p.currency        = "EUR";
    p.rawVatAmount    = "40.0";
    p.vatCurrency     = "EUR";
    p.countryCodeFrom = "CN";    // non-EU
    p.countryCodeTo   = "FR";

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    const auto &debits  = entry->getDebits();
    const auto &credits = entry->getCredits();
    QCOMPARE(debits.size(),  2);
    QCOMPARE(credits.size(), 2);

    bool foundDeductible = false;
    for (const auto &line : debits)
        if (line.account == "445663") { foundDeductible = true; QCOMPARE(line.currency_amount["EUR"], 40.0); }
    QVERIFY(foundDeductible);

    bool foundDue = false;
    for (const auto &line : credits)
        if (line.account == "445300") { foundDue = true; QCOMPARE(line.currency_amount["EUR"], 40.0); }
    QVERIFY(foundDue);

    QCOMPARE(entry->getDebitSum(),  240.0);
    QCOMPARE(entry->getCreditSum(), 240.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: domestic FR→FR, rawVatAmount set → no auto-liquidation (not intracom)
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_domestic_no_autoliquidation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2025, 8, 1);
    p.account         = "622600";
    p.label           = "services";
    p.accountSupplier = "FRSUPP";
    p.totalAmount     = 100.0;
    p.currency        = "EUR";
    p.rawVatAmount    = "20.0";   // set, but route is domestic → no self-VAT
    p.vatCurrency     = "EUR";
    p.countryCodeFrom = "FR";     // same as company → domestic
    p.countryCodeTo   = "FR";

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    // Only expense debit + supplier credit → 1 + 1
    QCOMPARE(entry->getDebits().size(),  1);
    QCOMPARE(entry->getCredits().size(), 1);

    // No self-VAT accounts anywhere
    for (const auto &line : entry->getDebits())
        QVERIFY(line.account != "445663");
    for (const auto &line : entry->getCredits())
        QVERIFY(line.account != "445300");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: third-party route (CN→DE, company=FR) → no auto-liquidation
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_thirdparty_no_autoliquidation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2025, 9, 1);
    p.account         = "607000";
    p.label           = "goods";
    p.accountSupplier = "DESUPP";
    p.totalAmount     = 100.0;
    p.currency        = "EUR";
    p.rawVatAmount    = "20.0";
    p.vatCurrency     = "EUR";
    p.countryCodeFrom = "CN";    // third-party: countryTo != FR
    p.countryCodeTo   = "DE";    // company is FR, so this route returns empty

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());

    QCOMPARE(entry->getDebits().size(),  1);   // expense only
    QCOMPARE(entry->getCredits().size(), 1);   // supplier only
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    for (const auto &line : entry->getDebits() + entry->getCredits()) {
        QVERIFY(line.account != "445663");
        QVERIFY(line.account != "445300");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: route qualifies but rawVatAmount is empty → no self-VAT lines
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_no_amount_no_lines()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2025, 10, 1);
    p.account         = "607000";
    p.label           = "goods";
    p.accountSupplier = "DESUPP";
    p.totalAmount     = 100.0;
    p.currency        = "EUR";
    p.rawVatAmount    = "0"; // explicit zero: accountant chose no self-VAT
    p.countryCodeFrom = "DE";
    p.countryCodeTo   = "FR";

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());

    // Route qualifies but amount explicitly "0" → no self-VAT lines
    QCOMPARE(entry->getDebits().size(),  1);
    QCOMPARE(entry->getCredits().size(), 1);
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
    QCOMPARE(entry->getDebitSum(), 100.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: invoice already has normal VAT → condition not met → no auto-liquidation
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_has_normal_vat_no_autoliquidation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2025, 11, 1);
    p.account         = "607000";
    p.label           = "goods-with-vat";
    p.accountSupplier = "DESUPP";
    p.totalAmount     = 120.0;
    p.currency        = "EUR";
    p.rawVatAmount    = "20.0";
    p.vatCurrency     = "EUR";
    p.countryCodeFrom = "DE";
    p.countryCodeTo   = "FR";
    // Normal VAT present → self-VAT must NOT fire
    p.country_vatRate_vat["FR"]["0.2"] = 20.0;

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());

    // Debits: expense(100) + regular VAT debit6(20) = 2 lines
    QCOMPARE(entry->getDebits().size(),  2);
    // Credits: supplier(120) = 1 line  (no self-VAT due line)
    QCOMPARE(entry->getCredits().size(), 1);
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    // No self-VAT due account
    for (const auto &line : entry->getCredits())
        QVERIFY(line.account != "445300");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: custom self-VAT accounts are used in the entry
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_custom_accounts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");

    // Customise EU accounts before building the factory
    sva.setData(sva.index(0, 1), "CUSTOM_DED", Qt::EditRole);
    sva.setData(sva.index(0, 2), "CUSTOM_DUE", Qt::EditRole);

    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2025, 12, 1);
    p.account         = "607000";
    p.label           = "custom";
    p.accountSupplier = "EUSUPP";
    p.totalAmount     = 100.0;
    p.currency        = "EUR";
    p.rawVatAmount    = "20.0";
    p.vatCurrency     = "EUR";
    p.countryCodeFrom = "IT";    // EU member
    p.countryCodeTo   = "FR";

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    bool foundCustomDed = false;
    for (const auto &line : entry->getDebits())
        if (line.account == "CUSTOM_DED") { foundCustomDed = true; QCOMPARE(line.currency_amount["EUR"], 20.0); }
    QVERIFY(foundCustomDed);

    bool foundCustomDue = false;
    for (const auto &line : entry->getCredits())
        if (line.account == "CUSTOM_DUE") { foundCustomDue = true; QCOMPARE(line.currency_amount["EUR"], 20.0); }
    QVERIFY(foundCustomDue);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: refund with self-VAT → deductible on credit, due on debit
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_refund()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    PurchaseInformation p;
    p.date            = QDate(2026, 1, 1);
    p.account         = "607000";
    p.label           = "refund-goods";
    p.accountSupplier = "DESUPP";
    p.totalAmount     = -100.0;  // refund
    p.currency        = "EUR";
    p.rawVatAmount    = "20.0";
    p.vatCurrency     = "EUR";
    p.countryCodeFrom = "DE";
    p.countryCodeTo   = "FR";

    auto entry = f.createEntry(p);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    // For refund: expense goes to credit, supplier goes to debit
    // Self-VAT deductible (normally debit) → credit for refund
    // Self-VAT due (normally credit) → debit for refund
    bool foundDeductibleOnCredit = false;
    for (const auto &line : entry->getCredits())
        if (line.account == "445662") { foundDeductibleOnCredit = true; QCOMPARE(line.currency_amount["EUR"], 20.0); }
    QVERIFY(foundDeductibleOnCredit);

    bool foundDueOnDebit = false;
    for (const auto &line : entry->getDebits())
        if (line.account == "445200") { foundDueOnDebit = true; QCOMPARE(line.currency_amount["EUR"], 20.0); }
    QVERIFY(foundDueOnDebit);

    // Total must still be 120 (100 expense + 20 self-VAT) on each side
    QCOMPARE(entry->getDebitSum(),  120.0);
    QCOMPARE(entry->getCreditSum(), 120.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9: real invoice filename with route in label, no VAT token in filename
//         → factory must default to 20% self-VAT on totalAmount
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_selfvat_invoice_us_fr_label_route()
{
    // US→FR non-EU service invoice (e.g. OpenAI API).  No VAT appears in the
    // filename; the route is encoded in the label as "-US-FR".
    const QString fileName =
        "2026-01-06__622810__api-web-openai-US-FR__FOPENA__25.73EUR.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date, QDate(2026, 1, 6));
    QCOMPARE(info.account, QString("622810"));
    QCOMPARE(info.label, QString("api-web-openai-US-FR"));
    QCOMPARE(info.accountSupplier, QString("FOPENA"));
    // Route comes from label suffix "-US-FR", NOT from the supplier name
    QCOMPARE(info.countryCodeFrom, QString("US"));
    QCOMPARE(info.countryCodeTo,   QString("FR"));
    QCOMPARE(info.totalAmount, 25.73);
    QCOMPARE(info.currency, QString("EUR"));
    QVERIFY(info.country_vatRate_vat.isEmpty()); // no VAT on the invoice
    QVERIFY(info.rawVatAmount.isEmpty());        // rawVatAmount absent from filename

    // ── Encode roundtrip ──────────────────────────────────────────────────────
    // rawVatAmount is not part of the filename encoding, so the roundtrip is exact
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── Build factory with FR company ─────────────────────────────────────────
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(info);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    // rawVatAmount is empty → factory falls back to totalAmountAbs * 20%
    // selfVatAmount = 25.73 * 0.20 = 5.146 EUR
    // Debits : expense(25.73) + TVA déductible(5.146) = 2 lines
    QCOMPARE(entry->getDebits().size(),  2);
    // Credits: supplier(25.73) + TVA due(5.146) = 2 lines
    QCOMPARE(entry->getCredits().size(), 2);

    bool foundExpense    = false;
    bool foundDeductible = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "622810") foundExpense    = true;
        if (line.account == "445663") foundDeductible = true; // default non-EU deductible
    }
    QVERIFY(foundExpense);
    QVERIFY(foundDeductible);

    bool foundSupplier = false;
    bool foundDue      = false;
    for (const auto &line : entry->getCredits()) {
        if (line.account == "FOPENA") foundSupplier = true;
        if (line.account == "445300") foundDue      = true; // default non-EU due
    }
    QVERIFY(foundSupplier);
    QVERIFY(foundDue);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: real Amazon EU invoice — route "-EU-FR" embedded in label
//       decode must extract countryCodeFrom="EU", encode must round-trip,
//       factory must use the EU self-VAT row
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_eu_fr_label_route()
{
    const QString fileName =
        "2026-01-31__622201__frais-vente-FR-AEU-2026-27277-EU-FR__FAMAZON__88.56GBP.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-vente-FR-AEU-2026-27277-EU-FR"));
    QCOMPARE(info.accountSupplier, QString("FAMAZON"));
    // Route comes from label suffix "-EU-FR"
    QCOMPARE(info.countryCodeFrom, QString("EU"));
    QCOMPARE(info.countryCodeTo,   QString("FR"));
    QCOMPARE(info.totalAmount,     88.56);
    QCOMPARE(info.currency,        QString("GBP"));
    QVERIFY(info.country_vatRate_vat.isEmpty()); // no VAT token in filename
    QVERIFY(info.rawVatAmount.isEmpty());

    // ── Encode round-trip ─────────────────────────────────────────────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── Build factory with FR company ─────────────────────────────────────────
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-31", "GBP", "EUR", 1.20);
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(info);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    // rawVatAmount is empty → factory falls back to totalAmountAbs * 20 %
    // selfVatAmount = 88.56 * 0.20 = 17.712 GBP
    // Debits : expense(88.56) + TVA déductible(17.712) = 2 lines
    QCOMPARE(entry->getDebits().size(),  2);
    // Credits: supplier(88.56) + TVA due(17.712) = 2 lines
    QCOMPARE(entry->getCredits().size(), 2);

    // "EU" as countryFrom → treated as EU intracom → EU self-VAT row
    const QString euDeductible = sva.getAccountVatDeductible("EU", "FR");
    const QString euDue        = sva.getAccountVatDue("EU",        "FR");
    QVERIFY(!euDeductible.isEmpty());
    QVERIFY(!euDue.isEmpty());

    bool foundExpense    = false;
    bool foundDeductible = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "622201")       foundExpense    = true;
        if (line.account == euDeductible)   foundDeductible = true;
    }
    QVERIFY(foundExpense);
    QVERIFY(foundDeductible);

    bool foundSupplier = false;
    bool foundDue      = false;
    for (const auto &line : entry->getCredits()) {
        if (line.account == "FAMAZON") foundSupplier = true;
        if (line.account == euDue)     foundDue      = true;
    }
    QVERIFY(foundSupplier);
    QVERIFY(foundDue);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Amazon GB→FR invoice — route "-GB-FR" embedded in label
//       GB is post-Brexit non-EU → factory must use the non-EU self-VAT row
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_gb_fr_label_route_noneu()
{
    const QString fileName =
        "2026-01-31__622201__frais-publicite-56780M7PA26-GB-FR__FAMAZON__61.05GBP.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-publicite-56780M7PA26-GB-FR"));
    QCOMPARE(info.accountSupplier, QString("FAMAZON"));
    // Route comes from label suffix "-GB-FR"
    QCOMPARE(info.countryCodeFrom, QString("GB"));
    QCOMPARE(info.countryCodeTo,   QString("FR"));
    QCOMPARE(info.totalAmount,     61.05);
    QCOMPARE(info.currency,        QString("GBP"));
    QVERIFY(info.country_vatRate_vat.isEmpty());
    QVERIFY(info.rawVatAmount.isEmpty());

    // ── Encode round-trip ─────────────────────────────────────────────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── GB is non-EU (post-Brexit) in BookAccountSelfVatTable ─────────────────
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    BookAccountSelfVatTable sva(dir, "FR");
    // GB → FR must resolve to the non-EU row, same as CN → FR
    QCOMPARE(sva.getAccountVatDeductible("GB", "FR"),
             sva.getAccountVatDeductible("CN", "FR"));
    QCOMPARE(sva.getAccountVatDue("GB", "FR"),
             sva.getAccountVatDue("CN", "FR"));
    // And must differ from an EU intracom route
    QVERIFY(sva.getAccountVatDeductible("GB", "FR") !=
            sva.getAccountVatDeductible("DE", "FR"));

    // ── Factory uses non-EU self-VAT accounts ─────────────────────────────────
    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-31", "GBP", "EUR", 1.20);
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(info);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());

    // rawVatAmount empty → factory defaults to totalAmountAbs * 20 %
    QCOMPARE(entry->getDebits().size(),  2);
    QCOMPARE(entry->getCredits().size(), 2);

    const QString nonEuDeductible = sva.getAccountVatDeductible("GB", "FR");
    const QString nonEuDue        = sva.getAccountVatDue("GB",        "FR");

    bool foundExpense    = false;
    bool foundDeductible = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "622201")         foundExpense    = true;
        if (line.account == nonEuDeductible)  foundDeductible = true;
    }
    QVERIFY(foundExpense);
    QVERIFY(foundDeductible);

    bool foundSupplier = false;
    bool foundDue      = false;
    for (const auto &line : entry->getCredits()) {
        if (line.account == "FAMAZON")  foundSupplier = true;
        if (line.account == nonEuDue)   foundDue      = true;
    }
    QVERIFY(foundSupplier);
    QVERIFY(foundDue);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Amazon TR→FR invoice with TRY currency
//       TR (Turkey) is a supported non-EU country; TRY is a supported currency
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_tr_fr_label_route_try_currency()
{
    const QString fileName =
        "2026-01-31__622201__frais-publicite-ADA2026000040109-TR-FR__FAMAZON__35.47TRY.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-publicite-ADA2026000040109-TR-FR"));
    QCOMPARE(info.accountSupplier, QString("FAMAZON"));
    // TR and FR recognised from label suffix "-TR-FR"
    QCOMPARE(info.countryCodeFrom, QString("TR"));
    QCOMPARE(info.countryCodeTo,   QString("FR"));
    QCOMPARE(info.totalAmount,     35.47);
    // TRY (Turkish lira) must be parsed as the currency
    QCOMPARE(info.currency,        QString("TRY"));
    QVERIFY(info.country_vatRate_vat.isEmpty());
    QVERIFY(info.rawVatAmount.isEmpty());

    // ── Encode round-trip ─────────────────────────────────────────────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── TR is non-EU in BookAccountSelfVatTable ───────────────────────────────
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    BookAccountSelfVatTable sva(QDir(tempDir.path()), "FR");

    // TR → FR must resolve to the non-EU row (same as CN → FR)
    QCOMPARE(sva.getAccountVatDeductible("TR", "FR"),
             sva.getAccountVatDeductible("CN", "FR"));
    QCOMPARE(sva.getAccountVatDue("TR", "FR"),
             sva.getAccountVatDue("CN", "FR"));
    // And must differ from an EU intracom route
    QVERIFY(sva.getAccountVatDeductible("TR", "FR") !=
            sva.getAccountVatDeductible("DE", "FR"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: Amazon PH→FR invoice
//       PH (Philippines) is a supported non-EU country
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_ph_fr_label_route()
{
    const QString fileName =
        "2026-01-31__622201__frais-publicite-ADA2026000040109-PH-FR__FAMAZON__35.47EUR.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-publicite-ADA2026000040109-PH-FR"));
    QCOMPARE(info.accountSupplier, QString("FAMAZON"));
    // PH and FR recognised from label suffix "-PH-FR"
    QCOMPARE(info.countryCodeFrom, QString("PH"));
    QCOMPARE(info.countryCodeTo,   QString("FR"));
    QCOMPARE(info.totalAmount,     35.47);
    QCOMPARE(info.currency,        QString("EUR"));
    QVERIFY(info.country_vatRate_vat.isEmpty());
    QVERIFY(info.rawVatAmount.isEmpty());

    // ── Encode round-trip ─────────────────────────────────────────────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── PH is non-EU in BookAccountSelfVatTable ───────────────────────────────
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    BookAccountSelfVatTable sva(QDir(tempDir.path()), "FR");

    // PH → FR must resolve to the non-EU row (same as CN → FR)
    QCOMPARE(sva.getAccountVatDeductible("PH", "FR"),
             sva.getAccountVatDeductible("CN", "FR"));
    QCOMPARE(sva.getAccountVatDue("PH", "FR"),
             sva.getAccountVatDue("CN", "FR"));
    // And must differ from an EU intracom route
    QVERIFY(sva.getAccountVatDeductible("PH", "FR") !=
            sva.getAccountVatDeductible("DE", "FR"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: VAT in EUR, total in SEK (cross-currency VAT)
//       Exchange rate: 27.67 SEK = 2.63 EUR → total 165.99 SEK ≈ 15.78 EUR
//       VAT rate extracted from "TVA20" → key "0.2" (20%)
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_mixed_currency_vat_eur_total_sek()
{
    const QString fileName =
        "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA20-2.63EUR__165.99SEK.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-vente-FR-AEU-2026-86900"));
    QCOMPARE(info.accountSupplier, QString("FAMZMK"));
    QCOMPARE(info.totalAmount,     165.99);
    QCOMPARE(info.currency,        QString("SEK"));
    QCOMPARE(info.rawVatAmount,    QString("2.63"));
    QCOMPARE(info.vatCurrency,     QString("EUR"));
    QCOMPARE(info.vatCountry,      QString("FR"));
    // Rate "TVA20" → stored as decimal 0.2, value 2.63 EUR
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    QVERIFY(info.country_vatRate_vat["FR"].contains("0.2"));
    QCOMPARE(info.country_vatRate_vat["FR"]["0.2"], 2.63);

    // ── Encode round-trip ─────────────────────────────────────────────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── Factory: balanced entry with SEK→EUR exchange rate ────────────────────
    // 27.67 SEK = 2.63 EUR  →  1 SEK = 2.63/27.67 EUR
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-31", "SEK", "EUR", 2.63 / 27.67);
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    BookAccountSelfVatTable sva(dir, "FR");
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(info);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
    QCOMPARE(entry->getDebits().size(),  2); // expense + VAT
    QCOMPARE(entry->getCredits().size(), 1); // supplier

    bool foundExpense = false, foundVat = false, foundSupplier = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "622201")              foundExpense = true;
        if (line.account.startsWith("44566"))      foundVat     = true;
    }
    for (const auto &line : entry->getCredits()) {
        if (line.account == "FAMZMK")              foundSupplier = true;
    }
    QVERIFY(foundExpense);
    QVERIFY(foundVat);
    QVERIFY(foundSupplier);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: VAT in SEK, total in EUR (cross-currency, opposite direction)
//       Same ratio: 27.67 SEK = 2.63 EUR → total 15.78 EUR, VAT 27.67 SEK
//       Rate extracted from "TVA20" → key "0.2" (20%)
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_mixed_currency_vat_sek_total_eur()
{
    const QString fileName =
        "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA20-27.67SEK__15.78EUR.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-vente-FR-AEU-2026-86900"));
    QCOMPARE(info.accountSupplier, QString("FAMZMK"));
    QCOMPARE(info.totalAmount,     15.78);
    QCOMPARE(info.currency,        QString("EUR"));
    QCOMPARE(info.rawVatAmount,    QString("27.67"));
    QCOMPARE(info.vatCurrency,     QString("SEK"));
    QCOMPARE(info.vatCountry,      QString("FR"));
    // Rate "TVA20" → key "0.2"
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    QVERIFY(info.country_vatRate_vat["FR"].contains("0.2"));
    QCOMPARE(info.country_vatRate_vat["FR"]["0.2"], 27.67);

    // ── Encode round-trip ─────────────────────────────────────────────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: VAT and total both in SEK, no rate in filename → rate computed ≈ 20%
//       27.67 SEK VAT, 165.99 SEK total → net 138.32 SEK → 27.67/138.32 ≈ 20%
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_same_currency_sek_rate_computed()
{
    const QString fileName =
        "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA-27.67SEK__165.99SEK.pdf";

    // ── Decode: rate not in filename → computed from amounts ──────────────────
    PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-vente-FR-AEU-2026-86900"));
    QCOMPARE(info.accountSupplier, QString("FAMZMK"));
    QCOMPARE(info.totalAmount,     165.99);
    QCOMPARE(info.currency,        QString("SEK"));
    QCOMPARE(info.rawVatAmount,    QString("27.67"));
    QCOMPARE(info.vatCurrency,     QString("SEK"));
    QCOMPARE(info.vatCountry,      QString("FR"));
    // Rate computed: 27.67 / (165.99 − 27.67) = 27.67 / 138.32 ≈ 20.0% → key "0.2"
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    QVERIFY(info.country_vatRate_vat["FR"].contains("0.2"));
    QCOMPARE(info.country_vatRate_vat["FR"]["0.2"], 27.67);

    // ── Encode round-trip (token preserved as "FR-TVA-27.67SEK") ─────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── Factory: balanced entry — both amounts in SEK, convert to EUR ─────────
    // 27.67 SEK = 2.63 EUR  →  1 SEK = 2.63/27.67 EUR
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-31", "SEK", "EUR", 2.63 / 27.67);
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    BookAccountSelfVatTable sva(dir, "FR");
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(info);
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
    QCOMPARE(entry->getDebits().size(),  2); // expense + VAT
    QCOMPARE(entry->getCredits().size(), 1); // supplier

    // VAT amount in EUR: 27.67 SEK × (2.63/27.67) = 2.63 EUR exactly
    const QString vatAccount = pa.getAccountsDebit6("FR", 0.2);
    double vatEur = 0.0;
    for (const auto &line : entry->getDebits()) {
        if (line.account == vatAccount) {
            vatEur = line.currency_amount.value("SEK") * (2.63 / 27.67);
        }
    }
    QVERIFY(qAbs(vatEur - 2.63) < 0.01);

    bool foundExpense = false, foundVat = false, foundSupplier = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "622201")              foundExpense = true;
        if (line.account == vatAccount)            foundVat     = true;
    }
    for (const auto &line : entry->getCredits()) {
        if (line.account == "FAMZMK")              foundSupplier = true;
    }
    QVERIFY(foundExpense);
    QVERIFY(foundVat);
    QVERIFY(foundSupplier);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: all four currency-variant filenames for the same invoice always produce
//       identical EUR amounts via JournalEntryFactory (FR/EUR company).
//
//  Exchange rate recorded: 27.67 SEK = 2.63 EUR  →  1 SEK = 2.63/27.67 EUR
//
//  The four variants (same invoice, different currency encoding):
//   1. VAT in EUR, total in EUR  → FR-TVA20-2.63EUR__15.78EUR
//   2. VAT in EUR, total in SEK  → FR-TVA20-2.63EUR__165.99SEK
//   3. VAT in SEK, total in EUR  → FR-TVA20-27.67SEK__15.78EUR
//   4. VAT in SEK, total in SEK  → FR-TVA-27.67SEK__165.99SEK
//
//  Expected EUR amounts in every entry:
//   - Supplier credit : 15.78 EUR
//   - VAT debit       :  2.63 EUR
//   - Expense debit   : 13.15 EUR  (= 15.78 − 2.63)
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_four_currency_variants_same_eur_amounts()
{
    const double SEK_EUR = 2.63 / 27.67; // 1 SEK in EUR

    const QStringList fileNames = {
        "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA20-2.63EUR__15.78EUR.pdf",
        "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA20-2.63EUR__165.99SEK.pdf",
        "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA20-27.67SEK__15.78EUR.pdf",
        "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA-27.67SEK__165.99SEK.pdf",
    };

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-31", "SEK", "EUR", SEK_EUR);
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    BookAccountSelfVatTable sva(dir, "FR");
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    const QString vatAccount = pa.getAccountsDebit6("FR", 0.2);

    for (const QString &fileName : fileNames) {
        PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

        QSharedPointer<JournalEntry> entry;
        QVERIFY2(!entry, qPrintable(fileName + ": entry should be null before creation"));
        entry = f.createEntry(info);
        QVERIFY2(!entry.isNull(), qPrintable(fileName + ": factory returned null entry"));

        // Entry must balance
        QVERIFY2(qAbs(entry->getDebitSum() - entry->getCreditSum()) < 0.01,
                 qPrintable(fileName + ": entry not balanced"));

        // Structure: 2 debits (expense + VAT), 1 credit (supplier)
        QCOMPARE(entry->getDebits().size(),  2);
        QCOMPARE(entry->getCredits().size(), 1);

        // ── Check EUR amounts on each line ────────────────────────────────────
        double supplierEur = 0.0, vatEur = 0.0, expenseEur = 0.0;

        for (const auto &line : entry->getCredits()) {
            if (line.account == "FAMZMK")
                supplierEur = line.currency_amount.value("EUR");
        }
        for (const auto &line : entry->getDebits()) {
            if (line.account == vatAccount)
                vatEur = line.currency_amount.value("EUR");
            if (line.account == "622201")
                expenseEur = line.currency_amount.value("EUR");
        }

        QVERIFY2(qAbs(supplierEur - 15.78) < 0.01,
                 qPrintable(fileName + QString(": supplier EUR %1 ≠ 15.78").arg(supplierEur)));
        QVERIFY2(qAbs(vatEur - 2.63) < 0.01,
                 qPrintable(fileName + QString(": VAT EUR %1 ≠ 2.63").arg(vatEur)));
        QVERIFY2(qAbs(expenseEur - 13.15) < 0.01,
                 qPrintable(fileName + QString(": expense EUR %1 ≠ 13.15").arg(expenseEur)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: GBP invoice with double-dash negative VAT token and negative total
//       Filename: FR-TVA--0.17EUR encodes a VAT token whose amount part ("0.17EUR")
//       is positive in the parsed structure (the leading minus is consumed by the
//       split on '-'); the invoice is a refund because totalAmount < 0.
//
//       Fake exchange rate: 1 GBP = 1.2 EUR.
//       Expected amounts (both on credit side — "2 negative amounts"):
//         • Expense (622201) : 0.87 EUR  (0.72833 GBP × 1.2, rounded)
//         • VAT    (445660)  : 0.17 EUR  (already in EUR, no conversion)
//         • Supplier debit   : 1.04 EUR  (0.87 GBP × 1.2)
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_invoice_gbp_negative_vat_and_total_with_conversion()
{
    const QString fileName =
        "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8372__FAMZMK__FR-TVA--0.17EUR__-0.87GBP.pdf";

    // ── Decode ────────────────────────────────────────────────────────────────
    const PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    QCOMPARE(info.date,            QDate(2026, 1, 31));
    QCOMPARE(info.account,         QString("622201"));
    QCOMPARE(info.label,           QString("frais-vente-FR-CN-AEU-2026-8372"));
    QCOMPARE(info.accountSupplier, QString("FAMZMK"));
    QCOMPARE(info.totalAmount,     -0.87);
    QCOMPARE(info.rawTotalAmount,  QString("-0.87"));
    QCOMPARE(info.currency,        QString("GBP"));
    QCOMPARE(info.vatTokens.size(),    1);
    QCOMPARE(info.vatTokens.first(),   QString("FR-TVA--0.17EUR"));
    QCOMPARE(info.rawVatAmount,    QString("-0.17"));
    QCOMPARE(info.vatCurrency,     QString("EUR"));
    QCOMPARE(info.vatCountry,      QString("FR"));

    // FAMZMK: capturedStart("MZ") = 2 < 3 → no route from supplier.
    // Label ends with "-8372" → no -XX-YY suffix → no route from label either.
    // countryCodeTo defaults to company country when absent from filename.
    QVERIFY(info.countryCodeFrom.isEmpty());
    QCOMPARE(info.countryCodeTo, QString("FR"));

    // Double-dash in "FR-TVA--0.17EUR" → rawVatAmount = "-0.17" (sign preserved).
    // country_vatRate_vat still stores absolute value 0.17; rate deduced from untaxed = 0.87 − 0.17 = 0.70 → ≈ 24.3%.
    QVERIFY(info.country_vatRate_vat.contains("FR"));
    const auto &frRates = info.country_vatRate_vat["FR"];
    QCOMPARE(frRates.size(), 1);
    const auto frRatesValues = frRates.values();
    QCOMPARE(frRatesValues.first(), 0.17);

    // ── Encode round-trip ─────────────────────────────────────────────────────
    QCOMPARE(PurchaseInvoiceManager::encode(info), fileName);

    // ── Factory: refund entry — both VAT and expense go to credit ("negative") ─
    // Fake rate: 1 GBP = 1.2 EUR.
    // isRefund = true  →  expense (622201) + VAT on credit side; supplier on debit.
    //
    // vatCurrency = "EUR" (≠ total currency "GBP"), so in the factory:
    //   totalVat (in GBP) = 0.17 × 1.0 / 1.2 ≈ 0.1417 GBP
    //   totalHT           = 0.87 − 0.1417 ≈ 0.7283 GBP
    //
    // Credit side:
    //   expense (622201) : 0.7283 GBP × 1.2 = 0.874 → rounded = 0.87 EUR
    //   VAT    (445660)  : 0.17 EUR  (no conversion, vatCurrency == companyCurrency)
    // Debit side:
    //   supplier (FAMZMK): 0.87 GBP × 1.2 = 1.044 → rounded = 1.04 EUR
    // Balance: 0.87 + 0.17 = 1.04 ✓

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-31", "GBP", "EUR", 1.2);
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    // JEF computes the rate with currency conversion (vatCurrency=EUR=companyCurrency):
    // totalHT = 0.87 GBP - 0.17 EUR / 1.2 = 0.72833 GBP
    // untaxedCC = 0.72833 × 1.2 = 0.874 EUR → rate = 0.17 / 0.874 ≈ 19.5% = 0.195
    pa.addAccount("FR", 0.195, "445660", "445710");
    JournalTable jt(dir);
    BookAccountSelfVatTable sva(dir, "FR");
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    const auto entry = f.createEntry(info);
    QVERIFY(!entry.isNull());

    // For a refund both the expense and VAT lines are on the credit side
    // (the "2 negative amounts" — negative from the expense-account perspective).
    QCOMPARE(entry->getCredits().size(), 2); // expense + VAT
    QCOMPARE(entry->getDebits().size(),  1); // supplier

    bool foundExpenseCredit = false;
    bool foundVatCredit     = false;
    for (const auto &line : entry->getCredits()) {
        if (line.account == "622201") {
            foundExpenseCredit = true;
            QVERIFY2(qAbs(line.currency_amount.value("EUR") - 0.87) < 0.01,
                     "Expense EUR amount should be ≈ 0.87");
        }
        if (line.account == "445660") {
            foundVatCredit = true;
            QVERIFY2(qAbs(line.currency_amount.value("EUR") - 0.17) < 0.001,
                     "VAT EUR amount should be 0.17");
        }
    }
    QVERIFY(foundExpenseCredit);
    QVERIFY(foundVatCredit);

    bool foundSupplierDebit = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "FAMZMK") {
            foundSupplierDebit = true;
            QVERIFY2(qAbs(line.currency_amount.value("EUR") - 1.04) < 0.01,
                     "Supplier EUR amount should be ≈ 1.04");
        }
    }
    QVERIFY(foundSupplierDebit);

    // Entry must balance
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
}

// ─────────────────────────────────────────────────────────────────────────────
// Amazon Payment filename parsing tests
// ─────────────────────────────────────────────────────────────────────────────

// 1. Full filename with all optional fields present (USD/com marketplace)
void TestBookEntries::test_amz_payment_decode_full_usd()
{
    // expenses present (2627.38 USD >> 200 EUR)
    // refunded-expenses present
    QString fname =
        "payment_com_2026_01_07__to__2026_01_21"
        "__balance-begin-1311.19USD"
        "__balance-end-1135.55USD"
        "__expenses-2627.38USD"
        "__refunded-expenses-153.17USD"
        "__177.90USD";

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(fname);

    QCOMPARE(info.countryCode, QString("com"));
    QCOMPARE(info.dateFrom,    QDate(2026, 1, 7));
    QCOMPARE(info.dateTo,      QDate(2026, 1, 21));
    QVERIFY(qAbs(info.balanceStart - 1311.19) < 0.001);
    QCOMPARE(info.balanceStartCurrency, QString("USD"));
    QVERIFY(qAbs(info.balanceEnd - 1135.55) < 0.001);
    QCOMPARE(info.balanceEndCurrency, QString("USD"));
    QVERIFY(info.hasExpenses);
    QVERIFY(qAbs(info.expenses - 2627.38) < 0.001);
    QCOMPARE(info.expensesCurrency, QString("USD"));
    QVERIFY(info.hasRefundedExpenses);
    QVERIFY(qAbs(info.refundedExpenses - 153.17) < 0.001);
    QCOMPARE(info.refundedExpensesCurrency, QString("USD"));
    QVERIFY(qAbs(info.paid - 177.90) < 0.001);
    QCOMPARE(info.paidCurrency, QString("USD"));
}

// 2. Missing expenses, small balance drop → no exception
void TestBookEntries::test_amz_payment_decode_missing_expenses_small_no_exception()
{
    // balanceStart=500, balanceEnd=490, paid=10 → proxy=max(0, 500-490-10)=0 USD → 0 EUR < 200
    QString fname =
        "payment_com_2026_02_01__to__2026_02_14"
        "__balance-begin-500.00USD"
        "__balance-end-490.00USD"
        "__100.00USD";

    bool threw = false;
    AmzPaymentInfo info;
    try {
        info = PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "No exception expected for small balance drop without expenses token");
    QVERIFY(!info.hasExpenses);
    QVERIFY(!info.hasRefundedExpenses);
    QVERIFY(qAbs(info.paid - 100.0) < 0.001);
}

// 3. Missing expenses, large balance drop → ExceptionWithTitleText
void TestBookEntries::test_amz_payment_decode_missing_expenses_large_exception()
{
    // balanceStart=1000, balanceEnd=500, paid=100 → proxy=max(0,1000-500-100)=400 USD
    // 400 * 0.92 = 368 EUR > 200 → must throw
    QString fname =
        "payment_com_2026_02_01__to__2026_02_14"
        "__balance-begin-1000.00USD"
        "__balance-end-500.00USD"
        "__100.00USD";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "Exception expected when expenses are absent but proxy > 200 EUR");
}

// 4. Missing refunded-expenses → always OK
void TestBookEntries::test_amz_payment_decode_missing_refunded_ok()
{
    QString fname =
        "payment_de_2026_03_01__to__2026_03_15"
        "__balance-begin-800.00EUR"
        "__balance-end-750.00EUR"
        "__expenses-300.00EUR"
        "__50.00EUR";

    bool threw = false;
    AmzPaymentInfo info;
    try {
        info = PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "No exception expected when refunded-expenses is absent");
    QVERIFY(info.hasExpenses);
    QVERIFY(!info.hasRefundedExpenses);
    QVERIFY(qAbs(info.expenses - 300.0) < 0.001);
}

// 5. Both optional tokens absent, trivial amounts → no exception
void TestBookEntries::test_amz_payment_decode_both_optional_absent_small()
{
    // proxy = max(0, 200-195-5) = 0 EUR → OK
    QString fname =
        "payment_fr_2026_04_01__to__2026_04_30"
        "__balance-begin-200.00EUR"
        "__balance-end-195.00EUR"
        "__5.00EUR";

    bool threw = false;
    AmzPaymentInfo info;
    try {
        info = PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "No exception for trivial amounts with both optionals absent");
    QVERIFY(!info.hasExpenses);
    QVERIFY(!info.hasRefundedExpenses);
}

// 6. Encode then decode roundtrip
void TestBookEntries::test_amz_payment_encode_decode_roundtrip()
{
    AmzPaymentInfo orig;
    orig.countryCode              = "co_uk";
    orig.dateFrom                 = QDate(2026, 5, 1);
    orig.dateTo                   = QDate(2026, 5, 14);
    orig.balanceStart             = 500.00;
    orig.hasBalanceStart          = true;
    orig.balanceStartCurrency     = "GBP";
    orig.balanceEnd               = 450.00;
    orig.hasBalanceEnd            = true;
    orig.balanceEndCurrency       = "GBP";
    orig.hasExpenses              = true;
    orig.expenses                 = 250.00;
    orig.expensesCurrency         = "GBP";
    orig.hasRefundedExpenses      = true;
    orig.refundedExpenses         = 20.00;
    orig.refundedExpensesCurrency = "GBP";
    orig.paid                     = 220.00;
    orig.paidCurrency             = "GBP";

    QString encoded = PurchaseAmzPaymentsManager::encode(orig);
    AmzPaymentInfo decoded = PurchaseAmzPaymentsManager::decode(encoded);

    QCOMPARE(decoded.countryCode,  orig.countryCode);
    QCOMPARE(decoded.dateFrom,     orig.dateFrom);
    QCOMPARE(decoded.dateTo,       orig.dateTo);
    QVERIFY(qAbs(decoded.balanceStart - orig.balanceStart) < 0.001);
    QCOMPARE(decoded.balanceStartCurrency, orig.balanceStartCurrency);
    QVERIFY(qAbs(decoded.balanceEnd - orig.balanceEnd) < 0.001);
    QVERIFY(decoded.hasExpenses);
    QVERIFY(qAbs(decoded.expenses - orig.expenses) < 0.001);
    QCOMPARE(decoded.expensesCurrency, orig.expensesCurrency);
    QVERIFY(decoded.hasRefundedExpenses);
    QVERIFY(qAbs(decoded.refundedExpenses - orig.refundedExpenses) < 0.001);
    QVERIFY(qAbs(decoded.paid - orig.paid) < 0.001);
    QCOMPARE(decoded.paidCurrency, orig.paidCurrency);
}

// 7. GBP marketplace (different EUR rate 1.16)
void TestBookEntries::test_amz_payment_currency_gbp()
{
    QString fname =
        "payment_co_uk_2026_06_01__to__2026_06_14"
        "__balance-begin-800.00GBP"
        "__balance-end-600.00GBP"
        "__expenses-350.00GBP"
        "__refunded-expenses-30.00GBP"
        "__180.00GBP";

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(fname);
    QCOMPARE(info.countryCode, QString("co_uk"));
    QCOMPARE(info.balanceStartCurrency, QString("GBP"));
    QVERIFY(info.hasExpenses);
    QVERIFY(qAbs(info.expenses - 350.0) < 0.001);
    // Also verify toEur works for GBP
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(100.0, "GBP") - 116.0) < 0.001);
}

// 8. Paid in different currency (EUR paid while balance is USD)
void TestBookEntries::test_amz_payment_paid_different_currency()
{
    QString fname =
        "payment_com_2026_07_01__to__2026_07_14"
        "__balance-begin-1000.00USD"
        "__balance-end-900.00USD"
        "__expenses-400.00USD"
        "__refunded-expenses-50.00USD"
        "__183.80EUR";

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(fname);
    QCOMPARE(info.paidCurrency, QString("EUR"));
    QVERIFY(qAbs(info.paid - 183.80) < 0.001);
    QCOMPARE(info.balanceStartCurrency, QString("USD"));
    QCOMPARE(info.expensesCurrency,     QString("USD"));
}

// 9. EUR marketplace (de) – expenses in EUR
void TestBookEntries::test_amz_payment_eur_marketplace_de()
{
    QString fname =
        "payment_de_2026_08_01__to__2026_08_14"
        "__balance-begin-1000.00EUR"
        "__balance-end-800.00EUR"
        "__expenses-500.00EUR"
        "__300.00EUR";

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(fname);
    QCOMPARE(info.countryCode, QString("de"));
    QCOMPARE(info.expensesCurrency, QString("EUR"));
    QVERIFY(qAbs(PurchaseAmzPaymentsManager::toEur(500.0, "EUR") - 500.0) < 0.001);
}

// 10. Just below 200 EUR proxy (USD) → no exception
// proxy = max(0, 300-200-100) = 0 USD → 0 EUR < 200 → OK
void TestBookEntries::test_amz_payment_threshold_just_below_no_exception()
{
    // proxy = max(0, 500 - 300 - 199) = 1 USD → 0.92 EUR < 200 → OK
    QString fname =
        "payment_com_2026_09_01__to__2026_09_14"
        "__balance-begin-500.00USD"
        "__balance-end-300.00USD"
        "__199.00USD";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "proxy = 1 USD = 0.92 EUR < 200 EUR, should not throw");
}

// 11. Just above 200 EUR proxy (USD) → exception
// proxy = max(0, 700 - 300 - 100) = 300 USD → 276 EUR > 200 → exception
void TestBookEntries::test_amz_payment_threshold_just_above_exception()
{
    QString fname =
        "payment_com_2026_09_15__to__2026_09_28"
        "__balance-begin-700.00USD"
        "__balance-end-300.00USD"
        "__100.00USD";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "proxy = 300 USD = 276 EUR > 200 EUR, must throw");
}

// 12. CAD marketplace – threshold ~294 CAD
// proxy = 300 CAD → 204 EUR > 200 → exception
void TestBookEntries::test_amz_payment_currency_cad()
{
    // Missing expenses, proxy = max(0, 800-400-100) = 300 CAD → 204 EUR > 200 → throw
    QString fname_throw =
        "payment_ca_2026_10_01__to__2026_10_14"
        "__balance-begin-800.00CAD"
        "__balance-end-400.00CAD"
        "__100.00CAD";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_throw);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "CAD: proxy 300 CAD = 204 EUR > 200, must throw");

    // proxy = 200 CAD → 136 EUR < 200 → OK
    QString fname_ok =
        "payment_ca_2026_10_01__to__2026_10_14"
        "__balance-begin-500.00CAD"
        "__balance-end-300.00CAD"
        "__0.00CAD";

    threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_ok);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "CAD: proxy 200 CAD = 136 EUR < 200, should not throw");
}

// 13. JPY marketplace – threshold ~32 258 JPY
void TestBookEntries::test_amz_payment_currency_jpy()
{
    // proxy = 35000 JPY → 217 EUR > 200 → throw
    QString fname_throw =
        "payment_co_jp_2026_10_01__to__2026_10_14"
        "__balance-begin-100000.00JPY"
        "__balance-end-60000.00JPY"
        "__5000.00JPY";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_throw);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "JPY: proxy 35000 JPY = 217 EUR > 200, must throw");

    // proxy = 30000 JPY → 186 EUR < 200 → OK
    QString fname_ok =
        "payment_co_jp_2026_10_01__to__2026_10_14"
        "__balance-begin-100000.00JPY"
        "__balance-end-65000.00JPY"
        "__5000.00JPY";

    threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_ok);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "JPY: proxy 30000 JPY = 186 EUR < 200, should not throw");
}

// 14. AUD marketplace – threshold ~333 AUD
void TestBookEntries::test_amz_payment_currency_aud()
{
    // proxy = 400 AUD → 240 EUR > 200 → throw
    QString fname_throw =
        "payment_com_au_2026_11_01__to__2026_11_14"
        "__balance-begin-1000.00AUD"
        "__balance-end-500.00AUD"
        "__100.00AUD";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_throw);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "AUD: proxy 400 AUD = 240 EUR > 200, must throw");

    // proxy = 300 AUD → 180 EUR < 200 → OK
    QString fname_ok =
        "payment_com_au_2026_11_01__to__2026_11_14"
        "__balance-begin-800.00AUD"
        "__balance-end-500.00AUD"
        "__0.00AUD";

    threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_ok);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "AUD: proxy 300 AUD = 180 EUR < 200, should not throw");
}

// 15. MXN marketplace – threshold ~4 348 MXN
void TestBookEntries::test_amz_payment_currency_mxn()
{
    // proxy = 5000 MXN → 230 EUR > 200 → throw
    QString fname_throw =
        "payment_com_mx_2026_11_15__to__2026_11_28"
        "__balance-begin-15000.00MXN"
        "__balance-end-9000.00MXN"
        "__1000.00MXN";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_throw);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "MXN: proxy 5000 MXN = 230 EUR > 200, must throw");

    // proxy = 4000 MXN → 184 EUR < 200 → OK
    QString fname_ok =
        "payment_com_mx_2026_11_15__to__2026_11_28"
        "__balance-begin-14000.00MXN"
        "__balance-end-9000.00MXN"
        "__1000.00MXN";

    threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_ok);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "MXN: proxy 4000 MXN = 184 EUR < 200, should not throw");
}

// 16. SEK marketplace – threshold ~2 299 SEK
void TestBookEntries::test_amz_payment_currency_sek()
{
    // proxy = 2500 SEK → 217.5 EUR > 200 → throw
    QString fname_throw =
        "payment_se_2026_12_01__to__2026_12_14"
        "__balance-begin-8000.00SEK"
        "__balance-end-5000.00SEK"
        "__500.00SEK";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_throw);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "SEK: proxy 2500 SEK = 217.5 EUR > 200, must throw");

    // proxy = 2000 SEK → 174 EUR < 200 → OK
    QString fname_ok =
        "payment_se_2026_12_01__to__2026_12_14"
        "__balance-begin-7500.00SEK"
        "__balance-end-5000.00SEK"
        "__500.00SEK";

    threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_ok);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "SEK: proxy 2000 SEK = 174 EUR < 200, should not throw");
}

// 17. PLN marketplace – threshold ~870 PLN
void TestBookEntries::test_amz_payment_currency_pln()
{
    // proxy = 1000 PLN → 230 EUR > 200 → throw
    QString fname_throw =
        "payment_pl_2026_12_15__to__2026_12_28"
        "__balance-begin-3000.00PLN"
        "__balance-end-1800.00PLN"
        "__200.00PLN";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_throw);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "PLN: proxy 1000 PLN = 230 EUR > 200, must throw");

    // proxy = 800 PLN → 184 EUR < 200 → OK
    QString fname_ok =
        "payment_pl_2026_12_15__to__2026_12_28"
        "__balance-begin-3000.00PLN"
        "__balance-end-2000.00PLN"
        "__200.00PLN";

    threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_ok);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "PLN: proxy 800 PLN = 184 EUR < 200, should not throw");
}

// 18. TRY marketplace – threshold ~7 407 TRY
void TestBookEntries::test_amz_payment_currency_try()
{
    // proxy = 8000 TRY → 216 EUR > 200 → throw
    QString fname_throw =
        "payment_com_tr_2026_01_01__to__2026_01_14"
        "__balance-begin-25000.00TRY"
        "__balance-end-15000.00TRY"
        "__2000.00TRY";

    bool threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_throw);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(threw, "TRY: proxy 8000 TRY = 216 EUR > 200, must throw");

    // proxy = 7000 TRY → 189 EUR < 200 → OK
    QString fname_ok =
        "payment_com_tr_2026_01_01__to__2026_01_14"
        "__balance-begin-25000.00TRY"
        "__balance-end-16000.00TRY"
        "__2000.00TRY";

    threw = false;
    try {
        PurchaseAmzPaymentsManager::decode(fname_ok);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "TRY: proxy 7000 TRY = 189 EUR < 200, should not throw");
}

// 19. Expenses present with amount < 200 EUR → always valid, no exception
void TestBookEntries::test_amz_payment_expenses_small_present_no_exception()
{
    QString fname =
        "payment_fr_2026_02_01__to__2026_02_14"
        "__balance-begin-500.00EUR"
        "__balance-end-480.00EUR"
        "__expenses-50.00EUR"
        "__30.00EUR";

    bool threw = false;
    AmzPaymentInfo info;
    try {
        info = PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }
    QVERIFY2(!threw, "Small expenses present in filename is always valid");
    QVERIFY(info.hasExpenses);
    QVERIFY(qAbs(info.expenses - 50.0) < 0.001);
}

// 20. refunded-expenses appears after expenses in filename → normal; test that
//     the order expenses / refunded-expenses is parsed correctly either way
void TestBookEntries::test_amz_payment_refunded_before_expenses_parsed_correctly()
{
    // Put refunded-expenses BEFORE expenses in the filename
    QString fname =
        "payment_de_2026_03_01__to__2026_03_14"
        "__balance-begin-1000.00EUR"
        "__balance-end-700.00EUR"
        "__refunded-expenses-40.00EUR"
        "__expenses-280.00EUR"
        "__60.00EUR";

    bool threw = false;
    AmzPaymentInfo info;
    try {
        info = PurchaseAmzPaymentsManager::decode(fname);
    } catch (const ExceptionWithTitleText &e) {
        threw = true;
        Q_UNUSED(e);
    }
    QVERIFY2(!threw, "Parser should handle refunded-expenses before expenses");
    QVERIFY(info.hasExpenses);
    QVERIFY(qAbs(info.expenses - 280.0) < 0.001);
    QVERIFY(info.hasRefundedExpenses);
    QVERIFY(qAbs(info.refundedExpenses - 40.0) < 0.001);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper shared by the createEntry(AmzPaymentInfo) tests
// ─────────────────────────────────────────────────────────────────────────────

static void setupAmzSettings(const QDir &dir,
                              const QString &debitAccount  = "467150",
                              const QString &amazonAccount = "FAMZMK")
{
    QFile f(dir.filePath("amazon_payment_settings.csv"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "ID;Param;Value\n";
    out << "debit_account;Debit account;"  << debitAccount  << "\n";
    out << "credit_account;Credit account;\n";
    out << "amazon_account;Amazon Purchase Account;" << amazonAccount << "\n";
}

// ── Group 1: EUR company, EUR payment ────────────────────────────────────────

// 1. Full EUR filename: balance + expenses + refund + paid — all EUR
void TestBookEntries::test_factory_amz_entry_eur_all_fields()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    // payment_de_2026_03_01__to__2026_03_15__balance-begin-800.00EUR
    //   __balance-end-750.00EUR__expenses-300.00EUR__refunded-expenses-20.00EUR__50.00EUR
    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_03_01__to__2026_03_15"
        "__balance-begin-800.00EUR"
        "__balance-end-750.00EUR"
        "__expenses-300.00EUR"
        "__refunded-expenses-20.00EUR"
        "__50.00EUR");

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDate(), QDate(2026, 3, 15));
    // Debit lines: balanceStart(800) + expenses(300) + paid(50) = 1150
    QCOMPARE(entry->getDebitSum(),  1150.0);
    // Credit lines: balanceEnd(750) + refundedExpenses(20) = 770
    QCOMPARE(entry->getCreditSum(), 770.0);
}

// 2. No balance tokens — only expenses + paid
void TestBookEntries::test_factory_amz_entry_eur_no_balance()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_fr_2026_04_01__to__2026_04_30"
        "__expenses-200.00EUR"
        "__100.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QVERIFY(!info.hasBalanceStart);
    QVERIFY(!info.hasBalanceEnd);
    // Debit: expenses(200) + paid(100) = 300 ; Credit: 0
    QCOMPARE(entry->getDebitSum(),  300.0);
    QCOMPARE(entry->getCreditSum(),   0.0);
}

// 3. Balance + expenses + refund
void TestBookEntries::test_factory_amz_entry_eur_expenses_refund()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_05_01__to__2026_05_14"
        "__balance-begin-1000.00EUR"
        "__balance-end-900.00EUR"
        "__expenses-400.00EUR"
        "__refunded-expenses-50.00EUR"
        "__150.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebitSum(),  1550.0); // 1000+400+150
    QCOMPARE(entry->getCreditSum(),  950.0); // 900+50
}

// 4. Balance + expenses only (no refund)
void TestBookEntries::test_factory_amz_entry_eur_expenses_only()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_06_01__to__2026_06_14"
        "__balance-begin-500.00EUR"
        "__balance-end-400.00EUR"
        "__expenses-250.00EUR"
        "__150.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QVERIFY(!info.hasRefundedExpenses);
    // Debit: 500+250+150=900; Credit: 400
    QCOMPARE(entry->getDebitSum(),  900.0);
    QCOMPARE(entry->getCreditSum(), 400.0);
}

// 5. Minimal filename — only paid, no balance, no expenses
void TestBookEntries::test_factory_amz_entry_eur_minimal()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_fr_2026_07_01__to__2026_07_14"
        "__75.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    // Only 1 debit line (paid), no credit lines
    QCOMPARE(entry->getDebits().size(),  1);
    QCOMPARE(entry->getCredits().size(), 0);
    QCOMPARE(entry->getDebitSum(), 75.0);
}

// ── Group 2: USD payment (conversion EUR company) ────────────────────────────

// 6. Full USD filename
void TestBookEntries::test_factory_amz_entry_usd_all_fields()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_com_2026_01_07__to__2026_01_21"
        "__balance-begin-1311.19USD"
        "__balance-end-1135.55USD"
        "__expenses-2627.38USD"
        "__refunded-expenses-153.17USD"
        "__177.90USD");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-21", "USD", "EUR", 0.92);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDate(), QDate(2026, 1, 21));
    // Debit (EUR): (1311.19+2627.38+177.90)*0.92
    double expDebit = (1311.19 + 2627.38 + 177.90) * 0.92;
    QVERIFY(qAbs(entry->getDebitSum()  - expDebit) < 0.02);
    double expCredit = (1135.55 + 153.17) * 0.92;
    QVERIFY(qAbs(entry->getCreditSum() - expCredit) < 0.02);
}

// 7. USD — no balance
void TestBookEntries::test_factory_amz_entry_usd_no_balance()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_com_2026_02_01__to__2026_02_14"
        "__expenses-500.00USD"
        "__200.00USD");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-02-14", "USD", "EUR", 0.92);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QVERIFY(!info.hasBalanceStart);
    QVERIFY(qAbs(entry->getDebitSum() - 700.0 * 0.92) < 0.02); // (500+200)*0.92
    QCOMPARE(entry->getCreditSum(), 0.0);
}

// 8. USD — balance only, no expenses, no refund
void TestBookEntries::test_factory_amz_entry_usd_balance_only()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info;
    info.countryCode           = "com";
    info.dateFrom              = QDate(2026, 3, 1);
    info.dateTo                = QDate(2026, 3, 14);
    info.balanceStart          = 800.0;
    info.balanceStartCurrency  = "USD";
    info.hasBalanceStart       = true;
    info.balanceEnd            = 600.0;
    info.balanceEndCurrency    = "USD";
    info.hasBalanceEnd         = true;
    info.paid                  = 200.0;
    info.paidCurrency          = "USD";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-03-14", "USD", "EUR", 0.92);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    // Debit: (800+200)*0.92=920; Credit: 600*0.92=552
    QVERIFY(qAbs(entry->getDebitSum()  - 1000.0 * 0.92) < 0.02);
    QVERIFY(qAbs(entry->getCreditSum() -  600.0 * 0.92) < 0.02);
    // 3 lines total: 2 debit (balStart + paid), 1 credit (balEnd)
    QCOMPARE(entry->getDebits().size(),  2);
    QCOMPARE(entry->getCredits().size(), 1);
}

// 9. Balance in USD, paid in EUR (mixed currencies)
void TestBookEntries::test_factory_amz_entry_usd_paid_eur()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_com_2026_07_01__to__2026_07_14"
        "__balance-begin-1000.00USD"
        "__balance-end-900.00USD"
        "__expenses-400.00USD"
        "__refunded-expenses-50.00USD"
        "__183.80EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-07-14", "USD", "EUR", 0.92);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(info.paidCurrency, QString("EUR"));
    QCOMPARE(info.balanceStartCurrency, QString("USD"));
    // Paid line uses EUR directly (rate=1)
    bool foundPaidEur = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "FAMZMK") {
            foundPaidEur = true;
            QVERIFY(qAbs(line.currency_amount.value("EUR") - 183.80) < 0.01);
        }
    }
    QVERIFY(foundPaidEur);
}

// 10. USD: verify conversion info appears in title of converted lines
void TestBookEntries::test_factory_amz_entry_usd_conversion_in_title()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info;
    info.countryCode          = "com";
    info.dateFrom             = QDate(2026, 4, 1);
    info.dateTo               = QDate(2026, 4, 14);
    info.paid                 = 392.02;
    info.paidCurrency         = "USD";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-04-14", "USD", "EUR", 0.92);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    // Each USD debit line should have "(Conv: ..." appended by JournalEntry
    for (const auto &line : entry->getDebits())
        QVERIFY(line.title.contains("(Conv:"));
}

// ── Group 3: other marketplace currencies ────────────────────────────────────

// 11. GBP — co_uk marketplace
void TestBookEntries::test_factory_amz_entry_gbp_all_fields()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_co_uk_2026_06_01__to__2026_06_14"
        "__balance-begin-800.00GBP"
        "__balance-end-600.00GBP"
        "__expenses-350.00GBP"
        "__refunded-expenses-30.00GBP"
        "__180.00GBP");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-06-14", "GBP", "EUR", 1.16);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(info.countryCode, QString("co_uk"));
    QVERIFY(qAbs(entry->getDebitSum()  - (800.0+350.0+180.0)*1.16) < 0.02);
    QVERIFY(qAbs(entry->getCreditSum() - (600.0+30.0)*1.16) < 0.02);
}

// 12. CAD — ca marketplace
void TestBookEntries::test_factory_amz_entry_cad_payment()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_ca_2026_10_01__to__2026_10_14"
        "__balance-begin-500.00CAD"
        "__balance-end-300.00CAD"
        "__expenses-150.00CAD"
        "__100.00CAD");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-10-14", "CAD", "EUR", 0.68);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(info.paidCurrency, QString("CAD"));
    QVERIFY(qAbs(entry->getDebitSum() - (500.0+150.0+100.0)*0.68) < 0.02);
}

// 13. JPY — co_jp marketplace
void TestBookEntries::test_factory_amz_entry_jpy_payment()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_co_jp_2026_10_01__to__2026_10_14"
        "__balance-begin-100000.00JPY"
        "__balance-end-65000.00JPY"
        "__expenses-28000.00JPY"
        "__7000.00JPY");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-10-14", "JPY", "EUR", 0.0062);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(info.paidCurrency, QString("JPY"));
    QVERIFY(entry->getDebitSum() > 0.0);
    QVERIFY(entry->getCreditSum() > 0.0);
}

// 14. AUD — com_au marketplace
void TestBookEntries::test_factory_amz_entry_aud_payment()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_com_au_2026_11_01__to__2026_11_14"
        "__balance-begin-800.00AUD"
        "__balance-end-500.00AUD"
        "__expenses-200.00AUD"
        "__100.00AUD");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-11-14", "AUD", "EUR", 0.60);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(info.countryCode, QString("com_au"));
    QVERIFY(qAbs(entry->getDebitSum() - (800.0+200.0+100.0)*0.60) < 0.02);
}

// 15. SEK — se marketplace
void TestBookEntries::test_factory_amz_entry_sek_payment()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_se_2026_12_01__to__2026_12_14"
        "__balance-begin-5000.00SEK"
        "__balance-end-3000.00SEK"
        "__expenses-1500.00SEK"
        "__500.00SEK");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-12-14", "SEK", "EUR", 0.087);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(info.paidCurrency, QString("SEK"));
    QVERIFY(qAbs(entry->getDebitSum() - (5000.0+1500.0+500.0)*0.087) < 0.02);
}

// ── Group 4: account routing verification ────────────────────────────────────

// 16. Verify BookAccountAmzBalanceTable.balanceAccount is used for balance lines
void TestBookEntries::test_factory_amz_entry_debit_account_in_balance()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir, "467150", "FAMZMK");

    AmzPaymentInfo info;
    info.countryCode          = "de";
    info.dateTo               = QDate(2026, 3, 15);
    info.balanceStart         = 500.0; info.balanceStartCurrency = "EUR"; info.hasBalanceStart = true;
    info.balanceEnd           = 400.0; info.balanceEndCurrency   = "EUR"; info.hasBalanceEnd   = true;
    info.paid                 = 100.0; info.paidCurrency          = "EUR";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    // Configure the balance account for amazon.de
    for (int i = 0; i < balanceTable.rowCount(); ++i) {
        if (balanceTable.data(balanceTable.index(i, 0)).toString() == "amazon.de") {
            balanceTable.setData(balanceTable.index(i, 1), "AMZ_DE_BAL", Qt::EditRole);
            break;
        }
    }
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());

    // Balance start → debit with the balance table account (not debitAccount)
    bool foundBalStart = false;
    for (const auto &line : entry->getDebits())
        if (line.account == "AMZ_DE_BAL" && qAbs(line.currency_amount.value("EUR") - 500.0) < 0.01)
            foundBalStart = true;
    QVERIFY(foundBalStart);

    // Balance end → credit with the balance table account
    bool foundBalEnd = false;
    for (const auto &line : entry->getCredits())
        if (line.account == "AMZ_DE_BAL" && qAbs(line.currency_amount.value("EUR") - 400.0) < 0.01)
            foundBalEnd = true;
    QVERIFY(foundBalEnd);

    // Paid line → debit with amazonAccount from settings (not balance account)
    bool foundPaid = false;
    for (const auto &line : entry->getDebits())
        if (line.account == "FAMZMK" && qAbs(line.currency_amount.value("EUR") - 100.0) < 0.01)
            foundPaid = true;
    QVERIFY(foundPaid);
}

// 17. Verify getAmazonAccount() is the account for the paid line
void TestBookEntries::test_factory_amz_entry_amazon_account_for_paid()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir, "467150", "FAMZMK");

    AmzPaymentInfo info;
    info.countryCode  = "com"; info.dateTo = QDate(2026, 1, 14);
    info.paid = 392.02;        info.paidCurrency = "EUR";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());

    bool foundPaid = false;
    for (const auto &line : entry->getDebits())
        if (line.account == "FAMZMK" && qAbs(line.currency_amount.value("EUR") - 392.02) < 0.01)
            foundPaid = true;
    QVERIFY(foundPaid);
}

// 18. Custom account names from settings: balance uses table, expenses/paid use settings
void TestBookEntries::test_factory_amz_entry_custom_accounts()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir, "MYDEBIT", "MYAMZACC");

    AmzPaymentInfo info;
    info.countryCode  = "de"; info.dateTo = QDate(2026, 2, 14);
    info.balanceStart = 200.0; info.balanceStartCurrency = "EUR"; info.hasBalanceStart = true;
    info.balanceEnd   = 150.0; info.balanceEndCurrency   = "EUR"; info.hasBalanceEnd   = true;
    info.paid = 50.0;          info.paidCurrency          = "EUR";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    // Configure a distinct balance account for amazon.de
    for (int i = 0; i < balanceTable.rowCount(); ++i) {
        if (balanceTable.data(balanceTable.index(i, 0)).toString() == "amazon.de") {
            balanceTable.setData(balanceTable.index(i, 1), "MYBALANCE", Qt::EditRole);
            break;
        }
    }
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());

    // Balance lines use the table's balanceAccount, not debitAccount
    bool foundBalDebit = false, foundBalCredit = false;
    for (const auto &line : entry->getDebits())
        if (line.account == "MYBALANCE") foundBalDebit = true;
    for (const auto &line : entry->getCredits())
        if (line.account == "MYBALANCE") foundBalCredit = true;
    QVERIFY(foundBalDebit);
    QVERIFY(foundBalCredit);

    // Paid line uses amazonAccount from settings
    bool foundMyAmz = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "MYAMZACC") foundMyAmz  = true;
    }
    QVERIFY(foundMyAmz);
}

// 19. No settings pointer → factory returns nullptr
void TestBookEntries::test_factory_amz_entry_no_settings_null()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);

    AmzPaymentInfo info;
    info.countryCode = "com"; info.dateTo = QDate(2026, 1, 14);
    info.paid = 100.0;        info.paidCurrency = "EUR";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    // No AmzPaymentSettings passed → nullptr
    BookAccountSelfVatTable sva(dir, "FR");
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, nullptr, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(entry.isNull());
}

// 20. Default amazon account is "FAMZMK"
void TestBookEntries::test_factory_amz_entry_default_amazon_account()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    // Write settings with empty debit account, amazon account not specified → defaults to FAMZMK
    {
        QFile f(dir.filePath("amazon_payment_settings.csv"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "ID;Param;Value\n";
        out << "debit_account;Debit account;467000\n";
        // amazon_account intentionally omitted → _ensureDefaults adds FAMZMK
    }

    AmzPaymentSettings amzSet(dir);
    QCOMPARE(amzSet.getAmazonAccount(), QString("FAMZMK"));

    AmzPaymentInfo info;
    info.countryCode = "de"; info.dateTo = QDate(2026, 5, 14);
    info.paid = 60.0;        info.paidCurrency = "EUR";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    bool foundFamzmk = false;
    for (const auto &line : entry->getDebits())
        if (line.account == "FAMZMK") foundFamzmk = true;
    QVERIFY(foundFamzmk);
}

// ── Group 5: line counts and sums ────────────────────────────────────────────

// 21. All fields: 3 debit lines (balStart + expenses + paid), 2 credit lines (balEnd + refund)
void TestBookEntries::test_factory_amz_entry_line_count_all_fields()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_08_01__to__2026_08_14"
        "__balance-begin-1000.00EUR"
        "__balance-end-800.00EUR"
        "__expenses-500.00EUR"
        "__refunded-expenses-100.00EUR"
        "__300.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebits().size(),  3); // balStart + expenses + paid
    QCOMPARE(entry->getCredits().size(), 2); // balEnd   + refundedExpenses
}

// 22. No optional fields: only 1 debit line (paid), 0 credit lines
void TestBookEntries::test_factory_amz_entry_line_count_no_optionals()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_fr_2026_09_01__to__2026_09_14"
        "__120.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebits().size(),  1);
    QCOMPARE(entry->getCredits().size(), 0);
}

// 23. No balance, with expenses and refund: 2 debit, 1 credit
void TestBookEntries::test_factory_amz_entry_line_count_no_balance()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_10_01__to__2026_10_14"
        "__expenses-200.00EUR"
        "__refunded-expenses-30.00EUR"
        "__80.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDebits().size(),  2); // expenses + paid
    QCOMPARE(entry->getCredits().size(), 1); // refundedExpenses
}

// 24. Verify debit sum with all EUR fields
void TestBookEntries::test_factory_amz_entry_debit_sum_all_fields()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_11_01__to__2026_11_14"
        "__balance-begin-600.00EUR"
        "__balance-end-550.00EUR"
        "__expenses-250.00EUR"
        "__refunded-expenses-40.00EUR"
        "__60.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    // Debit: balStart(600) + expenses(250) + paid(60) = 910
    QCOMPARE(entry->getDebitSum(), 910.0);
}

// 25. Verify credit sum with all EUR fields
void TestBookEntries::test_factory_amz_entry_credit_sum_all_fields()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_11_15__to__2026_11_28"
        "__balance-begin-700.00EUR"
        "__balance-end-650.00EUR"
        "__expenses-300.00EUR"
        "__refunded-expenses-80.00EUR"
        "__70.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    // Credit: balEnd(650) + refunded(80) = 730
    QCOMPARE(entry->getCreditSum(), 730.0);
}

// ── Group 6: title / label format ────────────────────────────────────────────

// 26. Title starts with "Paiement amazon."
void TestBookEntries::test_factory_amz_entry_title_paiement_amazon()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info;
    info.countryCode = "de"; info.dateTo = QDate(2026, 6, 14);
    info.paid = 100.0;       info.paidCurrency = "EUR";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    for (const auto &line : entry->getDebits())
        QVERIFY(line.title.startsWith("Paiement amazon."));
}

// 27. Title contains the paid amount
void TestBookEntries::test_factory_amz_entry_title_contains_paid_amount()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info;
    info.countryCode = "com"; info.dateTo = QDate(2026, 7, 14);
    info.paid = 177.90;      info.paidCurrency = "USD";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-07-14", "USD", "EUR", 0.92);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    for (const auto &line : entry->getDebits())
        QVERIFY(line.title.contains("177.90"));
}

// 28. Title contains the paid currency code
void TestBookEntries::test_factory_amz_entry_title_contains_currency()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info;
    info.countryCode = "co_uk"; info.dateTo = QDate(2026, 8, 14);
    info.paid = 180.0;          info.paidCurrency = "GBP";

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-08-14", "GBP", "EUR", 1.16);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    for (const auto &line : entry->getDebits())
        QVERIFY(line.title.contains("GBP"));
}

// 29. All lines share the same base title (before JournalEntry appends "(Conv:)")
void TestBookEntries::test_factory_amz_entry_title_all_lines_same()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_de_2026_12_01__to__2026_12_14"
        "__balance-begin-300.00EUR"
        "__balance-end-250.00EUR"
        "__expenses-100.00EUR"
        "__refunded-expenses-10.00EUR"
        "__60.00EUR");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());

    // For EUR all-EUR entry, no Conv suffix → all titles must be identical
    QStringList titles;
    for (const auto &line : entry->getDebits()  + entry->getCredits())
        titles << line.title;
    QVERIFY(!titles.isEmpty());
    for (const auto &t : titles)
        QCOMPARE(t, titles.first());
}

// 30. Entry date equals dateTo of the payment info
void TestBookEntries::test_factory_amz_entry_date_uses_date_to()
{
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir);

    AmzPaymentInfo info = PurchaseAmzPaymentsManager::decode(
        "payment_com_2026_01_07__to__2026_01_21"
        "__177.90USD");

    CompanyInfosTable ci(dir); CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-21", "USD", "EUR", 0.92);
    BooksAccountsSalesTable sa(dir); BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir); AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    auto entry = syncWait(f.createEntry(info));
    QVERIFY(!entry.isNull());
    QCOMPARE(entry->getDate(), QDate(2026, 1, 21)); // dateTo
    QCOMPARE(info.dateFrom,    QDate(2026, 1, 7));
    QCOMPARE(info.dateTo,      QDate(2026, 1, 21));
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers for InventoryMoveTree factory tests
// ─────────────────────────────────────────────────────────────────────────────

// Write a minimal purchase CSV accepted by PurchaseCsvLoader / PurchaseFileSettingsTree.
// The column IDs ("SKU", "Unit Price", "Currency") match the built-in aliases so no
// purchaseFileSettings.csv is required in the settings directory.
static void writeInventoryCsv(const QDir &dir, const QString &sku, double price,
                               const QString &currency = "EUR",
                               const QString &filename = "2026-01-31__test-FR.csv")
{
    QFile f(dir.filePath(filename));
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&f);
    out << "SKU;Unit Price;Currency\n";
    out << sku << ";" << QString::number(price, 'f', 2) << ";" << currency << "\n";
}

// Build a minimal InventoryMoveTree with the given import units.
// purchaseDir doubles as settingsDir for PurchaseFileSettingsTree (see InventoryMoveTree::loadPurchaseData).
static InventoryMoveTree *makeInventoryTree(
        const QDir &purchaseDir,
        const QHash<QString, QHash<QString, int>> &imported,
        const QHash<QString, QHash<QString, int>> &exported = {},
        const QString &companyCurrency = "EUR",
        const QString &companyCountry  = "FR")
{
    return new InventoryMoveTree(purchaseDir, imported, exported,
                                 {{0, {{"", 0.0}}}},       // no shipping cost
                                 companyCurrency,
                                 nullptr,            // no currency rate manager needed (EUR=EUR)
                                 QStringList(),      // no invoice lookup
                                 companyCountry,
                                 nullptr);           // no regraded SKU table
}

// Find the first EntryLine in a list whose account matches, or nullptr if absent.
static const JournalEntry::EntryLine *findLine(const QList<JournalEntry::EntryLine> &lines,
                                               const QString &account)
{
    for (const auto &l : lines)
        if (l.account == account) return &l;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// createEntry(InventoryMoveTree*) — null-guard tests
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_null_tree_returns_null()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    // (1) null tree pointer → nullptr
    QVERIFY(!f.createEntry(static_cast<const InventoryMoveTree *>(nullptr), "FR"));
}

void TestBookEntries::test_factory_inventory_null_selfvat_returns_null()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    // factory built WITHOUT selfVatBookAccounts → nullptr
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);

    // (2) no selfVat table → nullptr
    QVERIFY(!f.createEntry(tree, "FR"));
    delete tree;
}

// ─────────────────────────────────────────────────────────────────────────────
// EU → FR: line count
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_eu_to_france_line_count()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    // Configure the three new accounts so they are non-empty
    sva.setData(sva.index(0, 3), "707016", Qt::EditRole); // Sale7 EU
    sva.setData(sva.index(0, 4), "607016", Qt::EditRole); // Purchase7 EU
    sva.setData(sva.index(0, 5), "467800", Qt::EditRole); // Stock4 EU
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(tree, "FR");
    delete tree;

    // (3) entry is non-null
    QVERIFY(entry);
    // (4) exactly 3 debit lines: Stock4 + VatDeductible + Purchase7
    QCOMPARE(entry->getDebits().size(), 3);
    // (5) exactly 3 credit lines: Sale7 + Stock4 + VatDue
    QCOMPARE(entry->getCredits().size(), 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// EU → FR: debit = credit (balanced)
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_eu_to_france_balanced()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    sva.setData(sva.index(0, 3), "707016", Qt::EditRole);
    sva.setData(sva.index(0, 4), "607016", Qt::EditRole);
    sva.setData(sva.index(0, 5), "467800", Qt::EditRole);
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(tree, "FR");
    delete tree;

    QVERIFY(entry);
    // (6) balanced: debit sum == credit sum
    QVERIFY(qAbs(entry->getDebitSum() - entry->getCreditSum()) < 0.005);
}

// ─────────────────────────────────────────────────────────────────────────────
// EU → FR: account routing
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_eu_to_france_accounts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    sva.setData(sva.index(0, 3), "707016", Qt::EditRole); // Sale7
    sva.setData(sva.index(0, 4), "607016", Qt::EditRole); // Purchase7
    sva.setData(sva.index(0, 5), "467800", Qt::EditRole); // Stock4
    // VatDeductible (col 1) and VatDue (col 2) keep their defaults: 445662 / 445200
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(tree, "FR");
    delete tree;

    QVERIFY(entry);
    const auto &debits  = entry->getDebits();
    const auto &credits = entry->getCredits();

    // (7) Sale7 (707016) is in credits
    QVERIFY(findLine(credits, "707016") != nullptr);
    // (8) VatDue (445200) is in credits
    QVERIFY(findLine(credits, "445200") != nullptr);
    // (9) Stock4 (467800) is in credits
    QVERIFY(findLine(credits, "467800") != nullptr);
    // (10) Stock4 (467800) is also in debits
    QVERIFY(findLine(debits, "467800") != nullptr);
    // (11) VatDeductible (445662) is in debits
    QVERIFY(findLine(debits, "445662") != nullptr);
    // (12) Purchase7 (607016) is in debits
    QVERIFY(findLine(debits, "607016") != nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// EU → FR: amount computation (totalHT=666.59, VAT=133.32, TTC=799.91)
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_eu_to_france_amounts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    sva.setData(sva.index(0, 3), "707016", Qt::EditRole);
    sva.setData(sva.index(0, 4), "607016", Qt::EditRole);
    sva.setData(sva.index(0, 5), "467800", Qt::EditRole);
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(tree, "FR");
    delete tree;

    QVERIFY(entry);
    const double totalHT  = 666.59;
    const double vatAmt   = qRound(totalHT * 0.20 * 100.0) / 100.0; // 133.32
    const double totalTTC = totalHT + vatAmt;                         // 799.91

    const auto &debits  = entry->getDebits();
    const auto &credits = entry->getCredits();

    // (13) Sale7 credit amount == totalTTC
    const auto *sale7 = findLine(credits, "707016");
    QVERIFY(sale7);
    QVERIFY(qAbs(sale7->currency_amount.value("EUR") - totalTTC) < 0.005);

    // (14) Stock4 debit amount == totalHT
    const auto *stock4d = findLine(debits, "467800");
    QVERIFY(stock4d);
    QVERIFY(qAbs(stock4d->currency_amount.value("EUR") - totalHT) < 0.005);

    // (15) Stock4 credit amount == totalHT
    const auto *stock4c = findLine(credits, "467800");
    QVERIFY(stock4c);
    QVERIFY(qAbs(stock4c->currency_amount.value("EUR") - totalHT) < 0.005);

    // (16) VatDue credit amount == vatAmt
    const auto *vatDue = findLine(credits, "445200");
    QVERIFY(vatDue);
    QVERIFY(qAbs(vatDue->currency_amount.value("EUR") - vatAmt) < 0.005);

    // (17) VatDeductible debit amount == vatAmt
    const auto *vatDed = findLine(debits, "445662");
    QVERIFY(vatDed);
    QVERIFY(qAbs(vatDed->currency_amount.value("EUR") - vatAmt) < 0.005);

    // (18) Purchase7 debit amount == totalTTC
    const auto *purch7 = findLine(debits, "607016");
    QVERIFY(purch7);
    QVERIFY(qAbs(purch7->currency_amount.value("EUR") - totalTTC) < 0.005);
}

// ─────────────────────────────────────────────────────────────────────────────
// EU → FR: title strings
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_eu_to_france_titles()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    sva.setData(sva.index(0, 3), "707016", Qt::EditRole);
    sva.setData(sva.index(0, 4), "607016", Qt::EditRole);
    sva.setData(sva.index(0, 5), "467800", Qt::EditRole);
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(tree, "FR");
    delete tree;

    QVERIFY(entry);
    const auto &debits  = entry->getDebits();
    const auto &credits = entry->getCredits();

    const auto *sale7  = findLine(credits, "707016");
    const auto *purch7 = findLine(debits,  "607016");
    const auto *vatDue = findLine(credits, "445200");
    const auto *vatDed = findLine(debits,  "445662");
    const auto *stock4c = findLine(credits, "467800");
    const auto *stock4d = findLine(debits,  "467800");

    QVERIFY(sale7 && purch7 && vatDue && vatDed && stock4c && stock4d);

    // (19) Sale7 title contains "Vente intracom"
    QVERIFY(sale7->title.contains("Vente intracom"));
    // (20) Sale7 title contains the route "EU > FR"
    QVERIFY(sale7->title.contains("EU > FR"));
    // (21) VatDue title contains "Acquisition intracom"
    QVERIFY(vatDue->title.contains("Acquisition intracom"));
    // (22) Purchase7 title contains the route "EU > FR"
    QVERIFY(purch7->title.contains("EU > FR"));
    // (23) Stock4 debit title contains "Vente intracom"
    QVERIFY(stock4d->title.contains("Vente intracom"));
    // (24) Stock4 credit title contains "Acquisition intracom"
    QVERIFY(stock4c->title.contains("Acquisition intracom"));
    // (25) VatDeductible title contains "Acquisition intracom"
    QVERIFY(vatDed->title.contains("Acquisition intracom"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Export (FR → EU): no applicable movement → null
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_export_skipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    // Exported from FR to EU pool — countryTo would be "EU", not "FR"
    QHash<QString, QHash<QString, int>> exported;
    exported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, {}, exported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    sva.setData(sva.index(0, 3), "707016", Qt::EditRole);
    sva.setData(sva.index(0, 4), "607016", Qt::EditRole);
    sva.setData(sva.index(0, 5), "467800", Qt::EditRole);
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(tree, "FR");
    delete tree;

    // (26) Export-only tree produces null (no EU→company movement)
    QVERIFY(!entry);
}

// ─────────────────────────────────────────────────────────────────────────────
// Missing new accounts (Sale7 / Purchase7 / Stock4 empty) → null
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_missing_accounts_returns_null()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    writeInventoryCsv(purchaseDir, "SKU-A", 666.59);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    // selfVat table created with default empty Sale7 / Purchase7 / Stock4
    BookAccountSelfVatTable sva(dir, "FR");
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    // (27) All three new accounts are empty by default → throws ExceptionWithTitleText
    bool caught = false;
    try {
        f.createEntry(tree, "FR");
    } catch (const ExceptionWithTitleText &e) {
        caught = true;
        QCOMPARE(e.errorTitle(), QString("Missing Self-VAT Accounts"));
    }
    delete tree;

    QVERIFY(caught);
}

// ─────────────────────────────────────────────────────────────────────────────
// Zero unit price in CSV → totalPrice == 0.0 → row skipped → null
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_inventory_zero_price_skipped()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    QDir purchaseDir(dir.filePath("purchases"));
    dir.mkdir("purchases");

    // Write CSV with price = 0 (no valid price → no purchase data)
    writeInventoryCsv(purchaseDir, "SKU-A", 0.0);
    QHash<QString, QHash<QString, int>> imported;
    imported["FR"]["SKU-A"] = 1;
    auto *tree = makeInventoryTree(purchaseDir, imported);

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    BookAccountSelfVatTable sva(dir, "FR");
    sva.setData(sva.index(0, 3), "707016", Qt::EditRole);
    sva.setData(sva.index(0, 4), "607016", Qt::EditRole);
    sva.setData(sva.index(0, 5), "467800", Qt::EditRole);
    JournalTable jt(dir);
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    auto entry = f.createEntry(tree, "FR");
    delete tree;

    // (28) Zero price → totalPrice == 0.0 → row skipped → null
    QVERIFY(!entry);
}

// Helper shared by the two invoice-generation-with-refunds tests.
// Loads orderInfos from VatEu into an OrderManager and returns the "no invoices" map,
// then verifies that each entry for the given orderId has a non-null InvoicingInfo.
static void checkRefundsHaveInvoicingInfo(
        OrderManager &manager,
        const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext,
              OrderManager::ShipmentRefundsWithUpdates>>>> &noInvMap,
        const QString &targetOrderId,
        int expectedRefundCount)
{
    int refundCount = 0;
    int missingInfoCount = 0;

    for (auto chanIt = noInvMap->cbegin(); chanIt != noInvMap->cend(); ++chanIt) {
        for (auto storeIt = chanIt->cbegin(); storeIt != chanIt->cend(); ++storeIt) {
            for (auto ctxIt = storeIt->cbegin(); ctxIt != storeIt->cend(); ++ctxIt) {
                const OrderManager::ShipmentRefundsWithUpdates &entry = ctxIt.value();
                for (const auto &shipment : entry.shipmentsRefundsSameActivity) {
                    if (!shipment || shipment->getActivities().isEmpty()) continue;
                    const auto activities = shipment->getActivities();
                    if (activities.first().getEventId() != targetOrderId) continue;

                    ++refundCount;

                    const QString &actId = activities.first().getActivityId();
                    QSharedPointer<InvoicingInfo> info = manager.getInvoicingInfo(actId);
                    if (!info) info = entry.invoicingInfo;
                    if (!info || info->getItems().isEmpty())
                        ++missingInfoCount;
                }
            }
        }
    }

    QCOMPARE(refundCount, expectedRefundCount);
    QCOMPARE(missingInfoCount, 0); // every refund must have InvoicingInfo with line items
}

// ===========================================================================
// test_invoice_generation_refunds_synthetic
// Creates a synthetic VatEu-format CSV with:
//   - 1 SALE  (has Amazon invoice number)
//   - 2 REFUNDs (no invoice number / URL – same pattern as 404-4309379-2683555)
// Verifies that ImporterFileAmazonVatEu populates invoicingInfos for both
// refunds even when there is no Amazon invoice number/URL.
// ===========================================================================
void TestBookEntries::test_invoice_generation_refunds_synthetic()
{
    // Minimal VatEu CSV: only the columns the importer actually reads.
    const QString csvContent =
        "TRANSACTION_TYPE,TRANSACTION_EVENT_ID,ACTIVITY_TRANSACTION_ID,"
        "TRANSACTION_COMPLETE_DATE,TAX_CALCULATION_DATE,MARKETPLACE,"
        "VAT_CALCULATION_IMPUTATION_COUNTRY,PRODUCT_TAX_CODE,"
        "PRICE_OF_ITEMS_AMT_VAT_EXCL,TOTAL_ACTIVITY_VALUE_VAT_AMT,"
        "TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL,VAT_INV_NUMBER,TRANSACTION_CURRENCY_CODE,"
        "TAX_REPORTING_SCHEME,TAX_COLLECTION_RESPONSIBILITY,"
        "PRICE_OF_ITEMS_VAT_RATE_PERCENT,SELLER_SKU,QTY,"
        "SALE_DEPART_COUNTRY,SALE_ARRIVAL_COUNTRY,INVOICE_URL,ITEM_DESCRIPTION\n"

        // SALE – has Amazon invoice number
        "SALE,ORDER-TEST-001,ACT-SALE-001,"
        "27-01-2026,27-01-2026,amazon.it,"
        "IT,A_GEN_STANDARD,"
        "4.91,1.08,"
        "4.91,IT60000INV001,EUR,"
        "REGULAR,SELLER,"
        "0.22,SKU-001,1,"
        "IT,IT,https://example.com/inv001,Test Product\n"

        // REFUND 1 – no invoice number / URL
        "REFUND,ORDER-TEST-001,ACT-REFUND-001,"
        "29-01-2026,27-01-2026,amazon.it,"
        "IT,A_GEN_STANDARD,"
        "-2.46,-0.54,"
        "-2.46,,EUR,"
        "REGULAR,SELLER,"
        "0.22,SKU-001,1,"
        "IT,IT,,Test Product\n"

        // REFUND 2 – no invoice number / URL
        "REFUND,ORDER-TEST-001,ACT-REFUND-002,"
        "28-01-2026,27-01-2026,amazon.it,"
        "IT,A_GEN_STANDARD,"
        "-2.45,-0.54,"
        "-2.45,,EUR,"
        "REGULAR,SELLER,"
        "0.22,SKU-001,1,"
        "IT,IT,,Test Product\n";

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Write synthetic CSV
    const QString csvPath = tempDir.filePath("test_vat_eu.csv");
    {
        QFile f(csvPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(csvContent.toUtf8());
    }

    // Use a second temp dir for the importer's settings (prevents duplicate-import check)
    QTemporaryDir importerDir;
    QVERIFY(importerDir.isValid());

    ImporterFileAmazonVatEu vatEuImporter(importerDir.path());
    vatEuImporter.load();

    auto result = QCoro::waitFor(vatEuImporter.loadReport(csvPath));
    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);

    // Two refunds must have been parsed
    QCOMPARE(result.orderInfos->refunds.size(), 2);

    // Both refunds must appear in invoicingInfos with at least one line item.
    // Before fix: InvoicingInfo::create fails (no number/URL/items) → refundInfoCount == 0.
    // After fix:  InvoicingInfo created with line items             → refundInfoCount == 2.
    int refundInfoCount = 0;
    for (const auto &inv : result.orderInfos->invoicingInfos) {
        if (inv.shipmentOrRefundId == "ACT-REFUND-001"
                || inv.shipmentOrRefundId == "ACT-REFUND-002") {
            QVERIFY2(!inv.invoicingInfo.getItems().isEmpty(),
                     qPrintable("InvoicingInfo for " + inv.shipmentOrRefundId
                                + " must contain at least one line item"));
            ++refundInfoCount;
        }
    }
    QCOMPARE(refundInfoCount, 2);

    // Full integration: record into OrderManager and verify generateInvoices can proceed.
    QTemporaryDir managerDir;
    QVERIFY(managerDir.isValid());
    OrderManager manager(managerDir.path());

    ActivitySource source = vatEuImporter.getActivitySource();
    for (const auto &ship : result.orderInfos->shipments)
        manager.recordShipmentFromSource(ship.getId(), &source, &ship, QDate(), false);
    for (const auto &ref : result.orderInfos->refunds)
        manager.recordShipmentFromSource(ref.getId(), &source, &ref, QDate(), false);
    for (const auto &inv : result.orderInfos->invoicingInfos)
        manager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);

    auto noInvMap = manager.get_channel_site_ShipmentAndRefundsNoInvoices(
            QDate(2026, 1, 1), QDate(2026, 1, 31));
    QVERIFY(!noInvMap.isNull());

    checkRefundsHaveInvoicingInfo(manager, noInvMap, "ORDER-TEST-001", 2);
}

// ─── Helpers for tryToConnect same-currency tests ────────────────────────────

namespace {

class BookTableForAssoc : public AbstractBooksTable {
public:
    BookTableForAssoc(const BooksConnections *c, const QDir &dir)
        : AbstractBooksTable(c, dir, nullptr) {}
    QString getId() const override { return "BookForAssoc"; }
    void load(int) override {}
};

class BankStatementForAssoc : public AbstractBankStatement {
public:
    BankStatementForAssoc(QObject *parent = nullptr) : AbstractBankStatement(parent) {}
    QString getId() const override { return "BankStmtForAssoc"; }
    QString getName() const override { return "BankForAssoc"; }
    QString defaultAccount() const override { return "512000"; }
    QString defaultAccountFees() const override { return "627000"; }
    QString defaultJournal() const override { return "BQ"; }
    QSharedPointer<QList<BankRow>> readRows(const QString &) const override {
        return QSharedPointer<QList<BankRow>>::create();
    }
};

class BankTableForAssoc : public AbstractBooksTableBank {
public:
    BankTableForAssoc(const BooksConnections *c, const QDir &dir)
        : AbstractBooksTableBank(c, dir, nullptr) {}
    QString getId() const override { return "BankForAssoc"; }
    const AbstractBankStatement *getBankStatement() const override { return &m_stmt; }
private:
    BankStatementForAssoc m_stmt;
};

} // namespace

// ─── test_associate_usd_invoice_usd_bank_succeeds ────────────────────────────
// Confirms that tryToConnect does NOT need a rate manager when both sides share
// the same currency (USD). The UI-level bug in PaneBookKeeping::associate()
// was blocking same-currency associations by gating on the Fixer.io API key.
void TestBookEntries::test_associate_usd_invoice_usd_bank_succeeds()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    BooksConnections connections(dir);
    BookTableForAssoc bookTable(&connections, dir);
    BankTableForAssoc bankTable(&connections, dir);

    const QDate date(2025, 11, 15);
    bookTable.add("INVOICE_USD_001", "", date,  56.04, "USD", "Purchase USD", "60110", "40110", 0.0, "", "");
    bankTable.add("BANK_USD_001",    "", date, -56.04, "USD", "Bank USD",     "",      "",      0.0, "", "");

    QHash<AbstractBooksTable *, QModelIndexList> selection;
    selection[&bookTable] = {bookTable.index(0, 0)};
    selection[&bankTable] = {bankTable.index(0, 0)};

    // nullptr: no API key, no HTTP call — must succeed for same-currency
    bool threw = false;
    try {
        connections.tryToConnect(selection, nullptr);
    } catch (...) {
        threw = true;
    }

    QVERIFY2(!threw, "tryToConnect must succeed for USD-USD without a rate manager");
    QVERIFY(connections.contains("BookForAssoc", "INVOICE_USD_001"));
    QVERIFY(connections.contains("BankForAssoc", "BANK_USD_001"));
}

// ─── test_associate_eur_invoice_usd_bank_fails_without_rate ──────────────────
// Confirms that tryToConnect throws when a rate manager is required (EUR book
// entry paired with a USD bank entry) but none is provided.
void TestBookEntries::test_associate_eur_invoice_usd_bank_fails_without_rate()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    BooksConnections connections(dir);
    BookTableForAssoc bookTable(&connections, dir);
    BankTableForAssoc bankTable(&connections, dir);

    const QDate date(2025, 11, 15);
    bookTable.add("INVOICE_EUR_001", "", date,  56.04, "EUR", "Purchase EUR", "60110", "40110", 0.0, "", "");
    bankTable.add("BANK_USD_002",    "", date, -56.04, "USD", "Bank USD",     "",      "",      0.0, "", "");

    QHash<AbstractBooksTable *, QModelIndexList> selection;
    selection[&bookTable] = {bookTable.index(0, 0)};
    selection[&bankTable] = {bankTable.index(0, 0)};

    bool threw = false;
    try {
        connections.tryToConnect(selection, nullptr);
    } catch (const ExceptionWithTitleText &) {
        threw = true;
    }

    QVERIFY2(threw, "tryToConnect must throw for EUR-USD without a rate manager");
}

// ─── test_associate_usd_invoice_usd_bank_with_api_key_blocker ────────────────
// Shows that a CurrencyRateManager built with an empty API key is never
// consulted for same-currency associations, so the connection succeeds even
// when the key is empty. This validates that the fix (removing the API-key gate
// in PaneBookKeeping::associate and always calling tryToConnect) is safe.
void TestBookEntries::test_associate_usd_invoice_usd_bank_with_api_key_blocker()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    BooksConnections connections(dir);
    BookTableForAssoc bookTable(&connections, dir);
    BankTableForAssoc bankTable(&connections, dir);

    const QDate date(2025, 11, 15);
    bookTable.add("INVOICE_USD_002", "", date,  56.04, "USD", "Purchase USD", "60110", "40110", 0.0, "", "");
    bankTable.add("BANK_USD_003",    "", date, -56.04, "USD", "Bank USD",     "",      "",      0.0, "", "");

    QHash<AbstractBooksTable *, QModelIndexList> selection;
    selection[&bookTable] = {bookTable.index(0, 0)};
    selection[&bankTable] = {bankTable.index(0, 0)};

    // A rate manager with an empty key must not be called for same-currency.
    CurrencyRateManager rateManager(dir, "");

    bool threw = false;
    try {
        connections.tryToConnect(selection, &rateManager);
    } catch (...) {
        threw = true;
    }

    QVERIFY2(!threw, "Same-currency USD-USD must succeed even with an empty API key");
    QVERIFY(connections.contains("BookForAssoc", "INVOICE_USD_002"));
    QVERIFY(connections.contains("BankForAssoc", "BANK_USD_003"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: dual-amount VAT token must drive currencyRate for ALL journal
// lines, not just the VAT line.
//
// Invoice: 2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8372__FAMZMK__
//          FR-TVA--0.17EUR_-0.15GBP__-0.87GBP.pdf
//
// Token FR-TVA--0.17EUR_-0.15GBP encodes:
//   company VAT = 0.17 EUR,  source VAT = 0.15 GBP  → implied rate = 0.17/0.15 = 1.1333
//
// We inject a deliberately wrong CRM rate (1.5) so that any line which still
// uses CRM produces a wrong EUR amount; the test catches that via explicit
// amount checks and the debit == credit balance assertion.
//
// Expected (rate = 1.1333):
//   Debit  FAMZMK : 0.87 GBP × 1.1333 → 0.99 EUR
//   Credit 622201 : 0.72 GBP × 1.1333 → 0.82 EUR
//   Credit 445660 : 0.15 GBP × 1.1333 → 0.17 EUR   (exactly, no rounding)
//   CreditSum = 0.82 + 0.17 = 0.99 == DebitSum ✓
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_purchase_dual_amount_uses_invoice_rate()
{
    const QString fileName =
        "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8372__FAMZMK"
        "__FR-TVA--0.17EUR_-0.15GBP__-0.87GBP.pdf";

    const PurchaseInformation info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

    // Sanity-check the decode
    QCOMPARE(info.totalAmount,  -0.87);
    QCOMPARE(info.currency,     QString("GBP"));
    QVERIFY(info.country_vatRate_vatCompany.contains("FR"));

    // Set up environment: CRM has a deliberately wrong GBP→EUR rate (1.5).
    // If the factory falls back to CRM for the expense/supplier lines the amounts
    // will differ from the expected values, and the entry will not balance.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    crm.importRate("2026-01-31", "GBP", "EUR", 1.5); // wrong — should NOT be used

    BooksAccountsSalesTable sa(dir);
    // Default FR table includes 20 % (0.2) account; tolerance 0.99 % covers the
    // 0.208 key computed from 0.15 / 0.72 = 20.8 %.
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    BookAccountSelfVatTable selfVatJt(dir, "FR");
    AmzPaymentSettings amzSettingsJt(dir);
    BookAccountAmzBalanceTable balanceJt(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory factory(&crm, &ci, &sa, &gsa, &pa, &jt, &selfVatJt, &amzSettingsJt, &balanceJt);

    const auto entry = factory.createEntry(info);
    QVERIFY(!entry.isNull());

    // isRefund → expense + VAT on credit side, supplier on debit
    QCOMPARE(entry->getCredits().size(), 2);
    QCOMPARE(entry->getDebits().size(),  1);

    // ── Credit lines ─────────────────────────────────────────────────────────
    double expenseEur = 0.0;
    double vatEur     = 0.0;
    bool foundExpense = false;
    bool foundVat     = false;
    for (const auto &line : entry->getCredits()) {
        if (line.account == "622201") {
            foundExpense = true;
            expenseEur = line.currency_amount.value("EUR");
            // With correct rate 1.1333: 0.72 × 1.1333 = 0.816 → rounded to 0.82
            // With wrong CRM rate 1.5 : 0.72 × 1.5    = 1.08
            QVERIFY2(qAbs(expenseEur - 0.82) < 0.005,
                     qPrintable(QString("Expense EUR should be ≈ 0.82 (dual rate), got %1").arg(expenseEur)));
            QVERIFY2(!line.title.contains("@ 1.5"),
                     "Expense line must not use the CRM rate 1.5");
        }
        if (line.account == "445660") {
            foundVat = true;
            vatEur = line.currency_amount.value("EUR");
            // 0.15 GBP × 1.1333 = 0.17 EUR exactly
            QVERIFY2(qAbs(vatEur - 0.17) < 0.001,
                     qPrintable(QString("VAT EUR should be 0.17, got %1").arg(vatEur)));
            QVERIFY2(!line.title.contains("@ 1.5"),
                     "VAT line must not use the CRM rate 1.5");
        }
    }
    QVERIFY(foundExpense);
    QVERIFY(foundVat);

    // ── Debit line ────────────────────────────────────────────────────────────
    bool foundSupplier = false;
    for (const auto &line : entry->getDebits()) {
        if (line.account == "FAMZMK") {
            foundSupplier = true;
            const double supplierEur = line.currency_amount.value("EUR");
            // With correct rate 1.1333: 0.87 × 1.1333 = 0.986 → rounded to 0.99
            // With wrong CRM rate 1.5 : 0.87 × 1.5    = 1.305 → rounded to 1.31
            QVERIFY2(qAbs(supplierEur - 0.99) < 0.005,
                     qPrintable(QString("Supplier EUR should be ≈ 0.99 (dual rate), got %1").arg(supplierEur)));
            QVERIFY2(!line.title.contains("@ 1.5"),
                     "Supplier line must not use the CRM rate 1.5");
        }
    }
    QVERIFY(foundSupplier);

    // ── Balance ───────────────────────────────────────────────────────────────
    // With the correct rate the entry is exactly balanced (0.99 == 0.82 + 0.17).
    // With the CRM rate the entry would be unbalanced (1.31 ≠ 1.08 + 0.17 = 1.25).
    QCOMPARE(entry->getDebitSum(), entry->getCreditSum());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: when accountSupplier matches AmzPaymentSettings::getAmazonAccount(),
// createEntry(PurchaseInformation) uses getAccountDebit() for the supplier
// line instead of the raw accountSupplier value.
// A regular (non-Amazon) supplier account is left unchanged.
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_factory_purchase_amz_account_uses_debit_account()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    setupCompanyInfoFr(dir);
    // amazonAccount = "FAMZMK", debitAccount = "467150"
    setupAmzSettings(dir, "467150", "FAMZMK");

    CompanyInfosTable ci(dir);
    CurrencyRateManager crm(dir, "");
    BooksAccountsSalesTable sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable jt(dir);
    BookAccountSelfVatTable sva(dir, "FR");
    AmzPaymentSettings amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);
    BookAccountsGroupedSalesTable gsa(dir);
    JournalEntryFactory f(&crm, &ci, &sa, &gsa, &pa, &jt, &sva, &amzSet, &balanceTable);

    // ── Case 1: supplier is the Amazon account → must be replaced by debit account ──
    PurchaseInformation amzPurchase;
    amzPurchase.date           = QDate(2026, 1, 15);
    amzPurchase.account        = "622201";
    amzPurchase.label          = "frais-amazon";
    amzPurchase.accountSupplier = "FAMZMK"; // matches getAmazonAccount()
    amzPurchase.totalAmount    = 120.0;
    amzPurchase.currency       = "EUR";
    amzPurchase.country_vatRate_vat["FR"]["0.2"] = 20.0;

    const auto entry1 = f.createEntry(amzPurchase);
    QVERIFY(!entry1.isNull());
    QCOMPARE(entry1->getDebitSum(), entry1->getCreditSum());

    bool foundDebitAccount = false;
    bool foundAmzAccount   = false;
    for (const auto &line : entry1->getCredits()) {
        if (line.account == "467150") { foundDebitAccount = true; }
        if (line.account == "FAMZMK") { foundAmzAccount   = true; }
    }
    QVERIFY(foundDebitAccount);   // debit account used for supplier line
    QVERIFY(!foundAmzAccount);    // raw Amazon account must NOT appear

    // ── Case 2: regular supplier → account is left unchanged ──────────────────
    PurchaseInformation regularPurchase;
    regularPurchase.date           = QDate(2026, 1, 15);
    regularPurchase.account        = "607000";
    regularPurchase.label          = "fourniture-bureau";
    regularPurchase.accountSupplier = "401SOFTCO"; // not the Amazon account
    regularPurchase.totalAmount    = 60.0;
    regularPurchase.currency       = "EUR";
    regularPurchase.country_vatRate_vat["FR"]["0.2"] = 10.0;

    const auto entry2 = f.createEntry(regularPurchase);
    QVERIFY(!entry2.isNull());
    QCOMPARE(entry2->getDebitSum(), entry2->getCreditSum());

    bool foundRegularSupplier = false;
    for (const auto &line : entry2->getCredits()) {
        if (line.account == "401SOFTCO") { foundRegularSupplier = true; }
    }
    QVERIFY(foundRegularSupplier); // regular supplier account unchanged
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: AbstractBooksTable::sortByDate() sorts rows most-recent first.
// ─────────────────────────────────────────────────────────────────────────────
void TestBookEntries::test_abstract_books_table_sort_by_date()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());

    BooksConnections connections(dir);
    BookTableForAssoc table(&connections, dir);
    table.init();

    // Insert rows in arbitrary date order
    table.add("id1", "", QDate(2025, 6, 15),  10.0, "EUR", "June",     "601", "401", 0.0, "", "");
    table.add("id2", "", QDate(2026, 1, 31),  20.0, "EUR", "January",  "602", "402", 0.0, "", "");
    table.add("id3", "", QDate(2024, 12,  1),  5.0, "EUR", "December", "603", "403", 0.0, "", "");
    table.add("id4", "", QDate(2026, 3,  10), 30.0, "EUR", "March",    "604", "404", 0.0, "", "");

    // Verify pre-sort state (rows are inserted at position 0, so last added is first)
    QCOMPARE(table.rowCount(), 4);

    table.sortByDate();

    // After sort: most recent first
    QCOMPARE(table.rowCount(), 4);
    QCOMPARE(table.getDate(0), QDate(2026, 3, 10));
    QCOMPARE(table.getDate(1), QDate(2026, 1, 31));
    QCOMPARE(table.getDate(2), QDate(2025, 6, 15));
    QCOMPARE(table.getDate(3), QDate(2024, 12,  1));

    // Row data is carried with the sort
    QCOMPARE(table.getLabel(0), QString("March"));
    QCOMPARE(table.getLabel(1), QString("January"));
    QCOMPARE(table.getLabel(2), QString("June"));
    QCOMPARE(table.getLabel(3), QString("December"));
}

// ─────────────────────────────────────────────────────────────────────────────
// test_factory_amz_real_payment_files
//
// Decodes every filename supplied by the user, calls createEntry(), and
// verifies that the resulting journal entry is balanced (debitSum == creditSum).
//
// All non-EUR currencies use fake rate = 1.0.
// A credit account ("AMZCA") is configured so the factory adds the balancing
// credit line (net Amazon sales collected) to close every entry.
// ─────────────────────────────────────────────────────────────────────────────

void TestBookEntries::test_factory_amz_real_payment_files()
{
    struct Case {
        const char   *fname;
        QStringList   extraCurrencies; // non-EUR currencies needing rate = 1.0
    };

    const QList<Case> cases = {

        // ── BE marketplace (EUR, no conversion) ──────────────────────────────
        { "payment_be_2025_12_06__to__2026_01_03__expenses-304.15EUR__refunded-expenses-7.44EUR__194.17EUR.pdf",
          {} },
        { "payment_be_2026_01_03__to__2026_01_17__expenses-159.70EUR__243.02EUR.pdf",
          {} },
        { "payment_be_2026_01_17__to__2026_01_31__expenses-227.50EUR__refunded-expenses-14.09EUR__270.79EUR.pdf",
          {} },
        { "payment_be_2026_01_31__to__2026_02_14__expenses-302.44EUR__170.62EUR.pdf",
          {} },
        { "payment_be_2026_02_14__to__2026_02_28__expenses-163.48EUR__refunded-expenses-13.39EUR__168.49EUR.pdf",
          {} },
        { "payment_be_2026_02_28__to__2026_03_14__expenses-407.09EUR__refunded-expenses-22.72EUR__93.29EUR.pdf",
          {} },

        // ── CA marketplace (CAD rate=1.0; some paid in EUR) ───────────────────
        { "payment_ca_2025_12_25__to__2026_01_08__balance-begin-509.95CAD__balance-end-822.85CAD__expenses-1632.36CAD__refunded-expenses-92.60CAD__0CAD.pdf",
          {"CAD"} },
        { "payment_ca_2026_01_08__to__2026_01_22__balance-begin-822.85CAD__balance-end-730.89CAD__expenses-1935.67CAD__refunded-expenses-964.32CAD__133.62EUR.pdf",
          {"CAD"} },
        { "payment_ca_2026_01_22__to__2026_02_05__balance-begin-730.89CAD__balance-end-641.44CAD__expenses-1597.31CAD__refunded-expenses-133.54CAD__0CAD.pdf",
          {"CAD"} },
        { "payment_ca_2026_02_05__to__2026_02_19__balance-begin-641.44CAD__balance-end-861.91CAD__expenses-1608.91CAD__refunded-expenses-94.32CAD__222.32EUR.pdf",
          {"CAD"} },
        { "payment_ca_2026_02_19__to__2026_03_05__balance-begin-861.91CAD__balance-end-554.69CAD__expenses-1560.97CAD__refunded-expenses-170.40CAD__0CAD.pdf",
          {"CAD"} },

        // ── COM marketplace (USD rate=1.0) ────────────────────────────────────
        { "payment_com_2025_01_20__to__2026_02_17__expenses-12.04USD__refunded-expenses-4.38USD__0USD.pdf",
          {"USD"} },
        { "payment_com_2025_12_23__to__2026_01_20__expenses-48.64USD__75.32USD.pdf",
          {"USD"} },
        { "payment_com_2025_12_24__to__2026_01_07__balance-begin-1336.28USD__balance-end-1311.19USD__expenses-1667.73USD__refunded-expenses-56.98USD__524.50USD.pdf",
          {"USD"} },
        { "payment_com_2026_01_08__to__2026_01_22__balance-begin-1311.19USD__balance-end-1135.55USD__expenses-2627.38USD__refunded-expenses-153.17USD__177.90USD.pdf",
          {"USD"} },
        { "payment_com_2026_01_21__to__2026_02_04__balance-begin-1135.55USD__balance-end-1607.21USD__expenses-2516.58USD__refunded-expenses-150.79USD__0USD.pdf",
          {"USD"} },
        { "payment_com_2026_02_04__to__2026_02_18__balance-begin-1607.21USD__balance-end-2514.80USD__expenses-3229.53USD__refunded-expenses-211.22USD__728.85USD.pdf",
          {"USD"} },
        { "payment_com_2026_02_18__to__2026_03_04__balance-begin-2514.80USD__balance-end-2826.10USD__expenses-3554.01USD__refunded-expenses-224.77USD__1895.84USD.pdf",
          {"USD"} },

        // ── co.uk marketplace (GBP rate=1.0) ─────────────────────────────────
        { "payment_co.uk_2025_12_25__to__2026_01_08__balance-begin-24.92GBP__balance-end-24.92GBP__expenses-147.24GBP__refunded-expenses-12.26GBP__16.86GBP.pdf",
          {"GBP"} },
        { "payment_co.uk_2026_01_08__to__2026_01_22__balance-begin-24.92GBP__balance-end-24.92GBP__expenses-90.94GBP__refunded-expenses-9.48GBP__12.58GBP.pdf",
          {"GBP"} },
        { "payment_co.uk_2026_01_22__to__2026_02_05__balance-begin-27.92GBP__balance-end-0GBP__expenses-123.77GBP__refunded-expenses-16.80GBP__39.15GBP.pdf",
          {"GBP"} },
        { "payment_co.uk_2026_02_05__to__2026_02_19__balance-begin-39.15GBP__balance-end-24.92GBP__expenses-98.74GBP__84.57GBP.pdf",
          {"GBP"} },
        { "payment_co.uk_2026_02_19__to__2026_03_05__balance-begin-24.92GBP__balance-end-24.92GBP__expenses-188.61GBP__refunded-expenses-16.55GBP__30.41GBP.pdf",
          {"GBP"} },

        // ── DE marketplace (EUR, no conversion) ───────────────────────────────
        { "payment_de_2025_12_25__to__2026_01_08__expenses-1889.05EUR__refunded-expenses-130.42EUR__736.85EUR.pdf",
          {} },
        { "payment_de_2026_01_08__to__2026_01_22__expenses-1415.78EUR__refunded-expenses-149.66EUR__1054.33EUR.pdf",
          {} },
        { "payment_de_2026_01_22__to__2026_02_05__expenses-2037.25EUR__refunded-expenses-178.48EUR__1127.78EUR.pdf",
          {} },
        { "payment_de_2026_02_05__to__2026_02_19__expenses-1092.93EUR__refunded-expenses-153.77EUR__1036.20EUR.pdf",
          {} },
        { "payment_de_2026_02_19__to__2026_03_05__expenses-2000.94EUR__refunded-expenses-163.65EUR__563.60EUR.pdf",
          {} },

        // ── ES marketplace (EUR) ──────────────────────────────────────────────
        { "payment_es_2025_12_25__to__2026_01_08__expenses-345.89EUR__265.63EUR.pdf",
          {} },
        { "payment_es_2026_01_08__to__2026_01_22__expenses-249.87EUR__refunded-expenses-10.20EUR__294.73EUR.pdf",
          {} },
        { "payment_es_2026_01_22__to__2026_02_05__expenses-239.07EUR__refunded-expenses-19.33EUR__190.76EUR.pdf",
          {} },
        { "payment_es_2026_02_05__to__2026_02_19__expenses-394.88EUR__refunded-expenses-22.06EUR__306.28EUR.pdf",
          {} },
        { "payment_es_2026_02_19__to__2026_03_05__expenses-252.28EUR__refunded-expenses-29.16EUR__149.32EUR.pdf",
          {} },

        // ── FR marketplace (EUR) ──────────────────────────────────────────────
        { "payment_fr_2025_12_25__to__2026_01_08__expenses-3150.99EUR__refunded-expenses-35.97EUR__2062.85EUR.pdf",
          {} },
        { "payment_fr_2026_01_08__to__2026_01_22__expenses-1504.05EUR__refunded-expenses-44.40EUR__1169.28EUR.pdf",
          {} },
        { "payment_fr_2026_01_22__to__2026_02_05__expenses-2330.96EUR__refunded-expenses-67.24EUR__883.72EUR.pdf",
          {} },
        { "payment_fr_2026_02_05__to__2026_02_19__balance-begin-42.68EUR__balance-end-42.68EUR__expenses-2281.72EUR__refunded-expenses-53.20EUR__1490.92EUR.pdf",
          {} },
        { "payment_fr_2026_02_19__to__2026_03_05__balance-begin-42.68EUR__balance-end-42.68EUR__expenses-3226.96EUR__refunded-expenses-76.86EUR__1659.90EUR.pdf",
          {} },

        // ── IT marketplace (EUR) ──────────────────────────────────────────────
        { "payment_it_2025_12_25__to__2026_01_08__expenses-482.48EUR__refunded-expenses-28.18EUR__283.86EUR.pdf",
          {} },
        { "payment_it_2026_01_08__to__2026_01_22__expenses-165.71EUR__refunded-expenses-9.55EUR__126.01EUR.pdf",
          {} },
        { "payment_it_2026_01_22__to__2026_02_05__expenses-515.69EUR__refunded-expenses-15.19EUR__411.16EUR.pdf",
          {} },
        { "payment_it_2026_02_05__to__2026_02_19__expenses-493.64EUR__refunded-expenses-31.97EUR__365.78EUR.pdf",
          {} },
        { "payment_it_2026_02_19__to__2026_03_05__expenses-872.24EUR__refunded-expenses-21.33EUR__471.88EUR.pdf",
          {} },

        // ── JP marketplace (JPY rate=1.0) ─────────────────────────────────────
        { "payment_jp_2025_12_19__to__2026_02_12__expenses-6942JPY__1062JPY.pdf",
          {"JPY"} },
        { "payment_jp_2026_02_12__to__2026_02_26__expenses-12185JPY__2733JPY.pdf",
          {"JPY"} },
        { "payment_jp_2026_02_26__to__2026_03_12__expenses-10847JPY__1395JPY.pdf",
          {"JPY"} },

        // ── MX marketplace (MXN rate=1.0) ─────────────────────────────────────
        { "payment_mx_2025_12_25__to__2026_01_08__balance-begin-31.38MXN__balance-end-336.27MXN__expenses-416.01MXN__31.38MXN.pdf",
          {"MXN"} },
        { "payment_mx_2026_01_08__to__2026_01_22__expenses-176.26MXN__refunded-expenses-178.06MXN__278.45MXN.pdf",
          {"MXN"} },
        { "payment_mx_2026_01_22__to__2026_02_05__expenses-302.17MXN__0MXN.pdf",
          {"MXN"} },
        { "payment_mx_2026_02_05__to__2026_02_19__balance-begin-313.01MXN__balance-end-15.65MXN__expenses-0MXN__297.36MXN.pdf",
          {"MXN"} },
        { "payment_mx_2026_02_19__to__2026_03_05__balance-begin-15.65MXN__balance-end-59.68MXN__expenses-842.54MXN__refunded-expenses-255.64MXN__0MXN.pdf",
          {"MXN"} },

        // ── NL marketplace (EUR) ──────────────────────────────────────────────
        { "payment_nl_2025_12_18__to__2026_01_01__expenses-137.01EUR__refunded-expenses-10.21EUR__132.25EUR.pdf",
          {} },
        { "payment_nl_2026_01_01__to__2026_01_15__expenses-59.97EUR__22.95EUR.pdf",
          {} },
        { "payment_nl_2026_01_15__to__2026_01_29__expenses-69.75EUR__refunded-expenses-11.68EUR__56.53EUR.pdf",
          {} },
        { "payment_nl_2026_01_29__to__2026_02_12__expenses-177.22EUR__refunded-expenses-25.52EUR__34.23EUR.pdf",
          {} },
        { "payment_nl_2026_02_12__to__2026_02_26__expenses-103.06EUR__162.02EUR.pdf",
          {} },
        { "payment_nl_2026_02_26__to__2026_03_12__expenses-253.08EUR__refunded-expenses-16.70EUR__133.94EUR.pdf",
          {} },

        // ── PL marketplace (PLN rate=1.0; some paid in EUR) ───────────────────
        { "payment_pl_2025_12_22__to__2026_01_05__expenses-7.99PLN__27.70PLN.pdf",
          {"PLN"} },
        { "payment_pl_2026_01_05__to__2026_01_19__expenses-15.22PLN__17.21EUR.pdf",
          {"PLN"} },
        { "payment_pl_2026_01_19__to__2026_02_02__expenses-175.49PLN__refunded-expenses-73.46PLN__23.19EUR.pdf",
          {"PLN"} },
        { "payment_pl_2026_02_02__to__2026_02_16__expenses-17.02PLN__refunded-expenses-8.58PLN__0PLN.pdf",
          {"PLN"} },
        { "payment_pl_2026_02_16__to__2026_03_02__balance-begin-8.44PLN__balance-end-0PLN__expenses-22.87PLN__68.59PLN.pdf",
          {"PLN"} },
        { "payment_pl_2026_03_02__to__2026_03_16__expenses-31.87PLN__6.15EUR.pdf",
          {"PLN"} },

        // ── SE marketplace (SEK rate=1.0; negative paid → credit |paid|) ──────
        { "payment_se_2025_12_31__to__2026_01_14__expenses-487.84SEK__-487.84SEK.pdf",
          {"SEK"} },
        { "payment_se_2026_01_14__to__2026_01_28__expenses-610.33SEK__-301.14SEK.pdf",
          {"SEK"} },
        { "payment_se_2026_01_28__to__2026_02_11__expenses-542.11SEK__65.93SEK.pdf",
          {"SEK"} },
        { "payment_se_2026_02_11__to__2026_02_25__expenses-1587.49SEK__-332.19SEK.pdf",
          {"SEK"} },
        { "payment_se_2026_02_25__to__2026_03_11__expenses-1589.63SEK__refunded-expenses-99.80SEK__1335.65SEK.pdf",
          {"SEK"} },

        // ── TR marketplace (TRY rate=1.0; negative paid → credit |paid|) ──────
        { "payment_tr_2025_12_24__to__2026_01_07__expenses-187.02TRY__-187.02TRY.pdf",
          {"TRY"} },
        { "payment_tr_2026_01_07__to__2026_02_04__expenses-35.47TRY__35.47TRY.pdf",
          {"TRY"} },
    };

    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    QDir dir(tempDir.path());
    setupCompanyInfoFr(dir);
    setupAmzSettings(dir, "467150", "FAMZMK");

    // Pre-populate the balance-accounts CSV so that Accounts::account = "467008"
    // for every DEFAULT_AMAZON_SITE.  This triggers the balancing credit line in
    // the factory and makes every entry a proper balanced écriture comptable.
    {
        QFile f(dir.filePath("amazon_balance_accounts.csv"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "Amazon;Balance;Account\n";
        for (const QString &site : CountriesEu::DEFAULT_AMAZON_SITES) {
            out << site << ";467145;467008\n";
        }
    }

    CompanyInfosTable        ci(dir);
    CurrencyRateManager      crm(dir, "");
    BooksAccountsSalesTable  sa(dir);
    BookAccountPurchaseTable pa(dir, "FR");
    JournalTable             jt(dir);
    AmzPaymentSettings       amzSet(dir);
    BookAccountAmzBalanceTable balanceTable(dir);

    for (const Case &tc : std::as_const(cases)) {
        AmzPaymentInfo info;
        try {
            info = PurchaseAmzPaymentsManager::decode(tc.fname);
        } catch (const ExceptionWithTitleText &e) {
            QFAIL(qPrintable(QString("decode failed for '%1': %2")
                                 .arg(tc.fname, QString::fromUtf8(e.what()))));
        }

        const QString dateStr = info.dateTo.toString("yyyy-MM-dd");
        for (const QString &cur : std::as_const(tc.extraCurrencies)) {
            crm.importRate(dateStr, cur, "EUR", 1.0);
        }

        BookAccountsGroupedSalesTable gsa(dir);
        JournalEntryFactory factory(&crm, &ci, &sa, &gsa, &pa, &jt, nullptr, &amzSet, &balanceTable);
        auto entry = syncWait(factory.createEntry(info));

        QVERIFY2(!entry.isNull(),
                 qPrintable(QString("entry is null for '%1'").arg(tc.fname)));
        QVERIFY2(qAbs(entry->getDebitSum() - entry->getCreditSum()) < 0.01,
                 qPrintable(QString("unbalanced entry for '%1': debit=%2 credit=%3")
                                .arg(tc.fname)
                                .arg(entry->getDebitSum(),  0, 'f', 2)
                                .arg(entry->getCreditSum(), 0, 'f', 2)));
    }
}

// ── createEntryOssIoss helpers ────────────────────────────────────────────────

// Build a minimal GroupedShipmentData for OSS/IOSS tests.
static JournalEntryFactory::GroupedShipmentData makeGroup(
    TaxScheme scheme,
    const QString &countryFrom,
    const QString &countryTo,
    double vatRatePct,
    double totalRevenue,
    double totalVat,
    const QString &currency = "EUR",
    const QString &sampleId  = "TEST-001")
{
    JournalEntryFactory::GroupedShipmentData g;
    g.taxScheme    = scheme;
    g.countryFrom  = countryFrom;
    g.countryTo    = countryTo;
    g.vatRatePct   = vatRatePct;
    g.currency     = currency;
    g.totalRevenue = totalRevenue;
    g.totalVat     = totalVat;
    g.sampleEventId = sampleId;
    return g;
}

// Build a JournalEntryFactory backed by a temporary directory.
// saleAccounts is auto-filled with defaults (covers all EU OSS/IOSS rates).
struct OssTestFixture {
    QTemporaryDir        tempDir;
    CompanyInfosTable    companyInfos;
    CurrencyRateManager  crm;
    BooksAccountsSalesTable saleAccounts;
    BookAccountsGroupedSalesTable groupedSaleAccounts;
    BookAccountPurchaseTable purchaseAccounts;
    JournalTable         journalTable;
    BookAccountSelfVatTable selfVatAccounts;
    AmzPaymentSettings   amzPaymentSettings;
    BookAccountAmzBalanceTable amzBalanceTable;
    JournalEntryFactory  factory;

    explicit OssTestFixture(const QString &companyCurrency = "EUR",
                            const QString &companyCountry  = "FR")
        : companyInfos([&]() -> QDir {
              // Write company.csv before constructing the table
              QDir d(tempDir.path());
              QFile f(d.filePath("company.csv"));
              f.open(QIODevice::WriteOnly | QIODevice::Text);
              QTextStream s(&f);
              s << "Id;Parameter;Value\n";
              s << "Currency;Currency;" << companyCurrency << "\n";
              s << "Country;Country Code;" << companyCountry << "\n";
              return d;
          }())
        , crm(QDir(tempDir.path()), "")
        , saleAccounts(QDir(tempDir.path()))
        , groupedSaleAccounts(QDir(tempDir.path()))
        , purchaseAccounts(QDir(tempDir.path()), companyCountry)
        , journalTable(QDir(tempDir.path()))
        , selfVatAccounts(QDir(tempDir.path()), companyCountry)
        , amzPaymentSettings(QDir(tempDir.path()))
        , amzBalanceTable(QDir(tempDir.path()))
        , factory(&crm, &companyInfos, &saleAccounts, &groupedSaleAccounts,
                  &purchaseAccounts, &journalTable, &selfVatAccounts,
                  &amzPaymentSettings, &amzBalanceTable)
    {}
};

// ── Tests ──────────────────────────────────────────────────────────────────────

void TestBookEntries::test_oss_ioss_empty_groups_returns_empty()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    const QList<JournalEntryFactory::GroupedShipmentData> noGroups;
    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(noGroups, QDate(2025, 3, 31), QDate(2025, 3, 1)));
    QVERIFY(entries.isEmpty());
}

void TestBookEntries::test_oss_ioss_skips_non_oss_schemes()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // Domestic + OutOfScope groups — neither should produce an entry
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::DomesticVat,  "FR", "FR", 20.0, 100.0, 20.0));
    groups.append(makeGroup(TaxScheme::OutOfScope,   "US", "US",  0.0, 100.0,  0.0));
    groups.append(makeGroup(TaxScheme::Exempt,       "FR", "CH",  0.0, 100.0,  0.0));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2025, 3, 31), QDate(2025, 3, 1)));
    QVERIFY(entries.isEmpty());
}

void TestBookEntries::test_oss_ioss_skips_zero_vat_groups()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // OSS group with totalVat == 0 must be silently skipped
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuOssUnion, "FR", "SE", 25.0, 100.0, 0.0));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2025, 3, 31), QDate(2025, 3, 1)));
    QVERIFY(entries.isEmpty());
}

void TestBookEntries::test_oss_ioss_oss_single_destination_balance_and_structure()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // Two OSS groups from different origins, both going to Sweden (SE) at 25 %.
    // Expected: 1 entry, 2 debit lines, 1 credit line, balanced.
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuOssUnion, "FR", "SE", 25.0,  8.0, 2.0, "EUR", "ID-FR-SE"));
    groups.append(makeGroup(TaxScheme::EuOssUnion, "IT", "SE", 25.0, 12.0, 3.0, "EUR", "ID-IT-SE"));

    const QDate entryDate(2025, 3, 31);
    const QDate period(2025, 3, 1);

    const auto entries = syncWait(fx.factory.createEntryOssIoss(groups, entryDate, period));

    QCOMPARE(entries.size(), 1);
    const auto &e = entries.first();
    QVERIFY(!e.isNull());

    QCOMPARE(e->getDebits().size(), 2);
    QCOMPARE(e->getCredits().size(), 1);

    // All debit lines use the OSS-SE vatAccount
    for (const auto &line : e->getDebits()) {
        QVERIFY2(line.account.contains("4457OSS") && line.account.contains("SE"),
                 qPrintable("Unexpected debit account: " + line.account));
    }

    // Credit line uses the OSS-SE vatAccountToPay
    const auto credits = e->getCredits();
    const auto &creditLine = credits.first();
    QVERIFY2(creditLine.account.contains("4457OSS") && creditLine.account.contains("SE")
             && creditLine.account.contains("_PAY"),
             qPrintable("Unexpected credit account: " + creditLine.account));

    // Entry must balance
    QCOMPARE(e->getDebitSum(), e->getCreditSum());
    QCOMPARE(e->getDebitSum(), 5.0);

    // Entry date forwarded correctly
    QCOMPARE(e->getDate(), entryDate);
}

void TestBookEntries::test_oss_ioss_oss_two_destinations_two_entries()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // One group to SE, one group to SI — must produce two separate entries.
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuOssUnion, "IT", "SE", 25.0, 8.0, 2.0));
    groups.append(makeGroup(TaxScheme::EuOssUnion, "DE", "SI", 22.0, 9.0, 2.0));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2025, 3, 31), QDate(2025, 3, 1)));

    QCOMPARE(entries.size(), 2);
    for (const auto &e : entries) {
        QVERIFY(!e.isNull());
        // Each entry must be balanced
        QCOMPARE(e->getDebitSum(), e->getCreditSum());
        // Each entry has exactly 1 debit and 1 credit line (one origin per country)
        QCOMPARE(e->getDebits().size(), 1);
        QCOMPARE(e->getCredits().size(), 1);
    }

    // Collect debit account names to check both SE and SI are covered
    QStringList debitAccounts;
    for (const auto &e : entries) {
        const auto debits = e->getDebits();
        debitAccounts.append(debits.first().account);
    }
    const bool hasSweden   = std::any_of(debitAccounts.cbegin(), debitAccounts.cend(),
                                         [](const QString &a){ return a.contains("SE"); });
    const bool hasSlovenia = std::any_of(debitAccounts.cbegin(), debitAccounts.cend(),
                                         [](const QString &a){ return a.contains("SI"); });
    QVERIFY(hasSweden);
    QVERIFY(hasSlovenia);
}

void TestBookEntries::test_oss_ioss_oss_label_format()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // Single OSS group DE → SE so we can verify the exact label content.
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuOssUnion, "DE", "SE", 25.0, 10.33, 2.58, "EUR", "LBL-001"));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2025, 3, 31), QDate(2025, 3, 1)));

    QCOMPARE(entries.size(), 1);
    const auto &e = entries.first();

    // Debit label: "TVA OSS 03/2025 03/2025 Allemagne => Suède 10.33 EUR 25.00%"
    const auto ossDebits = e->getDebits();
    const QString &debitTitle = ossDebits.first().title;
    QVERIFY2(debitTitle.startsWith("TVA OSS "),
             qPrintable("Bad prefix: " + debitTitle));
    QVERIFY2(debitTitle.contains("03/2025 03/2025"),
             qPrintable("Period missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("Allemagne"),
             qPrintable("countryFrom missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("Suède"),
             qPrintable("countryTo missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("10.33"),
             qPrintable("revenue missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("EUR"),
             qPrintable("currency missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("25.00%"),
             qPrintable("rate missing: " + debitTitle));

    // Credit label: "TVA OSS 03/2025 Suède"
    const auto ossCredits = e->getCredits();
    const QString &creditTitle = ossCredits.first().title;
    QVERIFY2(creditTitle == QString("TVA OSS 03/2025 Suède"),
             qPrintable("Bad credit label: " + creditTitle));
}

void TestBookEntries::test_oss_ioss_ioss_single_group()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // One IOSS group: CN → FR at 20 %.
    // Expected: 1 entry with 1 debit + 1 credit, balanced.
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuIoss, "CN", "FR", 20.0, 15.27, 3.05, "EUR", "IOSS-001"));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2024, 3, 31), QDate(2024, 3, 1)));

    QCOMPARE(entries.size(), 1);
    const auto &e = entries.first();
    QVERIFY(!e.isNull());

    QCOMPARE(e->getDebits().size(), 1);
    QCOMPARE(e->getCredits().size(), 1);
    QCOMPARE(e->getDebitSum(), e->getCreditSum());
    QCOMPARE(e->getDebitSum(), 3.05);

    // Debit uses the IOSS-FR vatAccount
    const auto iossDebits = e->getDebits();
    const auto &debitLine = iossDebits.first();
    QVERIFY2(debitLine.account.contains("4457IOSS") && debitLine.account.contains("FR"),
             qPrintable("Unexpected debit account: " + debitLine.account));

    // Credit uses the IOSS-FR vatAccountToPay
    const auto iossCredits = e->getCredits();
    const auto &creditLine = iossCredits.first();
    QVERIFY2(creditLine.account.contains("4457IOSS") && creditLine.account.contains("FR")
             && creditLine.account.contains("_PAY"),
             qPrintable("Unexpected credit account: " + creditLine.account));
}

void TestBookEntries::test_oss_ioss_ioss_two_groups()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // Two IOSS groups (CN→FR 20 % and CN→NL 21 %) → two independent entries.
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuIoss, "CN", "FR", 20.0, 15.27, 3.05, "EUR", "IOSS-FR"));
    groups.append(makeGroup(TaxScheme::EuIoss, "CN", "NL", 21.0, 30.37, 6.38, "EUR", "IOSS-NL"));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2024, 3, 31), QDate(2024, 3, 1)));

    QCOMPARE(entries.size(), 2);

    double sumDebits  = 0.0;
    double sumCredits = 0.0;
    for (const auto &e : entries) {
        QVERIFY(!e.isNull());
        // Each entry has exactly 1 debit + 1 credit and is balanced
        QCOMPARE(e->getDebits().size(),  1);
        QCOMPARE(e->getCredits().size(), 1);
        QCOMPARE(e->getDebitSum(), e->getCreditSum());
        sumDebits  += e->getDebitSum();
        sumCredits += e->getCreditSum();
    }

    // Total VAT: 3.05 + 6.38 = 9.43
    QCOMPARE(sumDebits,  9.43);
    QCOMPARE(sumCredits, 9.43);
}

void TestBookEntries::test_oss_ioss_ioss_label_format()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuIoss, "CN", "FR", 20.0, 15.27, 3.05, "EUR", "LBL-IOSS"));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2024, 3, 31), QDate(2024, 3, 1)));

    QCOMPARE(entries.size(), 1);
    const auto &e = entries.first();

    // Debit label: "TVA IOSS 03/2024 03/2024 Chine => France 15.27 EUR 20.00%"
    const auto iossLblDebits = e->getDebits();
    const QString &debitTitle = iossLblDebits.first().title;
    QVERIFY2(debitTitle.startsWith("TVA IOSS "),
             qPrintable("Bad prefix: " + debitTitle));
    QVERIFY2(debitTitle.contains("03/2024 03/2024"),
             qPrintable("Period missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("Chine"),
             qPrintable("countryFrom missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("France"),
             qPrintable("countryTo missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("15.27"),
             qPrintable("revenue missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("EUR"),
             qPrintable("currency missing: " + debitTitle));
    QVERIFY2(debitTitle.contains("20.00%"),
             qPrintable("rate missing: " + debitTitle));

    // Credit label: "TVA IOSS 03/2024" (no country name)
    const auto iossLblCredits = e->getCredits();
    const QString &creditTitle = iossLblCredits.first().title;
    QVERIFY2(creditTitle == QString("TVA IOSS 03/2024"),
             qPrintable("Bad IOSS credit label: " + creditTitle));
}

void TestBookEntries::test_oss_ioss_mixed_oss_and_ioss()
{
    OssTestFixture fx;
    QVERIFY(fx.tempDir.isValid());

    // Mix: 1 OSS group (IT→SE) + 1 IOSS group (CN→FR)
    // Expected: 2 entries total (1 OSS + 1 IOSS), each balanced.
    QList<JournalEntryFactory::GroupedShipmentData> groups;
    groups.append(makeGroup(TaxScheme::EuOssUnion, "IT", "SE", 25.0, 8.0,  2.0, "EUR", "MIX-OSS"));
    groups.append(makeGroup(TaxScheme::EuIoss,     "CN", "FR", 20.0, 15.0, 3.0, "EUR", "MIX-IOSS"));

    // Also add a domestic group that must be ignored
    groups.append(makeGroup(TaxScheme::DomesticVat, "FR", "FR", 20.0, 100.0, 20.0));

    const auto entries = syncWait(
        fx.factory.createEntryOssIoss(groups, QDate(2025, 3, 31), QDate(2025, 3, 1)));

    QCOMPARE(entries.size(), 2);

    bool foundOss  = false;
    bool foundIoss = false;
    for (const auto &e : entries) {
        QVERIFY(!e.isNull());
        QCOMPARE(e->getDebitSum(), e->getCreditSum());

        const auto mixedDebits = e->getDebits();
        const QString &title = mixedDebits.first().title;
        if (title.contains("TVA OSS ")) {
            foundOss = true;
        }
        if (title.contains("TVA IOSS ")) {
            foundIoss = true;
        }
    }
    QVERIFY(foundOss);
    QVERIFY(foundIoss);
}

QTEST_MAIN(TestBookEntries)
#include "test_book_entries.moc"

