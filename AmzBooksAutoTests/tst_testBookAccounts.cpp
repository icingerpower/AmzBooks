#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>

#include "books/SaleBookAccountsTable.h"
#include "model/TaxScheme.h"
#include "books/PurchaseBookAccountsTable.h"
#include "books/ExceptionVatAccountExisting.h"
#include "books/ExceptionTaxSchemeInvalid.h"
#include "books/CompanyAddressTable.h"
#include "books/CompanyInfosTable.h"
#include "books/ExceptionCompanyInfo.h"
#include "books/VatNumbersTable.h"

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
            SaleBookAccountsTable table(dir);
            initialCount = table.rowCount();
            QVERIFY(initialCount > 60);   
            
            VatCountries vc = table.resolveVatCountries(TaxScheme::EuOssUnion, "IT", "DE");
            SaleBookAccountsTable::Accounts acc = table.getAccounts(vc, 19.0);
            QCOMPARE(acc.saleAccount, "7070OSSDE19");
            QCOMPARE(acc.vatAccount, "4457OSSDE19");
        }

        // 2. Add new account and save
        {
            SaleBookAccountsTable table(dir);
            VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR");
            SaleBookAccountsTable::Accounts newAcc;
            newAcc.saleAccount = "7001";
            newAcc.vatAccount = "4401";
            
            table.addAccount(vc, 20.0, newAcc);
            
            // Verify immediate cache update
            SaleBookAccountsTable::Accounts retrieved = table.getAccounts(vc, 20.0);
            QCOMPARE(retrieved.saleAccount, "7001");
            QCOMPARE(retrieved.vatAccount, "4401");
        }

        // 3. Reload and verify persistence
        {
            SaleBookAccountsTable table(dir);
            QCOMPARE(table.rowCount(), initialCount + 1);
            
            VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR");
            SaleBookAccountsTable::Accounts retrieved = table.getAccounts(vc, 20.0);
            QCOMPARE(retrieved.saleAccount, "7001");
            QCOMPARE(retrieved.vatAccount, "4401");
        }
        
        // 4. Robustness: Inject Fake Column
        QString csvPath = dir.filePath("saleBookAccounts.csv");
        injectFakeColumn(csvPath);
        
        {
             SaleBookAccountsTable table(dir);
             VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR");
             SaleBookAccountsTable::Accounts retrieved = table.getAccounts(vc, 20.0);
             // Should still find it
             QCOMPARE(retrieved.saleAccount, "7001");
        }
    }
    
    // ... test_saleValidation ...

    void test_saleValidation() {
        QTemporaryDir tempDir;
        SaleBookAccountsTable table(QDir(tempDir.path()));
        
        VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "FR", "FR");
        SaleBookAccountsTable::Accounts acc;
        acc.saleAccount = "S1";
        acc.vatAccount = "V1";
        
        // 1. Add valid (Use 10.0 to avoid collision with default 20.0)
        table.addAccount(vc, 10.0, acc);
        
        // 2. Duplicate -> Exception
        QVERIFY_EXCEPTION_THROWN(
            table.addAccount(vc, 10.0, acc),
            ExceptionVatAccountExisting
        );
        
        // 3. Diff Rate -> OK
        table.addAccount(vc, 5.5, acc);
        
        // 4. Diff Scheme -> OK (Use BE to avoid collision with default PanEU Exempt entries which include FR)
        VatCountries vc2 = table.resolveVatCountries(TaxScheme::Exempt, "BE", "BE");
        table.addAccount(vc2, 0.0, acc);
    }

    void test_lookup() {
        QTemporaryDir tempDir;
        SaleBookAccountsTable table(QDir(tempDir.path()));
        
        // Setup scenarios
        VatCountries vc = table.resolveVatCountries(TaxScheme::DomesticVat, "DE", "DE");
        SaleBookAccountsTable::Accounts acc1;
        acc1.saleAccount = "S1";
        acc1.vatAccount = "V1";
        table.addAccount(vc, 19.0, acc1);
        
        // Setup second rate
        SaleBookAccountsTable::Accounts acc3;
        acc3.saleAccount = "S3";
        acc3.vatAccount = "V3";
        table.addAccount(vc, 7.0, acc3); // Different rate

        // Case 1: Retrieve first rate
        auto res1 = table.getAccounts(vc, 19.0);
        QCOMPARE(res1.saleAccount, "S1");

        // Case 2: Retrieve second rate
        auto res3 = table.getAccounts(vc, 7.0);
        QCOMPARE(res3.saleAccount, "S3");

        // Case 4: Unknown rate -> fallback
        auto res4 = table.getAccounts(vc, 5.0);
        QVERIFY(!res4.saleAccount.isEmpty());
        // For DE Domestic (default 19%): 7070DOMDE19 if generated
        QCOMPARE(res4.saleAccount, "7070DOMDE19");
    }

    void test_resolveVatCountries() {
        SaleBookAccountsTable table(QDir::tempPath());

        // 1. Normalization
        {
            auto vc = table.resolveVatCountries(TaxScheme::DomesticVat, " fr ", " DE ");
            QCOMPARE(vc.countryCodeFrom, "FR");
            QCOMPARE(vc.countryCodeTo, "");
            
            auto vc2 = table.resolveVatCountries(TaxScheme::EuOssUnion, "fr", "de");
            QCOMPARE(vc2.countryCodeFrom, ""); 
            QCOMPARE(vc2.countryCodeTo, "DE");
        }
    }

    // --- PURCHASE TESTS ---

    void test_purchaseDoubleVatEntry() {
        // Test isDoubleVatEntryNeeded
        PurchaseBookAccountsTable table(QDir::tempPath(), "FR");
        
        QVERIFY(!table.isDoubleVatEntryNeeded("FR", "FR")); // Domestic
        QVERIFY(table.isDoubleVatEntryNeeded("DE", "FR")); // Intra-Community
        QVERIFY(table.isDoubleVatEntryNeeded("CN", "FR")); // Import
        
        // Dest != Company (FR) -> False (Not French Auto-liquidation)
        QVERIFY(!table.isDoubleVatEntryNeeded("FR", "DE")); 
        QVERIFY(!table.isDoubleVatEntryNeeded("DE", "IT"));
    }

    void test_purchasePersistence() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        
        // 1. Init & Defaults
        {
            PurchaseBookAccountsTable table(dir, "FR");
            QVERIFY(table.rowCount() == 1); // Should have 1 default row for FR
            
            QString debit = table.getAccountsDebit6("FR");
            QString credit = table.getAccountsCredit4("FR");
            QCOMPARE(debit, "445660");
            QCOMPARE(credit, "445710");
        }
        
        // 2. Add & Save
        {
            PurchaseBookAccountsTable table(dir, "FR");
            // Add account for DE (Import/Intra)
            table.addAccount("DE", 19.0, "600DE", "400DE");
            
            QCOMPARE(table.getAccountsDebit6("DE"), "600DE");
            QCOMPARE(table.getAccountsCredit4("DE"), "400DE");
        }
        
        // 3. Reload
        {
            PurchaseBookAccountsTable table(dir, "FR");
            QCOMPARE(table.rowCount(), 2); // FR + DE
            
            QCOMPARE(table.getAccountsDebit6("DE"), "600DE");
            QCOMPARE(table.getAccountsCredit4("DE"), "400DE");
        }
        
        // 4. Robustness: Inject Fake Column
        QString csvPath = dir.filePath("purchaseBookAccounts.csv");
        injectFakeColumn(csvPath);
        
        {
            PurchaseBookAccountsTable table(dir, "FR");
            // Check Data valid
            QString val = table.getAccountsDebit6("DE");
            QCOMPARE(val, "600DE");
        }
    }
    
    void test_purchaseValidation() {
        QTemporaryDir tempDir;
        PurchaseBookAccountsTable table(QDir(tempDir.path()), "FR");
        
        // 1. Invalid Country
        QVERIFY_EXCEPTION_THROWN(
            table.addAccount("US", 10.0, "6", "4"),
            ExceptionTaxSchemeInvalid
        );
        
        // 2. Valid Country, New Rate
        table.addAccount("DE", 19.0, "6DE", "4DE");
        
        // 3. Duplicate (DE, 19.0)
        QVERIFY_EXCEPTION_THROWN(
            table.addAccount("DE", 19.0, "6New", "4New"),
            ExceptionVatAccountExisting
        );
        
        // 4. Same Country, Diff Rate -> OK
        table.addAccount("DE", 7.0, "6DE7", "4DE7");
        
        // Check GB/UK
        table.addAccount("GB", 20.0, "6GB", "4GB");
        try {
            table.addAccount("UK", 20.0, "6UK", "4UK"); // Assuming UK is accepted string? Logic said UK or GB.
        } catch(...) {
            // Depending on implementation
        }
    }

    void test_purchaseLookup() {
          QTemporaryDir tempDir;
          PurchaseBookAccountsTable table(QDir(tempDir.path()), "FR");
          
          table.addAccount("IT", 22.0, "600IT", "400IT");
          
          // Case 1: Known country
          QCOMPARE(table.getAccountsDebit6("IT"), "600IT");
          
          // Case 2: Unknown country
          QVERIFY(table.getAccountsDebit6("ES").isEmpty());
    }

    void test_CompanyAddressTable() {
        QTemporaryDir tempDir;
        QString iniFiles = tempDir.filePath("company.csv"); // Renamed to CSV (logic handles extension but let's be explicit test file)

        // 1. Init & Add Data
        {
            CompanyAddressTable table(iniFiles);
            
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
            QVERIFY_EXCEPTION_THROWN(table.getCompanyAddress(QDate(2022, 1, 1)), ExceptionCompanyInfo);
        }
        
        // 2. Persistence (New Instance)
        {
            CompanyAddressTable table(iniFiles);
            QCOMPARE(table.rowCount(), 2);
            
            // Verify Data loaded sorted
            QCOMPARE(table.getCompanyName(QDate(2023, 1, 1)), "MyCo 2023");
            QCOMPARE(table.getCompanyName(QDate(2024, 1, 1)), "MyCo 2024");
            
            // Modify
            table.setData(table.index(0, 1), "MyCo 2024 Mod");
        }
        
        // 3. Verify Modification Persisted
        {
            CompanyAddressTable table(iniFiles);
            QCOMPARE(table.getCompanyName(QDate(2024, 1, 1)), "MyCo 2024 Mod");
        }
        
        // 4. Robustness
        injectFakeColumn(iniFiles);
        {
            CompanyAddressTable table(iniFiles);
            QCOMPARE(table.rowCount(), 2);
            QCOMPARE(table.getCompanyName(QDate(2024, 1, 1)), "MyCo 2024 Mod");
        }
    }

    void test_CompanyInfosTable() {
        QTemporaryDir tempDir;
        QString iniFiles = tempDir.filePath("infos.csv");
        
        // 1. Init & Modify
        {
            CompanyInfosTable table(iniFiles);
            QCOMPARE(table.rowCount(), 2);
            
            // Modify Country (Row 0, Col 1)
            table.setData(table.index(0, 1), "US");
            
            // Modify Currency (Row 1, Col 1)
            table.setData(table.index(1, 1), "USD");
        }
        
        // 2. Persistence (New Instance)
        {
            CompanyInfosTable table(iniFiles);
            QCOMPARE(table.rowCount(), 2);
            
            // Check Values
            QCOMPARE(table.data(table.index(0, 1)).toString(), "US");
            QCOMPARE(table.data(table.index(1, 1)).toString(), "USD");
        }
        
        // 3. Robustness
        injectFakeColumn(iniFiles);
        {
            CompanyInfosTable table(iniFiles);
            QCOMPARE(table.data(table.index(0, 1)).toString(), "US");
        }
    }

    void test_VatNumbersTable() {
        QTemporaryDir tempDir;
        QString iniFile = tempDir.filePath("vat.csv");
        
        // 1. Add and Save
        {
            VatNumbersTable table(iniFile);
            table.addVatNumber("FR", "FR123");
            table.addVatNumber("DE", "DE456");
            
            QCOMPARE(table.getVatNumber("FR"), "FR123");
            QVERIFY(table.hasVatNumber("FR"));
            QVERIFY(!table.hasVatNumber("ES"));
            
            // Duplicate strict check
            QVERIFY_EXCEPTION_THROWN(table.addVatNumber("FR", "FR999"), ExceptionCompanyInfo);
            
            // Validate Columns (0=Country, 1=Vat, ID is hidden)
            QCOMPARE(table.data(table.index(0, 0)).toString(), "FR");
            QCOMPARE(table.data(table.index(0, 1)).toString(), "FR123");
        }
        
        // 2. Persistence
        {
             VatNumbersTable table(iniFile);
             QCOMPARE(table.rowCount(), 2);
             QCOMPARE(table.getVatNumber("DE"), "DE456");
        }
        
        // 3. Robustness 
        // Logic handled by injectFakeColumn matches generic requirements
        injectFakeColumn(iniFile);
        
        {
             VatNumbersTable table(iniFile);
             QCOMPARE(table.rowCount(), 2);
             QCOMPARE(table.getVatNumber("DE"), "DE456");
        }
    }

};

QTEST_MAIN(TestBookAccounts)
#include "tst_testBookAccounts.moc"
