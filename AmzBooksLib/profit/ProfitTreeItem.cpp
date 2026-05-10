#include "ProfitTreeItem.h"
#include <QColor>
#include <algorithm>

ProfitTreeItem::ProfitTreeItem(const QVector<QVariant> &data, ProfitTreeItem *parent)
    : m_itemData(data), m_parentItem(parent)
{
}

ProfitTreeItem::~ProfitTreeItem()
{
    qDeleteAll(m_childItems);
}

void ProfitTreeItem::appendChild(ProfitTreeItem *item)
{
    m_childItems.append(item);
}

void ProfitTreeItem::removeChildren()
{
    qDeleteAll(m_childItems);
    m_childItems.clear();
}

void ProfitTreeItem::detachChildren()
{
    m_childItems.clear();
}

ProfitTreeItem *ProfitTreeItem::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int ProfitTreeItem::childCount() const
{
    return m_childItems.count();
}

int ProfitTreeItem::columnCount() const
{
    return m_itemData.count();
}

QVariant ProfitTreeItem::data(int column) const
{
    if (column < 0 || column >= m_itemData.size())
        return QVariant();
    
    // 5 is Average Unit Price. 
    // "Average unit price (can be got from purchase invoice, or 30% of average unit price otherwise, with pink background color)"
    // The "Profit" is requested as column 4 and 6 (0-indexed: 4 and 6). 
    // Wait, let's look at the implementation of `updateItemData` to be sure about indices.
    
    if (column == 5 && m_isPinkBackground) { // Average unit price column index assumption (will verify in tree)
         // We might return a background color here? 
         // Usually data() handles DisplayRole. The Model handles BackgroundRole.
         // But the item only stores data. logic should be in Model::data
    }
    
    if (column == 5 && m_isPinkBackground) { // Average unit price column index assumption (will verify in tree)
         // We might return a background color here? 
         // Usually data() handles DisplayRole. The Model handles BackgroundRole.
         // But the item only stores data. logic should be in Model::data
    }
    
    // Safety check for size, in case columns were added but item data not resized yet (though not expected if initialized correctly)
    if (column >= m_itemData.size()) return QVariant();

    return m_itemData.at(column);
}

int ProfitTreeItem::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<ProfitTreeItem*>(this));

    return 0;
}

ProfitTreeItem *ProfitTreeItem::parentItem()
{
    return m_parentItem;
}

void ProfitTreeItem::setData(int column, const QVariant &value)
{
    if (column < 0 || column >= m_itemData.size())
        return;
    m_itemData[column] = value;
}

void ProfitTreeItem::setUnitsReturned(int units) {
    m_unitsReturned = units;
    updateItemData();
}

int ProfitTreeItem::getUnitsReturned() const {
    return m_unitsReturned;
}

void ProfitTreeItem::setMonthlyUnitsSoldMedian(double median) { m_monthlyUnitsSoldMedian = median; updateItemData(); }
double ProfitTreeItem::getMonthlyUnitsSoldMedian() const { return m_monthlyUnitsSoldMedian; }

void ProfitTreeItem::setUnitsSoldPerMonth(const QHash<QString, int> &map) { 
    m_unitsSoldPerMonth = map; 
    // Recalculate median?
    // Convert map to list, sort, find median.
    if (m_unitsSoldPerMonth.isEmpty()) {
        m_monthlyUnitsSoldMedian = 0.0;
    } else {
        QList<int> values = m_unitsSoldPerMonth.values();
        std::sort(values.begin(), values.end());
        int n = values.size();
        if (n % 2 == 0) {
            m_monthlyUnitsSoldMedian = (double)(values[n/2 - 1] + values[n/2]) / 2.0;
        } else {
            m_monthlyUnitsSoldMedian = (double)values[n/2];
        }
    }
    updateItemData(); 
}
QHash<QString, int> ProfitTreeItem::getUnitsSoldPerMonth() const {
    return m_unitsSoldPerMonth;
}

void ProfitTreeItem::setProfit(double profit) {
    m_profit = profit; updateItemData();
}
double ProfitTreeItem::getProfit() const {
    return m_profit;
}

void ProfitTreeItem::setAdsCost(double cost) {
    m_adsCost = cost;
    updateItemData();
}
double ProfitTreeItem::getAdsCost() const {
    return m_adsCost;
}

void ProfitTreeItem::setStorageCost(double cost) {
    m_storageCost = cost;
    updateItemData();
}
double ProfitTreeItem::getStorageCost() const {
    return m_storageCost;
}

void ProfitTreeItem::setFbaFees(double fees) {
    m_fbaFees = fees;
    updateItemData();
}
double ProfitTreeItem::getFbaFees() const {
    return m_fbaFees;
}

void ProfitTreeItem::setReferralFees(double fees) {
    m_referralFees = fees; updateItemData();
}
double ProfitTreeItem::getReferralFees() const {
    return m_referralFees;
}

void ProfitTreeItem::setOtherFees(double fees) {
    m_otherFees = fees; updateItemData();
}
double ProfitTreeItem::getOtherFees() const {
    return m_otherFees;
}

