#include "InventoryTable.h"
#include "InventoryInvoicesTree.h"
#include "PurchaseCsvLoader.h"
#include "books/CompanyInfosTable.h"
#include "CurrencyRateManager.h"
#include "profit/PurchaseFileSettingsTree.h"
#include "utils/CsvReader.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <QFile>
#include <QTextStream>

InventoryTable::InventoryTable(const QDir &workingDir, 
                               const QDir &purchasesDir, 
                               const QDir &amzLedgerDir, 
                               int year, 
                               const QHash<QString, double> &country_pricePerKilo, 
                               CompanyInfosTable *companyInfos, 
                               CurrencyRateManager *currencyRateManager, 
                               QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
    , m_purchasesDir(purchasesDir)
    , m_amzLedgerDir(amzLedgerDir)
    , m_year(year)
    , m_country_pricePerKilo(country_pricePerKilo)
    , m_companyInfos(companyInfos)
    , m_currencRateManager(currencyRateManager)
    , m_invoicesTree(new InventoryInvoicesTree(m_workingDir, this))
{
}

InventoryTable::~InventoryTable()
{
}

void InventoryTable::load()
{
    beginResetModel();
    m_items.clear();
    
    // 1. Load Stock from Ledger + Manual Invoices (InventoryInvoicesTree)
    QHash<QString, SkuStockInfo> skuStock;
    loadInventoryFromLedger(skuStock);
    
    // 2. Load Purchases
    QHash<QString, QList<PurchaseBatch>> skuPurchases;
    loadPurchases(skuPurchases);
    
    // 3. Build Table (FIFO Logic)
    buildTable(skuStock, skuPurchases);
    
    endResetModel();
}

void InventoryTable::loadInventoryFromLedger(QHash<QString, SkuStockInfo> &skuStock)
{
    // A. Parse Amazon Ledger Files
    QStringList filters;
    filters << "*.csv" << "*.CSV" << "*.txt" << "*.TXT";
    // Usually ledger files are in m_amzLedgerDir
    QDirIterator it(m_amzLedgerDir.absolutePath(), filters, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    
    QStringList allFiles;
    while (it.hasNext()) {
        allFiles << it.next();
    }
    
    // Filter by Year and Month (12/2025)
    QString yearStr = QString::number(m_year);
    QStringList yearMatches;
    for (const QString &f : allFiles) {
        QFileInfo fi(f);
        if (fi.fileName().contains(yearStr)) {
            yearMatches << f;
        }
    }
    
    QStringList filesToLoad;
    if (!yearMatches.isEmpty()) {
        // Try to find December (End of Year)
        QStringList decMatches;
        for (const QString &f : yearMatches) {
            QFileInfo fi(f);
            QString name = fi.fileName();
            // Check for 12 or Dec
            if (name.contains("-12") || name.contains("_12") || 
                name.contains("Dec", Qt::CaseInsensitive)) {
                decMatches << f;
            }
        }
        
        if (!decMatches.isEmpty()) {
            filesToLoad = decMatches;
        } else {
            // Fallback to year matches if no December file found
            filesToLoad = yearMatches;
        }
    } else {
        // Fallback to all files if no year specific file found (Legacy/Test support)
        filesToLoad = allFiles;
    }
    
    for (const QString &filePath : filesToLoad) {
        auto seps = CsvReader::guessColStringSeps(filePath);
        CsvReader reader(filePath, seps.first, seps.second, true, "\n", 0, "UTF-8"); // Assume UTF-8 for Amazon reports
        
        if (!reader.readAll()) continue;
        
        const DataFromCsv *rode = reader.dataRode();
        const CsvHeader &header = rode->header;
        
        int colSku = header.pos("MSKU");
        int colQty = header.pos("Ending Warehouse Balance");
        int colLocation = header.pos("Location");
        
        if (colSku == -1 || colQty == -1) continue;
        
        for (const auto &line : rode->lines) {
            QString sku = line.value(colSku).trimmed();
            QString qtyStr = line.value(colQty).trimmed();
            QString country = (colLocation != -1) ? line.value(colLocation).trimmed() : "";
            
            // Should verify if this line is valid? 
            // Aggregating quantities.
            bool ok;
            double q = qtyStr.replace(",", ".").toDouble(&ok);
            if (ok) {
                skuStock[sku].totalQty += (int)q;
                if (!country.isEmpty()) {
                    skuStock[sku].qtyPerCountry[country] += (int)q;
                } else {
                    skuStock[sku].qtyPerCountry[""] += (int)q; // Unknown location
                }
            }
        }
    }
    
    // B. Parse Manual Invoices from InventoryInvoicesTree
    // "Inventory is what's in amazon ... + CSV invoices added in InventoryInvoicesTree"
    // "Usually the inventory not delivered yet"
    // These are CSV invoices. They contain Purchases (Qty).
    // We treat them as Stock that is NOT in Amazon yet (so location is likely Default or Transit?).
    // We add them to TotalQty.
    // InventoryInvoicesTree now is a Model. We use getCsvInvoices(m_year).
    
    QStringList manualFiles = m_invoicesTree->getCsvInvoices(m_year);
    PurchaseFileSettingsTree settingsTree(m_workingDir);
    
    for (const QString &filePath : manualFiles) {
        auto seps = CsvReader::guessColStringSeps(filePath);
        CsvReader reader(filePath, seps.first, seps.second, true, "\n", 0, "Latin1");
        
        if (!reader.readAll()) continue;
        
        const DataFromCsv *rode = reader.dataRode();
        QStringList headers = rode->header.getHeaderElements();
        for(QString &h : headers) h = h.trimmed();
        
        int colSku = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
        int colQty = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_QUANTITY);
        
        if (colSku == -1) continue;
        
        for (const auto &line : rode->lines) {
            QString sku = line.value(colSku).trimmed();
            int qty = 1; // Default to 1 if no quantity col? Or 0?
             // "Unité début" implies we should have quantity. 
             // If manual invoice is just list of items, presume 1?
            if (colQty != -1) {
                qty = (int)line.value(colQty).replace(",", ".").toDouble();
            }
            
            if (!sku.isEmpty()) {
                skuStock[sku].totalQty += qty;
                // Add to "" country (unknown/transit)
                skuStock[sku].qtyPerCountry[""] += qty;
            }
        }
    }
}

void InventoryTable::loadPurchases(QHash<QString, QList<PurchaseBatch>> &skuPurchases)
{
    // Collect purchase CSVs from purchasesDir.
    QStringList filters;
    filters << "*.csv" << "*.CSV";
    QDirIterator it(m_purchasesDir.absolutePath(), filters,
                    QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    QStringList allFiles;
    while (it.hasNext())
        allFiles << it.next();

    // Add invoice files for the current year BEFORE sorting so they are ordered
    // alongside the regular purchase CSVs for correct FIFO date sequencing.
    const QStringList manualFiles = m_invoicesTree->getCsvInvoices(m_year);
    allFiles << manualFiles;

    // Sort newest-first by bare filename (YYYY-MM-DD__ prefix).
    std::sort(allFiles.begin(), allFiles.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() > QFileInfo(b).fileName();
    });

    // Parse all files via the shared loader (with currency conversion).
    // whose name does not start with a valid YYYY-MM-DD__ prefix.
    const QString companyCurrency = (m_companyInfos && m_currencRateManager)
            ? m_companyInfos->getCurrency() : QString();
    const QList<PurchaseCsvLoader::Record> records =
            PurchaseCsvLoader::parseFiles(allFiles, m_workingDir,
                                          companyCurrency, m_currencRateManager);

    // Build per-SKU FIFO batch lists.  Files were sorted newest-first so
    // records are in newest-first order; buildTable walks them in that order.
    for (const PurchaseCsvLoader::Record &rec : records) {
        if (rec.quantity <= 0)
            continue;
        PurchaseBatch batch;
        batch.sku      = rec.sku;
        batch.title    = rec.title;
        batch.date     = rec.date;
        batch.price    = rec.unitPrice;  // already converted to companyCurrency
        batch.quantity = rec.quantity;
        batch.weight   = rec.weightKg;
        batch.fileName = rec.fileName;
        skuPurchases[rec.sku].append(batch);
    }
}

void InventoryTable::buildTable(const QHash<QString, SkuStockInfo> &skuStock, 
                                QHash<QString, QList<PurchaseBatch>> &skuPurchases)
{
    // Iterate over all SKUs that have stock
    for (auto it = skuStock.begin(); it != skuStock.end(); ++it) {
        QString sku = it.key();
        const SkuStockInfo &info = it.value();
        int stockToAccountFor = info.totalQty;
        
        if (stockToAccountFor <= 0) continue;
        
        // Calculate Weighted Average Shipping Cost for this SKU based on Location
        double totalWeightCost = 0.0;
        int weightCostQty = 0;
        
        for (auto locIt = info.qtyPerCountry.begin(); locIt != info.qtyPerCountry.end(); ++locIt) {
            QString country = locIt.key();
            int qty = locIt.value();
            if (qty <= 0) continue;
            
            double pricePerKilo = m_country_pricePerKilo.value(country, m_country_pricePerKilo.value(""));
            // We need weight from Purchases to calculate actual cost.
            // But we don't know which purchase is where.
            // We'll apply this per-kilo price to the weight of the batch later.
            // So we just calculate average PricePerKilo for the stock?
            // Yes.
            totalWeightCost += (pricePerKilo * qty);
            weightCostQty += qty;
        }
        
        double avgPricePerKilo = (weightCostQty > 0) ? (totalWeightCost / weightCostQty) : 0.0;
        
        // Get Purchases (sorted newest first)
        QList<PurchaseBatch> &batches = skuPurchases[sku];
        // Ensure sorted (though loadPurchases adds them in file order, which is correct)
        // Files are Newest -> Oldest. List append -> Newest at index 0?
        // Yes, "Sort allFiles... greater". And we loop files.
        // So skuPurchases[sku] has Newest batches first.
        
        if (batches.isEmpty()) {
            // Ghost Inventory? User didn't specify handling.
            // We create a "Unknown Source" row?
            // "Unit price is got from the CSV purchase invoices". If no invoice, no price.
            // But we must show inventory.
            // Create a dummy batch?
            InventoryItem item;
            item.sku = sku;
            item.title = "No Purchase Record";
            item.date = QDate();
            item.unitStart = stockToAccountFor;
            item.unitRemaining = stockToAccountFor;
            item.unitPrice = 0.0;
            item.totalPrice = 0.0;
            item.invoiceName = "Unknown";
            m_items.append(item);
            continue;
        }
        
        for (const PurchaseBatch &batch : batches) {
            if (stockToAccountFor <= 0) break;
            
            int taken = std::min(batch.quantity, stockToAccountFor);
            
            // Create Inventory Item
            InventoryItem item;
            item.sku = sku;
            item.title = batch.title;
            item.date = batch.date;
            item.unitStart = batch.quantity;
            item.unitRemaining = taken; // This batch contributes 'taken' units to current inventory
            
            // batch.price is already in companyCurrency (converted by PurchaseCsvLoader).
            double shippingCost = batch.weight * avgPricePerKilo;
            item.unitPrice = batch.price + shippingCost;
            item.totalPrice = item.unitRemaining * item.unitPrice;
            item.invoiceName = batch.fileName; // "Facture is the base name of the purchase file"
            
            m_items.append(item);
            
            stockToAccountFor -= taken;
        }
        
        // If still stock remaining (Unaccounted for by purchases)
        if (stockToAccountFor > 0) {
             InventoryItem item;
            item.sku = sku;
            item.title = batches.first().title; // Use title from latest purchase
            item.date = QDate(); // Unknown date
            item.unitStart = stockToAccountFor;
            item.unitRemaining = stockToAccountFor;
            item.unitPrice = 0.0; // Unknown price
            item.totalPrice = 0.0;
            item.invoiceName = "Old/Unknown";
            m_items.append(item);
        }
    }
}

void InventoryTable::exportToCsv(const QString &file, bool round)
{
    QFile csv(file);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    
    QTextStream out(&csv);
    out.setEncoding(QStringConverter::Utf8); 

    
    // Header
    QStringList headers;
    headers << tr("SKU") << tr("Titre") << tr("Date") 
            << tr("Unité début") << tr("Unité restante") 
            << tr("Prix unitaire") << tr("Prix total") << tr("Facture");
    out << headers.join(";") << "\n";
    
    // Sort before export? User says "The class implements sort. By default, sorting is by SKU".
    // m_items might be unsorted.
    
    for (const auto &item : m_items) {
        QStringList row;
        row << item.sku;
        row << item.title;
        row << item.date.toString("yyyy-MM-dd");
        row << QString::number(item.unitStart);
        row << QString::number(item.unitRemaining);
        
        if (round) {
            row << QString::number(item.unitPrice, 'f', 2);
            row << QString::number(item.totalPrice, 'f', 2);
        } else {
            row << QString::number(item.unitPrice);
            row << QString::number(item.totalPrice);
        }
        
        row << item.invoiceName;
        
        out << row.join(";") << "\n";
    }
    
    csv.close();
}

int InventoryTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

int InventoryTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return COL_COUNT;
}

