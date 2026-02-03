#include <QtTest>
#include "banks/BankPaypalEUR.h"
#include "banks/BankPaypalUSD.h"
#include "banks/BankStripeEUR.h"
#include "banks/BankStripeUSD.h"
#include "banks/BankWiseEUR.h"
#include "banks/BankWiseGBP.h"
#include "banks/BankWiseUSD.h"
#include "banks/BankQonto.h"
#include <QDir>
#include <QDebug>
#include "books/AbstractBooksTableBank.h"
#include "books/BooksConnections.h"

class TestBanks : public QObject
{
    Q_OBJECT

private slots:
    void testPaypalTheoretical();
    void testStripeTheoretical();
    void testWiseTheoretical();
    void testQontoTheoretical();
    void testRealData();
    void test_fileManagement();
};

void TestBanks::testPaypalTheoretical()
{
    // Create a dummy CSV file
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QSKIP("Could not create temporary directory");
    }
    QString fileName = tempDir.path() + "/paypal_test.csv";
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "\"Date\",\"Heure\",\"Fuseau horaire\",\"Nom\",\"Nom de la banque\",\"Type\",\"Etat\",\"Devise\",\"Brut\",\"Frais\",\"Net\",\"De l'adresse email\",\"A l'adresse email\",\"Nutr√©ro de transaction\",\"Description\",\"Adresse de livraison\",\"Etat de l'adresse\",\"Titre de l'objet\",\"Num√©ro de l'objet\",\"Montant des frais d'exp√©dition\",\"Montant de l'assurance\",\"TVA\",\"Option 1 - Nom\",\"Option 1 - Valeur\",\"Option 2 - Nom\",\"Option 2 - Valeur\",\"Num√©ro de r√©f√©rence\",\"Num√©ro de facture\",\"Num√©ro de client\",\"Num√©ro de t√©l√©phone\",\"Num√©ro de re√ßu\",\"Avant la commission\",\"Apr√®s la commission\",\"Solde\",\"Contact\",\"Titre de l'objet\"\n";
    out << "\"01/01/2025\",\"12:00:00\",\"GMT\",\"Client Name\",\"MyBank\",\"Paiement sur site\",\"Termin√©\",\"EUR\",\"10,00\",\"-0,50\",\"9,50\",\"client@test.com\",\"me@test.com\",\"TXN123\",\"Description of item\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"100,00\",\"\",\"\"\n";
    file.close();

    BankPaypalEUR bank;
    auto rows = bank.readRows(fileName);
    QVERIFY(rows != nullptr);
    QCOMPARE(rows->size(), 1);
    const auto &row = rows->first();
    QCOMPARE(row.date, QDate(2025, 1, 1));
    QCOMPARE(row.amount, 10.00);
    QCOMPARE(row.fees, 0.50); // Parsed as -(-0,50) = 0.50
    QCOMPARE(row.label, QString("MyBank Client Name Description of item"));
    QCOMPARE(row.currency, QString("EUR"));
}

void TestBanks::testStripeTheoretical()
{
    QTemporaryDir tempDir;
    QString fileName = tempDir.path() + "/stripe_test.csv";
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "\"id\",\"Type\",\"Description\",\"Created (UTC)\",\"Amount\",\"Currency\",\"Fee\"\n";
    out << "\"txn_123\",\"charge\",\"Charge for order\",\"2025-01-01 10:00\",\"100,00\",\"eur\",\"2,50\"\n";
    file.close();

    BankStripeEUR bank;
    auto rows = bank.readRows(fileName);
    QVERIFY(rows != nullptr);
    QCOMPARE(rows->size(), 1);
    const auto &row = rows->first();
    QCOMPARE(row.date, QDate(2025, 1, 1));
    QCOMPARE(row.amount, 100.00);
    QCOMPARE(row.fees, 2.50);
    QCOMPARE(row.currency, QString("EUR"));
}

void TestBanks::testWiseTheoretical()
{
    QTemporaryDir tempDir;
    QString fileName = tempDir.path() + "/transferwise_test.csv";
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "\"TransferWise ID\",\"Date\",\"Amount\",\"Currency\",\"Description\",\"Payment Reference\",\"Running Balance\",\"Exchange From\",\"Exchange To\",\"Exchange Rate\",\"Payer Name\",\"Payee Name\",\"Payee Account Number\",\"Merchant\",\"Card Last Four Digits\",\"Card Holder Name\",\"Attachment\",\"Note\"\n";
    out << "\"TW123\",\"01-01-2025\",\"100.00\",\"EUR\",\"Payment desc\",\"Ref123\",\"1000.00\",\"\",\"\",\"\",\"Payer\",\"Payee\",\"\",\"Merchant\",\"\",\"\",\"\",\"\"\n";
    file.close();

    BankWiseEUR bank;
    auto rows = bank.readRows(fileName);
    QVERIFY(rows != nullptr);
    QCOMPARE(rows->size(), 1);
    const auto &row = rows->first();
    QCOMPARE(row.date, QDate(2025, 1, 1));
    QCOMPARE(row.amount, 100.00);
    QCOMPARE(row.currency, QString("EUR"));
    // Wise title logic: Payer + Payee + Merchant + Description
    // Here: Payer + Payee + Merchant + Payment desc
    QCOMPARE(row.label, QString("Merchant Payee Payer Payment desc"));
}

