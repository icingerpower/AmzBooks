#include "OrderAddressTable.h"

const QStringList OrderAddressTable::COL_NAMES = {
    QObject::tr("Order ID"),
    QObject::tr("Full Name"),
    QObject::tr("Company"),
    QObject::tr("City"),
    QObject::tr("Country"),
    QObject::tr("Tax ID")
};

OrderAddressTable::OrderAddressTable(const QList<AbstractImporter::AddressToWithId> &addresses, QObject *parent)
    : QAbstractTableModel(parent)
    , m_data(addresses)
{
}

int OrderAddressTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_data.size();
}

int OrderAddressTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return COL_COUNT;
}

QVariant OrderAddressTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size())
        return QVariant();

    const auto &item = m_data.at(index.row());
    const auto &addr = item.address;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case COL_ORDER_ID: return item.orderId;
        case COL_FULL_NAME: return addr.getFullName();
        case COL_COMPANY: return addr.getCompanyName();
        case COL_CITY: return addr.getCity();
        case COL_COUNTRY: return addr.getCountryCode();
        case COL_TAX_ID: return addr.getTaxId();
        }
    }
    return QVariant();
}

QVariant OrderAddressTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < COL_NAMES.size())
            return COL_NAMES[section];
    }
    return QVariant();
}
