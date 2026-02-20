#ifndef ORDERTABLE_H
#define ORDERTABLE_H

#include <QList>
#include <QSharedPointer>
#include "orders/OrderTableAbstract.h"

class Shipment;

class OrderTable : public OrderTableAbstract
{
    Q_OBJECT

public:
    enum Columns {
        COL_DATE = 0,
        COL_ORDER_ID,
        COL_ACTIVITY_ID,
        COL_SALE_TYPE,
        COL_TYPE,
        COL_COUNTRY_FROM,
        COL_COUNTRY_TO,
        COL_VAT_PAID_TO,
        COL_IS_BUSINESS,
        COL_TAX_SOURCE,
        COL_TAX_SCHEME,
        COL_TAX_JURISDICTION,
        COL_CURRENCY,
        COL_AMOUNT_TAXED,
        COL_VAT_AMOUNT,
        COL_INVOICE_ID,
        COL_COUNT
    };

    explicit OrderTable(const QList<QSharedPointer<Shipment>> &shipments, QObject *parent = nullptr);

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    static const QStringList COL_NAMES;

    void buildRows(const QList<QSharedPointer<Shipment>> &shipments);
};

#endif // ORDERTABLE_H
