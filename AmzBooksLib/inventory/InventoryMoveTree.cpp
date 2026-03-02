#include "InventoryMoveTree.h"
#include "InventoryMoveTreeItem.h"
#include "InventoryInvoicesTree.h"
#include "PurchaseCsvLoader.h"
#include "books/CompanyInfosTable.h"
#include "books/SkuRegradedTable.h"
#include <QBrush>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include "books/ReportGenerator.h"

// ---------------------------------------------------------------------------
// SKU resolution: Amazon regraded SKUs
// ---------------------------------------------------------------------------
// Amazon marks regraded inventory with an "amzn.gr." prefix and appends two
// extra dash-separated components (a random key and an internal code) to the
// original SKU.  To locate the purchase data for such items, we strip the
// prefix and the trailing two components:
//
//   "amzn.gr.A5-BOOK-COVER-DESIGN-5-QaQJXV-PO"
//    remove prefix  → "A5-BOOK-COVER-DESIGN-5-QaQJXV-PO"
//    split by '-'   → ["A5","BOOK","COVER","DESIGN","5","QaQJXV","PO"]
//    drop last 2    → ["A5","BOOK","COVER","DESIGN","5"]
//    join           → "A5-BOOK-COVER-DESIGN-5"    ← canonical SKU
//
// Non-regraded SKUs are returned unchanged.
// When Amazon shortens the original SKU before prepending the prefix, this
// heuristic produces a wrong canonical (~20 % of cases); hasUnitPriceFor()
// lets callers detect that situation and fall back to a manual mapping.
QString InventoryMoveTree::resolveSkuForPurchaseLookup(const QString &sku)
{
    const QString prefix = QStringLiteral("amzn.gr.");
    if (!sku.startsWith(prefix))
        return sku;

    const QString withoutPrefix = sku.mid(prefix.size());
    QStringList parts = withoutPrefix.split(QLatin1Char('-'));
    if (parts.size() <= 2)
        return sku; // not enough parts to strip; return original unchanged

    parts.removeLast();
    parts.removeLast();
    return parts.join(QLatin1Char('-'));
}

bool InventoryMoveTree::hasUnitPriceFor(const QString &canonicalSku) const
{
    return m_canonicalsWithPrice.contains(canonicalSku);
}

InventoryMoveTree::InventoryMoveTree(const QDir &purchaseDir,
                                     const QHash<QString, QHash<QString, int>> &countryCode_sku_unitImported,
                                     const QHash<QString, QHash<QString, int>> &countryCode_sku_unitExported,
                                     const QHash<QString, double> &country_pricePerKilo,
                                     const QString &companyCurrency,
                                     const CurrencyRateManager *currencyRateManager,
                                     const QDir &workingDir,
                                     const QString &companyCountryCode,
                                     const SkuRegradedTable *skuRegradedTable,
                                     QObject *parent)
    : QAbstractItemModel(parent)
    , m_purchaseDir(purchaseDir)
    , m_workingDir(workingDir)
    , m_country_pricePerKilo(country_pricePerKilo)
    , m_companyCurrency(companyCurrency)
    , m_companyCountryCode(companyCountryCode)
    , m_currencyRateManager(currencyRateManager)
    , m_skuRegradedTable(skuRegradedTable)
    , m_rootItem(new InventoryMoveTreeItem())
    , m_countryCode_sku_unitImported(countryCode_sku_unitImported)
    , m_countryCode_sku_unitExported(countryCode_sku_unitExported)
    , m_watcher(new QFileSystemWatcher(this))
    , m_rebuildTimer(new QTimer(this))
{
    // Debounce: wait for the directory to be quiet for 300 ms before rebuilding.
    // This collapses rapid bursts (e.g. several CSV files copied at once) into
    // a single rebuild once the burst settles.
    m_rebuildTimer->setInterval(300);
    m_rebuildTimer->setSingleShot(true);
    connect(m_rebuildTimer, &QTimer::timeout, this, &InventoryMoveTree::rebuild);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &InventoryMoveTree::onDirectoryChanged);

    watchRecursive(m_purchaseDir.absolutePath());

    buildTree(countryCode_sku_unitImported, countryCode_sku_unitExported);
}

InventoryMoveTree::~InventoryMoveTree()
{
    delete m_rootItem;
}