void ProfitTreeItem::setFbaFeesMostSoldCountry(double fees) {
    m_fbaFeesMostSoldCountry = fees;
    updateItemData();
}
double ProfitTreeItem::getFbaFeesMostSoldCountry() const {
    return m_fbaFeesMostSoldCountry;
}

void ProfitTreeItem::setAverageUnitPrice(double price) {
    m_averageUnitPrice = price; updateItemData();
}
double ProfitTreeItem::getAverageUnitPrice() const {
    return m_averageUnitPrice;
}

void ProfitTreeItem::setUnitCost(double cost) {
    setAverageUnitPrice(cost);
}

void ProfitTreeItem::setAvgImportPrice(double price) {
    m_avgImportPrice = price; updateItemData();
}
double ProfitTreeItem::getAvgImportPrice() const {
    return m_avgImportPrice;
}

int ProfitTreeItem::getUnitsSoldNet() const
{
    return m_unitsGross - m_unitsReturned;
}

void ProfitTreeItem::setUnitsGross(int units) {
    m_unitsGross = units;
    updateItemData();
}
int ProfitTreeItem::getUnitsGross() const {
    return m_unitsGross;
}

void ProfitTreeItem::setRevenueForAvgPrice(double revenue) {
    m_revenueForAvgPrice = revenue;
    updateItemData();
}
double ProfitTreeItem::getRevenueForAvgPrice() const {
    return m_revenueForAvgPrice;
}

void ProfitTreeItem::setRevenue(double revenue) {
    m_revenue = revenue;
    updateItemData();
}
double ProfitTreeItem::getRevenue() const {
    return m_revenue;
}

void ProfitTreeItem::setAsin(const QString &asin) {
    m_asin = asin;
    updateItemData();
}
QString ProfitTreeItem::getAsin() const {
    return m_asin;
}

void ProfitTreeItem::setMsku(const QString &msku) {
    m_msku = msku;
    updateItemData();
}
QString ProfitTreeItem::getMsku() const {
    return m_msku;
}

void ProfitTreeItem::setParentAsin(const QString &parentAsin) {
    m_parentAsin = parentAsin;
    updateItemData();
}
QString ProfitTreeItem::getParentAsin() const {
    return m_parentAsin;
}

void ProfitTreeItem::setTitle(const QString &title) {
    m_title = title;
    updateItemData();
}
QString ProfitTreeItem::getTitle() const {
    return m_title;
}

void ProfitTreeItem::setIsPinkBackground(bool isPink) {
    m_isPinkBackground = isPink;
}
bool ProfitTreeItem::isPinkBackground() const {
    return m_isPinkBackground;
}

void ProfitTreeItem::setAgedInventorySurcharge(double surcharge) {
    m_agedInventorySurcharge = surcharge;
    updateItemData();
}
double ProfitTreeItem::getAgedInventorySurcharge() const {
    return m_agedInventorySurcharge;
}


void ProfitTreeItem::updateItemData()
{
    // Updated Columns:
    // 0: Parent ASIN
    // 1: MSKU
    // 2: Title
    // 3: Units sold (Net)
    // 4: Units Returned (Refunded)
    // 5: Return %
    // 6: Avg Sale Price (Revenue@Gross / Gross)
    // 7: Profit Per Unit (Total / Net)
    // 8: Profit %
    // 9: Profit / Capital
    // 10: Avg Import Price
    // 11: Unit Price (Cost)
    // 12: Profit without ads
    // 13: Ads
    // 14: Storage
    // 15: FBA
    // 16: Referral
    // 17: Other
    // 18: Total Ads
    // 19: Total Storage
    // 20: Total FBA
    // 21: Total Referral
    // 22: Total Other
    // 23: Total Amz Costs
    // 24: Most Sold
    // 25: ASIN
    
    // Resize if needed
    if (m_itemData.size() < 29) {
        m_itemData.resize(29);
    }
    
    // Per Unit Calculation Helper (uses Net Units)
    // m_unitsSold should be removed an computed in real time with m_unitsGross - m_unitsReturned to avoid sync issue
    double unitsNet = getUnitsSoldNet();

    // Calculate Avg Sale Price using Gross Units and Revenue from Gross>0 entries
    double avgSalePrice = (m_unitsGross > 0) ? m_revenueForAvgPrice / m_unitsGross : 0.0;
    
    // Calculate Profit % (Profit / Avg Sale Price)
    // Profit Per Unit is based on Net Units
    auto perUnit = [&](double val) -> double {
        return (unitsNet > 0) ? val / unitsNet : 0.0;
    };
    double profitPerUnit = perUnit(m_profit);
    double profitPercent = (avgSalePrice > 0.0001) ? (profitPerUnit / avgSalePrice) : 0.0;
    
    // Calculate Return % (Returned / Gross)
    // Usually Return Rate is Returned / Gross Sales
    double returnPercent = (m_unitsGross > 0) ? ((double)m_unitsReturned / (double)m_unitsGross) : 0.0;
    
    // Total Unit Cost (Purchase + Import)
    double totalUnitCost = m_averageUnitPrice + m_avgImportPrice;
    
    // Calculate Profit / Capital (Profit / Unit Cost)
    double profitPerCapital = (totalUnitCost > 0.0001) ? (profitPerUnit / totalUnitCost) : 0.0;


    m_itemData[0] = m_parentAsin;
    m_itemData[1] = m_msku;
    m_itemData[2] = m_title;
    m_itemData[3] = unitsNet;
    m_itemData[4] = m_monthlyUnitsSoldMedian;
    m_itemData[5] = m_unitsReturned;
    m_itemData[6] = (unitsNet > 0) ? m_agedInventorySurcharge / unitsNet : 0.0;
    m_itemData[7] = returnPercent;
    m_itemData[8] = avgSalePrice;
    m_itemData[9] = profitPerUnit;
    m_itemData[10] = profitPercent;
    m_itemData[11] = profitPerCapital;
    m_itemData[12] = m_avgImportPrice;
    m_itemData[13] = totalUnitCost;
    m_itemData[14] = perUnit(m_profit + m_adsCost);
    m_itemData[15] = perUnit(m_adsCost);
    m_itemData[16] = perUnit(m_storageCost);
    m_itemData[17] = perUnit(m_fbaFees);
    m_itemData[18] = perUnit(m_referralFees);
    m_itemData[19] = perUnit(m_otherFees);

    // Total Columns (Absolute Values)
    m_itemData[20] = m_adsCost;
    m_itemData[21] = m_storageCost;
    m_itemData[22] = m_fbaFees;
    m_itemData[23] = m_referralFees;
    m_itemData[24] = m_otherFees;
    double totalAmzCosts = m_adsCost + m_storageCost + m_fbaFees + m_referralFees + m_otherFees;
    m_itemData[25] = totalAmzCosts;

    m_itemData[26] = m_fbaFeesMostSoldCountry;
    m_itemData[27] = m_asin;
    m_itemData[28] = m_agedInventorySurcharge;
}

