#ifndef PROFITTREEITEM_H
#define PROFITTREEITEM_H

#include <QList>
#include <QVariant>
#include <QString>
#include <QHash>

class ProfitTreeItem
{
public:
    explicit ProfitTreeItem(const QVector<QVariant> &data, ProfitTreeItem *parentItem = nullptr);
    ~ProfitTreeItem();

    void appendChild(ProfitTreeItem *child);
    void removeChildren();
    void detachChildren(); // Clear list without deleting

    ProfitTreeItem *child(int row);
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    int row() const;
    ProfitTreeItem *parentItem();
    
    // Setters for aggregation
    void setData(int column, const QVariant &value);

    void setUnitsReturned(int units);
    int getUnitsReturned() const;

    void setMonthlyUnitsSoldMedian(double median); // Calculated
    double getMonthlyUnitsSoldMedian() const;
    
    void setUnitsSoldPerMonth(const QHash<QString, int> &map);
    QHash<QString, int> getUnitsSoldPerMonth() const;

    void setProfit(double profit);
    double getProfit() const;

    void setAdsCost(double cost);
    double getAdsCost() const;

    void setStorageCost(double cost);
    double getStorageCost() const;

    void setFbaFees(double fees);
    double getFbaFees() const;

    void setReferralFees(double fees);
    double getReferralFees() const;

    void setOtherFees(double fees);
    double getOtherFees() const;

    void setFbaFeesMostSoldCountry(double fees);
    double getFbaFeesMostSoldCountry() const;

    void setAverageUnitPrice(double price);
    double getAverageUnitPrice() const;

    void setUnitCost(double cost); // Alias for AverageUnitPrice/TotalUnitCost
    
    void setAvgImportPrice(double price);
    double getAvgImportPrice() const;

    int getUnitsSoldNet() const;

    void setUnitsGross(int units);
    int getUnitsGross() const;

    void setRevenueForAvgPrice(double revenue);
    double getRevenueForAvgPrice() const;

    void setRevenue(double revenue);
    double getRevenue() const;

    void setAsin(const QString &asin);
    QString getAsin() const;

    void setMsku(const QString &msku);
    QString getMsku() const;
    
    void setParentAsin(const QString &parentAsin);
    QString getParentAsin() const;

    void setTitle(const QString &title);
    QString getTitle() const;

    void setIsPinkBackground(bool isPink);
    bool isPinkBackground() const;

    void setAgedInventorySurcharge(double surcharge);
    double getAgedInventorySurcharge() const;

    // Aggregate children values into this item
    void aggregate();

private:
    QList<ProfitTreeItem*> m_childItems;
    QVector<QVariant> m_itemData;
    ProfitTreeItem *m_parentItem;
    
    // Explicit members for calculations (m_itemData will be updated from these)
    int m_unitsGross = 0;
    int m_unitsReturned = 0;
    double m_monthlyUnitsSoldMedian = 0.0;
    QHash<QString, int> m_unitsSoldPerMonth;
    double m_profit = 0.0;
    double m_adsCost = 0.0;
    double m_storageCost = 0.0;
    double m_fbaFees = 0.0;
    double m_referralFees = 0.0;
    double m_otherFees = 0.0;
    double m_fbaFeesMostSoldCountry = 0.0;
    double m_averageUnitPrice = 0.0; // Acts as Unit Cost (Total)
    double m_avgImportPrice = 0.0;
    double m_revenue = 0.0;
    double m_revenueForAvgPrice = 0.0;
    QString m_asin;
    QString m_msku;
    QString m_parentAsin;
    QString m_title;
    bool m_isPinkBackground = false;
    double m_agedInventorySurcharge = 0.0;

    void updateItemData();
};

#endif // PROFITTREEITEM_H
