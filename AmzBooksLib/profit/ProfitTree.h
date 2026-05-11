#ifndef PROFITTREE_H
#define PROFITTREE_H

#include <QAbstractItemModel>
#include <QDir>
#include <QDate>
#include <QPointer>

#include <QColor>
#include <QHash>
#include <QSet>

class CompanyInfosTable;
class ProfitTreeItem;

class CurrencyRateManager;

class ProfitTree : public QAbstractItemModel
{
    Q_OBJECT

public:
    static const int COL_PARENT_ASIN = 0;
    static const int COL_MSKU = 1;
    static const int COL_TITLE = 2;
    static const int COL_UNITS_SOLD = 3;
    static const int COL_MONTHLY_UNITS_SOLD = 4;
    static const int COL_UNITS_RETURNED = 5;
    static const int COL_AGED_SURCHARGE_PER_UNIT = 6;
    static const int COL_RETURN_PERCENT = 7;
    static const int COL_AVG_SALE_PRICE = 8;
    static const int COL_PROFIT_PER_UNIT = 9;
    static const int COL_PROFIT_PERCENT = 10;
    static const int COL_PROFIT_PER_CAPITAL = 11;
    static const int COL_AVG_IMPORT_PRICE = 12;
    static const int COL_UNIT_PRICE = 13;
    static const int COL_PROFIT_NO_ADS_PER_UNIT = 14;
    static const int COL_ADS_COST_PER_UNIT = 15;
    static const int COL_STORAGE_COST_PER_UNIT = 16;
    static const int COL_FBA_FEES_PER_UNIT = 17;
    static const int COL_REFERRAL_FEES_PER_UNIT = 18;
    static const int COL_OTHER_FEES_PER_UNIT = 19;
    static const int COL_TOTAL_ADS = 20;
    static const int COL_TOTAL_STORAGE = 21;
    static const int COL_TOTAL_FBA_FEES = 22;
    static const int COL_TOTAL_REFERRAL_FEES = 23;
    static const int COL_TOTAL_OTHER_FEES = 24;
    static const int COL_TOTAL_AMZ_COSTS = 25;
    static const int COL_FBA_FEES_MOST_SOLD = 26;
    static const int COL_ASIN = 27;
    static const int COL_AGED_SURCHARGE_TOTAL    = 28;

    explicit ProfitTree(const QDir &workingDir,
                        const QDir &economicsDir,
                        const QDir &purchasesDir,

                        const QDate &startDate, 
                        int minUnitSold, 
                        double avgPricePerKilo, 
                        CompanyInfosTable *companyInfos, 
                        CurrencyRateManager *currencyRateManager,
                        QObject *parent = nullptr);
    ~ProfitTree() override;

    void load();

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    QDir m_workingDir;
    QDir m_economicsDir;
    QDir m_purchasesDir;
    QDate m_startDate;

    int m_minUnitSold;
    double m_avgPricePerKilo;
    CompanyInfosTable *m_companyInfos;
    CurrencyRateManager *m_currencyRateManager;
    
    ProfitTreeItem *m_rootItem;
    
    void setupTreeData();

    // Inner helper types
    struct AggregatedData {
        QString asin; // Child ASIN from the economics report
        // LOGIC NOTE: We do NOT store "unitsSold" (Net) here to avoid synchronization issues.
        // Net Units = unitsGross - unitsReturned.
        // Always calculate Net Units dynamically.
        int unitsGross = 0; // Gross units sold
        int unitsReturned = 0;
        double revenue = 0.0;
        double revenueForAvgPrice = 0.0; // Revenue from units > 0 only
        double adsCost = 0.0;
        double storageCost = 0.0;
        double fbaFees = 0.0;
        double referralFees = 0.0;
        double otherFees = 0.0;
        double agedInventorySurcharge = 0.0;

        QHash<QString, int> unitsSoldPerMonth; // Key: "yyyy-MM", Value: Net Units for that month
        QHash<QString, int> salesPerCountry;
        QHash<QString, double> fbaFeesPerCountry;
        QHash<QString, int> fbaCountPerCountry;
    };

    struct PurchaseData {
        QString title;
        int titlePriority = 0; 
        double cost = 0.0;
        int costSamples = 0;
        double weight = 0.0;
        int weightSamples = 0;
        QString parentAsin;
    };
    
    // Helpers
    void processEconomicsFile(const QString &filePath,
                              QHash<QString, AggregatedData> &mskulAggMap,
                              QHash<QString, QSet<QString>> &parentMskusMap,
                              QHash<QString, QHash<QString, int>> &mskulParentUnits,
                              QHash<QString, PurchaseData> &purchaseDataMap);
                              
    ProfitTreeItem* createItemFromAgg(const AggregatedData &agg, const QString &msku, const PurchaseData &pData, ProfitTreeItem *parent);
    void calcMostSoldCountryFee(ProfitTreeItem *item, const AggregatedData &agg);
    
    // Implementation details for fee calculation and price retrieval
    double getPurchasePrice(const QString &asin, const QString &msku, const QString &countryCode, bool &isEstimate);
    QString getTitle(const QString &asin, const QString &msku);
    void loadPurchaseData(QHash<QString, PurchaseData> &purchaseDataMap);
};

#endif // PROFITTREE_H