void TestBanks::testQontoTheoretical()
{
    QTemporaryDir tempDir;
    QString fileName = tempDir.path() + "/qonto_test.csv";
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
     // "settlement_date_local" or "value_date_local"
    out << "\"transaction_id\",\"local_amount\",\"local_amount_currency\",\"amount\",\"currency\",\"settlement_date_local\",\"operation_date_local\",\"counterpart_name\",\"comment\",\"status\"\n";
    out << "\"Q123\",\"-10.00\",\"EUR\",\"-10.00\",\"EUR\",\"01-01-2025 10:00:00\",\"01-01-2025 10:00:00\",\"Counterpart\",\"Comment\",\"completed\"\n";
    file.close();

    BankQonto bank;
    auto rows = bank.readRows(fileName);
    QVERIFY(rows != nullptr);
    QCOMPARE(rows->size(), 1);
    const auto &row = rows->first();
    QCOMPARE(row.date, QDate(2025, 1, 1));
    QCOMPARE(row.amount, -10.00);
    QCOMPARE(row.currency, QString("EUR"));
    QCOMPARE(row.label, QString("Comment Counterpart"));
}

void TestBanks::testRealData()
{
    // Check in ../data/bankAccounts if available relative to build dir
    QDir dataDir("data/bankAccounts"); 
    if (!dataDir.exists()) {
        // Try fallback if running from IDE or specific location
        dataDir.setPath("../data/bankAccounts");
        if (!dataDir.exists()) {
             // If data is not found, skip or fail. The instruction is "Load the real data in bankAccounts ... reading it both with the new classes + real data"
             // Assuming test is run where data is available or copied.
             QSKIP("Real data not found in data/bankAccounts or ../data/bankAccounts");
        }
    }

    // Identify all banks
    QList<QSharedPointer<AbstractBankStatement>> banks;
    banks << QSharedPointer<BankPaypalEUR>::create();
    banks << QSharedPointer<BankPaypalUSD>::create();
    banks << QSharedPointer<BankStripeEUR>::create();
    banks << QSharedPointer<BankStripeUSD>::create();
    banks << QSharedPointer<BankWiseEUR>::create();
    banks << QSharedPointer<BankWiseGBP>::create();
    banks << QSharedPointer<BankWiseUSD>::create();
    banks << QSharedPointer<BankQonto>::create();
    // Assuming simple Qonto without currency variants for now, as implemented.

    // Iterate over subfolders (years)
    QStringList years = dataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &year : years) {
        QDir yearDir(dataDir.filePath(year));
        QStringList files = yearDir.entryList(QDir::Files);
        for (const QString &file : files) {
            QString absPath = yearDir.absoluteFilePath(file);
            
            // Try to match file with bank
            int matched = 0;
            for (const auto &bank : banks) {
                QStringList filters = bank->fileFilters();
                bool matches = false;
                for (const QString &filter : filters) {
                    QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(filter), QRegularExpression::CaseInsensitiveOption);
                    if (rx.match(file).hasMatch()) {
                        matches = true;
                        break;
                    }
                }
                
                if (matches) {
                    matched++;
                    qDebug() << "Checking file" << file << "with bank" << bank->getName();
                    auto rows = bank->readRows(absPath);
                    
                    // Verify no exception (implicit by reaching here)
                    // Check rows != nullptr
                    QVERIFY2(rows != nullptr, qPrintable("Rows shouldn't be null for " + file));
                    
                    if (rows->isEmpty()) {
                        // User rule: only stripe EUR/USD, paypal EUR can return 0 rows.
                        // Others must be a failure.
                        QSet<QString> allowedEmpty;
                        allowedEmpty << "paypal_eur" << "stripe_eur" << "stripe_usd";
                        
                        if (allowedEmpty.contains(bank->getId())) {
                            qWarning() << "File" << file << "matched" << bank->getName() << "but returned 0 rows (ALLOWED).";
                        } else {
                            QFAIL(qPrintable(QString("File %1 matched %2 but returned 0 rows, which is not allowed.").arg(file, bank->getName())));
                        }
                    } else {
                         qDebug() << "File" << file << "rows:" << rows->size();
                    }
                }
            }
            if (matched == 0 && file.endsWith(".csv", Qt::CaseInsensitive)) {
                qWarning() << "File" << file << "did not match any bank filter.";
            }
        }
    }
}