QVariant InventoryTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size()) return QVariant();
    
    const InventoryItem &item = m_items[index.row()];
    
    if (role == Qt::DisplayRole) {
        return item.data(index.column());
    }
    
    return QVariant();
}

QVariant InventoryTable::InventoryItem::data(int column) const
{
    switch (column) {
    case COL_SKU: return sku;
    case COL_TITLE: return title;
    case COL_DATE: return date;
    case COL_UNIT_START: return unitStart;
    case COL_UNIT_REMAINING: return unitRemaining;
    case COL_UNIT_PRICE: return unitPrice; // Formatting done in view? Or here?
        // User didn't specify formatting for UI, only for Export.
        // Returning double is safer for sorting.
    case COL_TOTAL_PRICE: return totalPrice;
    case COL_INVOICE: return invoiceName;
    }
    return QVariant();
}

QVariant InventoryTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case COL_SKU: return tr("SKU");
        case COL_TITLE: return tr("Titre");
        case COL_DATE: return tr("Date");
        case COL_UNIT_START: return tr("Unité début");
        case COL_UNIT_REMAINING: return tr("Unité restante");
        case COL_UNIT_PRICE: return tr("Prix unitaire");
        case COL_TOTAL_PRICE: return tr("Prix total");
        case COL_INVOICE: return tr("Facture");
        }
    }
    return QVariant();
}

void InventoryTable::sort(int column, Qt::SortOrder order)
{
    std::sort(m_items.begin(), m_items.end(), 
        [column, order](const InventoryItem &a, const InventoryItem &b) {
            QVariant va = a.data(column);
            QVariant vb = b.data(column);
            
            bool result = false;
            if (va.typeId() == QMetaType::Double || va.typeId() == QMetaType::Int) {
                 result = va.toDouble() < vb.toDouble();
            } else if (va.typeId() == QMetaType::QDate) {
                 result = va.toDate() < vb.toDate();
            } else {
                 result = va.toString() < vb.toString();
            }
            
            return (order == Qt::AscendingOrder) ? result : !result;
        });
    emit layoutChanged();
}

double InventoryTable::getTotalValue() const
{
    double total = 0.0;
    for (const auto &item : m_items) {
        total += item.totalPrice;
    }
    return total;
}

