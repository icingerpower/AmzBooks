#ifndef PANEORDERS_H
#define PANEORDERS_H

#include <QDate>
#include <QWidget>

class CompanyInfosTable;
class CurrencyRateManager;
class InventoryMoveTree;

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

private:
    Ui::PaneOrders *ui;
    void _connectSlots();
    void _loadInventoryMoveTree(const QDate &dateStart, const QDate &dateEnd);

    CompanyInfosTable  *m_companyInfos;
    CurrencyRateManager *m_currRateManager;
    InventoryMoveTree  *m_inventoryMoveTree;
};

#endif // PANEORDERS_H
