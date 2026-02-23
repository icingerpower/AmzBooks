#ifndef INVENTORYMOVETREE_H
#define INVENTORYMOVETREE_H

#include <QAbstractItemModel>
#include <QDate>
#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

class CurrencyRateManager;
class InventoryMoveTreeItem;
class SkuRegradedTable;

/*
 * InventoryMoveTree — QAbstractItemModel for inventory movements between the
 * EU central pool and individual Amazon Pan-EU warehouse countries.
 *
 * ── Tree structure ──────────────────────────────────────────────────────────
 *   Root (invisible)
 *   └─ Parent row  "EU → FR"  (one per country-direction pair)
 *      ├─ Child row  SKU_A   (one per SKU imported/exported in that direction)
 *      └─ Child row  SKU_B
 *
 * ── File naming — MUST follow "YYYY-MM-DD__*.csv" ──────────────────────────
 * "Newest-first" ordering is a plain LEXICOGRAPHIC DESCENDING sort on the
 * bare FILENAME (not the full path).  Consequences:
 *
 *   • Subdirectory names play no role; a file in any subdirectory is treated
 *     identically to one at the root of purchaseDir as long as its BASENAME
 *     starts with the date prefix.
 *
 *   • A file whose name does NOT start with a valid date prefix sorts BEFORE
 *     "0…" (ASCII), so it is treated as the NEWEST file.  Such files are
 *     normally absent, but their presence can silently override a dated file.
 *
 *   • Two files with the same basename in different subdirectories are
 *     considered equally recent; whichever QDirIterator yields first "wins"
 *     for the price, which is non-deterministic.  Keep basenames unique.
 *
 * ── Title priority vs. price recency — two INDEPENDENT rules ───────────────
 * These use different selection criteria; conflating them is a common bug.
 *
 *   Title → highest-priority LANGUAGE file wins, regardless of file date.
 *           Priority is derived from the filename (case-insensitive suffix):
 *             "-FR"              → 3  (highest)
 *             "-US"/"-COM"/"-CA" → 2
 *             anything else      → 1
 *           A SKU's title is replaced only by a strictly higher-priority file.
 *           Equal priorities do NOT replace the existing title.
 *
 *   Price / weight / currency
 *           → MOST RECENT file with a valid price (> 0) for that SKU wins.
 *             hasPriceFromFile is a "once-written" guard: the first (newest)
 *             valid price blocks every subsequent (older) file from overwriting
 *             it, even if the later file uses a different currency or has a
 *             higher title priority.
 *
 * Example: a 2025-07-01 US file (priority 2) may supply the price while a
 * 2025-06-01 FR file (priority 3) supplies the title — both rules apply
 * concurrently to the same SKU without interference.
 *
 * ── Shipping cost ───────────────────────────────────────────────────────────
 *   finalUnitPrice = convertedInvoiceUnitPrice
 *                 + weight_kg × country_pricePerKilo[warehouseCountry]
 *
 * warehouseCountry is the Amazon warehouse, which is:
 *   • the DESTINATION country for imports  (EU → "FR" → warehouseCountry="FR")
 *   • the ORIGIN country for exports       ("FR" → EU → warehouseCountry="FR")
 * Using the wrong country silently applies the wrong shipping rate.
 *
 * ── Currency conversion ─────────────────────────────────────────────────────
 * Conversion is applied ONLY when ALL FOUR conditions hold simultaneously:
 *   1. currencyRateManager != nullptr
 *   2. companyCurrency is not empty
 *   3. info.currency (invoice currency read from the CSV) is not empty
 *   4. info.currency != companyCurrency
 * When conversion is applied, the original invoice unit price is stored in
 * COL_ORIG_AMOUNT and the invoice currency code in COL_ORIG_CURRENCY.
 * When companyCurrency is empty, COL_CURRENCY shows the invoice currency
 * so the column is never blank for SKUs with a known invoice price.
 *
 * ── Parent-row aggregation ──────────────────────────────────────────────────
 *   Parent.totalPrice = Σ child.totalPrice          (exact sum)
 *   Parent.units      = Σ child.units
 *   Parent.unitPrice  = Parent.totalPrice / Parent.units   (units-weighted avg)
 *
 * Invariant: Parent.units × Parent.unitPrice ≈ Parent.totalPrice
 *   Floating-point division can introduce sub-cent rounding error; in practice
 *   the relative error is well below 1 %.
 *
 * COL_SKU at the parent level returns an INT (count of distinct child SKUs),
 * not a SKU string.  Sorting numerically on COL_SKU therefore sorts by the
 * number of distinct SKUs, not alphabetically.
 *
 * COL_ORIG_AMOUNT and COL_ORIG_CURRENCY are ALWAYS empty for parent rows,
 * even when all children were converted from the same invoice currency.
 * There is intentionally no aggregate for original amounts.
 *
 * ── Real-time directory monitoring ──────────────────────────────────────────
 * After construction the tree watches purchaseDir and all subdirectories it
 * contains at that moment via QFileSystemWatcher.  Any directory-level event
 * (file added or deleted) triggers a debounced rebuild via a QTimer (300 ms).
 * New subdirectories discovered during a rebuild are added to the watcher, so
 * they are monitored in subsequent rounds without requiring reconstruction.
 * The debounce collapses rapid event bursts (e.g. batch-copying several CSV
 * files at once) into a single rebuild once the burst settles.
 */
