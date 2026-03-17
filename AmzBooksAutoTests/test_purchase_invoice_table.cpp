#include <QtTest>
#include <QTemporaryDir>
#include <QtMath>

#include "books/PurchaseInvoiceTable.h"
#include "books/PurchaseInvoiceManager.h"
#include "books/BookAccountPurchaseTable.h"
#include "books/BooksConnections.h"
#include "ExceptionWithTitleText.h"

// Filenames used throughout the test.
static const QString kFilePln =
    "2026-01-31__622201__frais-vente-FR-CN-AEU-2026-23705__FAMZMK__FR-TVA--3.25EUR__-82.09PLN.pdf";
static const QString kFileEur =
    "2026-01-31__622201__frais-vente-FR-AEU-2026-6166__FAMZMK__FR-TVA-202.71EUR__1216.24EUR.pdf";
static const QString kFileGbp =
    "2026-01-31__622201__frais-vente-FR-AEU-2026-27277__FAMZMK__88.56GBP.pdf";

// Returns a BookAccountPurchaseTable backed by a process-lifetime temp dir.
// The table auto-fills with wildcard entries for standard VAT rates (20 %, 10 %, 5.5 %),
// which is enough for filename-parsing tests.
static const BookAccountPurchaseTable &decodeTestPurchaseTable()
{
    static QTemporaryDir s_tempDir;
    static BookAccountPurchaseTable s_table(QDir(s_tempDir.path()), "FR");
    return s_table;
}

// Column indices in AbstractBooksTable (base 9 columns, 0-based).
static const int COL_DATE         = 0;
static const int COL_AMOUNT       = 1;
static const int COL_CURRENCY     = 2;
static const int COL_LABEL        = 3;
static const int COL_ACCOUNT1     = 4;
static const int COL_ACCOUNT2     = 5;
static const int COL_VAT_ORIG     = 6;
static const int COL_VAT_COUNTRY  = 7;
static const int COL_VAT_CURRENCY = 8;
// PurchaseInvoiceTable extra columns (9, 10, 11).
static const int COL_COUNTRY_FROM = 9;
static const int COL_COUNTRY_TO   = 10;
static const int COL_VAT_RATE     = 11;

class TestPurchaseInvoiceTable : public QObject
{
    Q_OBJECT

private:
    // Create an empty file at path so PurchaseInvoiceManager::add() has a source to copy.
    static bool createDummyFile(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly);
    }

    // Return the row index whose rowId matches fileName, or -1.
    static int findRow(const PurchaseInvoiceTable &table, const QString &fileName)
    {
        for (int i = 0; i < table.rowCount(); ++i) {
            if (table.getRowId(table.index(i, 0)) == fileName) {
                return i;
            }
        }
        return -1;
    }

    // Convenience: get a display-role value from the table.
    static QVariant cell(const PurchaseInvoiceTable &table, int row, int col)
    {
        return table.data(table.index(row, col), Qt::DisplayRole);
    }

