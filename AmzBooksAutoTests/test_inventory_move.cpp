// test_inventory_move.cpp
// Unit tests for InventoryMoveTree / InventoryMoveTreeItem / PurchaseCsvLoader
// 16 test slots

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cmath>

#include "inventory/InventoryMoveTree.h"
#include "inventory/PurchaseCsvLoader.h"
#include "CurrencyRateManager.h"

// ---------------------------------------------------------------------------
// Helper: tolerance comparison for doubles
// ---------------------------------------------------------------------------
static bool nearlyEqual(double a, double b, double eps = 0.005)
{
    return std::abs(a - b) < eps;
}

class TestInventoryMove : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 10 test slots
    void test_basic_structure();            // tree shape, column count, headers
    void test_imports_and_exports();        // from/to directions
    void test_multi_country();              // FR + DE + GB rows
    void test_units_and_prices();           // child units, prices, total
    void test_parent_aggregation();         // parent totals and weighted avg price
    void test_currency_conversion();        // USD/GBP → EUR via CurrencyRateManager
    void test_shipping_cost();              // weight × pricePerKilo added to unit price
    void test_no_invoice_found();           // missing SKU gets "No invoice found" label
    void test_language_priority_title();    // FR file title wins over US/CA
    void test_sort();                       // sort parent rows by units asc/desc
    void test_amazon_format_headers();      // real Amazon CSV format: lowercase/hyphenated headers
    void test_orig_unit_price_and_currency(); // COL_ORIG_AMOUNT = unit price; COL_CURRENCY never empty
    void test_dialog_view_orders_currency();  // regression: Currency=EUR even when passed as string (no CompanyInfosTable)
    void test_parent_aggregation_weighted(); // parent total = sum of children; unit price = weighted avg
    void test_csv_loader_records_order_and_fields(); // parseFiles returns records newest-first, all fields set
    void test_csv_loader_latest_vs_fifo();   // latest-price policy vs FIFO batch policy on same records
    void test_csv_loader_purchase_date_rate(); // purchase-file date drives conversion, not "today's" rate

private:
    QDir m_testDir;
    QDir m_purchasesDir;

    // Write a Latin-1 CSV file (purchase invoice format used everywhere in the codebase).
    static void writeCsv(const QString &path, const QString &content);

    // Setup the shared company infos + currency rates files in m_testDir.
    void setupCompanyAndRates();

    // Build a minimal InventoryMoveTree for tests that don't need purchases dir.
    InventoryMoveTree *makeTree(
            const QHash<QString, QHash<QString, int>> &imported,
            const QHash<QString, QHash<QString, int>> &exported,
            const QHash<QString, double> &pricePerKilo = {},
            const QString &companyCurrency = QString(),
            const CurrencyRateManager *rates = nullptr);

    // Find the row index of a top-level parent item with the given from/to.
    // Returns -1 if not found.
    static int findParentRow(InventoryMoveTree &model,
                             const QString &from, const QString &to);

    // Convenience: data(parentRow, column) at the top level.
    static QVariant parentData(InventoryMoveTree &model, int parentRow, int col);

    // Convenience: data(childRow, column) under the given parent row.
    static QVariant childData(InventoryMoveTree &model,
                              int parentRow, int childRow, int col);

    // Find child row for a given SKU under parentRow. Returns -1 if not found.
    static int findChildRow(InventoryMoveTree &model, int parentRow, const QString &sku);
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void TestInventoryMove::writeCsv(const QString &path, const QString &content)
{
    QFile f(path);
    QFileInfo fi(f);
    fi.absoluteDir().mkpath(QStringLiteral("."));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Latin1);
        out << content;
        f.close();
    }
}

void TestInventoryMove::setupCompanyAndRates()
{
    // company.csv: company is in France, currency is EUR
    writeCsv(m_testDir.filePath(QStringLiteral("company.csv")),
             QStringLiteral("ID;Parameter;Value\n"
                            "country;Country Code;FR\n"
                            "currency;Currency Code;EUR\n"));

    // currency-rates.csv: 1 USD = 0.9 EUR, 1 GBP = 1.15 EUR
    // We use one representative date that matches our purchase filenames.
    writeCsv(m_testDir.filePath(QStringLiteral("currency-rates.csv")),
             QStringLiteral("Date,Source,Dest,Rate\n"
                            "2025-06-01,USD,EUR,0.9\n"
                            "2025-06-01,GBP,EUR,1.15\n"
                            "2025-06-15,USD,EUR,0.9\n"
                            "2025-06-15,GBP,EUR,1.15\n"
                            "2025-07-01,USD,EUR,0.9\n"
                            "2025-07-01,GBP,EUR,1.15\n"));
}

InventoryMoveTree *TestInventoryMove::makeTree(
        const QHash<QString, QHash<QString, int>> &imported,
        const QHash<QString, QHash<QString, int>> &exported,
        const QHash<QString, double> &pricePerKilo,
        const QString &companyCurrency,
        const CurrencyRateManager *rates)
{
    return new InventoryMoveTree(m_purchasesDir, imported, exported,
                                 pricePerKilo, companyCurrency, rates, QDir(), QString(), this);
}

int TestInventoryMove::findParentRow(InventoryMoveTree &model,
                                     const QString &from, const QString &to)
{
    for (int r = 0; r < model.rowCount(); ++r) {
        if (model.data(model.index(r, InventoryMoveTree::COL_FROM)).toString() == from
                && model.data(model.index(r, InventoryMoveTree::COL_TO)).toString() == to)
            return r;
    }
    return -1;
}

QVariant TestInventoryMove::parentData(InventoryMoveTree &model, int parentRow, int col)
{
    return model.data(model.index(parentRow, col));
}

QVariant TestInventoryMove::childData(InventoryMoveTree &model,
                                       int parentRow, int childRow, int col)
{
    QModelIndex pIdx = model.index(parentRow, 0);
    return model.data(model.index(childRow, col, pIdx));
}

int TestInventoryMove::findChildRow(InventoryMoveTree &model,
                                    int parentRow, const QString &sku)
{
    QModelIndex pIdx = model.index(parentRow, 0);
    for (int r = 0; r < model.rowCount(pIdx); ++r) {
        QModelIndex cIdx = model.index(r, InventoryMoveTree::COL_SKU, pIdx);
        if (model.data(cIdx).toString() == sku)
            return r;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// initTestCase / cleanupTestCase
// ---------------------------------------------------------------------------

void TestInventoryMove::initTestCase()
{
    m_testDir = QDir::current();
    if (m_testDir.exists(QStringLiteral("test_inv_move_env")))
        QDir(m_testDir.filePath(QStringLiteral("test_inv_move_env"))).removeRecursively();
    m_testDir.mkdir(QStringLiteral("test_inv_move_env"));
    m_testDir.cd(QStringLiteral("test_inv_move_env"));

    m_purchasesDir = QDir(m_testDir.filePath(QStringLiteral("purchases")));
    m_purchasesDir.mkpath(QStringLiteral("."));

    setupCompanyAndRates();
}

void TestInventoryMove::cleanupTestCase()
{
    if (m_testDir.exists())
        m_testDir.removeRecursively();
}

// ===========================================================================
// Test 1 – basic_structure
// 13 assertions: tree shape, column count, header labels, index validity.
// ===========================================================================
void TestInventoryMove::test_basic_structure()
{
    // Import 2 SKUs into FR
    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_A")] = 10;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_B")] = 5;

    auto *model = makeTree(imported, {});

    // [1] exactly 1 top-level parent row
    QCOMPARE(model->rowCount(), 1);

    // [2] 8 columns (COL_COUNT)
    QCOMPARE(model->columnCount(), static_cast<int>(InventoryMoveTree::COL_COUNT));

    // [3] header "From"
    QCOMPARE(model->headerData(InventoryMoveTree::COL_FROM, Qt::Horizontal).toString(),
             QStringLiteral("From"));

    // [4] header "Purchase File"
    QCOMPARE(model->headerData(InventoryMoveTree::COL_PURCHASE_FILE, Qt::Horizontal).toString(),
             QStringLiteral("Purchase File"));

    // [5] top-level index is valid
    QVERIFY(model->index(0, 0).isValid());

    // [6] top-level item has no parent (invalid parent index)
    QVERIFY(!model->parent(model->index(0, 0)).isValid());

    // [7] 2 children under the parent
    QModelIndex pIdx = model->index(0, 0);
    QCOMPARE(model->rowCount(pIdx), 2);

    // [8] children have a valid parent index
    QModelIndex cIdx = model->index(0, 0, pIdx);
    QVERIFY(model->parent(cIdx).isValid());

    // [9] column count consistent from parent index
    QCOMPARE(model->columnCount(pIdx), static_cast<int>(InventoryMoveTree::COL_COUNT));

    // [10] invalid index returns empty QVariant
    QVERIFY(!model->data(QModelIndex()).isValid());

    // [11] header "Currency"
    QCOMPARE(model->headerData(InventoryMoveTree::COL_CURRENCY, Qt::Horizontal).toString(),
             QStringLiteral("Currency"));

    // [12] header "Orig Unit Price" (unit price in original invoice currency before conversion)
    QCOMPARE(model->headerData(InventoryMoveTree::COL_ORIG_AMOUNT, Qt::Horizontal).toString(),
             QStringLiteral("Orig Unit Price"));

    // [13] header "Orig currency"
    QCOMPARE(model->headerData(InventoryMoveTree::COL_ORIG_CURRENCY, Qt::Horizontal).toString(),
             QStringLiteral("Orig currency"));

    delete model;
}

