#include <QtTest>
#include <QSignalSpy>
#include "books/EntrySelfTable.h"
#include "books/BooksConnections.h"
#include "books/AbstractBooksTable.h"
#include "books/AbstractBooksTableBank.h"
#include "books/ExceptionBookEquality.h"

class ConcreteBooksTable : public AbstractBooksTable {
public:
    ConcreteBooksTable(const BooksConnections *connections, const QDir &workingDir) 
        : AbstractBooksTable(connections, workingDir) {}
    QString getId() const override { return "Concrete"; }
};

class ConcreteBooksTableBank : public AbstractBooksTableBank {
public:
    ConcreteBooksTableBank(const BooksConnections *connections, const QDir &workingDir) 
        : AbstractBooksTableBank(connections, workingDir) {}
    
    // AbstractBooksTable methods
    QString getId() const override { return "ConcreteBank"; }

    // AbstractBooksTableBank methods
    const AbstractBankStatement *getBankStatement() const override { return nullptr; }
};

class TestBooksConnection : public QObject
{
    Q_OBJECT

private slots:
    void test_save_load();
    void test_setData();
    void test_connect_disconnect();
    void test_tryToConnect_Situations();
};

void TestBooksConnection::test_save_load()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());

    // 1. Create table and add rows
    {
        EntrySelfTable table(dir);
        table.addRow({"Label1", "Account1"});
        table.addRow({"Label2", "Account2"});
        
        QCOMPARE(table.rowCount(), 2);
        
        // Check data in memory
        QModelIndex idx0_0 = table.index(0, 0);
        QModelIndex idx0_1 = table.index(0, 1);
        QModelIndex idx1_0 = table.index(1, 0);
        QModelIndex idx1_1 = table.index(1, 1);

        // Newer items are inserted at 0
        QCOMPARE(table.data(idx0_0).toString(), "Label2");
        QCOMPARE(table.data(idx0_1).toString(), "Account2");
        QCOMPARE(table.data(idx1_0).toString(), "Label1");
        QCOMPARE(table.data(idx1_1).toString(), "Account1");
    }

    // 2. Reload in a new instance
    {
        EntrySelfTable table(dir);
        QCOMPARE(table.rowCount(), 2);

        QModelIndex idx0_0 = table.index(0, 0);
        QModelIndex idx0_1 = table.index(0, 1);
        QModelIndex idx1_0 = table.index(1, 0);
        QModelIndex idx1_1 = table.index(1, 1);
        
        QCOMPARE(table.data(idx0_0).toString(), "Label2");
        QCOMPARE(table.data(idx0_1).toString(), "Account2");
        QCOMPARE(table.data(idx1_0).toString(), "Label1");
        QCOMPARE(table.data(idx1_1).toString(), "Account1");
    }
}

void TestBooksConnection::test_setData()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());

    // 1. Create table and add a row
    {
        EntrySelfTable table(dir);
        table.addRow({"OriginalLabel", "OriginalAccount"});
        
        QCOMPARE(table.rowCount(), 1);
        QModelIndex idx0_0 = table.index(0, 0); // Label
        QModelIndex idx0_1 = table.index(0, 1); // Account

        QCOMPARE(table.data(idx0_0).toString(), "OriginalLabel");
        
        // 2. Modify data
        table.setData(idx0_0, "NewLabel", Qt::EditRole);
        table.setData(idx0_1, "NewAccount", Qt::EditRole);
        
        QCOMPARE(table.data(idx0_0).toString(), "NewLabel");
        QCOMPARE(table.data(idx0_1).toString(), "NewAccount");
    }

    // 3. Reload to verify persistence
    {
        EntrySelfTable table(dir);
        QCOMPARE(table.rowCount(), 1);
        QModelIndex idx0_0 = table.index(0, 0);
        QModelIndex idx0_1 = table.index(0, 1);

        QCOMPARE(table.data(idx0_0).toString(), "NewLabel");
        QCOMPARE(table.data(idx0_1).toString(), "NewAccount");
    }
}

