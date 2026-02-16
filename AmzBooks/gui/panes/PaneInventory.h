#ifndef PANEINVENTORY_H
#define PANEINVENTORY_H

#include <QWidget>

class InventoryInvoicesTree;
class InventoryTable;
class CompanyInfosTable;
class CurrencyRateManager;

namespace Ui {
class PaneInventory;
}

class PaneInventory : public QWidget
{
    Q_OBJECT

public:
    explicit PaneInventory(QWidget *parent = nullptr);
    ~PaneInventory();

    static const QString SETTINGS_KEY_AMZ_LEDGER_FOLDER;

public slots:
    void addExtraPurchase();
    void removeExtraPurchase();
    void computeInventory();
    void exportInventory();
    void browseAmzLedgerFolderPath();

private:
    Ui::PaneInventory *ui;
    void _connectSlots();

    InventoryInvoicesTree *m_invoicesTree;
    InventoryTable *m_inventoryTable;
    CompanyInfosTable *m_companyInfos;
    CurrencyRateManager *m_currRateManager;
};

#endif // PANEINVENTORY_H