// ===========================================================================
// Test 2 – imports_and_exports
// 10 assertions: from/to directions are correct for both import and export rows.
// ===========================================================================
void TestInventoryMove::test_imports_and_exports()
{
    QHash<QString, QHash<QString, int>> imported, exported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_A")] = 10;
    exported[QStringLiteral("FR")][QStringLiteral("SKU_B")] = 7;

    auto *model = makeTree(imported, exported);

    // [1] 2 top-level rows
    QCOMPARE(model->rowCount(), 2);

    // Find the import row (EU→FR)
    int impRow = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    // [2] import row found
    QVERIFY(impRow != -1);

    // Find the export row (FR→EU)
    int expRow = findParentRow(*model, QStringLiteral("FR"), QStringLiteral("EU"));
    // [3] export row found
    QVERIFY(expRow != -1);

    // [4] import from = "EU"
    QCOMPARE(parentData(*model, impRow, InventoryMoveTree::COL_FROM).toString(),
             QStringLiteral("EU"));

    // [5] import to = "FR"
    QCOMPARE(parentData(*model, impRow, InventoryMoveTree::COL_TO).toString(),
             QStringLiteral("FR"));

    // [6] export from = "FR"
    QCOMPARE(parentData(*model, expRow, InventoryMoveTree::COL_FROM).toString(),
             QStringLiteral("FR"));

    // [7] export to = "EU"
    QCOMPARE(parentData(*model, expRow, InventoryMoveTree::COL_TO).toString(),
             QStringLiteral("EU"));

    // [8] import parent has 1 child (SKU_A)
    QCOMPARE(model->rowCount(model->index(impRow, 0)), 1);

    // [9] export parent has 1 child (SKU_B)
    QCOMPARE(model->rowCount(model->index(expRow, 0)), 1);

    // [10] import child inherits the same from/to as parent
    QCOMPARE(childData(*model, impRow, 0, InventoryMoveTree::COL_FROM).toString(),
             QStringLiteral("EU"));

    delete model;
}

// ===========================================================================
// Test 3 – multi_country
// 10 assertions: FR imports, DE imports, GB exports coexist correctly.
// ===========================================================================
void TestInventoryMove::test_multi_country()
{
    QHash<QString, QHash<QString, int>> imported, exported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_FR")] = 20;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_FR2")] = 5;
    imported[QStringLiteral("DE")][QStringLiteral("SKU_DE")] = 30;
    exported[QStringLiteral("GB")][QStringLiteral("SKU_GB")] = 15;

    auto *model = makeTree(imported, exported);

    // [1] 3 parent rows (FR-import, DE-import, GB-export)
    QCOMPARE(model->rowCount(), 3);

    int rowFR = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    int rowDE = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("DE"));
    int rowGB = findParentRow(*model, QStringLiteral("GB"), QStringLiteral("EU"));

    // [2] FR import row found
    QVERIFY(rowFR != -1);
    // [3] DE import row found
    QVERIFY(rowDE != -1);
    // [4] GB export row found
    QVERIFY(rowGB != -1);

    // [5] FR parent has 2 children (SKU_FR + SKU_FR2)
    QCOMPARE(model->rowCount(model->index(rowFR, 0)), 2);

    // [6] DE parent has 1 child (SKU_DE)
    QCOMPARE(model->rowCount(model->index(rowDE, 0)), 1);

    // [7] GB parent has 1 child (SKU_GB)
    QCOMPARE(model->rowCount(model->index(rowGB, 0)), 1);

    // [8] FR parent COL_SKU value = 2 (distinct SKU count)
    QCOMPARE(parentData(*model, rowFR, InventoryMoveTree::COL_SKU).toInt(), 2);

    // [9] DE child from = "EU"
    int childDE = findChildRow(*model, rowDE, QStringLiteral("SKU_DE"));
    QVERIFY(childDE != -1);
    QCOMPARE(childData(*model, rowDE, childDE, InventoryMoveTree::COL_FROM).toString(),
             QStringLiteral("EU"));

    // [10] GB child to = "EU"
    int childGB = findChildRow(*model, rowGB, QStringLiteral("SKU_GB"));
    QVERIFY(childGB != -1);
    QCOMPARE(childData(*model, rowGB, childGB, InventoryMoveTree::COL_TO).toString(),
             QStringLiteral("EU"));

    delete model;
}

