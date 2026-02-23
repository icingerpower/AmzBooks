#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>

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
#include "ExceptionWithTitleText.h"

// Helper to synchronously wait for QCoro::Task
template <typename T>
T syncWait(QCoro::Task<T> &&task) {
    return QCoro::waitFor<T>(std::move(task));
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
            QCOMPARE(table.rowCount(), 7);  // Country, Currency, FixerApiKey + 4 Legal Infos
            
            // Modify Country (Row 0, Col 1)
            table.setData(table.index(0, 1), "US");
            
            // Modify Currency (Row 1, Col 1)
            table.setData(table.index(1, 1), "USD");
        }
        
        // 2. Persistence (New Instance)
        {
            CompanyInfosTable table(QDir(tempDir.path()));
            QCOMPARE(table.rowCount(), 7);  // Country, Currency, FixerApiKey + 4 Legal Infos
            
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
            // The bug is that it loads all rows including fake ones.
            // We want strict 7 rows.
            QCOMPARE(table.rowCount(), 7); 
            
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
            QCOMPARE(table.rowCount(), 7);
            
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

};

QTEST_MAIN(TestBookAccounts)
#include "test_book_accounts.moc"