void InventoryMoveTree::saveAsCsv(const QString &baseFilePath, bool multipleFile)
{
    QStringList headers;
    for (int c = 0; c < COL_COUNT; ++c) {
        headers << headerData(c, Qt::Horizontal).toString();
    }
    const QString headerLine = headers.join(";") + "\n";

    if (!multipleFile) {
        double totalValue = 0.0;
        for (int i = 0; i < m_rootItem->childCount(); ++i) {
            totalValue += m_rootItem->child(i)->data(COL_TOTAL_PRICE).toDouble();
        }
        QString path = baseFilePath + "__" + QString::number(totalValue, 'f', 2) + m_companyCurrency + ".csv";
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << headerLine;
            for (int i = 0; i < m_rootItem->childCount(); ++i) {
                InventoryMoveTreeItem *parentItem = m_rootItem->child(i);
                for (int j = 0; j < parentItem->childCount(); ++j) {
                    InventoryMoveTreeItem *childItem = parentItem->child(j);
                    QStringList rowData;
                    for (int c = 0; c < COL_COUNT; ++c) {
                        QString val;
                        if (c == COL_UNIT_PRICE || c == COL_TOTAL_PRICE) {
                            val = QString::number(childItem->data(c).toDouble(), 'f', 2);
                        } else {
                            val = childItem->data(c).toString();
                        }
                        val.replace("\"", "\"\"");
                        if (val.contains(";") || val.contains("\"") || val.contains("\n")) {
                            val = "\"" + val + "\"";
                        }
                        rowData << val;
                    }
                    out << rowData.join(";") << "\n";
                }
            }
        }
    } else {
        for (int i = 0; i < m_rootItem->childCount(); ++i) {
            InventoryMoveTreeItem *parentItem = m_rootItem->child(i);
            double value = parentItem->data(COL_TOTAL_PRICE).toDouble();
            QString from = parentItem->data(COL_FROM).toString();
            QString to = parentItem->data(COL_TO).toString();
            QString path = baseFilePath + "-" + from + "-" + to + "__" + QString::number(value, 'f', 2) + m_companyCurrency + ".csv";

            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << headerLine;
                for (int j = 0; j < parentItem->childCount(); ++j) {
                    InventoryMoveTreeItem *childItem = parentItem->child(j);
                    QStringList rowData;
                    for (int c = 0; c < COL_COUNT; ++c) {
                        QString val;
                        if (c == COL_UNIT_PRICE || c == COL_TOTAL_PRICE) {
                            val = QString::number(childItem->data(c).toDouble(), 'f', 2);
                        } else {
                            val = childItem->data(c).toString();
                        }
                        val.replace("\"", "\"\"");
                        if (val.contains(";") || val.contains("\"") || val.contains("\n")) {
                            val = "\"" + val + "\"";
                        }
                        rowData << val;
                    }
                    out << rowData.join(";") << "\n";
                }
            }
        }
    }
}

void InventoryMoveTree::saveAsPdf(const QString &baseFilePath, const QDate &startDate, const QDate &endDate, bool multipleFile)
{
    QStringList headers;
    int startCol = multipleFile ? COL_SKU : COL_FROM;
    for (int c = startCol; c < COL_COUNT; ++c) {
        headers << headerData(c, Qt::Horizontal).toString();
    }

    QString dateStr;
    if (startDate.isValid() && endDate.isValid()) {
        if (startDate.day() == 1 && endDate == startDate.addMonths(1).addDays(-1)) {
            dateStr = startDate.toString("yyyy-MM");
        } else {
            dateStr = startDate.toString("yyyy-MM-dd") + " to " + endDate.toString("yyyy-MM-dd");
        }
    }

    if (!multipleFile) {
        double totalValue = 0.0;
        for (int i = 0; i < m_rootItem->childCount(); ++i) {
            totalValue += m_rootItem->child(i)->data(COL_TOTAL_PRICE).toDouble();
        }
        QString path = baseFilePath + "__" + QString::number(totalValue, 'f', 2) + m_companyCurrency + ".pdf";

        ReportGenerator rep;
        QString title = tr("Inventory Moves");
        if (!dateStr.isEmpty()) {
            title += " - " + dateStr;
        }
        rep.addTitle(title);
        rep.startTable(headers);
        for (int i = 0; i < m_rootItem->childCount(); ++i) {
            InventoryMoveTreeItem *parentItem = m_rootItem->child(i);
            for (int j = 0; j < parentItem->childCount(); ++j) {
                InventoryMoveTreeItem *childItem = parentItem->child(j);
                double unitPrice = childItem->data(COL_UNIT_PRICE).toDouble();
                if (unitPrice == 0.0)
                    continue; // Remove lines with unit price to 0

                QStringList rowData;
                for (int c = startCol; c < COL_COUNT; ++c) {
                    if (c == COL_UNIT_PRICE || c == COL_TOTAL_PRICE) {
                        rowData << QString::number(childItem->data(c).toDouble(), 'f', 2);
                    } else {
                        rowData << childItem->data(c).toString();
                    }
                }
                rep.addRow(rowData);
            }
        }
        rep.endTable();
        rep.save(path);
    } else {
        for (int i = 0; i < m_rootItem->childCount(); ++i) {
            InventoryMoveTreeItem *parentItem = m_rootItem->child(i);
            double value = parentItem->data(COL_TOTAL_PRICE).toDouble();
            QString from = parentItem->data(COL_FROM).toString();
            QString to = parentItem->data(COL_TO).toString();
            QString path = baseFilePath + "-" + from + "-" + to + "__" + QString::number(value, 'f', 2) + m_companyCurrency + ".pdf";

            ReportGenerator rep;
            rep.setLandscape(true);
            QString title = tr("Inventory Moves - %1 to %2").arg(from, to);
            if (!dateStr.isEmpty()) {
                title += " (" + dateStr + ")";
            }
            rep.addTitle(title);
            rep.startTable(headers);
            for (int j = 0; j < parentItem->childCount(); ++j) {
                InventoryMoveTreeItem *childItem = parentItem->child(j);
                double unitPrice = childItem->data(COL_UNIT_PRICE).toDouble();
                if (unitPrice == 0.0)
                    continue; // Remove lines with unit price to 0

                QStringList rowData;
                for (int c = startCol; c < COL_COUNT; ++c) {
                    if (c == COL_UNIT_PRICE || c == COL_TOTAL_PRICE) {
                        rowData << QString::number(childItem->data(c).toDouble(), 'f', 2);
                    } else {
                        rowData << childItem->data(c).toString();
                    }
                }
                rep.addRow(rowData);
            }
            rep.endTable();
            rep.save(path);
        }
    }
}

