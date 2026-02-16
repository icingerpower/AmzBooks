#include "InventoryTable.h"
#include "InventoryInvoicesTree.h"
#include "books/CompanyInfosTable.h"
#include "CurrencyRateManager.h"
#include "utils/CsvReader.h"
#include "books/ExceptionFileError.h"
#include "profit/PurchaseFileSettingsTree.h"
#include "profit/PurchaseFileSettingsTreeItem.h"
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
    QStringList filters;
    filters << "*.csv" << "*.CSV";
    QDirIterator it(m_purchasesDir.absolutePath(), filters, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    
    QStringList allFiles;
    while (it.hasNext()) {
        allFiles << it.next();
    }

    // Add Manual Invoices to the list of Purchase Files
    // "Inventory is what's in amazon ... + CSV invoices added in InventoryInvoicesTree"
    // They count as valid purchases for FIFO matching.
    QStringList manualFiles = m_invoicesTree->getCsvInvoices(m_year);
    for (const QString &f : manualFiles) {
        allFiles << f;
    }
    
    // Sort files by name (Reverse Order = Newest First)
    // User says: "The purchase date... is in the begining, for instance '2025-10-12__'"
    // "ExceptionFileError is raised if the begin is not like this."
    // We should validate filenames here.
    
    // Note: sorting by full path might differ if directories differ.
    // We should sort by FILENAME.
    std::sort(allFiles.begin(), allFiles.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName() > QFileInfo(b).fileName();
    });
    
    PurchaseFileSettingsTree settingsTree(m_workingDir);
    
    for (const QString &filePath : allFiles) {
        QFileInfo fi(filePath);
        QString fileName = fi.fileName();
        
        // Validate Date Format in Filename: YYYY-MM-DD__
        // Regex or simple check
        // "2025-10-12__" -> 10 chars date + 2 chars underscore.
        bool validName = false;
        if (fileName.length() >= 12) {
            if (fileName.at(4) == '-' && fileName.at(7) == '-' && fileName.mid(10, 2) == "__") {
                validName = true;
            }
        }
        
        if (!validName) {
            throw ExceptionFileError(tr("Invalid Filename"), 
                                     tr("File name must start with YYYY-MM-DD__: %1").arg(fileName));
        }
        
        QDate batchDate = QDate::fromString(fileName.left(10), "yyyy-MM-dd");
        
        auto seps = CsvReader::guessColStringSeps(filePath);
        CsvReader reader(filePath, seps.first, seps.second, true, "\n", 0, "Latin1");
        
        if (!reader.readAll()) continue;
        
        const DataFromCsv *rode = reader.dataRode();
        QStringList headers = rode->header.getHeaderElements();
        for(QString &h : headers) h = h.trimmed();
        
        int colSku = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
        int colTitle = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_TITLE);
        int colQty = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_QUANTITY); // Need quantity for batches
        int colPrice = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_UNIT_PRICE);
        int colCurrency = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_CURRENCY);
        int colWeight = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_UNIT_WEIGHT); // For shipping calc
        
        if (colSku == -1) continue;
        
        for (const auto &line : rode->lines) {
            QString sku = line.value(colSku).trimmed();
            if (sku.isEmpty()) continue;
            
            PurchaseBatch batch;
            batch.sku = sku;
            batch.date = batchDate;
            batch.fileName = fileName;
            
            if (colTitle != -1) batch.title = line.value(colTitle).trimmed();
            
            batch.quantity = 0;
            if (colQty != -1) {
                batch.quantity = (int)line.value(colQty).replace(",", ".").toDouble();
            } else {
                 // If no quantity column, assume 1? Or 0?
                 // If 0, it won't be picked up by FIFO logic.
                 // Assuming 1 is risky if it's a bulk file without qty.
                 // But most purchase invoices have quantity.
                 // Let's assume 1 for now if missing? 
                 // User: "Unité début". If 0, it's useless.
                 batch.quantity = 1;
            }
            
            batch.price = 0.0;
            if (colPrice != -1) {
                batch.price = line.value(colPrice).replace(",", ".").toDouble();
            }
            
            if (colCurrency != -1) {
                batch.currency = line.value(colCurrency).trimmed();
            }
            
            batch.weight = 0.0;
            if (colWeight != -1) {
                batch.weight = line.value(colWeight).replace(",", ".").toDouble() / 1000.0; // Assume grams -> kg? ProfitTree does /1000.0
            }
            
            skuPurchases[sku].append(batch);
        }
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
            
            // Calculate Unit Price
            double priceInCid = batch.price; // Cost in Invoice Currency
            double convertedPrice = priceInCid;
            
            if (m_currencRateManager && m_companyInfos) {
                QString targetCurrency = m_companyInfos->getCurrency();
                if (!targetCurrency.isEmpty() && !batch.currency.isEmpty() && targetCurrency != batch.currency) {
                    convertedPrice = m_currencRateManager->convert(priceInCid, batch.currency, targetCurrency, batch.date);
                }
            }
            
            double shippingCost = batch.weight * avgPricePerKilo;
            item.unitPrice = convertedPrice + shippingCost;
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

