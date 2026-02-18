#include "ParamsTable.h"
#include "ExceptionWithTitleText.h"
#include <QDebug>

ParamsTable::ParamsTable(AbstractImporter *importer, QObject *parent)
    : QAbstractTableModel(parent)
    , m_importer(importer)
{
    if (m_importer) {
        // We use getLoadedParamValues() as the source of truth for current values
        // Note: AbstractImporter::getLoadedParamValues() returns a const reference to m_params
        // which contains both required params (initialized) and loaded values.
        m_keys = m_importer->getLoadedParamValues().keys();
    }
}

int ParamsTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_keys.count();
}

int ParamsTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant ParamsTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_keys.count() || !m_importer)
        return QVariant();

    const QString &key = m_keys.at(index.row());
    const auto &params = m_importer->getLoadedParamValues();
    if (!params.contains(key))
        return QVariant();

    const AbstractImporter::ParamInfo &info = params[key];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case ColParamName:
            return info.label.isEmpty() ? info.key : info.label;
        case ColParamValue:
            return info.value;
        default:
            return QVariant();
        }
    }
    
    if (role == Qt::ToolTipRole) {
        return info.description;
    }

    return QVariant();
}

bool ParamsTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole && index.column() == ColParamValue && m_importer) {
        if (index.row() < m_keys.count()) {
            const QString &key = m_keys.at(index.row());
            try {
                m_importer->setParam(key, value);
                emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
                return true;
            } catch (const ExceptionWithTitleText &e) {
                emit exceptionOccurred(e.errorTitle(), e.errorText());
                return false;
            }
        }
    }
    return false;
}

Qt::ItemFlags ParamsTable::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == ColParamValue) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

QVariant ParamsTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case ColParamName:
        return tr("Parameter");
    case ColParamValue:
        return tr("Value");
    default:
        return QVariant();
    }
}
