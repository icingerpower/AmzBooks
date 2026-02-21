#ifndef DIALOGVIEWORDERS_H
#define DIALOGVIEWORDERS_H

#include <QDialog>
#include <QDir>
#include <QSharedPointer>
#include "orders/AbstractImporter.h"

class OrderTable;
class TaxAmountTable;
class OrderAddressTable;
class OrderInvoicingTable;
class CurrencyRateManager;
class InventoryMoveTree;

namespace Ui {
class DialogViewOrders;
}

class DialogViewOrders : public QDialog
{
    Q_OBJECT

public:
    explicit DialogViewOrders(const AbstractImporter::OrderInfos &orderInfos, const CurrencyRateManager *currencyRateManager, const QString &destCurrency, const QDir &workingDir = QDir(), const QString &companyCountryCode = QString(), QWidget *parent = nullptr);
    ~DialogViewOrders();

private:
    Ui::DialogViewOrders *ui;
    OrderTable *m_orderTable;
    TaxAmountTable *m_taxAmountTable;
    OrderAddressTable *m_addressTable;
    OrderInvoicingTable *m_invoicingTable;
    class QStandardItemModel *m_refundClueModel;
    class QStandardItemModel *m_storeInfoModel;
    InventoryMoveTree *m_inventoryMoveTree;
};

#endif // DIALOGVIEWORDERS_H
