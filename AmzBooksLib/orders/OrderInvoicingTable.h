#ifndef ORDERINVOICINGTABLE_H
#define ORDERINVOICINGTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include "orders/AbstractImporter.h"

class OrderInvoicingTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        COL_ID = 0,
        COL_INVOICE_NUMBER,
        COL_PAYMENT_DATE,
        COL_LINK,
        COL_COUNT
    };

    explicit OrderInvoicingTable(const QList<AbstractImporter::InvoicingInfoWithId> &infos, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<AbstractImporter::InvoicingInfoWithId> m_data;
    static const QStringList COL_NAMES;
};

#endif // ORDERINVOICINGTABLE_H
