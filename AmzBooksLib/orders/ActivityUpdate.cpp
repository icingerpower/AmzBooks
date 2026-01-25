#include "ActivityUpdate.h"

ActivityUpdate::ActivityUpdate(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ActivityUpdate::setItems(const QList<ActivityUpdateItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}

int ActivityUpdate::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

int ActivityUpdate::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 6; // Date, Type, Number, Amount, Currency, Status
}

QVariant ActivityUpdate::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return QVariant();

    if (role == Qt::DisplayRole) {
        const auto &item = m_items[index.row()];
        switch (index.column()) {
        case 0: return item.date;
        case 1: return item.type;
        case 2: return item.number;
        case 3: return item.amount;
        case 4: return item.currency;
        case 5: return item.status;
        }
    }
    return QVariant();
}

QVariant ActivityUpdate::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();
    
    switch (section) {
    case 0: return tr("Date");
    case 1: return tr("Type");
    case 2: return tr("Number");
    case 3: return tr("Amount");
    case 4: return tr("Currency");
    case 5: return tr("Status");
    }
    return QVariant();
}
