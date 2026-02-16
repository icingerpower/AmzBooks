#include "ProfitTree.h"
#include <QDebug>
#include "ProfitTreeItem.h"
#include "books/CompanyInfosTable.h"
#include "profit/PurchaseFileSettingsTree.h"
#include "CurrencyRateManager.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include "CountriesEu.h"
#include "ExceptionRateCurrency.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>
#include <numeric>
#include "books/ExceptionFileError.h"

ProfitTree::ProfitTree(const QDir &workingDir,
                       const QDir &economicsDir, 
                       const QDir &purchasesDir, 
                       const QDate &startDate, 
                       int minUnitSold, 
                       double avgPricePerKilo,
                       CompanyInfosTable *companyInfos, 
                       CurrencyRateManager *currencyRateManager,
                       QObject *parent)
    : QAbstractItemModel(parent)
    , m_workingDir(workingDir)
    , m_economicsDir(economicsDir)
    , m_purchasesDir(purchasesDir)
    , m_startDate(startDate)
    , m_minUnitSold(minUnitSold)
    , m_avgPricePerKilo(avgPricePerKilo)
    , m_companyInfos(companyInfos)
    , m_currencyRateManager(currencyRateManager),
      m_rootItem(new ProfitTreeItem({
          tr("Parent ASIN"), 
          tr("MSKU"), 
          tr("Title"), 
          tr("Units sold"), 
          tr("Monthly Units"),
          tr("Unit returned"),
          tr("Return %"),
          tr("Avg Sale Price"), 
          tr("Profit Per Unit"), 
          tr("Profit %"), 
          tr("Profit / Capital"), 
          tr("Avg Import Price"), 
          tr("Unit Price"), 
          tr("Profit without ads"), 
          tr("Ads cost"), 
          tr("Storage cost"), 
          tr("FBA fees"), 
          tr("Referal fees"), 
          tr("Other fees"), 
          tr("Total Ads"),
          tr("Total Storage"),
          tr("Total FBA"),
          tr("Total Referral"),
          tr("Total Other"),
          tr("Total Amz Costs"),
          tr("FBA fees most sold country"), 
          tr("ASIN")
      }))
{
}

ProfitTree::~ProfitTree()
{
    delete m_rootItem;
}

void ProfitTree::load()
{
    setupTreeData();
}

QModelIndex ProfitTree::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    ProfitTreeItem *parentItem;
    if (!parent.isValid())
        parentItem = m_rootItem;
    else
        parentItem = static_cast<ProfitTreeItem*>(parent.internalPointer());

    ProfitTreeItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex ProfitTree::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    ProfitTreeItem *childItem = static_cast<ProfitTreeItem*>(index.internalPointer());
    ProfitTreeItem *parentItem = childItem->parentItem();

    if (parentItem == m_rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int ProfitTree::rowCount(const QModelIndex &parent) const
{
    ProfitTreeItem *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = m_rootItem;
    else
        parentItem = static_cast<ProfitTreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

int ProfitTree::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<ProfitTreeItem*>(parent.internalPointer())->columnCount();
    return m_rootItem->columnCount();
}

QVariant ProfitTree::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    ProfitTreeItem *item = static_cast<ProfitTreeItem*>(index.internalPointer());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        QVariant val = item->data(index.column());
        int col = index.column();

        if (col == COL_PROFIT_PERCENT || col == COL_RETURN_PERCENT) {
            double v = val.toDouble();
            return QString::number(v * 100.0, 'f', 1) + "%";
        }
        
        // Other double columns
        if (col == COL_AVG_SALE_PRICE ||
            col == COL_PROFIT_PER_UNIT ||
            col == COL_PROFIT_PER_CAPITAL ||
            col == COL_AVG_IMPORT_PRICE ||
            col == COL_UNIT_PRICE ||
            col == COL_PROFIT_NO_ADS_PER_UNIT ||
            col == COL_ADS_COST_PER_UNIT ||
            col == COL_STORAGE_COST_PER_UNIT ||
            col == COL_FBA_FEES_PER_UNIT ||
            col == COL_REFERRAL_FEES_PER_UNIT ||
            col == COL_OTHER_FEES_PER_UNIT ||
            col == COL_FBA_FEES_MOST_SOLD) 
        {
            double v = val.toDouble();
            return QString::number(v, 'f', 2);
        }

        return val;
    } else if (role == Qt::BackgroundRole) {
        if (index.column() == COL_UNIT_PRICE && item->isPinkBackground()) {
            return QColor(Qt::magenta).darker(180);
        }
    }

    return QVariant();
}