private slots:
    void test_purchase_invoice_table()
    {
        // ── Setup ─────────────────────────────────────────────────────────────
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());                                                    // 1
        const QDir dir(tempDir.path());

        // Create three dummy source files (content irrelevant – only the path matters).
        const QString src1 = dir.filePath("src1.pdf");
        const QString src2 = dir.filePath("src2.pdf");
        const QString src3 = dir.filePath("src3.pdf");
        QVERIFY(createDummyFile(src1));                                                // 2
        QVERIFY(createDummyFile(src2));                                                // 3
        QVERIFY(createDummyFile(src3));                                                // 4

        BooksConnections connections(dir);
        PurchaseInvoiceTable table(&connections, dir, "FR");

        QCOMPARE(table.rowCount(), 0);                                                 // 5

        // ── addInvoice ────────────────────────────────────────────────────────
        // File 1 – PLN total, EUR VAT (cross-currency, negative refund).
        auto infoPln = PurchaseInvoiceManager::decode(kFilePln, &decodeTestPurchaseTable(), "FR");
        table.addInvoice(src1, infoPln);
        QCOMPARE(table.rowCount(), 1);                                                 // 6
        QVERIFY(!infoPln.filePath.isEmpty());                                          // 7

        // File 2 – EUR total, EUR VAT (same currency, positive purchase).
        auto infoEur = PurchaseInvoiceManager::decode(kFileEur, &decodeTestPurchaseTable(), "FR");
        table.addInvoice(src2, infoEur);
        QCOMPARE(table.rowCount(), 2);                                                 // 8

        // File 3 – GBP total, no VAT.
        auto infoGbp = PurchaseInvoiceManager::decode(kFileGbp, &decodeTestPurchaseTable(), "FR");
        table.addInvoice(src3, infoGbp);
        QCOMPARE(table.rowCount(), 3);                                                 // 9

        // ── Model data after addInvoice – PLN invoice (negative) ──────────────
        const int rowPln = findRow(table, kFilePln);
        QVERIFY(rowPln >= 0);                                                          // 10
        QCOMPARE(table.getDate(rowPln),     QDate(2026, 1, 31));                       // 11
        QVERIFY(qAbs(table.getAmount(rowPln) - (-82.09)) < 0.001);                    // 12  – negative total
        QCOMPARE(table.getCurrency(rowPln),   QString("PLN"));                         // 13
        QCOMPARE(table.getLabel(rowPln),
                 QString("frais-vente-FR-CN-AEU-2026-23705"));                         // 14
        QCOMPARE(table.getAccount1(rowPln),   QString("622201"));                      // 15
        QCOMPARE(table.getAccount2(rowPln),   QString("FAMZMK"));                      // 16
        // VAT is stored in EUR (company currency) even though total is PLN.
        QVERIFY(qAbs(cell(table, rowPln, COL_VAT_ORIG).toDouble() - 3.25) < 0.001);  // 17
        QCOMPARE(cell(table, rowPln, COL_VAT_COUNTRY).toString(),  QString("FR"));     // 18
        // vatCurrency must be EUR so the caller knows a conversion rate is needed.
        QCOMPARE(cell(table, rowPln, COL_VAT_CURRENCY).toString(), QString("EUR"));    // 19  ← conversion rate indicator
        // Extra PurchaseInvoiceTable columns: no route code in this label/supplier.
        QCOMPARE(cell(table, rowPln, COL_COUNTRY_FROM).toString(), QString(""));       // 20
        QCOMPARE(cell(table, rowPln, COL_COUNTRY_TO).toString(),   QString("FR"));      // 21

        // ── Model data after addInvoice – EUR invoice (positive) ──────────────
        const int rowEur = findRow(table, kFileEur);
        QVERIFY(rowEur >= 0);                                                          // 22
        QCOMPARE(table.getDate(rowEur),     QDate(2026, 1, 31));                       // 23
        QVERIFY(qAbs(table.getAmount(rowEur) - 1216.24) < 0.001);                     // 24
        QCOMPARE(table.getCurrency(rowEur),   QString("EUR"));                         // 25
        QCOMPARE(table.getLabel(rowEur),
                 QString("frais-vente-FR-AEU-2026-6166"));                             // 26
        QCOMPARE(table.getAccount1(rowEur),   QString("622201"));                      // 27
        QCOMPARE(table.getAccount2(rowEur),   QString("FAMZMK"));                      // 28
        QVERIFY(qAbs(cell(table, rowEur, COL_VAT_ORIG).toDouble() - 202.71) < 0.01); // 29
        QCOMPARE(cell(table, rowEur, COL_VAT_COUNTRY).toString(),  QString("FR"));     // 30
        QCOMPARE(cell(table, rowEur, COL_VAT_CURRENCY).toString(), QString("EUR"));    // 31

        // ── Model data after addInvoice – GBP invoice (no VAT) ───────────────
        const int rowGbp = findRow(table, kFileGbp);
        QVERIFY(rowGbp >= 0);                                                          // 32
        QCOMPARE(table.getDate(rowGbp),     QDate(2026, 1, 31));                       // 33
        QVERIFY(qAbs(table.getAmount(rowGbp) - 88.56) < 0.001);                       // 34
        QCOMPARE(table.getCurrency(rowGbp),   QString("GBP"));                         // 35
        QCOMPARE(table.getLabel(rowGbp),
                 QString("frais-vente-FR-AEU-2026-27277"));                            // 36
        QCOMPARE(table.getAccount1(rowGbp),   QString("622201"));                      // 37
        QCOMPARE(table.getAccount2(rowGbp),   QString("FAMZMK"));                      // 38
        QVERIFY(qAbs(cell(table, rowGbp, COL_VAT_ORIG).toDouble()) < 0.001);          // 39  – no VAT
        QCOMPARE(cell(table, rowGbp, COL_VAT_COUNTRY).toString(),  QString(""));       // 40
        // vatCurrency falls back to invoice currency when no VAT.
        QCOMPARE(cell(table, rowGbp, COL_VAT_CURRENCY).toString(), QString("GBP"));   // 41

        // ── PurchaseInformation details (via getInvoices) ─────────────────────
        const QDate y2026s(2026, 1, 1);
        const QDate y2026e(2026, 12, 31);
        const QList<PurchaseInformation> invoices = table.getInvoices(y2026s, y2026e);
        QCOMPARE(invoices.size(), 3);                                                  // 42

        // Find each invoice by currency+total.
        const PurchaseInformation *piPln = nullptr;
        const PurchaseInformation *piEur = nullptr;
        const PurchaseInformation *piGbp = nullptr;
        for (const auto &inv : invoices) {
            if (inv.currency == "PLN") { piPln = &inv; }
            else if (inv.currency == "EUR") { piEur = &inv; }
            else if (inv.currency == "GBP") { piGbp = &inv; }
        }
        QVERIFY(piPln != nullptr);                                                     // 43
        QVERIFY(piEur != nullptr);                                                     // 44
        QVERIFY(piGbp != nullptr);                                                     // 45

        // PLN invoice: negative amounts, EUR VAT → conversion rate required.
        QVERIFY(qAbs(piPln->totalAmount - (-82.09)) < 0.001);                         // 46
        QCOMPARE(piPln->rawVatAmount,    QString("-3.25"));                            // 47  – negative VAT preserved
        QCOMPARE(piPln->vatCurrency,     QString("EUR"));                              // 48  – EUR ≠ PLN → rate needed
        QCOMPARE(piPln->vatCountry,      QString("FR"));                               // 49
        QCOMPARE(piPln->vatTokens.size(), 1);                                          // 50
        QCOMPARE(piPln->vatTokens.first(), QString("FR-TVA--3.25EUR"));               // 51
        // country_vatRate_vat must contain "FR" with a computed rate entry.
        QVERIFY(piPln->country_vatRate_vat.contains("FR"));                           // 52
        QVERIFY(!piPln->country_vatRate_vat["FR"].isEmpty());                          // 53
        // The stored VAT amount in the map is positive (abs value).
        {
            double sumVat = 0.0;
            for (const double v : piPln->country_vatRate_vat["FR"]) { sumVat += v; }
            QVERIFY(qAbs(sumVat - 3.25) < 0.001);                                     // 54
        }

        // EUR invoice: positive amounts, same currency.
        QVERIFY(qAbs(piEur->totalAmount - 1216.24) < 0.001);                          // 55
        QCOMPARE(piEur->vatCurrency,     QString("EUR"));                              // 56
        QCOMPARE(piEur->vatCountry,      QString("FR"));                               // 57
        QCOMPARE(piEur->vatTokens.size(), 1);                                          // 58
        QVERIFY(piEur->country_vatRate_vat.contains("FR"));                           // 59
        // Rate must be resolved to 0.2 (20 %).
        QVERIFY(piEur->country_vatRate_vat["FR"].contains("0.2"));                    // 60
        QVERIFY(qAbs(piEur->country_vatRate_vat["FR"]["0.2"] - 202.71) < 0.01);      // 61

        // GBP invoice: no VAT at all.
        QVERIFY(qAbs(piGbp->totalAmount - 88.56) < 0.001);                            // 62
        QVERIFY(piGbp->vatTokens.isEmpty());                                           // 63
        QVERIFY(piGbp->country_vatRate_vat.isEmpty());                                 // 64
        QVERIFY(piGbp->rawVatAmount.isEmpty());                                        // 65

        // ── Reload: new instance reads back from disk ─────────────────────────
        {
            BooksConnections connections2(dir);
            PurchaseInvoiceTable table2(&connections2, dir, "FR");
            table2.load(2026);

            QCOMPARE(table2.rowCount(), 3);                                            // 66

            // All three rows must be present.
            QVERIFY(findRow(table2, kFilePln) >= 0);                                   // 67
            QVERIFY(findRow(table2, kFileEur) >= 0);                                   // 68
            QVERIFY(findRow(table2, kFileGbp) >= 0);                                   // 69

            // Spot-check PLN row after reload: amount and vatCurrency.
            const int r = findRow(table2, kFilePln);
            QVERIFY(qAbs(table2.getAmount(r) - (-82.09)) < 0.001);                    // 70
            QCOMPARE(table2.getCurrency(r),   QString("PLN"));                         // 71
            // vatCurrency must survive the encode→decode round-trip.
            QCOMPARE(cell(table2, r, COL_VAT_CURRENCY).toString(), QString("EUR"));    // 72

            // Spot-check EUR row after reload.
            const int rEur2 = findRow(table2, kFileEur);
            QVERIFY(qAbs(table2.getAmount(rEur2) - 1216.24) < 0.001);                 // 73
            QVERIFY(qAbs(cell(table2, rEur2, COL_VAT_ORIG).toDouble() - 202.71) < 0.01); // 74

            // Spot-check GBP row after reload.
            const int rGbp2 = findRow(table2, kFileGbp);
            QVERIFY(qAbs(table2.getAmount(rGbp2) - 88.56) < 0.001);                   // 75
            QVERIFY(qAbs(cell(table2, rGbp2, COL_VAT_ORIG).toDouble()) < 0.001);      // 76
        }

        // ── removeInvoice ─────────────────────────────────────────────────────
        // Remove the GBP invoice (no VAT); verify model shrinks and disk is clean.
        {
            const int rGbpRemove = findRow(table, kFileGbp);
            QVERIFY(rGbpRemove >= 0);                                                  // 77
            table.removeInvoice(table.index(rGbpRemove, 0));
            QCOMPARE(table.rowCount(), 2);                                             // 78
            QCOMPARE(findRow(table, kFileGbp), -1);                                    // 79  – gone from model

            // Reload confirms file is gone from disk too.
            BooksConnections connections3(dir);
            PurchaseInvoiceTable table3(&connections3, dir, "FR");
            table3.load(2026);
            QCOMPARE(table3.rowCount(), 2);                                            // 80
            QCOMPARE(findRow(table3, kFileGbp), -1);                                   // 81

            // Remaining two rows are still there.
            QVERIFY(findRow(table3, kFilePln) >= 0);                                   // 82
            QVERIFY(findRow(table3, kFileEur) >= 0);                                   // 83
        }

        // ── Re-add the GBP file after removal ─────────────────────────────────
        {
            const QString src3b = dir.filePath("src3b.pdf");
            QVERIFY(createDummyFile(src3b));                                           // 84
            auto infoGbp2 = PurchaseInvoiceManager::decode(kFileGbp, &decodeTestPurchaseTable(), "FR");
            table.addInvoice(src3b, infoGbp2);
            QCOMPARE(table.rowCount(), 3);                                             // 85
            QVERIFY(findRow(table, kFileGbp) >= 0);                                    // 86
        }

        // ── columnCount ───────────────────────────────────────────────────────
        // Base 9 + 3 extra (Country From, Country To, VAT Rate).
        QCOMPARE(table.columnCount(), 12);                                             // 87

        // ── Extra columns via data() ──────────────────────────────────────────
        // PLN invoice has no route code → Country From / To are empty.
        const int rowPlnFinal = findRow(table, kFilePln);
        QVERIFY(rowPlnFinal >= 0);                                                     // 88
        QCOMPARE(cell(table, rowPlnFinal, COL_COUNTRY_FROM).toString(), QString("")); // 89
        QCOMPARE(cell(table, rowPlnFinal, COL_COUNTRY_TO).toString(),   QString("FR")); // 90
        // VAT Rate column: rate was computed as ~4.1 % (EUR vat / PLN net, unreliable
        // for cross-currency) – just verify it is non-empty and ends with "%".
        const QString rateStrPln = cell(table, rowPlnFinal, COL_VAT_RATE).toString();
        QVERIFY(!rateStrPln.isEmpty());                                                // 91
        QVERIFY(rateStrPln.endsWith('%'));                                              // 92

        // EUR invoice: same currency, rate must resolve to "20.00 %".
        const int rowEurFinal = findRow(table, kFileEur);
        QVERIFY(rowEurFinal >= 0);                                                     // 93
        QCOMPARE(cell(table, rowEurFinal, COL_VAT_RATE).toString(),
                 QString("20.00%"));                                                    // 94

        // GBP invoice (re-added): no VAT → VAT Rate column is empty.
        const int rowGbpFinal = findRow(table, kFileGbp);
        QVERIFY(rowGbpFinal >= 0);                                                     // 95
        QCOMPARE(cell(table, rowGbpFinal, COL_VAT_RATE).toString(), QString(""));     // 96

        // ── getInvoices range filter ──────────────────────────────────────────
        // No invoices outside 2026.
        QCOMPARE(table.getInvoices(QDate(2025, 1, 1), QDate(2025, 12, 31)).size(), 0); // 97
        // All three are within Jan 2026.
        QCOMPARE(table.getInvoices(QDate(2026, 1, 1), QDate(2026, 1, 31)).size(), 3); // 98

        // ── isSupplierWithCountries ───────────────────────────────────────────
        // None of the three invoices carry a country-from/to route, so FAMZMK
        // must not appear in the suppliers-with-countries set.
        QVERIFY(!table.isSupplierWithCountries("FAMZMK"));                             // 99

        // ── Table ID ─────────────────────────────────────────────────────────
        QCOMPARE(table.getId(), QString("purchase-invoices"));                         // 100
    }
};

QTEST_MAIN(TestPurchaseInvoiceTable)
#include "test_purchase_invoice_table.moc"
