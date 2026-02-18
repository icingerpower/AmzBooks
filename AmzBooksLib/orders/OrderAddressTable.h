#ifndef ORDERADDRESSTABLE_H
#define ORDERADDRESSTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include "orders/AbstractImporter.h"

class OrderAddressTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        COL_ORDER_ID = 0,
        COL_FULL_NAME,
        COL_COMPANY,
        COL_ADDRESS_LINE1,
        COL_ADDRESS_LINE2,
        COL_ADDRESS_LINE3,
        COL_CITY,
        COL_POSTAL_CODE,
        COL_STATE_OR_REGION,
        COL_COUNTRY,
        COL_EMAIL,
        COL_PHONE,
        COL_TAX_ID,
        COL_COUNT
    };

    explicit OrderAddressTable(const QList<AbstractImporter::AddressToWithId> &addresses, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<AbstractImporter::AddressToWithId> m_data;
    static const QStringList COL_NAMES;
};

#endif // ORDERADDRESSTABLE_H