class InventoryMoveTree : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Columns {
        COL_FROM = 0,
        COL_TO,
        COL_SKU,           // child: SKU string; parent: count of distinct child SKUs (int)
        COL_PRODUCT_NAME,
        COL_UNITS,
        COL_UNIT_PRICE,    // in companyCurrency; falls back to invoice currency if companyCurrency is empty
        COL_TOTAL_PRICE,   // in companyCurrency; same fallback as COL_UNIT_PRICE
        COL_CURRENCY,      // companyCurrency when non-empty, else invoice currency; never blank for known SKUs
        COL_ORIG_AMOUNT,   // invoice unit price before conversion; empty when no conversion occurred
                           // always empty for parent rows
        COL_ORIG_CURRENCY, // invoice currency code before conversion; empty when no conversion occurred
                           // always empty for parent rows
        COL_PURCHASE_FILE, // CSV basename that supplied the price;
                           // "No invoice found" when the SKU is absent from all files
        COL_COUNT
    };

    // purchaseDir           – scanned recursively for *.csv / *.CSV.  Files must
    //                         follow the "YYYY-MM-DD__" naming prefix (see class comment).
    //                         The directory is watched for changes; the tree rebuilds
    //                         automatically when files are added or removed.
    // countryCode_sku_unitImported
    //                       – units imported into each Amazon warehouse country from the
    //                         EU central pool (direction EU → countryCode).
    // countryCode_sku_unitExported
    //                       – units exported back to the EU pool from an Amazon warehouse
    //                         (direction countryCode → EU).
    // country_pricePerKilo  – shipping cost in companyCurrency per kilogram, keyed by
    //                         Amazon warehouse country code.  The key "" is a catch-all
    //                         fallback for unknown country codes.
    // companyCurrency       – ISO-4217 home currency code (e.g. "EUR").  Pass an empty
    //                         string when unknown; conversion is skipped and COL_CURRENCY
    //                         shows the invoice currency.
    // currencyRateManager   – converts invoice prices to companyCurrency.  May be null;
    //                         conversion is silently skipped when null (see class comment
    //                         for the full set of conditions required for conversion).
    // workingDir            – application working directory used to locate invoice CSVs
    //                         managed by InventoryInvoicesTree (workingDir/inventory/YYYY/).
    //                         When the inventory subdirectory does not exist the fallback
    //                         is silently skipped.  Pass QDir() to disable invoice lookup.
    // skuRegradedTable      – optional manual mapping from Amazon regraded SKUs to their
    //                         canonical SKUs.  Consulted as a fallback when the heuristic
    //                         (resolveSkuForPurchaseLookup) produces a canonical with no
    //                         purchase data (~20 % of cases).  May be null; the fallback
    //                         is silently skipped when null.
    explicit InventoryMoveTree(const QDir &purchaseDir,
                               const QHash<QString, QHash<QString, int>> &countryCode_sku_unitImported,
                               const QHash<QString, QHash<QString, int>> &countryCode_sku_unitExported,
                               const QHash<QString, double> &country_pricePerKilo,
                               const QString &companyCurrency,
                               const CurrencyRateManager *currencyRateManager,
                               const QDir &workingDir = QDir(),
                               const QString &companyCountryCode = QString(),
                               const SkuRegradedTable *skuRegradedTable = nullptr,
                               QObject *parent = nullptr);
    ~InventoryMoveTree() override;

    // Returns unique SKUs for all child items whose unit price is 0.0
    // (i.e., no purchase invoice was found). Each SKU appears at most once.
    QStringList getSkusWithNoPrice() const;

    // Resolves an Amazon regraded SKU ("amzn.gr.*") to its likely canonical SKU
    // using the strip-prefix + remove-last-2-parts heuristic.  Returns the input
    // unchanged when it does not start with "amzn.gr." or does not have enough
    // dash-separated parts.
    static QString resolveSkuForPurchaseLookup(const QString &sku);

    // Returns true if canonicalSku was resolved (via the heuristic or directly)
    // and had a non-zero unit price when the tree was last built.  A false result
    // means the heuristic either produced a wrong canonical or no purchase invoice
    // exists for it, and a manual SKU mapping may be required.
    bool hasUnitPriceFor(const QString &canonicalSku) const;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