// ---------------------------------------------------------------------------
// Real-time file-system monitoring
// ---------------------------------------------------------------------------

void InventoryMoveTree::onDirectoryChanged()
{
    // Calling start() on an already-running single-shot timer restarts its
    // countdown, so rapid successive events extend the quiet period and
    // produce exactly one rebuild once the burst stops.
    m_rebuildTimer->start();
}

void InventoryMoveTree::rebuild()
{
    beginResetModel();
    delete m_rootItem;
    m_rootItem = new InventoryMoveTreeItem();
    buildTree(m_countryCode_sku_unitImported, m_countryCode_sku_unitExported);
    // Pick up any subdirectories that appeared since the last build.
    watchRecursive(m_purchaseDir.absolutePath());
    endResetModel();
}

void InventoryMoveTree::watchRecursive(const QString &path)
{
    QStringList paths;
    paths << path;
    QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        paths << it.next();
    // addPaths() silently ignores paths that are already watched or do not exist.
    m_watcher->addPaths(paths);
}

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

QStringList InventoryMoveTree::getSkusWithNoPrice() const
{
    QStringList result;
    QSet<QString> seen;

    const int parentCount = m_rootItem->childCount();
    for (int i = 0; i < parentCount; ++i) {
        InventoryMoveTreeItem *parentItem = m_rootItem->child(i);
        const int childCount = parentItem->childCount();
        for (int j = 0; j < childCount; ++j) {
            InventoryMoveTreeItem *childItem = parentItem->child(j);
            if (childItem->data(COL_UNIT_PRICE).toDouble() == 0.0) {
                const QString sku = childItem->data(COL_SKU).toString();
                if (!seen.contains(sku)) {
                    seen.insert(sku);
                    result.append(sku);
                }
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void InventoryMoveTree::loadPurchaseData(QHash<QString, PurchaseInfo> &purchaseData) const
{
    // Collect purchase CSVs, sorted newest-first by bare filename.
    QStringList filters;
    filters << "*.csv" << "*.CSV";
    QDirIterator it(m_purchaseDir.absolutePath(), filters,
                    QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    QStringList allFiles;
    while (it.hasNext())
        allFiles << it.next();

    std::sort(allFiles.begin(), allFiles.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() > QFileInfo(b).fileName();
    });

    // Append invoice fallback files (all years) AFTER the sorted purchase CSVs.
    // The hasPriceFromFile guard ensures they are only consulted for SKUs whose
    // price was not found in any regular purchase file.
    {
        QDir invDir(m_workingDir.absoluteFilePath(QStringLiteral("inventory")));
        if (invDir.exists()) {
            InventoryInvoicesTree invoicesTree(m_workingDir);
            const QFileInfoList yearDirs =
                    invDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &yearFi : yearDirs) {
                bool ok;
                int year = yearFi.fileName().toInt(&ok);
                if (ok)
                    allFiles << invoicesTree.getCsvInvoices(year);
            }
        }
    }

    // Parse all files via the shared loader.  Currency conversion is applied
    // inside parseFiles so unitPrice is already in companyCurrency.
    // purchaseDir doubles as settingsDir for PurchaseFileSettingsTree.
    const QList<PurchaseCsvLoader::Record> records =
            PurchaseCsvLoader::parseFiles(allFiles, m_purchaseDir,
                                          m_companyCurrency, m_currencyRateManager);

    // Aggregate: "latest price + highest-priority title" policy.
    for (const PurchaseCsvLoader::Record &rec : records) {
        PurchaseInfo &info = purchaseData[rec.sku];

        // Title: keep the record from the highest-priority language file.
        if (!rec.title.isEmpty() && rec.titlePriority > info.titlePriority) {
            info.title         = rec.title;
            info.titlePriority = rec.titlePriority;
        }

        // Price / weight: first valid (> 0) record wins (newest file first).
        if (!info.hasPriceFromFile && rec.origUnitPrice > 0.0) {
            info.unitPrice        = rec.unitPrice;       // already converted
            info.origUnitPrice    = rec.origUnitPrice;
            info.origCurrency     = rec.origCurrency;
            info.invoiceCurrency  = rec.invoiceCurrency;
            info.weight           = rec.weightKg;
            info.purchaseDate     = rec.date;
            info.purchaseFile     = rec.fileName;
            info.hasPriceFromFile = true;
        }
    }
}

void InventoryMoveTree::buildTree(
        const QHash<QString, QHash<QString, int>> &countryCode_sku_unitImported,
        const QHash<QString, QHash<QString, int>> &countryCode_sku_unitExported)
{
    m_canonicalsWithPrice.clear();

    QHash<QString, PurchaseInfo> purchaseData;
    loadPurchaseData(purchaseData);

    const QString &companyCurrency = m_companyCurrency;

    // Create a Parent item with one Child per SKU, then aggregate.
    // warehouseCountry: the Amazon warehouse country (used for shipping cost lookup).
    auto addParentRow = [&](const QString &from, const QString &to,
                            const QHash<QString, int> &sku_units,
                            const QString &warehouseCountry)
    {
        InventoryMoveTreeItem *parentItem =
                new InventoryMoveTreeItem(from, to, companyCurrency, m_rootItem);

        for (auto it = sku_units.cbegin(); it != sku_units.cend(); ++it) {
            const QString &sku = it.key();
            const int units    = it.value();
            // Regraded SKUs (amzn.gr.*) are resolved to their original canonical SKU
            // before looking up purchase data; the tree item still shows the regraded name.
            // Primary: heuristic (strip "amzn.gr." + drop last 2 dash-separated parts).
            // Fallback: SkuRegradedTable manual mapping when the heuristic canonical has
            //           no purchase data (~20 % of cases where Amazon shortened the SKU).
            QString resolvedSku = resolveSkuForPurchaseLookup(sku);
            if (m_skuRegradedTable && !purchaseData.value(resolvedSku).hasPriceFromFile) {
                const QString mapped = m_skuRegradedTable->getSku(sku);
                if (!mapped.isEmpty())
                    resolvedSku = mapped;
            }
            const PurchaseInfo &info = purchaseData.value(resolvedSku);

            double finalPrice  = 0.0;
            // origAmount / origCurrency: only set when conversion was applied.
            double origAmount     = info.origCurrency.isEmpty() ? 0.0 : info.origUnitPrice;
            QString origCurrency  = info.origCurrency;
            QString purchaseFile;
            // Currency displayed in the tree: company currency when known,
            // otherwise fall back to the invoice currency so the column is never empty.
            const QString displayCurrency = !companyCurrency.isEmpty()
                    ? companyCurrency
                    : info.invoiceCurrency;

            if (info.hasPriceFromFile) {
                // unitPrice is already in companyCurrency (converted by PurchaseCsvLoader).
                // Shipping cost: weight (kg) × price-per-kg for this warehouse.
                double shippingCost = info.weight
                        * m_country_pricePerKilo.value(warehouseCountry, 0.0);

                finalPrice   = info.unitPrice + shippingCost;
                purchaseFile = info.purchaseFile;
                m_canonicalsWithPrice.insert(resolvedSku);
            } else {
                // No purchase invoice found for this SKU.
                finalPrice   = 0.0;
                purchaseFile = tr("No invoice found");
            }

            InventoryMoveTreeItem *childItem =
                    new InventoryMoveTreeItem(from, to, sku, info.title,
                                             units, finalPrice,
                                             displayCurrency,
                                             origAmount, origCurrency,
                                             purchaseFile, parentItem);
            parentItem->appendChild(childItem);
        }

        parentItem->aggregate();
        m_rootItem->appendChild(parentItem);
    };

    // Imported into countryCode from EU (e.g. FR → from=EU, to=FR).
    // Warehouse country = destination country code.
    for (auto it = countryCode_sku_unitImported.cbegin();
         it != countryCode_sku_unitImported.cend(); ++it) {
        addParentRow(QStringLiteral("EU"), it.key(), it.value(), it.key());
    }

    // Exported from countryCode to EU (e.g. FR → from=FR, to=EU).
    // Warehouse country = origin country code.
    for (auto it = countryCode_sku_unitExported.cbegin();
         it != countryCode_sku_unitExported.cend(); ++it) {
        addParentRow(it.key(), QStringLiteral("EU"), it.value(), it.key());
    }
}

// ---------------------------------------------------------------------------
// QAbstractItemModel interface
// ---------------------------------------------------------------------------

QModelIndex InventoryMoveTree::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    InventoryMoveTreeItem *parentItem = parent.isValid()
            ? static_cast<InventoryMoveTreeItem*>(parent.internalPointer())
            : m_rootItem;

    InventoryMoveTreeItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex InventoryMoveTree::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    InventoryMoveTreeItem *childItem  = static_cast<InventoryMoveTreeItem*>(index.internalPointer());
    InventoryMoveTreeItem *parentItem = childItem->parentItem();

    if (parentItem == m_rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int InventoryMoveTree::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;

    InventoryMoveTreeItem *parentItem = parent.isValid()
            ? static_cast<InventoryMoveTreeItem*>(parent.internalPointer())
            : m_rootItem;

    return parentItem->childCount();
}

int InventoryMoveTree::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<InventoryMoveTreeItem*>(parent.internalPointer())->columnCount();
    return m_rootItem->columnCount();
}

QVariant InventoryMoveTree::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        InventoryMoveTreeItem *item = static_cast<InventoryMoveTreeItem*>(index.internalPointer());
        return item->data(index.column());
    }
    if (role == Qt::BackgroundRole && !m_companyCountryCode.isEmpty()) {
        InventoryMoveTreeItem *item = static_cast<InventoryMoveTreeItem*>(index.internalPointer());
        if (item->getType() == InventoryMoveTreeItem::Parent) {
            const QString from = item->data(COL_FROM).toString();
            const QString to   = item->data(COL_TO).toString();
            if (from == m_companyCountryCode || to == m_companyCountryCode)
                return QBrush(CompanyInfosTable::getHighlightColorDark());
        }
    }
    return QVariant();
}