QVariant ProfitTree::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return m_rootItem->data(section);
    return QVariant();
}

Qt::ItemFlags ProfitTree::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEditable | QAbstractItemModel::flags(index);
}

void ProfitTree::sort(int column, Qt::SortOrder order)
{
    if (m_rootItem->childCount() <= 1) return;
    
    beginResetModel();
    
    // Get children list
    QList<ProfitTreeItem*> children;
    for (int i = 0; i < m_rootItem->childCount(); ++i) {
        children.append(m_rootItem->child(i));
    }
    
    // Sort
    bool isNumeric = (column >= 3 && column <= 13);
    std::sort(children.begin(), children.end(), 
        [column, order, isNumeric](ProfitTreeItem *a, ProfitTreeItem *b) {
            if (isNumeric) {
                double va = a->data(column).toDouble();
                double vb = b->data(column).toDouble();
                return (order == Qt::AscendingOrder) ? va < vb : va > vb;
            } else {
                QString sa = a->data(column).toString();
                QString sb = b->data(column).toString();
                return (order == Qt::AscendingOrder) ? sa < sb : sa > sb;
            }
        });
    
    // Rebuild children in sorted order
    m_rootItem->detachChildren();
    for (ProfitTreeItem *child : children) {
        m_rootItem->appendChild(child);
    }
    
    endResetModel();
}

