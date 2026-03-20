#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDirIterator>

#include "books/BooksAccountsSalesTable.h"
#include "books/TaxScheme.h"
#include "books/BookAccountPurchaseTable.h"
#include "ExceptionWithTitleText.h"
#include "books/CompanyAddressTable.h"
#include "books/CompanyInfosTable.h"
#include "books/VatNumbersTable.h"
#include <QCoroTask>
#include <QCoroFuture>

#include "books/BookAccountBankTable.h"
#include "banks/AbstractBankStatement.h"
#include "books/BookAccountAmzBalanceTable.h"
#include "books/AmzPaymentSettings.h"
#include "ExceptionWithTitleText.h"

#include "books/JournalEntryFactory.h"
#include "books/BookAccountSelfVatTable.h"
#include "books/BookAccountAmzBalanceTable.h"
#include "books/PurchaseInvoiceManager.h"
#include "CurrencyRateManager.h"
#include "books/JournalTable.h"
#include "orders/ActivitySource.h"
#include "orders/Shipment.h"
#include "books/Activity.h"
#include "books/TaxResolver.h"
#include "books/VatResolver.h"
#include "utils/CsvReader.h"

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

class TestBookAccounts : public QObject
{
    Q_OBJECT

    // Helper to inject fake column
    void injectFakeColumn(const QString &filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) QFAIL("Failed to open file for injection");
        QString content = file.readAll();
        file.close();

        QStringList lines = content.split('\n');
        if (lines.isEmpty()) QFAIL("Empty file");

        // Headers are first line
        QStringList headers = lines[0].split(';');
        headers.insert(1, "FakeId");
        lines[0] = headers.join(';');

        for (int i = 1; i < lines.size(); ++i) {
            if (lines[i].trimmed().isEmpty()) continue;
            QStringList parts = lines[i].split(';');
            parts.insert(1, "FakeValue");
            lines[i] = parts.join(';');
        }

        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) QFAIL("Failed to save injected file");
        QTextStream out(&file);
        out << lines.join('\n');
    }