QVariant InventoryMoveTree::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }

    switch (section) {
    case COL_FROM:          return tr("From");
    case COL_TO:            return tr("To");
    case COL_SKU:           return tr("SKU");
    case COL_PRODUCT_NAME:  return tr("Product name");
    case COL_UNITS:         return tr("Units");
    case COL_UNIT_PRICE:    return tr("Unit price");
    case COL_TOTAL_PRICE:   return tr("Total price");
    case COL_CURRENCY:      return tr("Currency");
    case COL_ORIG_AMOUNT:   return tr("Orig Unit Price");
    case COL_ORIG_CURRENCY: return tr("Orig currency");
    case COL_PURCHASE_FILE: return tr("Purchase File");
    }
    return QVariant();
}

Qt::ItemFlags InventoryMoveTree::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void InventoryMoveTree::sort(int column, Qt::SortOrder order)
{
    if (m_rootItem->childCount() <= 1)
        return;

    beginResetModel();

    // Collect and sort top-level (Parent) items only.
    QList<InventoryMoveTreeItem*> parents;
    for (int i = 0; i < m_rootItem->childCount(); ++i)
        parents.append(m_rootItem->child(i));

    const bool isNumeric = (column == COL_UNITS
                            || column == COL_UNIT_PRICE
                            || column == COL_TOTAL_PRICE);

    std::sort(parents.begin(), parents.end(),
              [column, order, isNumeric](InventoryMoveTreeItem *a, InventoryMoveTreeItem *b) {
                  if (isNumeric) {
                      double va = a->data(column).toDouble();
                      double vb = b->data(column).toDouble();
                      return (order == Qt::AscendingOrder) ? va < vb : va > vb;
                  }
                  QString sa = a->data(column).toString();
                  QString sb = b->data(column).toString();
                  return (order == Qt::AscendingOrder) ? sa < sb : sa > sb;
              });

    m_rootItem->detachChildren();
    for (InventoryMoveTreeItem *p : parents)
        m_rootItem->appendChild(p);

    endResetModel();
}
