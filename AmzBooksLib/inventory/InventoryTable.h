#ifndef INVENTORYTABLE_H
#define INVENTORYTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QHash>
#include <QDate>
#include <QList>

class CompanyInfosTable;
class CurrencyRateManager;
class InventoryInvoicesTree;

class InventoryTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit InventoryTable(const QDir &workingDir, 
                            const QDir &purchasesDir, 
                            const QDir &amzLedgerDir, 
                            int year, 
                            const QHash<QString, double> &country_pricePerKilo, 
                            CompanyInfosTable *companyInfos, 
                            CurrencyRateManager *currencyRateManager, 
                            QObject *parent = nullptr);
    ~InventoryTable() override;

    // columns
    enum Columns {
        COL_SKU = 0,
        COL_TITLE,
        COL_DATE,
        COL_UNIT_START,
        COL_UNIT_REMAINING,
        COL_UNIT_PRICE,
        COL_TOTAL_PRICE,
        COL_INVOICE,
        COL_COUNT
    };

    void load();
    void exportToCsv(const QString &csvFile, bool round = true);
    double getTotalValue() const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    QDir m_workingDir;
    QDir m_purchasesDir;
    QDir m_amzLedgerDir;
    int m_year;
    QHash<QString, double> m_country_pricePerKilo;
    CompanyInfosTable *m_companyInfos;
    CurrencyRateManager *m_currencRateManager;
    
    InventoryInvoicesTree *m_invoicesTree;

    struct InventoryItem {
        QString sku;
        QString title;
        QDate date;
        int unitStart = 0;
        int unitRemaining = 0;
        double unitPrice = 0.0;
        double totalPrice = 0.0;
        QString invoiceName;
        
        QVariant data(int column) const;
    };
    
    QList<InventoryItem> m_items;
    
    // Internal struct for Purchase Logic.
    // price is already in companyCurrency: PurchaseCsvLoader::parseFiles handles
    // conversion before batches are built, so buildTable uses it directly.
    struct PurchaseBatch {
        QString sku;
        QString title;
        QDate date;
        double price;    // in companyCurrency (or invoice currency when no conversion)
        int quantity;
        double weight;
        QString fileName;
    };

    struct SkuStockInfo {
        int totalQty = 0;
        QHash<QString, int> qtyPerCountry; // Country Code -> Qty
    };
    
    // Helpers
    void loadInventoryFromLedger(QHash<QString, SkuStockInfo> &skuStock);
    void loadPurchases(QHash<QString, QList<PurchaseBatch>> &skuPurchases);
    void buildTable(const QHash<QString, SkuStockInfo> &skuStock, 
                    QHash<QString, QList<PurchaseBatch>> &skuPurchases);
};

#endif // INVENTORYTABLE_H