public slots:
    // Deletes all item data and rebuilds from the stored hashes and current CSV
    // files.  Also re-registers any newly-appeared subdirectories with m_watcher.
    // Safe to call from external code (e.g. after updating SkuRegradedTable).
    void rebuild();

private slots:
    // Triggered by m_watcher whenever a watched directory reports a change.
    // Restarts m_rebuildTimer; the actual rebuild fires once the directory has
    // been quiet for the debounce interval (300 ms).
    void onDirectoryChanged();

private:

    // Adds path and every subdirectory it currently contains to m_watcher.
    // Paths already registered are silently ignored by QFileSystemWatcher.
    void watchRecursive(const QString &path);

    QDir m_purchaseDir;
    QDir m_workingDir;
    QHash<QString, double> m_country_pricePerKilo;
    QString m_companyCurrency;
    QString m_companyCountryCode;
    const CurrencyRateManager *m_currencyRateManager;
    const SkuRegradedTable    *m_skuRegradedTable;
    InventoryMoveTreeItem *m_rootItem;

    // Stored so that rebuild() can reconstruct the tree without external input.
    // The import/export unit counts cannot be derived from the purchase CSV files.
    QHash<QString, QHash<QString, int>> m_countryCode_sku_unitImported;
    QHash<QString, QHash<QString, int>> m_countryCode_sku_unitExported;

    // QFileSystemWatcher emits directoryChanged() when any watched path changes.
    // QTimer debounces bursts of events into a single rebuild().
    QFileSystemWatcher *m_watcher;
    QTimer             *m_rebuildTimer;

    // Set of canonical SKUs (after heuristic resolution) that had a non-zero
    // unit price in the most-recently completed buildTree() run.
    // Cleared at the start of each buildTree(); used by hasUnitPriceFor().
    QSet<QString> m_canonicalsWithPrice;

    // Per-SKU data aggregated by loadPurchaseData() from PurchaseCsvLoader records.
    // Currency conversion is applied by PurchaseCsvLoader::parseFiles before
    // this struct is populated, so unitPrice is always in companyCurrency (or the
    // invoice currency when companyCurrency is empty).
    struct PurchaseInfo {
        QString title;
        int titlePriority = 0;          // 0 = unset; FR=3, US/CA/COM=2, others=1
        double unitPrice = 0.0;         // in companyCurrency (or invoice currency when no conversion)
        double origUnitPrice = 0.0;     // raw invoice price; equals unitPrice when no conversion
        QString origCurrency;           // invoice currency when conversion was applied; empty otherwise
        QString invoiceCurrency;        // raw invoice currency from CSV (always set when available)
        QDate purchaseDate;             // parsed from the YYYY-MM-DD__ filename prefix
        double weight = 0.0;            // unit weight in kg
        bool hasPriceFromFile = false;  // "once-written" guard; blocks older records from overwriting
        QString purchaseFile;           // basename of the file that provided the price
    };

    void buildTree(const QHash<QString, QHash<QString, int>> &countryCode_sku_unitImported,
                   const QHash<QString, QHash<QString, int>> &countryCode_sku_unitExported);

    // Collects purchase CSVs (newest-first) plus invoice fallback files, calls
    // PurchaseCsvLoader::parseFiles (with currency conversion), then aggregates:
    //   Title:  highest-language-priority record wins (FR > US/CA/COM > others).
    //   Price:  first valid (> 0) record wins (hasPriceFromFile guard).
    void loadPurchaseData(QHash<QString, PurchaseInfo> &purchaseData) const;
};

#endif // INVENTORYMOVETREE_H