void TestBooksConnection::test_connect_disconnect()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());

    // 1. Setup tables
    BooksConnections connections(dir);
    ConcreteBooksTable booksTable(&connections, dir);
    booksTable.add("BOOK_ID_1", QDate::currentDate(), 100.0, "EUR", "Label", "Acc1", "Acc2", 0, "", "");
    
    EntrySelfTable selfTable(dir);
    selfTable.addRow({"SelfLabel", "SelfAccount"});
    
    // 2. Connect
    QModelIndex bookIdx = booksTable.index(0, 0);
    QModelIndex selfIdx = selfTable.index(0, 0);
    
    connections.tryToConnect(&booksTable, bookIdx, &selfTable, selfIdx);
    
    // 3. Reload
    {
        BooksConnections connections2(dir);
        connections2.disconnect(&booksTable, bookIdx);
    }
    
     // 4. Check if disconnected
     QFile file(dir.absoluteFilePath("booksConnections.csv"));
     QVERIFY(file.exists());
     if (file.open(QIODevice::ReadOnly)) {
         auto content = file.readAll();
     }
}

// ... (existing test methods) ...

void TestBooksConnection::test_tryToConnect_Situations()
{
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());
    BooksConnections connections(dir);
    
    ConcreteBooksTable bookTable(&connections, dir);
    ConcreteBooksTableBank bankTable(&connections, dir);
    
    // Add data to tables. 
    // Format: date, amount (orig), currency, label, acc1, acc2, vat...
    
    // Row 0: EUR 100
    bookTable.add("BOOK_100_EUR", QDate::currentDate(), 100.0, "EUR", "Label", "", "", 0, "", "");
    bankTable.add("BANK_100_EUR", QDate::currentDate(), 100.0, "EUR", "Label", "", "", 0, "", "");
    
    // Row 1: EUR 200
    bookTable.add("BOOK_200_EUR", QDate::currentDate(), 200.0, "EUR", "Label", "", "", 0, "", "");
    bankTable.add("BANK_200_EUR", QDate::currentDate(), 200.0, "EUR", "Label", "", "", 0, "", "");
    
    // Row 2: USD 110
    bankTable.add("BANK_110_USD", QDate::currentDate(), 110.0, "USD", "Label", "", "", 0, "", "");
    
    // Row 3: USD 100
    bankTable.add("BANK_100_USD", QDate::currentDate(), 100.0, "USD", "Label", "", "", 0, "", "");

    // Row 4: Negative amounts
    bookTable.add("BOOK_NEG_100", QDate::currentDate(), -100.0, "EUR", "Label", "", "", 0, "", "");
    bankTable.add("BANK_NEG_100", QDate::currentDate(), -100.0, "EUR", "Label", "", "", 0, "", "");
    
    // Test 1: Same Currency, Equal Amounts (100 vs 100) -> Success
    {
        connections.tryToConnect(&bookTable, bookTable.index(0, 0), &bankTable, bankTable.index(0, 0));
        QVERIFY(connections.contains("Concrete", "BOOK_100_EUR"));
    }
    
    // Test 2: Same Currency, Unequal Amounts (100 vs 200) -> Exception
    {
        bool exceptionThrown = false;
        try {
            connections.tryToConnect(&bookTable, bookTable.index(0, 0), &bankTable, bankTable.index(1, 0));
        } catch (const ExceptionBookEquality &) {
            exceptionThrown = true;
        }
        QVERIFY(exceptionThrown);
    }
    
    // Test 3: Different Currency, Rate Matches (100 EUR vs 110 USD, Rate 1.1) -> Success
    // Logic: abs(Left) - abs(Right * Rate) < tolerance
    // 100 - (110 * ?) 
    // Wait, let's verify logic in implementation: 
    // amountDiff = std::abs(amountLeft) - std::abs(amountRight * currencyRate);
    // If Left=100, Right=110. 
    // If Rate converts Right->Left (USD->EUR). 110 USD * (1/1.1) ~ 100 EUR. 
    // So Rate should be ~0.909.
    // IF Rate converts Left->Right (EUR->USD). 100 EUR = 110 USD.
    // Usage: tryToConnect(left, ..., right, ..., rate).
    // If implementation is: abs(Left) - abs(Right * Rate).
    // Then Rate must convert Right currency to Left currency unit size? Or value?
    // AmountLeft (in EUR) - AmountRight (in USD) * Rate. 
    // To match, AmountRight * Rate should be ~ AmountLeft.
    // 110 * Rate = 100 => Rate = 100/110 = 0.909.
    // So if I pass 0.909, it should match. 
    // If I pass 1.1? 100 - 110*1.1 = 100 - 121 != 0.
    
    // Let's assume user might pass "1.1" meaning 1 EUR = 1.1 USD.
    // If so, 100 EUR = 110 USD.
    // If implementation expects abs(Left) - abs(Right * Rate).
    // Then we need 100 - 110 * Rate = 0. => Rate = 0.909.
    // If user passes 1.1 (EUR->USD rate), then logic should probably be:
    // abs(Left * Rate) - abs(Right).
    // 100 * 1.1 - 110 = 0.
    //
    // Let's check implementation I wrote:
    // amountDiff = std::abs(amountLeft) - std::abs(amountRight * currencyRate);
    // This implies Rate converts Right to Left. (USD to EUR).
    // If user usually thinks in "Exchange Rate to target". 
    // If Bank is Foreign (USD) and Book is Local (EUR).
    // Rate USD -> EUR. 
    //
    // Let's test with 0.909 (1/1.1).
    {
         connections.tryToConnect(&bookTable, bookTable.index(0, 0), &bankTable, bankTable.index(2, 0), 100.0/110.0);
         // Should succeed.
    }
    // And test with 1.1 to confirm failure if Rate is inverted direction.
    {
         bool exceptionThrown = false;
         try {
             connections.tryToConnect(&bookTable, bookTable.index(0, 0), &bankTable, bankTable.index(2, 0), 1.1);
         } catch (const ExceptionBookEquality &) {
             exceptionThrown = true;
         }
         QVERIFY(exceptionThrown);
    }
    
    // Test 4: Different Currency, Rate Matches Mismatched amounts
    // 100 EUR vs 100 USD. Rate 0.909.
    // 100 EUR vs (100 USD * 0.909 = 90.9 EUR). Diff ~10. Exception.
    {
         bool exceptionThrown = false;
         try {
             connections.tryToConnect(&bookTable, bookTable.index(0, 0), &bankTable, bankTable.index(3, 0), 100.0/110.0);
         } catch (const ExceptionBookEquality &) {
             exceptionThrown = true;
         }
         QVERIFY(exceptionThrown);
    }
    
    // Test 5: Close amounts (within 1%)
    // 100.0 vs 100.9 (Diff 0.9 < 1.0). Should Success.
    bookTable.add("BOOK_100_9", QDate::currentDate(), 100.9, "EUR", "Label", "", "", 0, "", "");
    {
        connections.tryToConnect(&bookTable, bookTable.index(0, 0), &bankTable, bankTable.index(0, 0)); // 100 vs 100 ok.
        
        // Connect 100 (Bank) with 100.9 (Book)
        // Left=100.9 (Row 3), Right=100 (Row 0). Diff=0.9. Max(100.9, 100)=100.9. 1% = 1.009.
        // 0.9 < 1.009. OK.
        connections.tryToConnect(&bookTable, bookTable.index(3, 0), &bankTable, bankTable.index(0, 0));
    }
    
    // Test 6: Negative vs Positive (match absolute)
    // -100 vs -100. abs(-100) - abs(-100) = 0. OK.
    // bookTable Row 2 is NEG_100. bankTable Row 4 is NEG_100.
    {
        connections.tryToConnect(&bookTable, bookTable.index(2, 0), &bankTable, bankTable.index(4, 0));
    }
    // -100 vs 100. abs(-100) - abs(100) = 0. OK.
    // bookTable Row 2 (-100). bankTable Row 0 (100).
    {
        connections.tryToConnect(&bookTable, bookTable.index(2, 0), &bankTable, bankTable.index(0, 0));
    }
}

QTEST_MAIN(TestBooksConnection)
#include "test_books_connections.moc"