void ProfitTreeItem::aggregate()
{
    m_unitsGross = 0;
    m_unitsReturned = 0;
    m_monthlyUnitsSoldMedian = 0.0;
    m_unitsSoldPerMonth.clear();
    m_revenue = 0.0;
    m_revenueForAvgPrice = 0.0;
    m_profit = 0.0;
    m_adsCost = 0.0;
    m_storageCost = 0.0;
    m_fbaFees = 0.0;
    m_referralFees = 0.0;
    m_otherFees = 0.0;
    m_agedInventorySurcharge = 0.0;
    m_avgImportPrice = 0.0;

    double totalCost = 0.0;
    double totalImportCost = 0.0;

    for (ProfitTreeItem *child : m_childItems) {
        m_unitsGross += child->getUnitsGross();
        m_unitsReturned += child->getUnitsReturned();

        // Aggregate Monthly
        QHash<QString, int> childMap = child->getUnitsSoldPerMonth();
        QHashIterator<QString, int> i(childMap);
        while (i.hasNext()) {
            i.next();
            m_unitsSoldPerMonth[i.key()] += i.value();
        }
        
        m_profit += child->getProfit();
        m_adsCost += child->getAdsCost();
        m_storageCost += child->getStorageCost();
        m_fbaFees += child->getFbaFees();
        m_referralFees += child->getReferralFees();
        m_otherFees += child->getOtherFees();
        m_agedInventorySurcharge += child->getAgedInventorySurcharge();
        m_revenue += child->getRevenue();
        m_revenueForAvgPrice += child->getRevenueForAvgPrice();
        
        // Weighted Averages by Net Units?
        // Cost is usually per unit sold (Net)
        auto unitsSold = child->getUnitsSoldNet();
        if (unitsSold > 0) {
            totalImportCost += child->getAvgImportPrice() * unitsSold;
            totalCost += child->getAverageUnitPrice() * unitsSold;
        }
    }
    
    // Recalculate Median from aggregated map
    if (m_unitsSoldPerMonth.isEmpty()) {
        m_monthlyUnitsSoldMedian = 0.0;
    } else {
        QList<int> values = m_unitsSoldPerMonth.values();
        std::sort(values.begin(), values.end());
        int n = values.size();
        if (n % 2 == 0) {
            m_monthlyUnitsSoldMedian = (double)(values[n/2 - 1] + values[n/2]) / 2.0;
        } else {
            m_monthlyUnitsSoldMedian = (double)values[n/2];
        }
    }
    
    auto unitsSoldNet = getUnitsSoldNet();
    if (unitsSoldNet > 0) {
        m_averageUnitPrice = totalCost / unitsSoldNet;
        m_avgImportPrice = totalImportCost / unitsSoldNet;
    } else {
        // Fallback simple average
        double sumPrices = 0.0;
        double sumImport = 0.0;
        int count = 0;
        for (ProfitTreeItem *child : m_childItems) {
            double p = child->getAverageUnitPrice();
            if (p > 0) {
                sumPrices += p;
                sumImport += child->getAvgImportPrice();
                count++;
            }
        }
        m_averageUnitPrice = (count > 0) ? (sumPrices / count) : 0.0;
        m_avgImportPrice = (count > 0) ? (sumImport / count) : 0.0;
    }
    
    updateItemData();
}
