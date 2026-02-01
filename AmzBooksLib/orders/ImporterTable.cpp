#include "ImporterTable.h"
#include <algorithm>

ImporterTable::ImporterTable(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ImporterTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_importers.count();
}

int ImporterTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant ImporterTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_importers.count())
        return QVariant();

    if (role == Qt::DisplayRole) {
        const AbstractImporter *importer = m_importers.at(index.row());
        switch (index.column()) {
        case ColName:
            return importer->getLabel();
        case ColChannel:
            return importer->getActivitySource().channel;
        case ColSubchannel:
            return importer->getActivitySource().subchannel;
        case ColReport:
            return importer->getActivitySource().reportOrMethode;
        default:
            return QVariant();
        }
    }

    return QVariant();
}

QVariant ImporterTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case ColName:
        return tr("Name");
    case ColChannel:
        return tr("Channel");
    case ColSubchannel:
        return tr("Subchannel");
    case ColReport:
        return tr("Report");
    default:
        return QVariant();
    }
}

void ImporterTable::sortImporters()
{
    std::sort(m_importers.begin(), m_importers.end(), [](const AbstractImporter *a, const AbstractImporter *b) {
        int cmp = a->getLabel().compare(b->getLabel(), Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        
        cmp = a->getActivitySource().channel.compare(b->getActivitySource().channel, Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        
        cmp = a->getActivitySource().subchannel.compare(b->getActivitySource().subchannel, Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        
        return a->getActivitySource().reportOrMethode.compare(b->getActivitySource().reportOrMethode, Qt::CaseInsensitive) < 0;
    });
}