void ProfitTree::setupTreeData()
{
    beginResetModel();
    m_rootItem->removeChildren();

    // 1. Gather Purchase Data
    QHash<QString, PurchaseData> purchaseDataMap;
    loadPurchaseData(purchaseDataMap);

    // 2. Aggregate Data from Economics CSVs
    QHash<QString, QHash<QString, AggregatedData>> parentMap;
    
    QStringList filters;
    filters << "*.csv" << "*.CSV";
    QDirIterator it(m_economicsDir.absolutePath(), filters, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    
    bool filesFound = false;
    while (it.hasNext()) {
        filesFound = true;
        processEconomicsFile(it.next(), parentMap, purchaseDataMap);
    }
    
    if (!filesFound) {
        throw ExceptionFileError(tr("No Economics Files"), 
                                 tr("No economics CSV files found recursively in: %1").arg(m_economicsDir.absolutePath()));
    }
    
    if (parentMap.isEmpty()) {
        throw ExceptionFileError(tr("No Valid Economics Data"), 
                                 tr("Economics files were found but no valid data rows could be extracted."));
    }

    // 3. Build Tree
    for (auto itParent = parentMap.begin(); itParent != parentMap.end(); ++itParent) {
        QString parentKey = itParent.key();
        QHash<QString, AggregatedData> &childrenData = itParent.value();
        
        bool flatten = (childrenData.size() <= 1 && parentKey == childrenData.begin().key());
        // Logic: if only 1 child and ParentASIN == MSKU (or generic), flatten?
        // User said: "only if more than one row for the parent ASIN"
        
        if (childrenData.size() > 1) {
            // Create Parent
            QVector<QVariant> pData(27);
            pData[0] = parentKey;
            ProfitTreeItem *pItem = new ProfitTreeItem(pData, m_rootItem);
            pItem->setParentAsin(parentKey);
            
            QString bestTitle;
            int maxPriority = -1;
            
            // Parent Aggregation for Country Logic
            QHash<QString, int> totalSalesPerCountry;
            QHash<QString, double> totalFbaFeesPerCountry;
            QHash<QString, int> totalFbaCountPerCountry;

            for (auto itChild = childrenData.begin(); itChild != childrenData.end(); ++itChild) {
                QString msku = itChild.key();
                AggregatedData &agg = itChild.value();
                const PurchaseData &pDataChild = purchaseDataMap[msku];
                
                ProfitTreeItem *cItem = createItemFromAgg(agg, msku, pDataChild, pItem);
                pItem->appendChild(cItem);
                
                calcMostSoldCountryFee(cItem, agg);
                
                // Aggregate for Parent
                QHashIterator<QString, int> itS(agg.salesPerCountry);
                while (itS.hasNext()) {
                    itS.next();
                    totalSalesPerCountry[itS.key()] += itS.value();
                }
                QHashIterator<QString, double> itF(agg.fbaFeesPerCountry);
                while (itF.hasNext()) {
                    itF.next();
                    totalFbaFeesPerCountry[itF.key()] += itF.value();
                }
                QHashIterator<QString, int> itC(agg.fbaCountPerCountry);
                while (itC.hasNext()) {
                    itC.next();
                    totalFbaCountPerCountry[itC.key()] += itC.value();
                }
                
                // Track Best Title
                // Priority: COM(5) > CA(4) > FR(3) > DE(2) > Other(1)
                // PurchaseData already has titlePriority from file loading?
                // Yes: data.titlePriority = priority;
                if (!pDataChild.title.isEmpty() && pDataChild.titlePriority > maxPriority) {
                    maxPriority = pDataChild.titlePriority;
                    bestTitle = pDataChild.title;
                }
            }
            if (!bestTitle.isEmpty()) pItem->setTitle(bestTitle);
            
            if (!bestTitle.isEmpty()) pItem->setTitle(bestTitle);
            
            pItem->aggregate();
            
            // Set Parent FBA Fees Most Sold Country
            double maxSales = -1.0;
            QString topCountry;
            QHashIterator<QString, int> itP(totalSalesPerCountry);
            while (itP.hasNext()) {
                itP.next();
                if (itP.value() > maxSales) {
                    maxSales = itP.value();
                    topCountry = itP.key();
                }
            }
            
            double fbaParent = 0.0;
            if (!topCountry.isEmpty()) {
                int count = totalFbaCountPerCountry.value(topCountry);
                double totalFba = totalFbaFeesPerCountry.value(topCountry);
                if (count > 0) {
                    fbaParent = totalFba / count;
                }
            }
            pItem->setFbaFeesMostSoldCountry(fbaParent);
            pItem->aggregate();
            
            if (pItem->getUnitsGross() >= m_minUnitSold) {
                m_rootItem->appendChild(pItem);
            } else {
                delete pItem;
            }
        } else if (childrenData.size() == 1) {
            QString msku = childrenData.begin().key();
            AggregatedData &agg = childrenData.begin().value();
            
            ProfitTreeItem *item = createItemFromAgg(agg, msku, purchaseDataMap[msku], m_rootItem);
            item->setParentAsin(parentKey);
            calcMostSoldCountryFee(item, agg);
            
            if (item->getUnitsGross() >= m_minUnitSold) {
                m_rootItem->appendChild(item);
            } else {
                delete item;
            }
        }
    }
    
    endResetModel();
}

void ProfitTree::loadPurchaseData(QHash<QString, PurchaseData> &purchaseDataMap)
{
    QStringList filters;
    filters << "*.csv" << "*.CSV";
    QDirIterator it(m_purchasesDir.absolutePath(), filters, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    
    PurchaseFileSettingsTree settingsTree(m_workingDir);
    
    bool filesFound = false;
    // Collect all files first to sort them
    QStringList allFiles;
    while (it.hasNext()) {
        allFiles << it.next();
    }
    
    // Sort in reverse order (Assuming filename contains date or index increasing with time)
    // E.g. "purchases-2026.csv" > "purchases-2025.csv"
    std::sort(allFiles.begin(), allFiles.end(), std::greater<QString>());
    
    if (!allFiles.isEmpty()) filesFound = true;

    for (const QString &filePath : allFiles) {
        QFileInfo fi(filePath);
        QString fileName = fi.fileName();
        
        // Priority
        int priority = 1;
        if (fileName.contains("-US", Qt::CaseInsensitive) || fileName.contains("-COM", Qt::CaseInsensitive)) {
            priority = 5;
        }
        else if (fileName.contains("-CA", Qt::CaseInsensitive)) {
            priority = 4;
        }
        else if (fileName.contains("-FR", Qt::CaseInsensitive)) {
            priority = 3;
        }
        else if (fileName.contains("-DE", Qt::CaseInsensitive)) {
            priority = 2;
        }
        
        // Detect Separator
        auto seps = CsvReader::guessColStringSeps(filePath);
        CsvReader reader(filePath, seps.first, seps.second, true, "\n", 0, "Latin1");
        if (!reader.readAll()) {
            continue;
        }
        
        const DataFromCsv *rode = reader.dataRode();
        QStringList headers = rode->header.getHeaderElements();
        for(QString &h : headers) {
            h = h.trimmed();
        }
        
        int colTitle = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_TITLE);
        int colMsku = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
        int colUnitPrice = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_UNIT_PRICE);
        int colWeight = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_UNIT_WEIGHT);
        
        if (colMsku == -1) {
            continue;
        }
        
        for (const auto &line : rode->lines) {
            if (line.size() <= colMsku) {
                continue;
            }
            QString msku = line[colMsku].trimmed();
            if (msku.isEmpty()) {
                continue;
            }
            
            PurchaseData &data = purchaseDataMap[msku];
            
            if (colTitle != -1 && line.size() > colTitle) {
                QString title = line[colTitle].trimmed();
                if (!title.isEmpty() && priority > data.titlePriority) {
                    data.title = title;
                    data.titlePriority = priority;
                }
            }
            
            if (colUnitPrice != -1 && line.size() > colUnitPrice) {
                bool ok;
                double price = QString(line[colUnitPrice]).replace(",", ".").toDouble(&ok);
                if (ok && price > 0) {
                    if (data.costSamples == 0) {
                        data.cost = price;
                        data.costSamples = 1; 
                    }
                }
            }
            
            if (colWeight != -1 && line.size() > colWeight) {
                bool ok;
                double w = QString(line[colWeight]).replace(",", ".").toDouble(&ok);
                if (ok && w > 0) {
                    if (data.weightSamples == 0) {
                        data.weight = w / 1000.0;
                        data.weightSamples = 1;
                    }
                }
            }
        }
    }
    
    if (!filesFound) {
        throw ExceptionFileError(tr("No Invoice Files"), 
                                 tr("No purchase invoice CSV files found recursively in: %1").arg(m_purchasesDir.absolutePath()));
    }
    
    if (purchaseDataMap.isEmpty()) {
        throw ExceptionFileError(tr("No Valid Invoice Data"), 
                                 tr("Purchase invoice files were found but no valid data rows could be extracted."));
    }
}

