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

class TestBanks : public QObject
{
    Q_OBJECT

private slots:
    void testPaypalTheoretical();
    void testStripeTheoretical();
    void testWiseTheoretical();
    void testQontoTheoretical();
    void testRealData();
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

QTEST_MAIN(TestBanks)
#include "test_banks.moc"