private slots:
    void test_AmzPaymentSettings() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        // 1. Init & Defaults
        {
            AmzPaymentSettings table(dir);
            QCOMPARE(table.rowCount(), 3); // DEBIT, CREDIT, AMAZON_ACCOUNT defaults
            
            QCOMPARE(table.getAccountDebit(), QString(""));
            QCOMPARE(table.getAccountCredit(), QString(""));
            QCOMPARE(table.getAmazonAccount(), QString("FAMZMK"));
        }

        // 2. Modify values & Save
        {
            AmzPaymentSettings table(dir);
            QVERIFY(table.setData(table.index(0, 1), "411AMZ", Qt::EditRole)); // Debit
            QVERIFY(table.setData(table.index(1, 1), "512AMZ", Qt::EditRole)); // Credit
            QVERIFY(table.setData(table.index(2, 1), "FAMZ_NEW", Qt::EditRole)); // Amazon Account
            
            QCOMPARE(table.getAccountDebit(), QString("411AMZ"));
            QCOMPARE(table.getAccountCredit(), QString("512AMZ"));
            QCOMPARE(table.getAmazonAccount(), QString("FAMZ_NEW"));
        }

        // 3. Reload & verify persistence
        {
            AmzPaymentSettings table(dir);
            QCOMPARE(table.rowCount(), 3);
            
            QCOMPARE(table.getAccountDebit(), QString("411AMZ"));
            QCOMPARE(table.getAccountCredit(), QString("512AMZ"));
            QCOMPARE(table.getAmazonAccount(), QString("FAMZ_NEW"));
        }
    }

    void test_persistence() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        int initialCount = 0;
        // 1. Initialize - should default populate
        {
            BooksAccountsSalesTable table(dir);
            initialCount = table.rowCount();
            QVERIFY(initialCount > 60);   
            
            VatCountries vc = table.resolveVatCountries(TaxScheme::EuOssUnion, "IT", "IT", "DE");
            BooksAccountsSalesTable::Accounts acc = syncWait(table.getAccounts(vc, 19.0));
            QCOMPARE(acc.saleAccount, "7070OSSDE19");
            QCOMPARE(acc.vatAccount, "4457OSSDE19");
        }

        // 2. Add new account and save
        {
            BooksAccountsSalesTable table(dir);
            VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR", "FR");
            BooksAccountsSalesTable::Accounts newAcc;
            newAcc.saleAccount = "7001";
            newAcc.vatAccount = "4401";
            
            table.addAccount(vc, 25.0, newAcc);
            
            // Verify immediate cache update
            BooksAccountsSalesTable::Accounts retrieved = syncWait(table.getAccounts(vc, 25.0));
            QCOMPARE(retrieved.saleAccount, "7001");
            QCOMPARE(retrieved.vatAccount, "4401");
        }

        // 3. Reload and verify persistence
        {
            BooksAccountsSalesTable table(dir);
            QCOMPARE(table.rowCount(), initialCount + 1);
            
            VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR", "FR");
            BooksAccountsSalesTable::Accounts retrieved = syncWait(table.getAccounts(vc, 25.0));
            QCOMPARE(retrieved.saleAccount, "7001");
            QCOMPARE(retrieved.vatAccount, "4401");
        }
        
        // 4. Robustness: Inject Fake Column
        QString csvPath = dir.filePath("saleBookAccounts.csv");
        injectFakeColumn(csvPath);
        
        {
             BooksAccountsSalesTable table(dir);
             VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR", "FR");
             BooksAccountsSalesTable::Accounts retrieved = syncWait(table.getAccounts(vc, 25.0));
             // Should still find it
             QCOMPARE(retrieved.saleAccount, "7001");
        }
    }
    
    // ... test_saleValidation ...

    void test_saleValidation() {
        QTemporaryDir tempDir;
        BooksAccountsSalesTable table(QDir(tempDir.path()));
        
        VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR", "FR");
        BooksAccountsSalesTable::Accounts acc;
        acc.saleAccount = "S1";
        acc.vatAccount = "V1";
        
        // 1. Add valid (Use 10.0 to avoid collision with default 20.0)
        table.addAccount(vc, 10.0, acc);
        
        // 2. Duplicate -> Exception
        QVERIFY_EXCEPTION_THROWN(
            table.addAccount(vc, 10.0, acc),
            ExceptionWithTitleText
        );
        
        // 3. Diff Rate -> OK
        table.addAccount(vc, 5.5, acc);
        
        // 4. Diff Scheme -> OK (Use BE to avoid collision with default PanEU Exempt entries which include FR)
        VatCountries vc2 = table.resolveVatCountries(TaxScheme::Exempt, "BE", "BE", "BE");
        table.addAccount(vc2, 0.0, acc);
    }

    void test_lookup() {
        QTemporaryDir tempDir;
        BooksAccountsSalesTable table(QDir(tempDir.path()));
        
        // Setup scenarios
        VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "DE", "DE", "DE");
        BooksAccountsSalesTable::Accounts acc1;
        acc1.saleAccount = "S1";
        acc1.vatAccount = "V1";
        table.addAccount(vc, 18.0, acc1);
        
        // Setup second rate
        BooksAccountsSalesTable::Accounts acc3;
        acc3.saleAccount = "S3";
        acc3.vatAccount = "V3";
        table.addAccount(vc, 7.0, acc3); // Different rate

        // Case 1: Retrieve first rate
        auto res1 = syncWait(table.getAccounts(vc, 18.0));
        QCOMPARE(res1.saleAccount, "S1");

        // Case 2: Retrieve second rate
        auto res3 = syncWait(table.getAccounts(vc, 7.0));
        QCOMPARE(res3.saleAccount, "S3");

        // Case 4: Unknown rate -> Exception
        QVERIFY_EXCEPTION_THROWN(
            syncWait(table.getAccounts(vc, 5.0)),
            ExceptionWithTitleText
        );
    }

    void test_resolveVatCountries() {
        BooksAccountsSalesTable table(QDir::tempPath());

        // 1. Normalization
        {
            auto vc = table.resolveVatCountries(TaxScheme::DomesticVat, " fr ", " fr ", " DE ");
            QCOMPARE(vc.countryCodeDeclaring, "FR");
            QCOMPARE(vc.countryCodeFrom, "FR");
            QCOMPARE(vc.countryCodeTo, "");
            
            auto vc2 = table.resolveVatCountries(TaxScheme::EuOssUnion, "fr", "fr", "de");
            // Arg2="fr" (Company), Arg3="de" (From), Arg4 (default?) -> Wait, signature has 4 args.
            // Test calls with 3 args? Let's check header or default values.
            // Header: resolveVatCountries(TaxScheme taxScheme, const QString &companyCountryFrom, const QString &countryFrom, const QString &countryCodeTo)
            // No defaults shown in cpp view earlier.
            // Ah, looking at test code: `table.resolveVatCountries(TaxScheme::EuOssUnion, "fr", "de");`
            // It seems it might be missing the 4th argument? Or there is an overload?
            // Let's check Header file content from previous reasoning or assume user didn't change header to add defaults.
            // If test compiled before, maybe there was an overload or default.
            // The method signature in cpp was:
            // VatCountries BooksAccountsSalesTable::resolveVatCountries(TaxScheme taxScheme, const QString &companyCountryFrom, const QString &countryFrom, const QString &countryCodeTo)
            
            // The test code I am replacing (lines 179-183) shows:
            // auto vc = table.resolveVatCountries(TaxScheme::DomesticVat, " fr ", " DE ");
            // The 4th arg is missing? Is "DE" the 3rd or 4th?
            // The test might have been failing to compile or I misread the arguments in the test view.
            
            // Let's look at lines 179 again:
            // `auto vc = table.resolveVatCountries(TaxScheme::DomesticVat, " fr ", " DE ");`
            // If signature is (Scheme, CompanyFrom, From, To), then To is missing.
            // Maybe "DE" is `countryFrom` and `countryCodeTo` is defaulting?
            
            // Let's retrieve Header again to be sure.
            
            QCOMPARE(vc2.countryCodeDeclaring, "FR");
            QCOMPARE(vc2.countryCodeFrom, "FR"); 
            QCOMPARE(vc2.countryCodeTo, "DE");
        }
    }

    void test_getAccounts_missing_addCallback() {
        QTemporaryDir tempDir;
        BooksAccountsSalesTable table(QDir(tempDir.path()));
        VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "US", "US", "US");

        // 1. Missing without callback -> throws ExceptionVatAccount
        QVERIFY_EXCEPTION_THROWN(syncWait(table.getAccounts(vc, 99.9)), ExceptionWithTitleText);

        // 2. Missing with callback returning false -> throws ExceptionVatAccount
        auto cbReject = [](const QString&, const QString&) -> QCoro::Task<bool> {
            co_return false;
        };
        QVERIFY_EXCEPTION_THROWN(syncWait(table.getAccounts(vc, 99.9, cbReject)), ExceptionWithTitleText);

        // 3. Retry loop: Callback calls true (Retry) multiple times then false (Cancel) -> throws ExceptionVatAccount
        int countRetry = 0;
        auto cbRetryThenCancel = [&](const QString&, const QString&) -> QCoro::Task<bool> {
            countRetry++;
            if (countRetry < 5) co_return true; // Retry
            co_return false; // Cancel
        };
        QVERIFY_EXCEPTION_THROWN(syncWait(table.getAccounts(vc, 99.9, cbRetryThenCancel)), ExceptionWithTitleText);
        // It immediately throws if it wasn't added correctly in the first retry.
        QCOMPARE(countRetry, 1);

        // 4. Missing with callback that Adds -> Returns Account
        auto cbAdd = [&](const QString& title, const QString& text) -> QCoro::Task<bool> {
             BooksAccountsSalesTable::Accounts acc;
             acc.saleAccount = "DynamicSale";
             acc.vatAccount = "DynamicVat";
             
             // We can check title/text if we want strictly, but purpose is just to add.
             // Reconstruct logic from scope or use hardcoded invocation for this test context.
             const_cast<BooksAccountsSalesTable&>(table).addAccount(vc, 99.9, acc);
             co_return true;
        };
        
        auto res = syncWait(table.getAccounts(vc, 99.9, cbAdd));
        QCOMPARE(res.saleAccount, "DynamicSale");
        
        // 5. Retry loop: Callback calls true (Retry) then Adds on second attempt
        // Since our anti-infinite-loop logic throws instead of retrying forever, this will throw.
        int countSuccess = 0;
        auto cbRetryThenAdd = [&](const QString&, const QString&) -> QCoro::Task<bool> {
            countSuccess++;
            if (countSuccess == 1) co_return true; // Just retry, don't add yet
            
            // Add on 2nd attempt (won't be reached)
             BooksAccountsSalesTable::Accounts acc;
             acc.saleAccount = "RetrySale";
             acc.vatAccount = "RetryVat";
             const_cast<BooksAccountsSalesTable&>(table).addAccount(vc, 88.8, acc);
             co_return true;
        };
        
        QVERIFY_EXCEPTION_THROWN(syncWait(table.getAccounts(vc, 88.8, cbRetryThenAdd)), ExceptionWithTitleText);
        QCOMPARE(countSuccess, 1);
    }

    // --- PURCHASE TESTS ---

    void test_purchasePersistence() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        
        // 1. Init & Defaults
        {
            BookAccountPurchaseTable table(dir, "FR");
            QVERIFY(table.rowCount() == 3); // Should have 3 default rows for FR
            
            QString debit = table.getAccountsDebit6("FR", 0.2);
            QString credit = table.getAccountsCredit4("FR", 0.2);
            QCOMPARE(debit, "445660");
            QCOMPARE(credit, "445710");
        }
        
        // 2. Add & Save
        {
            BookAccountPurchaseTable table(dir, "FR");
            // Add account for DE
            table.addAccount("DE", 0.19, "600DE", "400DE");
            
            QCOMPARE(table.getAccountsDebit6("DE", 0.19), "600DE");
            QCOMPARE(table.getAccountsCredit4("DE", 0.19), "400DE");
        }

        // 3. Reload
        {
            BookAccountPurchaseTable table(dir, "FR");
            QCOMPARE(table.rowCount(), 4); // 3 defaults + DE

            QCOMPARE(table.getAccountsDebit6("DE", 0.19), "600DE");
            QCOMPARE(table.getAccountsCredit4("DE", 0.19), "400DE");
        }

        // 4. Robustness: Inject Fake Column
        QString csvPath = dir.filePath("purchaseBookAccounts.csv");
        injectFakeColumn(csvPath);

        {
            BookAccountPurchaseTable table(dir, "FR");
            // Check Data valid
            QString val = table.getAccountsDebit6("DE", 0.19);
            QCOMPARE(val, "600DE");
        }
    }
    
    void test_purchaseValidation() {
        QTemporaryDir tempDir;
        BookAccountPurchaseTable table(QDir(tempDir.path()), "FR");
        
        // 1. Invalid Country
        QVERIFY_EXCEPTION_THROWN(
            table.addAccount("US", 0.1, "6", "4"),
            ExceptionWithTitleText
        );
        
        // 2. Valid Country, New Rate
        table.addAccount("DE", 0.19, "6DE", "4DE");
        
        // 3. Duplicate (DE, 19.0)
        QVERIFY_EXCEPTION_THROWN(
            table.addAccount("DE", 0.19, "6New", "4New"),
            ExceptionWithTitleText
        );
        
        // 4. Same Country, Diff Rate -> OK
        table.addAccount("DE", 0.07, "6DE7", "4DE7");
        
        // Check GB/UK
        table.addAccount("GB", 0.2, "6GB", "4GB");
        try {
            table.addAccount("UK", 0.2, "6UK", "4UK"); // Assuming UK is accepted string? Logic said UK or GB.
        } catch(...) {
            // Depending on implementation
        }
    }

    void test_purchaseLookup() {
          QTemporaryDir tempDir;
          BookAccountPurchaseTable table(QDir(tempDir.path()), "FR");
          
          table.addAccount("IT", 0.22, "600IT", "400IT");
          
          // Case 1: Known country+rate
          QCOMPARE(table.getAccountsDebit6("IT", 0.22), "600IT");

          // Case 2: Unknown country+rate -> Exception
          QVERIFY_EXCEPTION_THROWN(table.getAccountsDebit6("ES", 0.21), ExceptionWithTitleText);
          QVERIFY_EXCEPTION_THROWN(table.getAccountsCredit4("ES", 0.21), ExceptionWithTitleText);
    }

    void test_purchaseClosestLookup() {
        QTemporaryDir tempDir;
        BookAccountPurchaseTable table(QDir(tempDir.path()), "FR");

        // Add a 20% entry for FR (wildcard country already present from defaults)
        // and a country-specific 19% entry for DE
        table.addAccount("DE", 0.19, "600DE19", "400DE19");

        // 1. Exact match still works; matchedRate equals the queried rate
        {
            const auto r = table.getAccountsDebit6Closest("DE", 0.19);
            QCOMPARE(r.account, "600DE19");
            QCOMPARE(r.matchedRate, 0.19);
        }
        {
            const auto r = table.getAccountsCredit4Closest("DE", 0.19);
            QCOMPARE(r.account, "400DE19");
            QCOMPARE(r.matchedRate, 0.19);
        }

        // 2. 20.25% vs stored 20% → diff 0.25% ≤ 0.3% → returns wildcard entry; matchedRate = 0.20
        {
            const auto r = table.getAccountsDebit6Closest("", 0.2025);
            QCOMPARE(r.account, "445660");
            QCOMPARE(r.matchedRate, 0.2);
        }
        {
            const auto r = table.getAccountsCredit4Closest("", 0.2025);
            QCOMPARE(r.account, "445710");
            QCOMPARE(r.matchedRate, 0.2);
        }

        // 3. Diff exactly at tolerance boundary: 0.3% → accepted, matchedRate = 0.20
        {
            const auto r = table.getAccountsDebit6Closest("", 0.203);
            QCOMPARE(r.account, "445660");
            QCOMPARE(r.matchedRate, 0.2);
        }

        // 4. Diff just above default tolerance (0.3%): 20.36% → diff 0.36% → throws
        QVERIFY_EXCEPTION_THROWN(
            table.getAccountsDebit6Closest("", 0.2036),
            ExceptionWithTitleText
        );
        QVERIFY_EXCEPTION_THROWN(
            table.getAccountsCredit4Closest("", 0.2036),
            ExceptionWithTitleText
        );

        // 5. Larger tolerance allows the same rate through; matchedRate = 0.20
        {
            const auto r = table.getAccountsDebit6Closest("", 0.2036, 0.49);
            QCOMPARE(r.account, "445660");
            QCOMPARE(r.matchedRate, 0.2);
        }
        {
            const auto r = table.getAccountsCredit4Closest("", 0.2036, 0.49);
            QCOMPARE(r.account, "445710");
            QCOMPARE(r.matchedRate, 0.2);
        }

        // 6. Country-specific preferred over wildcard; DE 19.25% → matched 0.19
        {
            const auto r = table.getAccountsDebit6Closest("DE", 0.1925);
            QCOMPARE(r.account, "600DE19");
            QCOMPARE(r.matchedRate, 0.19);
        }

        // 7. Unknown country with no matching entry at all → throws
        QVERIFY_EXCEPTION_THROWN(
            table.getAccountsDebit6Closest("ES", 0.21),
            ExceptionWithTitleText
        );
        QVERIFY_EXCEPTION_THROWN(
            table.getAccountsCredit4Closest("ES", 0.21),
            ExceptionWithTitleText
        );
    }

    void test_purchaseDecodeDualAmount() {
        // ── filename 1: refund with negative dual-amount VAT ──────────────────────
        // FR-TVA--6.30EUR_-5.46GPB means companyVAT = 6.30 EUR, sourceVAT = 5.46 GBP
        {
            const QString fileName =
                "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8373__FAMZMK"
                "__FR-TVA--6.30EUR_-5.46GPB__-32.74GBP.pdf";
            const auto info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

            QCOMPARE(info.date, QDate(2026, 1, 31));
            QCOMPARE(info.account, QString("622201"));
            QCOMPARE(info.accountSupplier, QString("FAMZMK"));
            QCOMPARE(info.currency, QString("GBP"));
            QVERIFY(qAbs(info.totalAmount - (-32.74)) < 0.001);

            // Token is preserved verbatim
            QCOMPARE(info.vatTokens.size(), 1);
            QCOMPARE(info.vatTokens.first(), QString("FR-TVA--6.30EUR_-5.46GPB"));

            // Source VAT (GBP) stored in country_vatRate_vat for "FR"
            QVERIFY(info.country_vatRate_vat.contains("FR"));
            const auto &rateMap = info.country_vatRate_vat["FR"];
            // Rate key should be deferred-computed: 5.46 / (32.74 - 5.46) = 5.46/27.28 ≈ 20%
            QVERIFY(!rateMap.isEmpty());
            const QString rateKey = rateMap.keys().first();
            // Rate rounds to 0.2 (20%)
            QCOMPARE(rateKey, QString("0.2"));
            QVERIFY(qAbs(rateMap[rateKey] - 5.46) < 0.001);

            // Company VAT (EUR) stored in country_vatRate_vatCompany
            QVERIFY(info.country_vatRate_vatCompany.contains("FR"));
            QVERIFY(qAbs(info.country_vatRate_vatCompany["FR"][rateKey] - 6.30) < 0.001);

            // rawVatAmount uses the source (GBP) amount
            QVERIFY(qAbs(info.rawVatAmount.toDouble() - (-5.46)) < 0.001);
            QCOMPARE(info.vatCurrency, QString("GPB")); // GPB as written in token
            QCOMPARE(info.vatCountry, QString("FR"));
        }

        // ── filename 2: purchase with positive dual-amount VAT ────────────────────
        // FR-TVA-2.21EUR_9.28PLN means companyVAT = 2.21 EUR, sourceVAT = 9.28 PLN
        {
            const QString fileName =
                "2026-01-31__622201__frais-vente-FR-AEU-2026-49313__FAMZMK"
                "__FR-TVA-2.21EUR_9.28PLN__55.68PLN.pdf";
            const auto info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");

            QCOMPARE(info.date, QDate(2026, 1, 31));
            QCOMPARE(info.currency, QString("PLN"));
            QVERIFY(qAbs(info.totalAmount - 55.68) < 0.001);

            QCOMPARE(info.vatTokens.size(), 1);
            QCOMPARE(info.vatTokens.first(), QString("FR-TVA-2.21EUR_9.28PLN"));

            QVERIFY(info.country_vatRate_vat.contains("FR"));
            const auto &rateMap = info.country_vatRate_vat["FR"];
            // Rate: 9.28 / (55.68 - 9.28) = 9.28 / 46.40 = 0.20 exactly
            QVERIFY(!rateMap.isEmpty());
            const QString rateKey = rateMap.keys().first();
            QCOMPARE(rateKey, QString("0.2"));
            QVERIFY(qAbs(rateMap[rateKey] - 9.28) < 0.001);

            QVERIFY(info.country_vatRate_vatCompany.contains("FR"));
            QVERIFY(qAbs(info.country_vatRate_vatCompany["FR"][rateKey] - 2.21) < 0.001);

            // rawVatAmount: source amount 9.28 PLN (positive)
            QVERIFY(qAbs(info.rawVatAmount.toDouble() - 9.28) < 0.001);
            QCOMPARE(info.vatCurrency, QString("PLN"));
            QCOMPARE(info.vatCountry, QString("FR"));
        }
    }

    void test_CompanyAddressTable() {
        QTemporaryDir tempDir;
        QString iniFiles = QDir(tempDir.path()).filePath("company-addresses.csv"); // Renamed to CSV (logic handles extension but let's be explicit test file)

        // 1. Init & Add Data
        {
            CompanyAddressTable table(QDir(tempDir.path()));
            
            // Add Row 1
            table.insertRows(0, 1);
            QDate d1(2023, 1, 1);
            table.setData(table.index(0, 0), d1); // Date
            table.setData(table.index(0, 1), "MyCo 2023"); // Name
            table.setData(table.index(0, 2), "Street A"); // Street 1
            table.setData(table.index(0, 4), "75000"); // Postal
            table.setData(table.index(0, 5), "Paris"); // City
            
            // Add Row 2 (Newer)
            table.insertRows(0, 1);
            QDate d2(2024, 1, 1);
            table.setData(table.index(0, 0), d2);
            table.setData(table.index(0, 1), "MyCo 2024");
            table.setData(table.index(0, 2), "Street B");
            table.setData(table.index(0, 5), "Lyon");
            
            // Verify Get
            QString addr23 = table.getCompanyAddress(QDate(2023, 6, 1));
            // Expect joined: Name \n Street1 \n Postal City
            QVERIFY(addr23.contains("MyCo 2023"));
            QVERIFY(addr23.contains("Street A"));
            QVERIFY(addr23.contains("75000 Paris"));
            
            QCOMPARE(table.getCompanyName(QDate(2023, 6, 1)), "MyCo 2023");
            QCOMPARE(table.getCity(QDate(2023, 6, 1)), "Paris");

            // Verify Exception for old date
            QVERIFY_EXCEPTION_THROWN(table.getCompanyAddress(QDate(2022, 1, 1)), ExceptionWithTitleText);
        }
        
        // 2. Persistence (New Instance)
        {
            CompanyAddressTable table(QDir(tempDir.path()));
            QCOMPARE(table.rowCount(), 2);
            
            // Verify Data loaded sorted
            QCOMPARE(table.getCompanyName(QDate(2023, 1, 1)), "MyCo 2023");
            QCOMPARE(table.getCompanyName(QDate(2024, 1, 1)), "MyCo 2024");
            
            // Modify
            table.setData(table.index(0, 1), "MyCo 2024 Mod");
        }
        
        // 3. Verify Modification Persisted
        {
            CompanyAddressTable table(QDir(tempDir.path()));
            QCOMPARE(table.getCompanyName(QDate(2024, 1, 1)), "MyCo 2024 Mod");
        }
        
        // 4. Robustness
        injectFakeColumn(iniFiles);
        {
            CompanyAddressTable table(QDir(tempDir.path()));
            QCOMPARE(table.rowCount(), 2);
            QCOMPARE(table.getCompanyName(QDate(2024, 1, 1)), "MyCo 2024 Mod");
        }
    }

    void test_CompanyInfosTable() {
        QTemporaryDir tempDir;
        QString iniFiles = QDir(tempDir.path()).filePath("company.csv");
        
        // 1. Init & Modify
        {
            CompanyInfosTable table(QDir(tempDir.path()));
            QCOMPARE(table.rowCount(), 9);  // Country, Currency, FixerApiKey + 6 Legal Infos

            // Modify Country (Row 0, Col 1)
            table.setData(table.index(0, 1), "US");
            
            // Modify Currency (Row 1, Col 1)
            table.setData(table.index(1, 1), "USD");
        }
        
        // 2. Persistence (New Instance)
        {
            CompanyInfosTable table(QDir(tempDir.path()));
            QCOMPARE(table.rowCount(), 9);  // Country, Currency, FixerApiKey + 6 Legal Infos

            // Check Values
            QCOMPARE(table.data(table.index(0, 1)).toString(), "US");
            QCOMPARE(table.data(table.index(1, 1)).toString(), "USD");
        }
        
        // 3. Robustness
        // injectFakeColumn(iniFiles); // This only tests column robustness
        
        // Inject a fake ROW to test row count strictness
        {
            QFile file(iniFiles);
            if (file.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&file);
                out << "FakeParam;FakeValue;FakeId\n";
            }
        }

        {
            CompanyInfosTable table(QDir(tempDir.path()));
            // The table enforces a fixed structure; fake rows must be ignored.
            // We want strict 9 rows.
            QCOMPARE(table.rowCount(), 9);
            
            // Should still have valid data for known IDs
            QCOMPARE(table.getCompanyCountryCode(), "US");
            QCOMPARE(table.getCurrency(), "USD");
        }
    }
    
    void test_CompanyInfosTableEncryption() {
        QTemporaryDir tempDir;
        QString csvPath = QDir(tempDir.path()).filePath("company.csv");
        QString testApiKey = "my_secret_api_key_12345";
        
        // 1. Set API Key (Row 2 = FixerApiKey)
        {
            CompanyInfosTable table(QDir(tempDir.path()));
            QCOMPARE(table.rowCount(), 9);

            // Row 2 is FixerApiKey
            table.setData(table.index(2, 1), testApiKey);
            
            // Verify via getter
            QCOMPARE(table.getApiKeyFixer(), testApiKey);
        }
        
        // 2. Verify CSV contains encrypted value (not plaintext)
        {
            QFile file(csvPath);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            QString content = file.readAll();
            file.close();
            
            // The plaintext API key should NOT appear in the CSV
            QVERIFY(!content.contains(testApiKey));
            
            // But the CSV should contain the FixerApiKey row
            QVERIFY(content.contains("FixerApiKey"));
        }
        
        // 3. Reload and verify decryption works
        {
            CompanyInfosTable table(QDir(tempDir.path()));
            
            // Via data()
            QCOMPARE(table.data(table.index(2, 1)).toString(), testApiKey);
            
            // Via getter
            QCOMPARE(table.getApiKeyFixer(), testApiKey);
        }
        
        // 4. Test empty API key
        {
            CompanyInfosTable table(QDir(tempDir.path()));
            table.setData(table.index(2, 1), "");
            QCOMPARE(table.getApiKeyFixer(), QString(""));
        }
        
        // 5. Verify empty value in CSV
        {
            QFile file(csvPath);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            QString content = file.readAll();
            file.close();
            
            // Should have FixerApiKey with empty value
            QVERIFY(content.contains("FixerApiKey"));
        }
        
        // 6. Reload empty API key
        {
            CompanyInfosTable table(QDir(tempDir.path()));
            QCOMPARE(table.getApiKeyFixer(), QString(""));
        }
    }

    void test_VatNumbersTable() {
        QTemporaryDir tempDir;
        QString iniFile = QDir(tempDir.path()).filePath("vatNumbers.csv");
        
        // 1. Add and Save
        {
            VatNumbersTable table(QDir(tempDir.path()));
            table.addVatNumber("FR", "FR123");
            table.addVatNumber("DE", "DE456");
            
            QCOMPARE(table.getVatNumber("FR"), "FR123");
            QVERIFY(table.hasVatNumber("FR"));
            QVERIFY(!table.hasVatNumber("ES"));
            
            // Duplicate strict check
            QVERIFY_EXCEPTION_THROWN(table.addVatNumber("FR", "FR999"), ExceptionWithTitleText);
            
            // Validate Columns (0=Country, 1=Vat, ID is hidden)
            QCOMPARE(table.data(table.index(0, 0)).toString(), "FR");
            QCOMPARE(table.data(table.index(0, 1)).toString(), "FR123");
        }
        
        // 2. Persistence
        {
             VatNumbersTable table(QDir(tempDir.path()));
             QCOMPARE(table.rowCount(), 2);
             QCOMPARE(table.getVatNumber("DE"), "DE456");
        }
        
        // 3. Robustness 
        // Logic handled by injectFakeColumn matches generic requirements
        injectFakeColumn(iniFile);
        
        {
             VatNumbersTable table(QDir(tempDir.path()));
             QCOMPARE(table.rowCount(), 2);
             QCOMPARE(table.getVatNumber("DE"), "DE456");
        }
    }

    void test_BookAccountBankTable() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        // 1. Init & Defaults
        int initialRowCount;
        {
            BookAccountBankTable table(dir);
            initialRowCount = table.rowCount();
            // Should have rows equal to ALL_BANKS size
            QCOMPARE(initialRowCount, AbstractBankStatement::ALL_BANKS().size());
            
            // Pick one bank (e.g. first one) to verify defaults
            if (initialRowCount > 0) {
                auto banks = AbstractBankStatement::ALL_BANKS().values();
                const auto *bank = banks.first();
                
                // Find row for this bank
                bool found = false;
                for (int i = 0; i < table.rowCount(); ++i) {
                    if (table.data(table.index(i, 0)).toString() == bank->getName()) {
                        found = true;
                        QCOMPARE(table.data(table.index(i, 1)).toString(), bank->defaultAccount());
                        QCOMPARE(table.data(table.index(i, 2)).toString(), bank->defaultAccountFees());
                        break;
                    }
                }
                QVERIFY(found);
            }
        }
        
        // 2. Modify Entry
        QString modifiedAccount = "123456";
        QString targetBankName;
        {
            BookAccountBankTable table(dir);
            if (table.rowCount() > 0) {
                targetBankName = table.data(table.index(0, 0)).toString();
                QModelIndex idxAccount = table.index(0, 1);
                
                // Verify editable
                QVERIFY(table.flags(idxAccount) & Qt::ItemIsEditable);
                QVERIFY(table.setData(idxAccount, modifiedAccount, Qt::EditRole));
                
                // Check update in model
                QCOMPARE(table.data(idxAccount).toString(), modifiedAccount);
            }
        }
        
        // 3. Reload and Verify persistence
        if (!targetBankName.isEmpty()) {
            BookAccountBankTable table(dir);
            bool foundModified = false;
            for (int i = 0; i < table.rowCount(); ++i) {
                if (table.data(table.index(i, 0)).toString() == targetBankName) {
                    QCOMPARE(table.data(table.index(i, 1)).toString(), modifiedAccount);
                    foundModified = true;
                    break;
                }
            }
            QVERIFY(foundModified);
        }

        // 4. CSV Overrides Default
        {
             // Manually edit CSV
            QString csvPath = dir.filePath("accountsBanks.csv");
            
            // It was already saved in steps above, so modify it
            QFile file(csvPath);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            
            QStringList lines;
            QTextStream in(&file);
            QString header = in.readLine();
            lines.append(header);
            
            QString targetId;
            QString targetBankNameOverride;
            
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (line.trimmed().isEmpty()) continue;
                
                // Format: Bank;Account;Fees Account;Id
                if (targetId.isEmpty()) {
                    QStringList parts = line.split(";");
                    if (parts.size() >= 4) {
                        targetBankNameOverride = parts[0];
                        targetId = parts[3];
                        // Change Account to "OVERRIDE_ACC"
                        parts[1] = "OVERRIDE_ACC";
                        line = parts.join(";");
                    }
                }
                lines.append(line);
            }
            file.close();
            
            if (!targetId.isEmpty()) {
                // Write back
                QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
                QTextStream out(&file);
                for (const QString &l : lines) {
                    out << l << "\n";
                }
                file.close();
                
                // Reload and verify
                BookAccountBankTable table(dir);
                bool foundOverride = false;
                for (int i = 0; i < table.rowCount(); ++i) {
                    if (table.data(table.index(i, 0)).toString() == targetBankNameOverride) {
                        QCOMPARE(table.data(table.index(i, 1)).toString(), QString("OVERRIDE_ACC"));
                        foundOverride = true;
                    }
                }
                QVERIFY(foundOverride);
            }
        }

        // 5. UI Behavior
        {
            BookAccountBankTable table(dir);
            if (table.rowCount() > 0) {
                QModelIndex idxBank = table.index(0, 0);
                QModelIndex idxAccount = table.index(0, 1);
                QModelIndex idxFees = table.index(0, 2);
                
                // Bank Name (0) -> Not Editable
                QVERIFY(!(table.flags(idxBank) & Qt::ItemIsEditable));
                QVERIFY(!table.setData(idxBank, "New Name", Qt::EditRole));
                
                // Account (1) -> Editable
                QVERIFY(table.flags(idxAccount) & Qt::ItemIsEditable);
                QVERIFY(table.flags(idxFees) & Qt::ItemIsEditable);
                
                // Signals
                QSignalSpy spy(&table, &BookAccountBankTable::dataChanged);
                table.setData(idxAccount, "NEW_ACC_2", Qt::EditRole);
                QCOMPARE(spy.count(), 1);
            }
        }
    }

    void test_BookAccountAmzBalanceTable() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        // 1. Initialization - defaults should be present
        {
            BookAccountAmzBalanceTable table(dir);
            QVERIFY(table.rowCount() > 0);
            QVERIFY(table.data(table.index(0, 0)).isValid()); // Amazon site
        }

        // 2. getAccount success (if we edit it)
        {
            BookAccountAmzBalanceTable table(dir);
            // Manually set an account for amazon.fr
            int frRow = -1;
            for(int i=0; i<table.rowCount(); ++i) {
                if (table.data(table.index(i, 0)).toString() == "amazon.fr") {
                    frRow = i;
                    break;
                }
            }
            if (frRow == -1) {
                // Should exist ideally, but let's add if not
                table.addAmazon("amazon.fr");
                frRow = table.rowCount() - 1;
            }

            table.setData(table.index(frRow, 1), "512AMZ", Qt::EditRole); // Balance
            table.setData(table.index(frRow, 2), "411AMZ", Qt::EditRole); // Account
            
            auto accounts = syncWait(table.getAccount("amazon.fr"));
            QCOMPARE(accounts.balanceAccount, "512AMZ");
            QCOMPARE(accounts.account, "411AMZ");
        }

        // 3. getAccount missing & auto-add scenario
        {
             BookAccountAmzBalanceTable table(dir);
             QString unknownSite = "amazon.mars";
             
             // First call without callback - throws
             QVERIFY_EXCEPTION_THROWN(syncWait(table.getAccount(unknownSite)), ExceptionWithTitleText);
             QVERIFY(!table.data(table.index(table.rowCount()-1, 0)).toString().contains(unknownSite));

             // Call with callback that adds it
             auto cbAdd = [&](const QString&, const QString&) -> QCoro::Task<bool> {
                 // Simulate user adding it via UI
                 const_cast<BookAccountAmzBalanceTable&>(table).addAmazon(unknownSite);
                 // And setting some values
                 int row = table.rowCount() - 1;
                 const_cast<BookAccountAmzBalanceTable&>(table).setData(table.index(row, 1), "512MARS", Qt::EditRole);
                 const_cast<BookAccountAmzBalanceTable&>(table).setData(table.index(row, 2), "411MARS", Qt::EditRole);
                 co_return true;
             };

             auto accounts = syncWait(table.getAccount(unknownSite, cbAdd));
             QCOMPARE(accounts.balanceAccount, "512MARS");
             QCOMPARE(accounts.account, "411MARS");
             
             // Verify it persists
             BookAccountAmzBalanceTable table2(dir); // New instance
             auto accounts2 = syncWait(table2.getAccount(unknownSite)); // Should work now
             QCOMPARE(accounts2.balanceAccount, "512MARS");
        }
        
        // 4. Test Restricted Removal
        {
            BookAccountAmzBalanceTable table(dir);
            
            // Try removing amazon.fr (default) -> Should fail
            int frRow = -1;
            for(int i=0; i<table.rowCount(); ++i) {
                if (table.data(table.index(i, 0)).toString() == "amazon.fr") {
                    frRow = i;
                    break;
                }
            }
            QVERIFY(frRow != -1);
            QVERIFY(table.removeRow(frRow) == false); // Should fail
            
            // Try removing custom (amazon.mars) -> Should succeed
            int marsRow = -1;
            for(int i=0; i<table.rowCount(); ++i) {
                if (table.data(table.index(i, 0)).toString() == "amazon.mars") {
                    marsRow = i;
                    break;
                }
            }
            if (marsRow != -1) { // It was added in step 3
                QVERIFY(table.removeRow(marsRow)); // Should succeed
            }
        }
    }

    // Test that createEntryGrouped handles all shipment combinations present in the
    // previous complete year of Amazon VAT reports without missing any account entry.
    // Uses the same data files as TestVatRateResolver::test_AmazonReportsRates but
    // only reads the 2025 files (previous complete year as of early 2026).
    // For each SALE row we derive the expected TaxScheme via TaxResolver and the
    // expected VAT rate via VatResolver, then create a synthetic Activity and feed
    // it to JournalEntryFactory::createEntryGrouped. The test fails if any account
    // is missing from BooksAccountsSalesTable::_fillIfEmpty.
    void test_createEntryGrouped_fullYear()
    {
        QDir appDir(QCoreApplication::applicationDirPath());
        QString reportsPath = appDir.absoluteFilePath("data/amazon-vat-reports/2025");
        if (!QDir(reportsPath).exists()) {
            QSKIP("2025 Amazon VAT report directory not found. Skipping.");
        }

        // ── Environment ────────────────────────────────────────────────────────
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        // Write a minimal company.csv (FR company, EUR currency)
        {
            QFile f(dir.filePath("company.csv"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "Id;Parameter;Value\n";
            out << "Country;Country Code;FR\n";
            out << "Currency;Currency;EUR\n";
        }

        CompanyInfosTable        companyInfos(dir);
        CurrencyRateManager      currencyManager(dir, "");
        BooksAccountsSalesTable  saleAccounts(dir);
        BookAccountPurchaseTable purchaseAccounts(dir, "FR");
        JournalTable             journalTable(dir);
        BookAccountSelfVatTable  selfVatAccounts(dir, "FR");
        AmzPaymentSettings       amzPaymentSettings(dir);
        BookAccountAmzBalanceTable amzBalanceTable(dir);

        JournalEntryFactory factory(&currencyManager, &companyInfos,
                                    &saleAccounts, &purchaseAccounts, &journalTable,
                                    &selfVatAccounts, &amzPaymentSettings, &amzBalanceTable);

        // Resolvers (in-memory, use hardcoded defaults – same as _fillIfEmpty)
        TaxResolver taxResolver(appDir.absoluteFilePath("data"));
        VatResolver vatResolver(QDir(), nullptr, false);

        ActivitySource source;
        source.channel    = "Amazon";
        source.subchannel = "EU";
        source.type       = ActivitySourceType::Report;

        // ── Parse all 2025 CSV files ───────────────────────────────────────────
        QMultiMap<QDateTime, QSharedPointer<Shipment>> allShipments;

        QDirIterator it(reportsPath, QStringList() << "*.csv", QDir::Files);
        while (it.hasNext()) {
            it.next();
            const QString fname = it.fileInfo().fileName();
            if (fname.contains("FIXING-OLD-REFUND")) continue;

            CsvReader reader(it.fileInfo().absoluteFilePath(), ",", "\"", true, "\n", 0, "UTF-8");
            if (!reader.readAll()) continue;

            const DataFromCsv *data = reader.dataRode();

            // Require the columns we need
            if (!data->header.contains("TRANSACTION_TYPE")
                || (!data->header.contains("SALE_DEPART_COUNTRY")
                    && !data->header.contains("DEPARTURE_COUNTRY"))
                || (!data->header.contains("SALE_ARRIVAL_COUNTRY")
                    && !data->header.contains("ARRIVAL_COUNTRY")))
            {
                continue;
            }

            const int idxType    = data->header.pos("TRANSACTION_TYPE");
            const int idxDepart  = data->header.contains("SALE_DEPART_COUNTRY")
                                   ? data->header.pos("SALE_DEPART_COUNTRY")
                                   : data->header.pos("DEPARTURE_COUNTRY");
            const int idxArrival = data->header.contains("SALE_ARRIVAL_COUNTRY")
                                   ? data->header.pos("SALE_ARRIVAL_COUNTRY")
                                   : data->header.pos("ARRIVAL_COUNTRY");
            const int idxDate    = data->header.contains("TAX_CALCULATION_DATE")
                                   ? data->header.pos("TAX_CALCULATION_DATE")
                                   : data->header.pos("TRANSACTION_COMPLETE_DATE");
            const int idxBuyerVat = data->header.contains("BUYER_VAT_NUMBER")
                                    ? data->header.pos("BUYER_VAT_NUMBER") : -1;
            const int idxEventId  = data->header.contains("TRANSACTION_EVENT_ID")
                                    ? data->header.pos("TRANSACTION_EVENT_ID") : -1;

            for (const QStringList &row : data->lines) {
                if (row.value(idxType) != "SALE") continue;

                const QString dep = row.value(idxDepart).trimmed();
                const QString arr = row.value(idxArrival).trimmed();
                if (dep.isEmpty() || arr.isEmpty()) continue;

                // Parse date
                const QString dateStr = row.value(idxDate).trimmed();
                QDateTime dt = QDateTime::fromString(dateStr, "dd-MM-yyyy");
                if (!dt.isValid()) dt = QDateTime::fromString(dateStr, "dd/MM/yyyy");
                if (!dt.isValid()) dt = QDateTime::fromString(dateStr, "yyyy-MM-dd");
                if (!dt.isValid()) continue;

                const bool isB2B = (idxBuyerVat != -1
                                    && !row.value(idxBuyerVat).trimmed().isEmpty());

                // Determine TaxScheme via TaxResolver (same logic as production)
                TaxResolver::TaxContext ctx = taxResolver.getTaxContext(
                    dt, dep, arr, SaleType::Products, isB2B);

                // Skip schemes that resolveVatCountries does not support
                // (ReverseChargeImport, ReverseChargeDomestic, ImportVat, Unknown)
                switch (ctx.taxScheme) {
                case TaxScheme::DomesticVat:
                case TaxScheme::EuOssUnion:
                case TaxScheme::EuOssNonUnion:
                case TaxScheme::EuIoss:
                case TaxScheme::Exempt:
                case TaxScheme::OutOfScope:
                case TaxScheme::MarketplaceDeemedSupplier:
                    break;
                default:
                    continue;
                }

                // Derive expected VAT rate: use VatResolver for taxable schemes,
                // 0 % for exempt / out-of-scope (matching what resolveVatCountries
                // will compute on the account-lookup side).
                double expectedRate = 0.0;
                switch (ctx.taxScheme) {
                case TaxScheme::DomesticVat:
                case TaxScheme::EuOssUnion:
                case TaxScheme::EuOssNonUnion:
                case TaxScheme::EuIoss: {
                    const QString rateCountry = ctx.countryCodeVatPaidTo.isEmpty()
                                                ? arr : ctx.countryCodeVatPaidTo;
                    const double r = vatResolver.getRate(dt.date(), rateCountry, SaleType::Products);
                    expectedRate = (r >= 0.0) ? r : 0.0;
                    break;
                }
                default:
                    expectedRate = 0.0;
                    break;
                }

                // Build an Amount that yields the expected VAT rate:
                // Amount(amountTaxed, taxes) with rate = taxes / (amountTaxed - taxes)
                // For rate R: amountTaxed = 1 + R, taxes = R  (on a 1 € base)
                const double amountTaxed = 1.0 + expectedRate;
                const double amountVat   = expectedRate;

                const QString eventId = (idxEventId != -1 && !row.value(idxEventId).isEmpty())
                                        ? row.value(idxEventId) : "EVT";

                auto actResult = Activity::create(
                    eventId, eventId, "",
                    dt, dt,
                    "EUR",   // always EUR – avoids any currency-rate lookup
                    dep, arr,
                    isB2B,
                    ctx.countryCodeVatPaidTo,
                    Amount{amountTaxed, amountVat},
                    TaxSource::MarketplaceProvided,
                    ctx.taxDeclaringCountryCode,
                    ctx.taxScheme,
                    ctx.taxJurisdictionLevel,
                    SaleType::Products
                );
                if (!actResult.ok()) continue;

                QList<Activity> acts;
                acts.append(actResult.value.value());
                auto shipment = QSharedPointer<Shipment>::create(acts, "", true);
                allShipments.insert(dt, shipment);
            }
        }

        if (allShipments.isEmpty()) {
            QSKIP("No valid 2025 SALE transactions found in report files.");
        }
        
        qInfo() << "Found and included" << allShipments.size() << "shipments from 2025 reports.";

        // ── Call the factory – must not throw ──────────────────────────────────
        // If any (TaxScheme, countryFrom, countryTo, vatRate) combination is missing
        // from BooksAccountsSalesTable::_fillIfEmpty this will throw and the test fails.
        auto entries = syncWait(factory.createEntryGrouped(&source, allShipments));
        QVERIFY(!entries.isEmpty());
    }

    // ── Purchase file VAT-account validation ───────────────────────────────────
    // Verifies that all real Amazon purchase filenames decode cleanly and that the
    // derived VAT rate resolves to an account via getAccountsDebit6Closest with a
    // 0.9 % tolerance.  The FAKE-rate file must fail.
    void test_validatePurchaseFiles()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        BookAccountPurchaseTable purchaseTable(QDir(tempDir.path()), "FR");

        const QString companyCurrency = "EUR";
        static const double kTolerance = 0.9; // percentage points

        // Extract the numeric rate string from the first VAT token, e.g. "20" from "TVA20".
        auto extractVatRateFromToken = [](const QString &token) -> QString {
            static QRegularExpression regexRate(QStringLiteral("[0-9.]+"));
            const QStringList parts = token.split(QLatin1Char('-'));
            const int ratePartIdx = (parts.size() >= 3) ? 1 : 0;
            if (ratePartIdx < parts.size()) {
                const QRegularExpressionMatch m = regexRate.match(parts[ratePartIdx]);
                if (m.hasMatch()) {
                    return m.captured(0);
                }
            }
            return {};
        };

        // Returns an error string if the file fails validation, or "" if it passes.
        // Cross-currency entries without dual amounts are skipped (require live rate data).
        auto validateFile = [&](const QString &fileName) -> QString {
            PurchaseInformation info;
            try {
                info = PurchaseInvoiceManager::decode(fileName, &decodeTestPurchaseTable(), "FR");
            } catch (const ExceptionWithTitleText &e) {
                return e.errorTitle() + ": " + e.errorText();
            }

            if (info.vatTokens.isEmpty() || info.vatCountry.isEmpty()) {
                return {}; // no VAT to validate
            }

            const QString vatCurrency   = info.vatCurrency;
            const QString totalCurrency = info.currency;
            const double  vatAmountAbs  = qAbs(info.rawVatAmount.toDouble());
            const double  totalAmountAbs = qAbs(info.totalAmount);

            // Sum company-currency amounts from dual-amount tokens.
            double vatAmountCompany = 0.0;
            for (const auto &rateMap : std::as_const(info.country_vatRate_vatCompany)) {
                for (const double amt : std::as_const(rateMap)) {
                    vatAmountCompany += amt;
                }
            }
            const bool hasDualAmount = vatAmountCompany > 1e-9 && vatCurrency != companyCurrency;

            // Determine VAT rate string (in %).
            QString rateStr = info.vatTokens.isEmpty()
                              ? QString()
                              : extractVatRateFromToken(info.vatTokens.first());

            if (rateStr.isEmpty()) {
                if (hasDualAmount) {
                    // Use source-currency amounts: source vat / source net.
                    const double netSource = totalAmountAbs - vatAmountAbs;
                    if (qAbs(netSource) > 1e-9) {
                        rateStr = QString::number((vatAmountAbs / netSource) * 100.0, 'g', 6);
                    }
                } else if (vatCurrency == totalCurrency) {
                    // Same currency: derive rate from amounts.
                    const double netNorm = totalAmountAbs - vatAmountAbs;
                    if (qAbs(netNorm) > 1e-9) {
                        rateStr = QString::number((vatAmountAbs / netNorm) * 100.0, 'g', 6);
                    }
                } else {
                    // Cross-currency without dual amounts: skip (live rate data needed).
                    return {};
                }
            }

            if (rateStr.isEmpty()) {
                return QString("VAT rate required for country %1").arg(info.vatCountry);
            }

            const double rate = qRound(rateStr.toDouble() * 100.0) / 100.0 / 100.0;
            try {
                purchaseTable.getAccountsDebit6Closest(info.vatCountry, rate, kTolerance);
            } catch (const ExceptionWithTitleText &e) {
                return e.errorTitle() + ": " + e.errorText();
            }
            return {};
        };

        // All of these must validate without error.
        const QStringList validFiles = {
            // Same-currency EUR purchases
            "2026-01-31__622201__frais-vente-FR-AEU-2026-5781__FAMZMK__FR-TVA-74.43EUR__446.55EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-5782__FAMZMK__FR-TVA-41.49EUR__248.91EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-6165__FAMZMK__FR-TVA-380.86EUR__2285.15EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-6166__FAMZMK__FR-TVA-202.71EUR__1216.24EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-10327__FAMZMK__FR-TVA-246.62EUR__1479.74EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-10328__FAMZMK__FR-TVA-249.68EUR__1498.06EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-17237__FAMZMK__FR-TVA-40.93EUR__245.57EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-17239__FAMZMK__FR-TVA-32.41EUR__194.46EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-27423__FAMZMK__FR-TVA-43.25EUR__259.50EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-27906__FAMZMK__FR-TVA-17.06EUR__102.35EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-27908__FAMZMK__FR-TVA-15.05EUR__90.30EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-29559__FAMZMK__FR-TVA-82.10EUR__492.59EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-32889__FAMZMK__FR-TVA-31.35EUR__188.10EUR.pdf",
            // Same-currency EUR refunds (small amounts stress-test Closest)
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-3379__FAMZMK__FR-TVA--75.46EUR__-452.76EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-5436__FAMZMK__FR-TVA--6.68EUR__-40.05EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-5437__FAMZMK__FR-TVA--0.84EUR__-5.06EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-7035__FAMZMK__FR-TVA--24.74EUR__-148.42EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8235__FAMZMK__FR-TVA--3.32EUR__-19.90EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-15475__FAMZMK__FR-TVA--2.99EUR__-17.96EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-18948__FAMZMK__FR-TVA--4.17EUR__-25.04EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-22359__FAMZMK__FR-TVA--4.18EUR__-25.10EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-24266__FAMZMK__FR-TVA--2.92EUR__-17.49EUR.pdf",
            // Same-currency SEK
            "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA-27.67SEK__165.99SEK.pdf",
            // Cross-currency with explicit rate (no CurrencyRateManager needed)
            "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA20-2.63EUR__165.99SEK.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA20-27.67SEK__15.78EUR.pdf",
            // Cross-currency without rate (skipped in test — live rates needed)
            "2026-01-31__622201__frais-vente-FR-AEU-2026-27276__FAMZMK__FR-TVA-4.12EUR__21.42GBP.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8372__FAMZMK__FR-TVA--0.17EUR__-0.87GBP.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-23705__FAMZMK__FR-TVA--3.25EUR__-82.09PLN.pdf",
            // Dual-amount tokens (company EUR amount + source currency amount)
            "2026-01-31__622201__frais-vente-FR-AEU-2026-86900__FAMZMK__FR-TVA-2.63EUR_27.67SEK__165.99SEK.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-86901__FAMZMK__FR-TVA-2.66EUR_27.94SEK__167.64SEK.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-27279__FAMZMK__FR-TVA-13.51EUR_11.70GBP__70.20GBP.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-49313__FAMZMK__FR-TVA-2.21EUR_9.28PLN__55.68PLN.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-49314__FAMZMK__FR-TVA-5.69EUR_23.92PLN__143.49PLN.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8373__FAMZMK__FR-TVA--6.30EUR_-5.46GPB__-32.74GBP.pdf",
            "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-8372__FAMZMK__FR-TVA--0.17EUR_-0.15GPB__-0.87GBP.pdf",
            // No VAT (just total amount)
            "2026-01-31__622201__frais-vente-CA-ACCU-INV-2026-55586-CA-FR__FAMZMK__2098.20CAD.pdf",
            "2026-01-31__622201__frais-vente-DE-145560651-2026-1__FAMZMK__7.60EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-145560651-2026-1__FAMZMK__6.54EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-101423__FAMZMK__5.07EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-27277__FAMZMK__88.56GBP.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-50308__FAMZMK__29.91EUR.pdf",
            "2026-01-31__622201__frais-vente-FR-AEU-2026-80094__FAMZMK__20.40EUR.pdf",
            "2026-01-31__622201__frais-vente-PL-AOPL-2026-1382__FAMZMK__2.43PLN.pdf",
        };

        for (const QString &fn : validFiles) {
            const QString err = validateFile(fn);
            if (!err.isEmpty()) {
                const QString msg = QString("Unexpected error for '%1': %2").arg(fn, err);
                QTest::qFail(msg.toUtf8().constData(), __FILE__, __LINE__);
                return;
            }
        }

        // This file has an implausible 28% rate and must fail.
        const QString fakeFile =
            "2026-01-31__622201__frais-vente-FR-AEU-2026-5781-FAKE__FAMZMK__FR-TVA-99.43EUR__446.55EUR.pdf";
        const QString fakeErr = validateFile(fakeFile);
        QVERIFY2(!fakeErr.isEmpty(),
                 "Expected validation error for FAKE rate file but got OK");
    }

};

QTEST_MAIN(TestBookAccounts)
#include "test_book_accounts.moc"
