#ifndef DIALOGVIEWORDERS_H
#define DIALOGVIEWORDERS_H

#include <QDialog>
#include <QSharedPointer>
#include "orders/AbstractImporter.h"

class OrderTable;
class TaxAmountTable;
class OrderAddressTable;
class OrderInvoicingTable;
class CurrencyRateManager;

namespace Ui {
class DialogViewOrders;
}

class DialogViewOrders : public QDialog
{
    Q_OBJECT

public:
    explicit DialogViewOrders(const AbstractImporter::OrderInfos &orderInfos, const CurrencyRateManager *currencyRateManager, const QString &destCurrency, QWidget *parent = nullptr);
    ~DialogViewOrders();

private:
    Ui::DialogViewOrders *ui;
    OrderTable *m_orderTable;
    TaxAmountTable *m_taxAmountTable;
    OrderAddressTable *m_addressTable;
    OrderInvoicingTable *m_invoicingTable;
};

#endif // DIALOGVIEWORDERS_H