void ProfitTree::processEconomicsFile(const QString &filePath, 
                                      QHash<QString, QHash<QString, AggregatedData>> &parentMap, 
                                      QHash<QString, PurchaseData> &purchaseDataMap)
{
    // Check if Economics file
    CsvReader reader(filePath, "\t", "\"", true, "\n", 0, "UTF-8"); // TSV? Or CSV? Extension is .csv but content might be tab?
    // User example: "Amazon store\tStart date..." -> Looks like TSV or fixed width?
    // "Amazon store   Start date..."
    // Usually Amazon reports are TSV or comma. 
    // I will try comma first, if fails try tab? Or guess?
    // CsvReader::guessColStringSeps?
    // Let's assume standard CSV (comma/semicolon) or Tab.
    // NOTE: User sample looks tab separated or space aligned.
    // "Amazon store\tStart date" usually implies Tab.
    // Safe bet: Try guessing or assume standardized delimiter.
    // I'll try auto-detect or default to TAB for "Amazon * Reports".
    
    // Attempt guess
    auto seps = CsvReader::guessColStringSeps(filePath);
    reader = CsvReader(filePath, seps.first, seps.second, true, "\n", 0, "UTF-8"); 
    
    if (!reader.readAll()) {
        return;
    }
    
    const DataFromCsv *rode = reader.dataRode();
    const CsvHeader &header = rode->header;

    
    
    // Map Columns
    // Required (Strict)
    int colParent = header.pos({"Parent ASIN", "ParentASIN"});
    int colMsku = header.pos({"MSKU", "SKU"});
    
    // Optional (for now? No, user said strict fee columns. What about these?)
    // User said "ProfitTree can't catch".
    // If these are missing, we CANNOT proceed.
    // So strict check matches rule.
    
    int colCountry = header.pos({"Amazon store", "Marketplace"});
    
    int colCurrency = header.pos({"Currency code", "Currency"});
    
    int colDate = header.pos({"Start date", "Date"});
    
    // Metrics
    
    // Strict Core columns
    int colGross = header.pos({"Units sold", "Units Sold"});
    
    int colSales = header.pos({"Net sales", "Sales", "Product Sales"});
    
    // Strict Optional metrics (Ads, Storage) -> No longer optional, exception if missing
    int colAds = header.pos({"SponsoredProductFee@stringId:SC_FBA_SER_total:X"
                             , "Sponsored Products charge total"
                             , "Ads Cost"
                             , "AdvertisingCost"});
    
    // Storage components
    QList<int> storageCols;
    storageCols << header.pos({"Monthly storage fee total", "Base monthly storage fee total"})
                // << header.pos("Base monthly storage fee total") already included in the previous one
                << header.pos({"Storage utilisation surcharge total", "Storage utilization surcharge total"})
                << header.pos("Aged inventory surcharge total");

    // FBA Fees: STRICT CHECK
    int colFba = header.pos({
                                "Fulfilment by Amazon fulfilment fees total",
                                "FBA fulfilment fees total",
                                "FBA fulfillment fees total",
                                "FbaFulfilmentFee@stringId:SC_FBA_SER_total:X"
                            });
    
    int colReferral = header.pos({"Referral fee total", "ReferralFee@stringId:SC_FBA_SER_total:X"});
    int colReferralRefund1 = header.pos(
                {"RefundedReferralFee@stringId:SC_FBA_SER_total:X"
                , "Refund administration fee total"}
                );
    /*
    int colReferralRefund2 = -1; // We do it exceptionnaly as this column were added mid-2026
    if (header.contains("RefundCommissionFee@stringId:SC_FBA_SER_total:X")) {
        colReferralRefund2 =
            header.pos("RefundCommissionFee@stringId:SC_FBA_SER_total:X");
    }
    else if (header.contains("Referral Fee Refunds total")) {
        colReferralRefund2 =
            header.pos("Referral Fee Refunds total");
    }
    //*/
    int colReferralRefund2 = header.pos({"RefundCommissionFee@stringId:SC_FBA_SER_total:X"
                                         , "Referral Fee Refunds total"});

    // Other Fees: Sum of remaining (Strict)
    QList<int> otherCols;
    
    // Explicitly add all required columns
    QStringList otherColNames{
        "Digital Services Fee (FBA Fulfilment fees) total"
        ,"Digital Services Fee (Selling on Amazon fees) total"
        , "FBA disposal order fee total"
        , "FBA removal order fee total"
        , "Inbound Transportation Fee total"
        , "Inbound Transportation Program Fee total"
        , "Liquidation processing fee total"
        , "Liquidation referral fee total"
        // , "Refund administration fee total" -> REMOVED to avoid double counting (included in colReferralRefund2)
        , "FbaCustomerReturnPerUnitFee total"
        , "Returns Processing Fee for Non-Apparel and Non-Shoes total"
        , "Returns processing fee for Apparel and Shoes total"
        , "total" // It is for return fees total

        , "FbaCustomerReturnPerUnitFee@stringId:SC_FBA_SER_total:X"
        , "Other fee"
    };
    for (const auto &otherColName : otherColNames)
    {
        if (header.contains(otherColName))
        {
            otherCols << header.pos(otherColName);
        }
    }

    int colReimb = header.pos("FBA Inventory Reimbursement total");

    int colRefunded = header.pos("Units returned");
    
    // Iterate lines
    for (const auto &line : rode->lines) {
        QString parentAsin = line.value(colParent).trimmed();
        QString msku = line.value(colMsku).trimmed();
        if (msku.isEmpty())  {
            continue;
        }
        if (parentAsin.isEmpty()) {
            parentAsin = msku;
        }
        
        QString currency = (colCurrency != -1) ? line.value(colCurrency).trimmed() : "EUR";
        QString country = (colCountry != -1) ? line.value(colCountry).trimmed() : "Unknown";
        QString dateStr = (colDate != -1) ? line.value(colDate).trimmed() : "";
        QDate date = QDate::currentDate(); // parsing dateStr? 
        if (date < m_startDate)
        {
            continue;
        }
        // 01/01/2026. QDate::fromString(dateStr, "MM/dd/yyyy")?
        // User sample: 01/01/2026 -> MM/dd/yyyy or dd/MM/yyyy?
        // Usually, Amazon Reports use localized dates or standard.
        // I will attempt standard formats.
        if (!dateStr.isEmpty()) {
            date = QDate::fromString(dateStr, "dd/MM/yyyy");
            if (!date.isValid()) {
                date = QDate::fromString(dateStr, "MM/dd/yyyy");
            }
            if (!date.isValid()) {
                date = QDate::fromString(dateStr, Qt::ISODate);
            }
        }
        Q_ASSERT(date.isValid());
        
        // Helper to get val
        auto getVal = [&](int col) -> double {
            if (col == -1 || col >= line.size()) return 0.0;
            return QString(line[col]).replace(",", ".").toDouble();
        };
        
        // Helper to convert
        auto valCvt = [&](double val) -> double {
            if (m_currencyRateManager && m_companyInfos) {
                QString target = m_companyInfos->getCurrency();
                if (!target.isEmpty() && target != currency) {
                    return m_currencyRateManager->convert(val, currency, target, date);
                }
            }
            return val;
        };
        
        double grossUnits = (colGross != -1) ? getVal(colGross) : 0.0;
        double refundedUnits = (colRefunded != -1) ? getVal(colRefunded) : 0.0;
        
        // Net logic: Gross - Refunded. 
        double unitsSold = grossUnits - refundedUnits; // Net Units

        double sales = valCvt(getVal(colSales));
        double ads = valCvt(getVal(colAds));
        double fba = valCvt(getVal(colFba));
        if (msku == "CJYD206234728BY" && fba > 0)
        {
            int TEMP=10;++TEMP;
        }
        double referral = valCvt(getVal(colReferral)) + valCvt(getVal(colReferralRefund1));
        if (colReferralRefund2 != -1)
        {
            referral += valCvt(getVal(colReferralRefund2));
        }
        
        double storage = 0.0;
        for (int c : storageCols) {
            storage += valCvt(getVal(c));
        }
        
        double other = 0.0;
        for (int c : otherCols) {
            other += valCvt(getVal(c));
        }
        
        double reimb = valCvt(getVal(colReimb));
        // Reimbursement reduces "Other Fees" (Cost) -> Negative Cost = Income?
        // Or strictly Profit = Sales - Fees - Costs. Reimbursement is "Income".
        // Let's treat it as reducing Other Fees. (Since other fees are Costs)
        other -= reimb; 
        
        // Populate AggregatedData
        AggregatedData &agg = parentMap[parentAsin][msku];
        agg.unitsGross += (int)grossUnits;
        agg.unitsReturned += (int)refundedUnits;
        agg.revenue += sales;
        
        // Revenue For Avg Price: Only if Gross Units > 0
        if (grossUnits > 0) {
            agg.revenueForAvgPrice += sales;
        }

        agg.adsCost += ads;
        agg.storageCost += storage;
        agg.fbaFees += fba;
        agg.referralFees += referral;
        agg.otherFees += other;
        
        // Track Country Sales (Net Units)
        // Track Country Sales (Net Units)
        if (unitsSold > 0) {
            agg.salesPerCountry[country] += (int)unitsSold;
        }
        
        // Track Monthly Sales (Net Units)
        // Key: yyyy-MM
        if (date.isValid()) {
            QString monthKey = date.toString("yyyy-MM");
            agg.unitsSoldPerMonth[monthKey] += (int)unitsSold;
        }
        
        // Track FBA Fees for this country for averaging
        agg.fbaFeesPerCountry[country] += fba;
        agg.fbaCountPerCountry[country] += (int)unitsSold; // Or transactions?
        // "FBA fees most sold country are the FBA fees average"
        // Fee Total / Units? 
        // If FBA Fee Total is for ALL units, then Average = Total / Units.
    }
}

