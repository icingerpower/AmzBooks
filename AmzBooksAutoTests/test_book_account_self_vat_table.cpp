#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QSignalSpy>

#include "books/BookAccountSelfVatTable.h"

class TestBookAccountSelfVatTable : public QObject
{
    Q_OBJECT

private:
    void injectFakeColumn(const QString &filePath)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            QFAIL("Failed to open file for injection");
        QString content = file.readAll();
        file.close();

        QStringList lines = content.split('\n');
        if (lines.isEmpty())
            QFAIL("Empty file");

        QStringList headers = lines[0].split(';');
        headers.insert(1, "FakeColumn");
        lines[0] = headers.join(';');

        for (int i = 1; i < lines.size(); ++i) {
            if (lines[i].trimmed().isEmpty()) continue;
            QStringList parts = lines[i].split(';');
            parts.insert(1, "FakeValue");
            lines[i] = parts.join(';');
        }

        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            QFAIL("Failed to save injected file");
        QTextStream out(&file);
        out << lines.join('\n');
    }

private slots:

    // -------------------------------------------------------------------------
    // Structure & Default Values
    // -------------------------------------------------------------------------

    void test_defaultValues()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        QCOMPARE(table.rowCount(), 2);
        QCOMPARE(table.columnCount(), 3);

        // EU row (index 0)
        QCOMPARE(table.data(table.index(0, 0)).toString(), QString("EU"));
        QCOMPARE(table.data(table.index(0, 1)).toString(), QString("445662"));
        QCOMPARE(table.data(table.index(0, 2)).toString(), QString("445200"));

        // non-EU row (index 1)
        QCOMPARE(table.data(table.index(1, 0)).toString(), QString("non-EU"));
        QCOMPARE(table.data(table.index(1, 1)).toString(), QString("445663"));
        QCOMPARE(table.data(table.index(1, 2)).toString(), QString("445300"));

        // CSV file must be created on construction
        QVERIFY(QFile::exists(QDir(tempDir.path()).filePath("selfVatPurchaseAccount.csv")));
    }

    void test_headerData()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        QCOMPARE(table.headerData(0, Qt::Horizontal).toString(), QString("Country"));
        QCOMPARE(table.headerData(1, Qt::Horizontal).toString(), QString("Deductible VAT"));
        QCOMPARE(table.headerData(2, Qt::Horizontal).toString(), QString("Due VAT"));

        // Out-of-range column returns invalid variant
        QVERIFY(!table.headerData(3, Qt::Horizontal).isValid());

        // Vertical orientation not provided → invalid
        QVERIFY(!table.headerData(0, Qt::Vertical).isValid());

        // Wrong role → invalid
        QVERIFY(!table.headerData(0, Qt::Horizontal, Qt::DecorationRole).isValid());
    }

    void test_invalidIndexData()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // Invalid index
        QVERIFY(!table.data(QModelIndex()).isValid());

        // Row out of range
        QVERIFY(!table.data(table.index(2, 0)).isValid());
        QVERIFY(!table.data(table.index(-1, 0)).isValid());

        // Column out of range
        QVERIFY(!table.data(table.index(0, 3)).isValid());
    }

    void test_rowColumnCountWithParent()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // With a valid parent, both counts must be 0 (tree model convention)
        QCOMPARE(table.rowCount(table.index(0, 0)), 0);
        QCOMPARE(table.columnCount(table.index(0, 0)), 0);
    }

    // -------------------------------------------------------------------------
    // Flags / Editability
    // -------------------------------------------------------------------------

    void test_flags()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // Column 0 (Country label) must NOT be editable for either row
        QVERIFY(!(table.flags(table.index(0, 0)) & Qt::ItemIsEditable));
        QVERIFY(!(table.flags(table.index(1, 0)) & Qt::ItemIsEditable));

        // Columns 1 and 2 MUST be editable for both rows
        QVERIFY(table.flags(table.index(0, 1)) & Qt::ItemIsEditable);
        QVERIFY(table.flags(table.index(0, 2)) & Qt::ItemIsEditable);
        QVERIFY(table.flags(table.index(1, 1)) & Qt::ItemIsEditable);
        QVERIFY(table.flags(table.index(1, 2)) & Qt::ItemIsEditable);

        // Invalid index returns NoItemFlags
        QCOMPARE(table.flags(QModelIndex()), Qt::NoItemFlags);
    }

    // -------------------------------------------------------------------------
    // setData
    // -------------------------------------------------------------------------

    void test_setData()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // Column 0 is read-only
        QVERIFY(!table.setData(table.index(0, 0), "EU-modified", Qt::EditRole));
        QCOMPARE(table.data(table.index(0, 0)).toString(), QString("EU")); // unchanged

        // Column 1 (deductible) is writable for EU row
        QVERIFY(table.setData(table.index(0, 1), "111111", Qt::EditRole));
        QCOMPARE(table.data(table.index(0, 1)).toString(), QString("111111"));

        // Column 2 (due) is writable for non-EU row
        QVERIFY(table.setData(table.index(1, 2), "222222", Qt::EditRole));
        QCOMPARE(table.data(table.index(1, 2)).toString(), QString("222222"));

        // Setting the same value returns false (no-op)
        QVERIFY(!table.setData(table.index(0, 1), "111111", Qt::EditRole));

        // Invalid index returns false
        QVERIFY(!table.setData(QModelIndex(), "x", Qt::EditRole));
    }

    void test_setData_dataChangedSignal()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        QSignalSpy spy(&table, &BookAccountSelfVatTable::dataChanged);

        // A genuine change emits dataChanged
        table.setData(table.index(0, 1), "NEW_ACCOUNT", Qt::EditRole);
        QCOMPARE(spy.count(), 1);

        // Same value again does NOT emit
        table.setData(table.index(0, 1), "NEW_ACCOUNT", Qt::EditRole);
        QCOMPARE(spy.count(), 1);

        // Attempt to edit column 0 does NOT emit
        table.setData(table.index(0, 0), "X", Qt::EditRole);
        QCOMPARE(spy.count(), 1);

        // Changing non-EU row also emits
        table.setData(table.index(1, 2), "NEW_DUE", Qt::EditRole);
        QCOMPARE(spy.count(), 2);
    }

    // -------------------------------------------------------------------------
    // Routing logic — company = FR
    // -------------------------------------------------------------------------

    void test_routing_FR_intracom()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // EU member → company country: intracom → uses EU row
        QCOMPARE(table.getAccountVatDeductible("DE", "FR"), QString("445662"));
        QCOMPARE(table.getAccountVatDue("DE",             "FR"), QString("445200"));

        QCOMPARE(table.getAccountVatDeductible("IT", "FR"), QString("445662"));
        QCOMPARE(table.getAccountVatDue("IT",             "FR"), QString("445200"));

        QCOMPARE(table.getAccountVatDeductible("ES", "FR"), QString("445662"));
        QCOMPARE(table.getAccountVatDeductible("PL", "FR"), QString("445662"));
        QCOMPARE(table.getAccountVatDeductible("BE", "FR"), QString("445662"));
        QCOMPARE(table.getAccountVatDeductible("NL", "FR"), QString("445662"));
    }

    void test_routing_FR_extracom()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // Non-EU → company country: extracom → uses non-EU row
        QCOMPARE(table.getAccountVatDeductible("CN", "FR"), QString("445663"));
        QCOMPARE(table.getAccountVatDue("CN",             "FR"), QString("445300"));

        QCOMPARE(table.getAccountVatDeductible("US", "FR"), QString("445663"));
        QCOMPARE(table.getAccountVatDue("US",             "FR"), QString("445300"));

        QCOMPARE(table.getAccountVatDeductible("CH", "FR"), QString("445663")); // Switzerland is not EU
        QCOMPARE(table.getAccountVatDeductible("JP", "FR"), QString("445663"));
        QCOMPARE(table.getAccountVatDeductible("BR", "FR"), QString("445663"));
    }

    void test_routing_FR_none()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // Third-party: countryTo != FR → returns empty
        QVERIFY(table.getAccountVatDeductible("CN", "DE").isEmpty());
        QVERIFY(table.getAccountVatDue("CN",         "DE").isEmpty());
        QVERIFY(table.getAccountVatDeductible("DE", "IT").isEmpty());
        QVERIFY(table.getAccountVatDue("DE",         "IT").isEmpty());

        // Domestic: FR → FR → no self-VAT → returns empty
        QVERIFY(table.getAccountVatDeductible("FR", "FR").isEmpty());
        QVERIFY(table.getAccountVatDue("FR",         "FR").isEmpty());

        // Outbound: company sells abroad → countryTo != FR → empty
        QVERIFY(table.getAccountVatDeductible("FR", "DE").isEmpty());
        QVERIFY(table.getAccountVatDue("FR",         "DE").isEmpty());
    }

    void test_routing_gbPostBrexit()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // GB left the EU on 2020-12-31; current date is 2026 → non-EU row
        const QString deductible = table.getAccountVatDeductible("GB", "FR");
        const QString due        = table.getAccountVatDue("GB",        "FR");

        QVERIFY(!deductible.isEmpty());
        QVERIFY(!due.isEmpty());

        // Must match the non-EU row defaults
        QCOMPARE(deductible, QString("445663"));
        QCOMPARE(due,        QString("445300"));
    }

    // -------------------------------------------------------------------------
    // Routing logic — company = DE
    // -------------------------------------------------------------------------

    void test_routing_companyDE()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "DE");

        // Intracom: EU member → DE
        QCOMPARE(table.getAccountVatDeductible("FR", "DE"), QString("445662"));
        QCOMPARE(table.getAccountVatDue("FR",             "DE"), QString("445200"));

        // Extracom: non-EU → DE
        QCOMPARE(table.getAccountVatDeductible("CN", "DE"), QString("445663"));
        QCOMPARE(table.getAccountVatDue("CN",             "DE"), QString("445300"));

        // Domestic: DE → DE → empty
        QVERIFY(table.getAccountVatDeductible("DE", "DE").isEmpty());
        QVERIFY(table.getAccountVatDue("DE",         "DE").isEmpty());

        // Third-party: CN → FR (company is DE, not FR) → empty
        QVERIFY(table.getAccountVatDeductible("CN", "FR").isEmpty());
        QVERIFY(table.getAccountVatDue("CN",         "FR").isEmpty());
    }

    // -------------------------------------------------------------------------
    // Custom accounts are reflected in routing
    // -------------------------------------------------------------------------

    void test_customAccounts_routingUsesEditedValues()
    {
        QTemporaryDir tempDir;
        BookAccountSelfVatTable table(QDir(tempDir.path()), "FR");

        // Customise EU row
        table.setData(table.index(0, 1), "EU_DED", Qt::EditRole);
        table.setData(table.index(0, 2), "EU_DUE", Qt::EditRole);

        // Customise non-EU row
        table.setData(table.index(1, 1), "NONEU_DED", Qt::EditRole);
        table.setData(table.index(1, 2), "NONEU_DUE", Qt::EditRole);

        // Intracom → EU custom values
        QCOMPARE(table.getAccountVatDeductible("DE", "FR"), QString("EU_DED"));
        QCOMPARE(table.getAccountVatDue("DE",             "FR"), QString("EU_DUE"));

        // Extracom → non-EU custom values
        QCOMPARE(table.getAccountVatDeductible("CN", "FR"), QString("NONEU_DED"));
        QCOMPARE(table.getAccountVatDue("CN",             "FR"), QString("NONEU_DUE"));

        // Third-party → still empty
        QVERIFY(table.getAccountVatDeductible("CN", "DE").isEmpty());
    }

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    void test_persistence()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());

        // 1. Modify both rows and implicitly save
        {
            BookAccountSelfVatTable table(dir, "FR");
            QVERIFY(table.setData(table.index(0, 1), "SAVE_EU_DED",   Qt::EditRole));
            QVERIFY(table.setData(table.index(0, 2), "SAVE_EU_DUE",   Qt::EditRole));
            QVERIFY(table.setData(table.index(1, 1), "SAVE_NON_DED",  Qt::EditRole));
            QVERIFY(table.setData(table.index(1, 2), "SAVE_NON_DUE",  Qt::EditRole));
        }

        // 2. Reload from disk and verify all values survived
        {
            BookAccountSelfVatTable table(dir, "FR");
            QCOMPARE(table.rowCount(), 2);
            QCOMPARE(table.data(table.index(0, 1)).toString(), QString("SAVE_EU_DED"));
            QCOMPARE(table.data(table.index(0, 2)).toString(), QString("SAVE_EU_DUE"));
            QCOMPARE(table.data(table.index(1, 1)).toString(), QString("SAVE_NON_DED"));
            QCOMPARE(table.data(table.index(1, 2)).toString(), QString("SAVE_NON_DUE"));

            // Routing uses the persisted accounts
            QCOMPARE(table.getAccountVatDeductible("IT", "FR"), QString("SAVE_EU_DED"));
            QCOMPARE(table.getAccountVatDue("US",        "FR"), QString("SAVE_NON_DUE"));
        }
    }

    // -------------------------------------------------------------------------
    // Robustness: extra column injected into CSV
    // -------------------------------------------------------------------------

    void test_robustness_fakeColumn()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        const QString csvPath = dir.filePath("selfVatPurchaseAccount.csv");

        // Initial save
        {
            BookAccountSelfVatTable table(dir, "FR");
            QCOMPARE(table.rowCount(), 2);
        }

        // Inject a spurious column between Country and DeductibleVat
        injectFakeColumn(csvPath);

        // Reload must still recover correct data
        {
            BookAccountSelfVatTable table(dir, "FR");
            QCOMPARE(table.rowCount(), 2);
            QCOMPARE(table.data(table.index(0, 0)).toString(), QString("EU"));
            QCOMPARE(table.data(table.index(0, 1)).toString(), QString("445662"));
            QCOMPARE(table.data(table.index(0, 2)).toString(), QString("445200"));
            QCOMPARE(table.data(table.index(1, 0)).toString(), QString("non-EU"));
        }
    }

    // -------------------------------------------------------------------------
    // CSV file content
    // -------------------------------------------------------------------------

    void test_csvFileContent()
    {
        QTemporaryDir tempDir;
        QDir dir(tempDir.path());
        const QString csvPath = dir.filePath("selfVatPurchaseAccount.csv");

        { BookAccountSelfVatTable table(dir, "FR"); }

        QVERIFY(QFile::exists(csvPath));

        QFile file(csvPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream in(&file);
        const QString header = in.readLine();

        // All three column IDs must be present in the header
        QVERIFY(header.contains("Country"));
        QVERIFY(header.contains("DeductibleVat"));
        QVERIFY(header.contains("DueVat"));
    }
};

QTEST_MAIN(TestBookAccountSelfVatTable)
#include "test_book_account_self_vat_table.moc"