void TestBanks::test_fileManagement()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());
    
    // Setup connections (needed for Table, though we don't use connection logic here)
    // We need 'BooksConnections' matching constructor of Table.
    // AbstractBooksTableBank(const BooksConnections *bookConnections, const QDir &workingDir, ...)
    // So we need a dummy BooksConnections.
    // But BooksConnections needs a QDir.
    // Let's forward declare or include needed headers for BooksConnections?
    // In test_banks.cpp, we don't assume BooksConnections include was comprehensive.
    // Check includes.
    
    // We need "books/BooksConnections.h"
    // And "banks/BankPaypalEUR.h" etc are here. 
    
    // Wait, test_banks.cpp didn't include "books/BooksConnections.h" in my view? 
    // Let's check lines 1-12 of test_banks.cpp again.
    // It includes banks headers.
    // I should check if BankPaypalEUR inherits AbstractBooksTableBank directly?
    // No, BankPaypalEUR is a BankStatement.
    // AbstractBooksTableBank is the TABLE.
    // We need a Concrete Table class for testing, or use one of the Factories?
    // AbstractBooksTableBank is abstract? No, it has pure virtual getBankStatement().
    // We need a concrete subclass.
    
    // Let's create a Helper Concrete Table for testing
    class TestBankTable : public AbstractBooksTableBank {
    public:
         TestBankTable(const QDir &wd) : AbstractBooksTableBank(nullptr, wd, nullptr) {
             m_stmt = QSharedPointer<BankPaypalEUR>::create();
         }
         const AbstractBankStatement *getBankStatement() const override { return m_stmt.data(); }
         QString getId() const override { return "TestBankTable"; }
         
         QSharedPointer<AbstractBankStatement> m_stmt;
    };
    
    // Prepare dummy files
    QDir srcDir = dir;
    srcDir.mkdir("source");
    srcDir.cd("source");
    

    QString file1 = srcDir.absoluteFilePath("file1_2025.csv");
    {
        QFile f(file1);
        f.open(QIODevice::WriteOnly);
        QTextStream out(&f);
        // Paypal EUR format
        out << "\"Date\",\"Heure\",\"Fuseau horaire\",\"Nom\",\"Nom de la banque\",\"Type\",\"Etat\",\"Devise\",\"Brut\",\"Frais\",\"Net\",\"De l'adresse email\",\"A l'adresse email\",\"Nutr√©ro de transaction\",\"Description\",\"Adresse de livraison\",\"Etat de l'adresse\",\"Titre de l'objet\",\"Num√©ro de l'objet\",\"Montant des frais d'exp√©dition\",\"Montant de l'assurance\",\"TVA\",\"Option 1 - Nom\",\"Option 1 - Valeur\",\"Option 2 - Nom\",\"Option 2 - Valeur\",\"Num√©ro de r√©f√©rence\",\"Num√©ro de facture\",\"Num√©ro de client\",\"Num√©ro de t√©l√©phone\",\"Num√©ro de re√ßu\",\"Avant la commission\",\"Apr√®s la commission\",\"Solde\",\"Contact\",\"Titre de l'objet\"\n";
        out << "\"01/01/2025\",\"12:00:00\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"10,00\",\"0,00\",\"10,00\",\"\",\"\",\"TXN1\",\"Desc1\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"10,00\",\"\",\"\"\n";
        out << "\"01/01/2025\",\"12:00:01\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"11,00\",\"0,00\",\"11,00\",\"\",\"\",\"TXN1b\",\"Desc1b\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"11,00\",\"\",\"\"\n";
        out << "\"01/01/2025\",\"12:00:02\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"12,00\",\"0,00\",\"12,00\",\"\",\"\",\"TXN1c\",\"Desc1c\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"12,00\",\"\",\"\"\n";
        out << "\"01/01/2025\",\"12:00:03\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"13,00\",\"0,00\",\"13,00\",\"\",\"\",\"TXN1d\",\"Desc1d\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"13,00\",\"\",\"\"\n";
        out << "\"01/01/2025\",\"12:00:04\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"14,00\",\"0,00\",\"14,00\",\"\",\"\",\"TXN1e\",\"Desc1e\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"14,00\",\"\",\"\"\n";
        f.close();
    }
    
    QString file2 = srcDir.absoluteFilePath("file2_2025.csv");
    {
        QFile f(file2);
        f.open(QIODevice::WriteOnly);
        QTextStream out(&f);
        out << "\"Date\",\"Heure\",\"Fuseau horaire\",\"Nom\",\"Nom de la banque\",\"Type\",\"Etat\",\"Devise\",\"Brut\",\"Frais\",\"Net\",\"De l'adresse email\",\"A l'adresse email\",\"Nutr√©ro de transaction\",\"Description\",\"Adresse de livraison\",\"Etat de l'adresse\",\"Titre de l'objet\",\"Num√©ro de l'objet\",\"Montant des frais d'exp√©dition\",\"Montant de l'assurance\",\"TVA\",\"Option 1 - Nom\",\"Option 1 - Valeur\",\"Option 2 - Nom\",\"Option 2 - Valeur\",\"Num√©ro de r√©f√©rence\",\"Num√©ro de facture\",\"Num√©ro de client\",\"Num√©ro de t√©l√©phone\",\"Num√©ro de re√ßu\",\"Avant la commission\",\"Apr√®s la commission\",\"Solde\",\"Contact\",\"Titre de l'objet\"\n";
        out << "\"02/01/2025\",\"12:00:00\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"20,00\",\"0,00\",\"20,00\",\"\",\"\",\"TXN2\",\"Desc2\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"20,00\",\"\",\"\"\n";
        out << "\"02/01/2025\",\"12:00:01\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"21,00\",\"0,00\",\"21,00\",\"\",\"\",\"TXN2b\",\"Desc2b\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"21,00\",\"\",\"\"\n";
        out << "\"02/01/2025\",\"12:00:02\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"22,00\",\"0,00\",\"22,00\",\"\",\"\",\"TXN2c\",\"Desc2c\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"22,00\",\"\",\"\"\n";
        out << "\"02/01/2025\",\"12:00:03\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"23,00\",\"0,00\",\"23,00\",\"\",\"\",\"TXN2d\",\"Desc2d\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"23,00\",\"\",\"\"\n";
        out << "\"02/01/2025\",\"12:00:04\",\"GMT\",\"\",\"\",\"\",\"\",\"EUR\",\"24,00\",\"0,00\",\"24,00\",\"\",\"\",\"TXN2e\",\"Desc2e\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"24,00\",\"\",\"\"\n";
        f.close();
    }
    
    // Test 1: Add File and Verify Persistence
    {
        TestBankTable table(dir);
        table.addFilePaths({file1});
        
        QCOMPARE(table.rowCount(), 5);
        
        // Verify file copied to banks/2025/file1_2025.csv
        QString expectedPath = dir.filePath("banks/2025/file1_2025.csv");
        QVERIFY(QFile::exists(expectedPath));
    }
    
    // Test 2: Load from Disk
    {
        TestBankTable table(dir);
        // Initial empty
        QCOMPARE(table.rowCount(), 0);
        
        table.load(2025);
        QCOMPARE(table.rowCount(), 5);
        QCOMPARE(table.data(table.index(0, 1)).toDouble(), 10.0);
    }
    
    // Test 3: Remove by Index (and check File Deletion)
    {
        // Add second file
        TestBankTable table(dir);
        table.load(2025); // Has file1 (5 rows)
        table.addFilePaths({file2}); // Adds file2 (5 rows)
        
        QCOMPARE(table.rowCount(), 10);
        QString path1 = dir.filePath("banks/2025/file1_2025.csv");
        QString path2 = dir.filePath("banks/2025/file2_2025.csv");
        QVERIFY(QFile::exists(path1));
        QVERIFY(QFile::exists(path2));
        
        // Select ONE Row from File 2
        // File 1 rows are 0-4. File 2 rows are 5-9.
        QModelIndex idxFile2 = table.index(7, 0); // 3rd row of File 2 (22.00)
        QCOMPARE(table.data(table.index(7, 1)).toDouble(), 22.0);
        
        QList<QModelIndex> selection;
        selection << idxFile2;
        
        table.remove(selection);
        
        // Should remove ALL 5 rows from File 2.
        // Remaining: 5 rows from File 1.
        QCOMPARE(table.rowCount(), 5);
        
        // Verify File 2 deleted
        QVERIFY(!QFile::exists(path2));
        // Verify File 1 still exists
        QVERIFY(QFile::exists(path1));
    }
    
    // Test 4: Remove by FilePath
    {
         TestBankTable table(dir);
         table.load(2025); // Should only load file1 now (5 rows)
         QCOMPARE(table.rowCount(), 5);
         
         QString path1 = dir.filePath("banks/2025/file1_2025.csv");
         table.removeFile(path1);
         
         QCOMPARE(table.rowCount(), 0);
         QVERIFY(!QFile::exists(path1));
    }

}

QTEST_MAIN(TestBanks)
#include "test_banks.moc"