ProfitTreeItem* ProfitTree::createItemFromAgg(
        const AggregatedData &agg, const QString &msku, const PurchaseData &pData, ProfitTreeItem *parent)
{
    double unitCost = pData.cost;
    bool isPink = false;
    
    // Fallback Cost
    if (unitCost <= 0.0001) {
        // 30% of Average Sale Price
        // Avg Sale Price = Revenue / Units
        // Use Gross Units for Avg Price Calculation?
        // Logic: if we don't have cost, assume 30% of selling price.
        double avgSale = (agg.unitsGross > 0) ? (agg.revenueForAvgPrice / agg.unitsGross) : 0.0;
        if (avgSale > 0) {
            unitCost = avgSale * 0.30;
            isPink = true;
        }
    }
    
    // Profit = Revenue - Fees - (Units * Cost)
    // Fees are typically positive numbers in the CSV (Cost magnitude).
    double totalFees = agg.adsCost + agg.storageCost + agg.fbaFees + agg.referralFees + agg.otherFees;
    // Convert Price to Currency
    double avgImportPrice = pData.weight * m_avgPricePerKilo;
    
    // Total Unit Cost = Purchase Cost + Import Cost
    double unitTotalCost = unitCost + avgImportPrice;
    
    // COGS = Net Units * UnitCost
    // Or Gross? Typically COGS is on Sold units. Returned units might be put back to stock?
    // If we count refund deduction, we usually assume Net Sales for COGS.
    auto unitsSoldNet = agg.unitsGross - agg.unitsReturned;
    double cogs = unitsSoldNet * unitTotalCost;
    
    // Profit = Revenue - Fees - COGS
    double profit = agg.revenue - totalFees - cogs;
    
    QVector<QVariant> data(27);
    data[COL_PARENT_ASIN] = ""; 
    data[COL_MSKU] = msku;
    data[COL_TITLE] = pData.title;
    data[COL_UNITS_SOLD] = unitsSoldNet;
    
    data[COL_ASIN] = msku; // ASIN/MSKU placeholder
    
    ProfitTreeItem *item = new ProfitTreeItem(data, parent);
    item->setUnitsSoldPerMonth(agg.unitsSoldPerMonth);
    // Median is calculated in setUnitsSoldPerMonth
    
    // ... rest
    
    item->setUnitsGross(agg.unitsGross); // NEW
    item->setUnitsReturned(agg.unitsReturned);
    item->setRevenue(agg.revenue);
    item->setRevenueForAvgPrice(agg.revenueForAvgPrice); // NEW
    
    // Unit Cost (Purchase Price or Fallback)
    item->setUnitCost(unitCost);
    if (isPink) item->setIsPinkBackground(true);
    
    // Import Price per unit
    double importPrice = pData.weight * m_avgPricePerKilo;
    item->setAvgImportPrice(importPrice);
    
    // Profit
    item->setProfit(profit);
    
    // FBA Fees Most Sold Country
    double maxSales = -1.0;
    QString topCountry;
    QHashIterator<QString, int> it(agg.salesPerCountry);
    while (it.hasNext()) {
        it.next();
        if (it.value() > maxSales) {
            maxSales = it.value();
            topCountry = it.key();
        }
    }
    
    double fbaMostSold = 0.0;
    if (!topCountry.isEmpty()) {
        int count = agg.fbaCountPerCountry.value(topCountry);
        double totalFba = agg.fbaFeesPerCountry.value(topCountry);
        if (count > 0) {
            fbaMostSold = totalFba / count;
        }
    }
    item->setFbaFeesMostSoldCountry(fbaMostSold);
    
    item->setAdsCost(agg.adsCost);
    item->setStorageCost(agg.storageCost);
    if (msku == "CJYD193175416PK")
    {
        int TEMP=10;++TEMP;
    }
    item->setFbaFees(agg.fbaFees);
    item->setReferralFees(agg.referralFees);
    item->setOtherFees(agg.otherFees);
    
    item->setAvgImportPrice(avgImportPrice);
    
    item->setMsku(msku);
    item->setTitle(pData.title);
    item->setIsPinkBackground(isPink);
    
    return item;
}

void ProfitTree::calcMostSoldCountryFee(ProfitTreeItem *item, const AggregatedData &agg)
{
    QString maxCountry;
    int maxSales = -1;
    
    for (auto it = agg.salesPerCountry.begin(); it != agg.salesPerCountry.end(); ++it) {
        if (it.value() > maxSales) {
            maxSales = it.value();
            maxCountry = it.key();
        }
    }
    
    if (!maxCountry.isEmpty()) {
        double fees = agg.fbaFeesPerCountry.value(maxCountry);
        int units = agg.fbaCountPerCountry.value(maxCountry);
        if (units > 0) {
            item->setFbaFeesMostSoldCountry(fees / units);
        }
    }
}

// Stubs for private methods if needed (declared in header) -> Actually I implemented them or inlined them.
// Need to match header implementation details.
double ProfitTree::getPurchasePrice(const QString &asin, const QString &msku, const QString &countryCode, bool &isEstimate) {
    return 0;
} // unused
QString ProfitTree::getTitle(const QString &asin, const QString &msku) { return ""; } // unused