// ===========================================================================
// Test 4 – units_and_prices
// 14 assertions: child units, unit prices, total prices match the purchase CSV.
// No currency conversion, no shipping cost in this test.
// ===========================================================================
void TestInventoryMove::test_units_and_prices()
{
    // Purchase file: SKU_P1 @ 4.00 EUR (20 g → 0 shipping since no pricePerKilo)
    //                SKU_P2 @ 7.50 EUR
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__p-units.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,Widget A,SKU_P1,100,4.00,EUR,20\n"
                            "2,Gadget B,SKU_P2,50,7.50,EUR,30\n"));

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_P1")] = 12;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_P2")] = 8;

    auto *model = makeTree(imported, {});

    int pRow = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    QVERIFY(pRow != -1);

    int c1 = findChildRow(*model, pRow, QStringLiteral("SKU_P1"));
    int c2 = findChildRow(*model, pRow, QStringLiteral("SKU_P2"));
    QVERIFY(c1 != -1);
    QVERIFY(c2 != -1);

    // [1] SKU_P1 units = 12
    QCOMPARE(childData(*model, pRow, c1, InventoryMoveTree::COL_UNITS).toInt(), 12);

    // [2] SKU_P2 units = 8
    QCOMPARE(childData(*model, pRow, c2, InventoryMoveTree::COL_UNITS).toInt(), 8);

    // [3] SKU_P1 unit price = 4.00 (no shipping cost, no pricePerKilo)
    QVERIFY(nearlyEqual(childData(*model, pRow, c1, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 4.00));

    // [4] SKU_P2 unit price = 7.50
    QVERIFY(nearlyEqual(childData(*model, pRow, c2, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 7.50));

    // [5] SKU_P1 total price = 12 × 4.00 = 48.00
    QVERIFY(nearlyEqual(childData(*model, pRow, c1, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 48.00));

    // [6] SKU_P2 total price = 8 × 7.50 = 60.00
    QVERIFY(nearlyEqual(childData(*model, pRow, c2, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 60.00));

    // [7] product name SKU_P1 = "Widget A"
    QCOMPARE(childData(*model, pRow, c1, InventoryMoveTree::COL_PRODUCT_NAME).toString(),
             QStringLiteral("Widget A"));

    // [8] product name SKU_P2 = "Gadget B"
    QCOMPARE(childData(*model, pRow, c2, InventoryMoveTree::COL_PRODUCT_NAME).toString(),
             QStringLiteral("Gadget B"));

    // [9] purchase file is not empty
    QVERIFY(!childData(*model, pRow, c1, InventoryMoveTree::COL_PURCHASE_FILE).toString().isEmpty());

    // [10] purchase file is not "No invoice found"
    QVERIFY(childData(*model, pRow, c1, InventoryMoveTree::COL_PURCHASE_FILE).toString()
            != QStringLiteral("No invoice found"));

    // [11] COL_CURRENCY = invoice currency ("EUR") when no CompanyInfosTable provided
    QCOMPARE(childData(*model, pRow, c1, InventoryMoveTree::COL_CURRENCY).toString(),
             QStringLiteral("EUR"));

    // [12] COL_ORIG_CURRENCY is empty: no conversion performed (no rates manager)
    QVERIFY(!childData(*model, pRow, c1, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || childData(*model, pRow, c1, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    // [13] COL_ORIG_AMOUNT is invalid/empty: no conversion performed
    QVERIFY(!childData(*model, pRow, c2, InventoryMoveTree::COL_ORIG_AMOUNT).isValid()
            || nearlyEqual(childData(*model, pRow, c2, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 0.0));

    // [14] parent COL_ORIG_CURRENCY is always empty (no single orig currency for aggregates)
    QVERIFY(!parentData(*model, pRow, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || parentData(*model, pRow, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    delete model;
}

// ===========================================================================
// Test 5 – parent_aggregation
// 12 assertions: parent units total, weighted average unit price, total price,
// and the SKU column count.
// ===========================================================================
void TestInventoryMove::test_parent_aggregation()
{
    // SKU_Q1: 10 units @ 5.00 EUR  → total 50.00
    // SKU_Q2: 5  units @ 3.00 EUR  → total 15.00
    // SKU_Q3: 15 units @ 2.00 EUR  → total 30.00
    // Parent totals: 30 units, 95.00, avg = 95/30 ≈ 3.1667
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__p-agg.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,Agg1,SKU_Q1,100,5.00,EUR,0\n"
                            "2,Agg2,SKU_Q2,100,3.00,EUR,0\n"
                            "3,Agg3,SKU_Q3,100,2.00,EUR,0\n"));

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("DE")][QStringLiteral("SKU_Q1")] = 10;
    imported[QStringLiteral("DE")][QStringLiteral("SKU_Q2")] = 5;
    imported[QStringLiteral("DE")][QStringLiteral("SKU_Q3")] = 15;

    auto *model = makeTree(imported, {});

    int pRow = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("DE"));
    QVERIFY(pRow != -1);

    // [1] parent has 3 children
    QCOMPARE(model->rowCount(model->index(pRow, 0)), 3);

    // [2] parent SKU column = 3 (distinct SKU count)
    QCOMPARE(parentData(*model, pRow, InventoryMoveTree::COL_SKU).toInt(), 3);

    // [3] parent total units = 30
    QCOMPARE(parentData(*model, pRow, InventoryMoveTree::COL_UNITS).toInt(), 30);

    // [4] parent total price = 95.00
    QVERIFY(nearlyEqual(parentData(*model, pRow, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 95.00));

    // [5] parent weighted avg unit price = 95/30 ≈ 3.1667
    QVERIFY(nearlyEqual(parentData(*model, pRow, InventoryMoveTree::COL_UNIT_PRICE).toDouble(),
                        95.0 / 30.0, 0.01));

    // [6] parent purchase file column is empty
    QVERIFY(parentData(*model, pRow, InventoryMoveTree::COL_PURCHASE_FILE).toString().isEmpty());

    // [7] parent product name column is empty
    QVERIFY(parentData(*model, pRow, InventoryMoveTree::COL_PRODUCT_NAME).toString().isEmpty());

    // Verify individual children contribute correctly to totals
    int cQ1 = findChildRow(*model, pRow, QStringLiteral("SKU_Q1"));
    int cQ3 = findChildRow(*model, pRow, QStringLiteral("SKU_Q3"));

    // [8] SKU_Q1 total = 10 × 5.00 = 50.00
    QVERIFY(nearlyEqual(childData(*model, pRow, cQ1, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 50.00));

    // [9] SKU_Q3 total = 15 × 2.00 = 30.00
    QVERIFY(nearlyEqual(childData(*model, pRow, cQ3, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 30.00));

    // [10] sum of child total prices equals parent total price
    double sum = 0.0;
    QModelIndex pIdx = model->index(pRow, 0);
    for (int r = 0; r < model->rowCount(pIdx); ++r)
        sum += model->data(model->index(r, InventoryMoveTree::COL_TOTAL_PRICE, pIdx)).toDouble();
    QVERIFY(nearlyEqual(sum, 95.00));

    // [11] parent COL_ORIG_AMOUNT is always empty/invalid
    QVERIFY(!parentData(*model, pRow, InventoryMoveTree::COL_ORIG_AMOUNT).isValid()
            || nearlyEqual(parentData(*model, pRow, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 0.0));

    // [12] parent COL_ORIG_CURRENCY is always empty
    QVERIFY(!parentData(*model, pRow, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || parentData(*model, pRow, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    delete model;
}

// ===========================================================================
// Test 6 – currency_conversion
// 18 assertions: USD and GBP prices are converted to EUR correctly,
// orig amount/currency columns filled; EUR unchanged with empty orig columns.
// ===========================================================================
void TestInventoryMove::test_currency_conversion()
{
    // SKU_USD: 10 USD/unit  → 10 × 0.9  = 9.00 EUR
    // SKU_GBP: 8 GBP/unit  → 8  × 1.15 = 9.20 EUR
    // SKU_EUR: 5 EUR/unit  → 5.00 EUR (no conversion needed)
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__p-curr.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,USD Item,SKU_USD,100,10.00,USD,0\n"
                            "2,GBP Item,SKU_GBP,100,8.00,GBP,0\n"
                            "3,EUR Item,SKU_EUR,100,5.00,EUR,0\n"));

    CurrencyRateManager rates(m_testDir, QStringLiteral("no_key"));

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_USD")] = 4;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_GBP")] = 3;
    imported[QStringLiteral("DE")][QStringLiteral("SKU_EUR")] = 10;

    auto *model = makeTree(imported, {}, {}, QStringLiteral("EUR"), &rates);

    int rowFR = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    int rowDE = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("DE"));
    QVERIFY(rowFR != -1);
    QVERIFY(rowDE != -1);

    int cUSD = findChildRow(*model, rowFR, QStringLiteral("SKU_USD"));
    int cGBP = findChildRow(*model, rowFR, QStringLiteral("SKU_GBP"));
    int cEUR = findChildRow(*model, rowDE, QStringLiteral("SKU_EUR"));
    QVERIFY(cUSD != -1);
    QVERIFY(cGBP != -1);
    QVERIFY(cEUR != -1);

    // [1] USD → EUR: 10.00 USD × 0.9 = 9.00 EUR
    QVERIFY(nearlyEqual(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 9.00));

    // [2] GBP → EUR: 8.00 GBP × 1.15 = 9.20 EUR
    QVERIFY(nearlyEqual(childData(*model, rowFR, cGBP, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 9.20));

    // [3] EUR unchanged: 5.00 EUR
    QVERIFY(nearlyEqual(childData(*model, rowDE, cEUR, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 5.00));

    // [4] SKU_USD total price = 4 × 9.00 = 36.00
    QVERIFY(nearlyEqual(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 36.00));

    // [5] SKU_GBP total price = 3 × 9.20 = 27.60
    QVERIFY(nearlyEqual(childData(*model, rowFR, cGBP, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 27.60));

    // [6] SKU_EUR total price = 10 × 5.00 = 50.00
    QVERIFY(nearlyEqual(childData(*model, rowDE, cEUR, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 50.00));

    // [7] parent FR total price = 36.00 + 27.60 = 63.60
    QVERIFY(nearlyEqual(parentData(*model, rowFR, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 63.60));

    // [8] parent FR units = 4 + 3 = 7
    QCOMPARE(parentData(*model, rowFR, InventoryMoveTree::COL_UNITS).toInt(), 7);

    // [9] parent FR avg unit price = 63.60 / 7 ≈ 9.086
    QVERIFY(nearlyEqual(parentData(*model, rowFR, InventoryMoveTree::COL_UNIT_PRICE).toDouble(),
                        63.60 / 7.0, 0.01));

    // [10] DE parent units = 10
    QCOMPARE(parentData(*model, rowDE, InventoryMoveTree::COL_UNITS).toInt(), 10);

    // [11] USD child COL_ORIG_CURRENCY = "USD" (conversion was applied)
    QCOMPARE(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_ORIG_CURRENCY).toString(),
             QStringLiteral("USD"));

    // [12] USD child COL_ORIG_AMOUNT = 10.00 (invoice unit price, not 4 × 10.00 = 40.00 total)
    QVERIFY(nearlyEqual(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 10.00));

    // [13] GBP child COL_ORIG_CURRENCY = "GBP"
    QCOMPARE(childData(*model, rowFR, cGBP, InventoryMoveTree::COL_ORIG_CURRENCY).toString(),
             QStringLiteral("GBP"));

    // [14] GBP child COL_ORIG_AMOUNT = 8.00 (invoice unit price, not 3 × 8.00 = 24.00 total)
    QVERIFY(nearlyEqual(childData(*model, rowFR, cGBP, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 8.00));

    // [15] EUR child COL_ORIG_CURRENCY is empty (same currency, no conversion)
    QVERIFY(!childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    // [16] EUR child COL_ORIG_AMOUNT is invalid/empty (no conversion)
    QVERIFY(!childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_AMOUNT).isValid()
            || nearlyEqual(childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 0.0));

    // [17] FR parent COL_ORIG_CURRENCY is always empty (aggregate row)
    QVERIFY(!parentData(*model, rowFR, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || parentData(*model, rowFR, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    // [18] USD child COL_CURRENCY = "EUR" (company currency)
    QCOMPARE(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_CURRENCY).toString(),
             QStringLiteral("EUR"));

    delete model;
}

// ===========================================================================
// Test 7 – shipping_cost
// 10 assertions: weight × pricePerKilo is added to the invoice price.
// Different countries have different price-per-kilo rates.
// ===========================================================================
void TestInventoryMove::test_shipping_cost()
{
    // SKU_S1: 5.00 EUR, weight = 400 g (= 0.4 kg)
    //   FR: pricePerKilo = 2.0 → shipping = 0.8 → total = 5.80
    // SKU_S2: 3.00 EUR, weight = 1000 g (= 1.0 kg)
    //   DE: pricePerKilo = 3.0 → shipping = 3.0 → total = 6.00
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__p-ship.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,Ship1,SKU_S1,100,5.00,EUR,400\n"
                            "2,Ship2,SKU_S2,100,3.00,EUR,1000\n"));

    QHash<QString, double> ppk;
    ppk[QStringLiteral("FR")] = 2.0;
    ppk[QStringLiteral("DE")] = 3.0;

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_S1")] = 6;
    imported[QStringLiteral("DE")][QStringLiteral("SKU_S2")] = 4;

    auto *model = makeTree(imported, {}, ppk);

    int rowFR = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    int rowDE = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("DE"));
    QVERIFY(rowFR != -1);
    QVERIFY(rowDE != -1);

    int cS1 = findChildRow(*model, rowFR, QStringLiteral("SKU_S1"));
    int cS2 = findChildRow(*model, rowDE, QStringLiteral("SKU_S2"));
    QVERIFY(cS1 != -1);
    QVERIFY(cS2 != -1);

    // [1] SKU_S1 unit price = 5.00 + 0.4×2.0 = 5.80
    QVERIFY(nearlyEqual(childData(*model, rowFR, cS1, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 5.80));

    // [2] SKU_S2 unit price = 3.00 + 1.0×3.0 = 6.00
    QVERIFY(nearlyEqual(childData(*model, rowDE, cS2, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 6.00));

    // [3] SKU_S1 total = 6 × 5.80 = 34.80
    QVERIFY(nearlyEqual(childData(*model, rowFR, cS1, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 34.80));

    // [4] SKU_S2 total = 4 × 6.00 = 24.00
    QVERIFY(nearlyEqual(childData(*model, rowDE, cS2, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 24.00));

    // [5] FR parent total = 34.80
    QVERIFY(nearlyEqual(parentData(*model, rowFR, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 34.80));

    // [6] DE parent total = 24.00
    QVERIFY(nearlyEqual(parentData(*model, rowDE, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 24.00));

    // [7] FR parent units = 6
    QCOMPARE(parentData(*model, rowFR, InventoryMoveTree::COL_UNITS).toInt(), 6);

    // [8] DE parent units = 4
    QCOMPARE(parentData(*model, rowDE, InventoryMoveTree::COL_UNITS).toInt(), 4);

    // [9] Also verify export direction: SKU_S1 exported from FR → same shipping cost applies
    QHash<QString, QHash<QString, int>> exported;
    exported[QStringLiteral("FR")][QStringLiteral("SKU_S1")] = 3;
    InventoryMoveTree model2(m_purchasesDir, {}, exported, ppk, QString(), nullptr, QDir(), QString(), this);
    int expRow = findParentRow(model2, QStringLiteral("FR"), QStringLiteral("EU"));
    QVERIFY(expRow != -1);
    int cS1exp = findChildRow(model2, expRow, QStringLiteral("SKU_S1"));
    QVERIFY(cS1exp != -1);
    QVERIFY(nearlyEqual(childData(model2, expRow, cS1exp, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 5.80));

    // [10] export parent total = 3 × 5.80 = 17.40
    QVERIFY(nearlyEqual(parentData(model2, expRow, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 17.40));

    delete model;
}

// ===========================================================================
// Test 8 – no_invoice_found
// 13 assertions: SKU not in any purchase file gets "No invoice found" label
// and zero unit price; known SKU is unaffected; orig columns are empty.
// ===========================================================================
void TestInventoryMove::test_no_invoice_found()
{
    // Only SKU_KNOWN is in the purchase file; SKU_MISSING is not.
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__p-noinv.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,Known,SKU_KNOWN,100,6.00,EUR,0\n"));

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_KNOWN")]   = 5;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_MISSING")] = 3;

    auto *model = makeTree(imported, {});

    int pRow = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    QVERIFY(pRow != -1);

    int cKnown   = findChildRow(*model, pRow, QStringLiteral("SKU_KNOWN"));
    int cMissing = findChildRow(*model, pRow, QStringLiteral("SKU_MISSING"));
    QVERIFY(cKnown   != -1);
    QVERIFY(cMissing != -1);

    // [1] Missing SKU unit price = 0
    QVERIFY(nearlyEqual(childData(*model, pRow, cMissing, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 0.0));

    // [2] Missing SKU total price = 0
    QVERIFY(nearlyEqual(childData(*model, pRow, cMissing, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 0.0));

    // [3] Missing SKU purchase file = "No invoice found"
    QCOMPARE(childData(*model, pRow, cMissing, InventoryMoveTree::COL_PURCHASE_FILE).toString(),
             QStringLiteral("No invoice found"));

    // [4] Known SKU unit price = 6.00 (unaffected)
    QVERIFY(nearlyEqual(childData(*model, pRow, cKnown, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 6.00));

    // [5] Known SKU purchase file != "No invoice found"
    QVERIFY(childData(*model, pRow, cKnown, InventoryMoveTree::COL_PURCHASE_FILE).toString()
            != QStringLiteral("No invoice found"));

    // [6] Known SKU total = 5 × 6.00 = 30.00
    QVERIFY(nearlyEqual(childData(*model, pRow, cKnown, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 30.00));

    // [7] Parent total units = 5 + 3 = 8
    QCOMPARE(parentData(*model, pRow, InventoryMoveTree::COL_UNITS).toInt(), 8);

    // [8] Parent total price = 30.00 (missing contributes 0)
    QVERIFY(nearlyEqual(parentData(*model, pRow, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 30.00));

    // [9] Parent avg unit price = 30.00 / 8 = 3.75
    QVERIFY(nearlyEqual(parentData(*model, pRow, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 30.0 / 8.0, 0.01));

    // [10] Missing SKU units are still correctly recorded
    QCOMPARE(childData(*model, pRow, cMissing, InventoryMoveTree::COL_UNITS).toInt(), 3);

    // [11] Missing SKU COL_ORIG_CURRENCY is empty (no invoice, no conversion)
    QVERIFY(!childData(*model, pRow, cMissing, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || childData(*model, pRow, cMissing, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    // [12] Missing SKU COL_ORIG_AMOUNT is invalid/empty
    QVERIFY(!childData(*model, pRow, cMissing, InventoryMoveTree::COL_ORIG_AMOUNT).isValid()
            || nearlyEqual(childData(*model, pRow, cMissing, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 0.0));

    // [13] Known SKU COL_ORIG_CURRENCY is empty (EUR invoice → EUR company, no conversion)
    QVERIFY(!childData(*model, pRow, cKnown, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || childData(*model, pRow, cKnown, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    delete model;
}

// ===========================================================================
// Test 9 – language_priority_title
// 10 assertions:
//   - Title priority: FR > US/CA > others (regardless of file date).
//   - Price recency: most recent file wins (regardless of language).
// ===========================================================================
void TestInventoryMove::test_language_priority_title()
{
    // Newer file (US language): title "US Title", price 8.00 USD
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-07-01__inv-US.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,US Title,SKU_LANG,100,8.00,USD,0\n"
                            "1,Other Title,SKU_OTHER,100,2.00,EUR,0\n"));

    // Older file (FR language): title "FR Titre", price 5.00 EUR
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__inv-FR.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,FR Titre,SKU_LANG,100,5.00,EUR,0\n"));

    // Even older file (no language marker): title "Generic"
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-05-01__inv-generic.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,Generic,SKU_LANG,100,3.00,EUR,0\n"
                            "1,Only Source,SKU_OTHER,50,1.00,EUR,0\n"));

    CurrencyRateManager rates(m_testDir, QStringLiteral("no_key"));

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_LANG")]  = 10;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_OTHER")] = 5;

    auto *model = makeTree(imported, {}, {}, QStringLiteral("EUR"), &rates);

    int pRow  = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    QVERIFY(pRow != -1);

    int cLang  = findChildRow(*model, pRow, QStringLiteral("SKU_LANG"));
    int cOther = findChildRow(*model, pRow, QStringLiteral("SKU_OTHER"));
    QVERIFY(cLang  != -1);
    QVERIFY(cOther != -1);

    // [1] SKU_LANG title = "FR Titre" (FR priority wins over US even though US is newer)
    QCOMPARE(childData(*model, pRow, cLang, InventoryMoveTree::COL_PRODUCT_NAME).toString(),
             QStringLiteral("FR Titre"));

    // [2] SKU_LANG price comes from newest file (US, 2025-07-01): 8.00 USD × 0.9 = 7.20 EUR
    QVERIFY(nearlyEqual(childData(*model, pRow, cLang, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 7.20));

    // [3] SKU_LANG purchase file is the US file (newest with a price)
    QVERIFY(childData(*model, pRow, cLang, InventoryMoveTree::COL_PURCHASE_FILE).toString()
            .contains(QStringLiteral("2025-07-01")));

    // [4] SKU_OTHER title = "Other Title" (only US file has it, generic file has different title)
    //     US file (priority 2) > generic (priority 1)
    QCOMPARE(childData(*model, pRow, cOther, InventoryMoveTree::COL_PRODUCT_NAME).toString(),
             QStringLiteral("Other Title"));

    // [5] SKU_OTHER price = 2.00 EUR (from newer US file)
    QVERIFY(nearlyEqual(childData(*model, pRow, cOther, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 2.00));

    // [6] SKU_LANG units = 10
    QCOMPARE(childData(*model, pRow, cLang, InventoryMoveTree::COL_UNITS).toInt(), 10);

    // [7] SKU_LANG total = 10 × 7.20 = 72.00
    QVERIFY(nearlyEqual(childData(*model, pRow, cLang, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 72.00));

    // [8] SKU_OTHER total = 5 × 2.00 = 10.00
    QVERIFY(nearlyEqual(childData(*model, pRow, cOther, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 10.00));

    // [9] Parent total = 72.00 + 10.00 = 82.00
    QVERIFY(nearlyEqual(parentData(*model, pRow, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 82.00));

    // [10] Parent units = 15
    QCOMPARE(parentData(*model, pRow, InventoryMoveTree::COL_UNITS).toInt(), 15);

    delete model;
}

// ===========================================================================
// Test 10 – sort
// 10 assertions: sort(COL_UNITS, AscendingOrder) and Descending reorder
// parent rows correctly; children remain attached to their parent.
// ===========================================================================
void TestInventoryMove::test_sort()
{
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__p-sort.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,Sort1,SKU_SORT1,100,1.00,EUR,0\n"
                            "2,Sort2,SKU_SORT2,100,1.00,EUR,0\n"
                            "3,Sort3,SKU_SORT3,100,1.00,EUR,0\n"));

    // FR: 10 units, DE: 30 units, GB: 20 units (exports)
    QHash<QString, QHash<QString, int>> exported;
    exported[QStringLiteral("FR")][QStringLiteral("SKU_SORT1")] = 10;
    exported[QStringLiteral("DE")][QStringLiteral("SKU_SORT2")] = 30;
    exported[QStringLiteral("GB")][QStringLiteral("SKU_SORT3")] = 20;

    InventoryMoveTree model(m_purchasesDir, {}, exported, {}, QString(), nullptr, QDir(), QString(), this);

    // [1] 3 parent rows exist before sorting
    QCOMPARE(model.rowCount(), 3);

    // Sort ascending by units
    model.sort(InventoryMoveTree::COL_UNITS, Qt::AscendingOrder);

    // [2] still 3 rows after sort
    QCOMPARE(model.rowCount(), 3);

    // Ascending: row 0 should have the smallest units (10)
    // [3]
    QCOMPARE(model.data(model.index(0, InventoryMoveTree::COL_UNITS)).toInt(), 10);

    // [4] row 1 = 20
    QCOMPARE(model.data(model.index(1, InventoryMoveTree::COL_UNITS)).toInt(), 20);

    // [5] row 2 = 30
    QCOMPARE(model.data(model.index(2, InventoryMoveTree::COL_UNITS)).toInt(), 30);

    // [6] children are still attached: row 0 (10 units, FR export) has 1 child
    QCOMPARE(model.rowCount(model.index(0, 0)), 1);

    // [7] that child has units = 10
    QCOMPARE(model.data(model.index(0, InventoryMoveTree::COL_UNITS,
                                    model.index(0, 0))).toInt(), 10);

    // Sort descending by units
    model.sort(InventoryMoveTree::COL_UNITS, Qt::DescendingOrder);

    // [8] row 0 = 30 (largest first)
    QCOMPARE(model.data(model.index(0, InventoryMoveTree::COL_UNITS)).toInt(), 30);

    // [9] row 1 = 20
    QCOMPARE(model.data(model.index(1, InventoryMoveTree::COL_UNITS)).toInt(), 20);

    // [10] row 2 = 10
    QCOMPARE(model.data(model.index(2, InventoryMoveTree::COL_UNITS)).toInt(), 10);
}

// ===========================================================================
// Test 11 – amazon_format_headers
// 11 assertions: SKUs in real Amazon purchase CSV format are correctly parsed.
// Real Amazon files use tab-separated, lowercase/hyphenated column names:
//   order-number, product-name, sku, quantity, unit-weight, price, total-price, currency
// and comma as decimal separator.
// Before the fix colSku == -1 → entire file skipped → "No invoice found" for all SKUs.
// ===========================================================================
void TestInventoryMove::test_amazon_format_headers()
{
    // Write a CSV using the exact Amazon purchase file format.
    // Tab-separated, lowercase/hyphenated headers, comma decimal separator.
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__amz-fmt.csv")),
             QStringLiteral("order-number\tproduct-name\tsku\tquantity\tunit-weight\tprice\ttotal-price\tcurrency\n"
                            "ORD-001\tBlue Prayer Mat\tAMZ_SKU_001\t6\t650\t2,8\t16,8\tEUR\n"
                            "ORD-002\tRed Carpet\tAMZ_SKU_002\t4\t800\t5,50\t22,00\tEUR\n"));

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("AMZ_SKU_001")]      = 6;
    imported[QStringLiteral("FR")][QStringLiteral("AMZ_SKU_002")]      = 4;
    imported[QStringLiteral("FR")][QStringLiteral("AMZ_SKU_NOTFOUND")] = 2;

    auto *model = makeTree(imported, {});

    int pRow = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    QVERIFY(pRow != -1);

    int c1  = findChildRow(*model, pRow, QStringLiteral("AMZ_SKU_001"));
    int c2  = findChildRow(*model, pRow, QStringLiteral("AMZ_SKU_002"));
    int cNf = findChildRow(*model, pRow, QStringLiteral("AMZ_SKU_NOTFOUND"));
    QVERIFY(c1  != -1);
    QVERIFY(c2  != -1);
    QVERIFY(cNf != -1);

    // [1] AMZ_SKU_001 must be found in the Amazon-format file (key regression check)
    QVERIFY(childData(*model, pRow, c1, InventoryMoveTree::COL_PURCHASE_FILE).toString()
            != QStringLiteral("No invoice found"));

    // [2] AMZ_SKU_001 unit price = 2.8 EUR (comma decimal correctly parsed)
    QVERIFY(nearlyEqual(childData(*model, pRow, c1, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 2.8));

    // [3] AMZ_SKU_001 product name comes from "product-name" column
    QCOMPARE(childData(*model, pRow, c1, InventoryMoveTree::COL_PRODUCT_NAME).toString(),
             QStringLiteral("Blue Prayer Mat"));

    // [4] AMZ_SKU_002 must also be found
    QVERIFY(childData(*model, pRow, c2, InventoryMoveTree::COL_PURCHASE_FILE).toString()
            != QStringLiteral("No invoice found"));

    // [5] AMZ_SKU_002 unit price = 5.50 EUR
    QVERIFY(nearlyEqual(childData(*model, pRow, c2, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 5.50));

    // [6] AMZ_SKU_NOTFOUND is genuinely absent → still gets "No invoice found"
    QCOMPARE(childData(*model, pRow, cNf, InventoryMoveTree::COL_PURCHASE_FILE).toString(),
             QStringLiteral("No invoice found"));

    // [7] AMZ_SKU_001 total price = 6 × 2.8 = 16.8
    QVERIFY(nearlyEqual(childData(*model, pRow, c1, InventoryMoveTree::COL_TOTAL_PRICE).toDouble(), 16.8));

    delete model;
}

// ===========================================================================
// Test 12 – orig_unit_price_and_currency
// 18 assertions:
//   a) COL_ORIG_AMOUNT header is "Orig Unit Price" (not "Orig amount").
//   b) COL_ORIG_AMOUNT stores the invoice *unit price* (not units × unit price).
//   c) COL_UNIT_PRICE (company currency) ≠ COL_ORIG_AMOUNT (invoice currency) when they differ.
//   d) COL_CURRENCY is the company currency for all children when company info is provided.
//   e) COL_CURRENCY falls back to the invoice currency when no CompanyInfosTable is given,
//      so it is never left empty for items whose invoice is known.
//
// Assertions [1], [4], [9], [10] will FAIL with the current implementation.
// ===========================================================================
void TestInventoryMove::test_orig_unit_price_and_currency()
{
    // Purchase file: SKU_USD2 @ 10.00 USD, SKU_EUR2 @ 5.00 EUR
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-15__p-origprice.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,USD Item,SKU_USD2,100,10.00,USD,0\n"
                            "2,EUR Item,SKU_EUR2,100,5.00,EUR,0\n"));

    CurrencyRateManager rates(m_testDir, QStringLiteral("no_key")); // 1 USD = 0.9 EUR

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_USD2")] = 4;
    imported[QStringLiteral("DE")][QStringLiteral("SKU_EUR2")] = 10;

    // --- Part A: with company currency "EUR" ---
    auto *model = makeTree(imported, {}, {}, QStringLiteral("EUR"), &rates);

    int rowFR = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    int rowDE = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("DE"));
    QVERIFY(rowFR != -1);
    QVERIFY(rowDE != -1);

    int cUSD = findChildRow(*model, rowFR, QStringLiteral("SKU_USD2"));
    int cEUR = findChildRow(*model, rowDE, QStringLiteral("SKU_EUR2"));
    QVERIFY(cUSD != -1);
    QVERIFY(cEUR != -1);

    // [1] COL_ORIG_AMOUNT header = "Orig Unit Price" (rename from "Orig amount")
    //     FAILS with current code (returns "Orig amount").
    QCOMPARE(model->headerData(InventoryMoveTree::COL_ORIG_AMOUNT, Qt::Horizontal).toString(),
             QStringLiteral("Orig Unit Price"));

    // [2] USD child COL_CURRENCY = company currency "EUR"
    QCOMPARE(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_CURRENCY).toString(),
             QStringLiteral("EUR"));

    // [3] EUR child COL_CURRENCY = company currency "EUR" (even when no conversion)
    QCOMPARE(childData(*model, rowDE, cEUR, InventoryMoveTree::COL_CURRENCY).toString(),
             QStringLiteral("EUR"));

    // [4] USD child COL_ORIG_AMOUNT = 10.00 (invoice *unit price*, not 4 × 10.00 = 40.00)
    //     FAILS with current code (returns 40.00).
    QVERIFY(nearlyEqual(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 10.00));

    // [5] USD child COL_UNIT_PRICE ≈ 9.00 (10.00 USD × 0.9 converted to EUR)
    QVERIFY(nearlyEqual(childData(*model, rowFR, cUSD, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 9.00));

    // [6] COL_UNIT_PRICE (EUR) ≠ COL_ORIG_AMOUNT (USD) – different because different currencies
    QVERIFY(!nearlyEqual(
        childData(*model, rowFR, cUSD, InventoryMoveTree::COL_UNIT_PRICE).toDouble(),
        childData(*model, rowFR, cUSD, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble()));

    // [7] EUR child COL_ORIG_AMOUNT is empty (same currency, no conversion)
    QVERIFY(!childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_AMOUNT).isValid()
            || nearlyEqual(childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 0.0));

    // [8] EUR child COL_ORIG_CURRENCY is empty (same currency, no conversion)
    QVERIFY(!childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_CURRENCY).isValid()
            || childData(*model, rowDE, cEUR, InventoryMoveTree::COL_ORIG_CURRENCY).toString().isEmpty());

    delete model;

    // --- Part B: without company info – COL_CURRENCY must fall back to invoice currency ---
    auto *modelNoCompany = makeTree(imported, {}); // nullptr company + nullptr rates
    QVERIFY(modelNoCompany != nullptr);

    int rowFR2 = findParentRow(*modelNoCompany, QStringLiteral("EU"), QStringLiteral("FR"));
    int rowDE2 = findParentRow(*modelNoCompany, QStringLiteral("EU"), QStringLiteral("DE"));
    QVERIFY(rowFR2 != -1);
    QVERIFY(rowDE2 != -1);

    int cUSD2 = findChildRow(*modelNoCompany, rowFR2, QStringLiteral("SKU_USD2"));
    int cEUR2 = findChildRow(*modelNoCompany, rowDE2, QStringLiteral("SKU_EUR2"));
    QVERIFY(cUSD2 != -1);
    QVERIFY(cEUR2 != -1);

    // [9] USD invoice, no company info → COL_CURRENCY = "USD" (invoice currency fallback)
    //     FAILS with current code (returns "").
    QCOMPARE(childData(*modelNoCompany, rowFR2, cUSD2, InventoryMoveTree::COL_CURRENCY).toString(),
             QStringLiteral("USD"));

    // [10] EUR invoice, no company info → COL_CURRENCY = "EUR" (invoice currency fallback)
    //      FAILS with current code (returns "").
    QCOMPARE(childData(*modelNoCompany, rowDE2, cEUR2, InventoryMoveTree::COL_CURRENCY).toString(),
             QStringLiteral("EUR"));

    delete modelNoCompany;
}

// ===========================================================================
// Test 13 – dialog_view_orders_currency
// Regression test for the bug seen in the DialogViewOrders screenshot where
// the Currency column showed "USD" (invoice currency) instead of "EUR"
// (company currency).
//
// Root cause: InventoryMoveTree accepted CompanyInfosTable* but
// DialogViewOrders only had a const CurrencyRateManager* and a destCurrency
// string — no way to pass the company currency without a CompanyInfosTable
// object, so it passed nullptr which made companyCurrency="" and COL_CURRENCY
// fell back to the invoice currency.
//
// Fix: constructor now accepts const QString &companyCurrency directly.
//
// 4 assertions:
//   [1] COL_CURRENCY = "EUR" (company currency, not "USD" invoice currency)
//   [2] COL_ORIG_CURRENCY = "USD" (invoice currency, recorded because conversion happened)
//   [3] COL_ORIG_AMOUNT ≈ 15.00 (invoice unit price in USD)
//   [4] COL_UNIT_PRICE ≈ 13.50 (15.00 USD × 0.9 = 13.50 EUR)
// ===========================================================================
void TestInventoryMove::test_dialog_view_orders_currency()
{
    // USD-priced invoice: 15.00 USD/unit
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-15__p-dlg.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,Dialog Item,SKU_DLG,100,15.00,USD,0\n"));

    CurrencyRateManager rates(m_testDir, QStringLiteral("no_key")); // 1 USD = 0.9 EUR

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("SK")][QStringLiteral("SKU_DLG")] = 2;

    // DialogViewOrders now passes destCurrency directly (no CompanyInfosTable needed).
    InventoryMoveTree model(m_purchasesDir, imported, {}, {}, QStringLiteral("EUR"), &rates, QDir(), QString(), this);

    int pRow = findParentRow(model, QStringLiteral("EU"), QStringLiteral("SK"));
    QVERIFY(pRow != -1);
    int cDlg = findChildRow(model, pRow, QStringLiteral("SKU_DLG"));
    QVERIFY(cDlg != -1);

    // [1] COL_CURRENCY = "EUR" (company currency, not "USD" invoice fallback)
    QCOMPARE(childData(model, pRow, cDlg, InventoryMoveTree::COL_CURRENCY).toString(),
             QStringLiteral("EUR"));

    // [2] COL_ORIG_CURRENCY = "USD" (conversion was applied: invoice ≠ company currency)
    QCOMPARE(childData(model, pRow, cDlg, InventoryMoveTree::COL_ORIG_CURRENCY).toString(),
             QStringLiteral("USD"));

    // [3] COL_ORIG_AMOUNT = 15.00 (original USD unit price)
    QVERIFY(nearlyEqual(childData(model, pRow, cDlg, InventoryMoveTree::COL_ORIG_AMOUNT).toDouble(), 15.00));

    // [4] COL_UNIT_PRICE = 13.50 (15.00 USD × 0.9 = 13.50 EUR)
    QVERIFY(nearlyEqual(childData(model, pRow, cDlg, InventoryMoveTree::COL_UNIT_PRICE).toDouble(), 13.50));
}

// ===========================================================================
// Test 14 – parent_aggregation_weighted
// Verifies the three invariants of parent aggregation with deliberately
// skewed unit counts (1 vs 999 vs 50) so the weighted average is very
// different from a simple arithmetic mean:
//
//   SKU_W1:   1 unit  @ 200.00 EUR  → child total  200.00
//   SKU_W2: 999 units @   0.10 EUR  → child total   99.90
//   SKU_W3:  50 units @   7.50 EUR  → child total  375.00
//
//   Parent: 1050 units, total 674.90 EUR
//   Weighted avg unit price = 674.90 / 1050 ≈ 0.6428
//
// Invariants checked:
//   [A] parent total price = sum of child total prices
//   [B] parent unit price  = parent total price / parent units (weighted avg)
//   [C] parent units × parent unit price ≈ parent total price (within 1%)
//
// 11 assertions (after pre-checks).
// ===========================================================================
void TestInventoryMove::test_parent_aggregation_weighted()
{
    writeCsv(m_purchasesDir.filePath(QStringLiteral("2025-06-01__p-wagg.csv")),
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                            "1,W1,SKU_W1,100,200.00,EUR,0\n"
                            "2,W2,SKU_W2,100,0.10,EUR,0\n"
                            "3,W3,SKU_W3,100,7.50,EUR,0\n"));

    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_W1")] = 1;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_W2")] = 999;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_W3")] = 50;

    auto *model = makeTree(imported, {});

    int pRow = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    QVERIFY(pRow != -1);

    int cW1 = findChildRow(*model, pRow, QStringLiteral("SKU_W1"));
    int cW2 = findChildRow(*model, pRow, QStringLiteral("SKU_W2"));
    int cW3 = findChildRow(*model, pRow, QStringLiteral("SKU_W3"));
    QVERIFY(cW1 != -1);
    QVERIFY(cW2 != -1);
    QVERIFY(cW3 != -1);

    // Child unit prices (read back from model to use the exact stored values)
    const double upW1 = childData(*model, pRow, cW1, InventoryMoveTree::COL_UNIT_PRICE).toDouble();
    const double upW2 = childData(*model, pRow, cW2, InventoryMoveTree::COL_UNIT_PRICE).toDouble();
    const double upW3 = childData(*model, pRow, cW3, InventoryMoveTree::COL_UNIT_PRICE).toDouble();

    // [1] SKU_W1 unit price = 200.00
    QVERIFY(nearlyEqual(upW1, 200.00));

    // [2] SKU_W2 unit price = 0.10
    QVERIFY(nearlyEqual(upW2, 0.10));

    // [3] SKU_W3 unit price = 7.50
    QVERIFY(nearlyEqual(upW3, 7.50));

    // Child totals
    const double totW1 = childData(*model, pRow, cW1, InventoryMoveTree::COL_TOTAL_PRICE).toDouble();
    const double totW2 = childData(*model, pRow, cW2, InventoryMoveTree::COL_TOTAL_PRICE).toDouble();
    const double totW3 = childData(*model, pRow, cW3, InventoryMoveTree::COL_TOTAL_PRICE).toDouble();

    // [4] SKU_W1 total =   1 × 200.00 = 200.00
    QVERIFY(nearlyEqual(totW1, 1 * 200.00));

    // [5] SKU_W2 total = 999 × 0.10   =  99.90
    QVERIFY(nearlyEqual(totW2, 999 * 0.10));

    // [6] SKU_W3 total =  50 × 7.50   = 375.00
    QVERIFY(nearlyEqual(totW3, 50 * 7.50));

    // Invariant A: parent total price = sum of child total prices
    const double childSum = totW1 + totW2 + totW3;                    // 200.00 + 99.90 + 375.00 = 674.90
    const double parentTotal = parentData(*model, pRow, InventoryMoveTree::COL_TOTAL_PRICE).toDouble();

    // [7] sum of child totals matches the expected grand total
    QVERIFY(nearlyEqual(childSum, 674.90));

    // [8] parent total price = sum of child total prices (invariant A)
    QVERIFY(nearlyEqual(parentTotal, childSum));

    // Invariant B: parent unit price = weighted average (total price ÷ total units)
    const int parentUnits = parentData(*model, pRow, InventoryMoveTree::COL_UNITS).toInt();
    const double parentUnitPrice = parentData(*model, pRow, InventoryMoveTree::COL_UNIT_PRICE).toDouble();
    const double expectedWeightedAvg = childSum / parentUnits;         // 674.90 / 1050 ≈ 0.6428

    // [9] parent total units = 1 + 999 + 50 = 1050
    QCOMPARE(parentUnits, 1050);

    // [10] parent unit price equals the units-weighted average (invariant B)
    QVERIFY(nearlyEqual(parentUnitPrice, expectedWeightedAvg, 0.001));

    // Invariant C: parent units × parent unit price ≈ parent total price (within 1%)
    const double reconstructed = parentUnits * parentUnitPrice;
    const double relativeError = std::abs(reconstructed - parentTotal) / parentTotal;

    // [11] rounding error stays below 1 %
    QVERIFY(relativeError < 0.01);

    delete model;
}

// ---------------------------------------------------------------------------
// test 15 — PurchaseCsvLoader: records are in file-order, fields populated
// ---------------------------------------------------------------------------
void TestInventoryMove::test_csv_loader_records_order_and_fields()
{
    // Two purchase CSV files for the same SKU_X at different dates and prices.
    // The newer file also has weight and quantity; the older one has a FR suffix
    // (higher title priority).
    const QString newer = m_purchasesDir.filePath(
            QStringLiteral("2025-06-01__items.csv"));
    const QString older = m_purchasesDir.filePath(
            QStringLiteral("2025-01-01__items-FR.csv"));

    writeCsv(newer,
             QStringLiteral("SKU,Title,Unit Price,Currency,Unit Weight,Quantity\n"
                             "SKU_X,Title EN,30.00,EUR,500,2\n"));
    writeCsv(older,
             QStringLiteral("SKU,Title,Unit Price,Currency,Unit Weight,Quantity\n"
                             "SKU_X,Titre FR,10.00,EUR,500,5\n"));

    // parseFiles expects files newest-first (caller responsibility).
    const QStringList files = { newer, older };
    const QList<PurchaseCsvLoader::Record> records =
            PurchaseCsvLoader::parseFiles(files, m_purchasesDir);

    // [1] two records total (one per file)
    QCOMPARE(records.size(), 2);

    // [2] first record comes from the newer file
    QCOMPARE(records[0].fileName,
             QFileInfo(newer).fileName());

    // [3] second record comes from the older file
    QCOMPARE(records[1].fileName,
             QFileInfo(older).fileName());

    // [4] newer record: price and quantity
    QVERIFY(nearlyEqual(records[0].unitPrice, 30.00));
    QCOMPARE(records[0].quantity, 2);

    // [5] newer record: weight converted from grams to kg
    QVERIFY(nearlyEqual(records[0].weightKg, 0.5));

    // [6] older record (FR suffix): titlePriority = 3
    QCOMPARE(records[1].titlePriority, 3);

    // [7] older record: title
    QCOMPARE(records[1].title, QStringLiteral("Titre FR"));

    // [8] no conversion when no rateManager — origCurrency must be empty
    QVERIFY(records[0].origCurrency.isEmpty());

    // [9] invoiceCurrency is populated from CSV
    QCOMPARE(records[0].invoiceCurrency, QStringLiteral("EUR"));
}

// ---------------------------------------------------------------------------
// test 16 — PurchaseCsvLoader: latest-price policy vs FIFO on same records
// ---------------------------------------------------------------------------
void TestInventoryMove::test_csv_loader_latest_vs_fifo()
{
    // Three files for SKU_X with prices 30 (newest), 20, 10 (oldest).
    // Quantities: 2, 3, 5.  Total stock to account for: 7 units.
    const QString f1 = m_purchasesDir.filePath(
            QStringLiteral("2025-09-01__batch.csv"));
    const QString f2 = m_purchasesDir.filePath(
            QStringLiteral("2025-06-01__batch.csv"));
    const QString f3 = m_purchasesDir.filePath(
            QStringLiteral("2025-03-01__batch.csv"));

    writeCsv(f1, QStringLiteral("SKU,Title,Unit Price,Currency,Quantity\n"
                                "SKU_X,Widget,30.00,EUR,2\n"));
    writeCsv(f2, QStringLiteral("SKU,Title,Unit Price,Currency,Quantity\n"
                                "SKU_X,Widget,20.00,EUR,3\n"));
    writeCsv(f3, QStringLiteral("SKU,Title,Unit Price,Currency,Quantity\n"
                                "SKU_X,Widget,10.00,EUR,5\n"));

    // Files must be passed newest-first (as both consumers do after sorting).
    const QStringList files = { f1, f2, f3 };
    const QList<PurchaseCsvLoader::Record> records =
            PurchaseCsvLoader::parseFiles(files, m_purchasesDir);

    // [1] three records returned
    QCOMPARE(records.size(), 3);

    // ── Latest-price policy (InventoryMoveTree) ──────────────────────────────
    // Take the first valid price per SKU — that's the newest file's price.
    double latestPrice = 0.0;
    for (const PurchaseCsvLoader::Record &r : records) {
        if (r.sku == QStringLiteral("SKU_X") && r.origUnitPrice > 0.0) {
            latestPrice = r.unitPrice;
            break;  // first hit = newest file
        }
    }

    // [2] latest-price policy gives the price from the newest file (30.00)
    QVERIFY(nearlyEqual(latestPrice, 30.00));

    // ── FIFO policy (InventoryTable) ─────────────────────────────────────────
    // Walk newest-first; take as many units as available from each batch until
    // all 7 stock units are accounted for.
    int stockLeft = 7;
    double fifoTotalCost = 0.0;
    for (const PurchaseCsvLoader::Record &r : records) {
        if (r.sku != QStringLiteral("SKU_X") || stockLeft <= 0)
            continue;
        int taken = std::min(r.quantity, stockLeft);
        fifoTotalCost += taken * r.unitPrice;
        stockLeft -= taken;
    }

    // [3] all 7 units were accounted for
    QCOMPARE(stockLeft, 0);

    // [4] FIFO total cost: 2×30 + 3×20 + 2×10 = 60 + 60 + 20 = 140
    QVERIFY(nearlyEqual(fifoTotalCost, 140.00));

    // [5] FIFO average price (140/7 = 20.00) ≠ latest price (30.00)
    //     This is the key difference between the two policies.
    const double fifoAvgPrice = fifoTotalCost / 7.0;
    QVERIFY(nearlyEqual(fifoAvgPrice, 20.00));
    QVERIFY(!nearlyEqual(fifoAvgPrice, latestPrice));

    // ── Conversion absent when no rateManager ────────────────────────────────
    // [6] all origCurrency fields are empty (no conversion was requested)
    for (const PurchaseCsvLoader::Record &r : records)
        QVERIFY(r.origCurrency.isEmpty());
}

// ---------------------------------------------------------------------------
// test 17 — purchase-file date drives the conversion rate, not "today's" rate
// ---------------------------------------------------------------------------
void TestInventoryMove::test_csv_loader_purchase_date_rate()
{
    // Use a dedicated purchases directory so the InventoryMoveTree only scans
    // this test's own file and never requests a rate for a date not in the CSV.
    QDir dateTestPurchases(m_testDir.filePath(QStringLiteral("purchases_date_rate")));
    dateTestPurchases.mkpath(QStringLiteral("."));

    // Two USD→EUR rates at deliberately different dates.
    // January rate (0.50) is for the purchase date; June rate (0.90) represents
    // a later date.  The loader must use the purchase-file date, not June.
    writeCsv(m_testDir.filePath(QStringLiteral("currency-rates.csv")),
             QStringLiteral("Date,Source,Dest,Rate\n"
                            "2025-01-01,USD,EUR,0.50\n"
                            "2025-06-01,USD,EUR,0.90\n"));

    CurrencyRateManager rates(m_testDir, QStringLiteral("fake_key"));

    // Purchase CSV dated January 2025: price 10.00 USD per unit.
    const QString file = dateTestPurchases.filePath(
            QStringLiteral("2025-01-01__USD-date-test.csv"));
    writeCsv(file,
             QStringLiteral("Order ID,Title,SKU,Quantity,Unit Price,Currency,Unit Weight\n"
                             "D1,Date Item,SKU_DATE_RATE,1,10.00,USD,0\n"));

    // ── PurchaseCsvLoader level ──────────────────────────────────────────────
    const QList<PurchaseCsvLoader::Record> records =
            PurchaseCsvLoader::parseFiles({file}, m_testDir,
                                          QStringLiteral("EUR"), &rates);

    QCOMPARE(records.size(), 1);
    const PurchaseCsvLoader::Record &r = records[0];

    // [1] origUnitPrice holds the raw invoice price
    QVERIFY(nearlyEqual(r.origUnitPrice, 10.00));

    // [2] origCurrency is set (conversion was applied)
    QCOMPARE(r.origCurrency, QStringLiteral("USD"));

    // [3] unitPrice must use the January rate (0.50): 10.00 × 0.50 = 5.00 EUR
    QVERIFY(nearlyEqual(r.unitPrice, 5.00));

    // [4] the June rate (0.90) would have given a different result
    QVERIFY(!nearlyEqual(r.unitPrice, 9.00));

    // ── InventoryMoveTree level ──────────────────────────────────────────────
    // One unit of SKU_DATE_RATE imported into FR — price must also reflect Jan rate.
    QHash<QString, QHash<QString, int>> imported;
    imported[QStringLiteral("FR")][QStringLiteral("SKU_DATE_RATE")] = 1;

    InventoryMoveTree *model = new InventoryMoveTree(
            dateTestPurchases, imported, {},
            {}, QStringLiteral("EUR"), &rates, QDir(), QString(), this);

    const int pRow = findParentRow(*model, QStringLiteral("EU"), QStringLiteral("FR"));
    QVERIFY(pRow != -1);
    const int cRow = findChildRow(*model, pRow, QStringLiteral("SKU_DATE_RATE"));
    QVERIFY(cRow != -1);

    // [5] tree child price also uses the January purchase-date rate: 5.00 EUR
    const double treePrice =
            childData(*model, pRow, cRow, InventoryMoveTree::COL_UNIT_PRICE).toDouble();
    QVERIFY(nearlyEqual(treePrice, 5.00));

    delete model;
}

QTEST_MAIN(TestInventoryMove)
#include "test_inventory_move.moc"
