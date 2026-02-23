#ifndef PANEORDERS_H
#define PANEORDERS_H

#include <QCoroTask>
#include <QDate>
#include <QWidget>

class CompanyInfosTable;
class CurrencyRateManager;
class InventoryMoveTree;
class SkuRegradedTable;

namespace Ui {
class PaneOrders;
}

class PaneOrders : public QWidget
{
    Q_OBJECT

public:
    explicit PaneOrders(QWidget *parent = nullptr);
    ~PaneOrders();

public slots:
    void displayRangeOrders();
    void displayRecentOrders();
    void displayMonthlyOrders();
    void displayOrdersNoInvoices();
    void filter();
    void filterReset();
    void displayNoPriceSkus();
    void editRegradedSkus();

private:
    Ui::PaneOrders *ui;
    void _connectSlots();
    void _loadInventoryMoveTree(const QDate &dateStart, const QDate &dateEnd);

    // Coroutine body for displayNoPriceSkus().
    QCoro::Task<> displayNoPriceSkusAsync();

    CompanyInfosTable  *m_companyInfos;
    CurrencyRateManager *m_currRateManager;
    InventoryMoveTree  *m_inventoryMoveTree;
    // Persistent mapping from Amazon-regraded SKUs to their canonical SKUs.
    // Created once at construction; lives for the pane's lifetime.
    SkuRegradedTable   *m_skuRegradedTable;
};

#endif // PANEORDERS_H
