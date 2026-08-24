#include "RecordListTable.h"

#include "ExceptionWithTitleText.h"

RecordListTable::RecordListTable(AbstractImporter *importer, const QString &paramKey,
                                 QObject *parent)
    : QAbstractTableModel(parent)
    , m_importer(importer)
    , m_key(paramKey)
{
    if (m_importer) {
        const auto &params = m_importer->getLoadedParamValues();
        if (params.contains(m_key)) {
            m_fields = params[m_key].fields;
        }
        m_rows = m_importer->getParamRecords(m_key);
    }
}

int RecordListTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int RecordListTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_fields.size();
}

QVariant RecordListTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_fields.size()) {
        return QVariant();
    }

    const AbstractImporter::FieldInfo &field = m_fields.at(index.column());
    const QString value = m_rows.at(index.row()).value(field.key).toString();

    if (role == Qt::EditRole) {
        return value;
    }
    if (role == Qt::DisplayRole) {
        if (field.secret && !value.isEmpty()) {
            return QStringLiteral("••••••••");
        }
        return value;
    }
    return QVariant();
}

bool RecordListTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole
        || index.row() >= m_rows.size() || index.column() >= m_fields.size()) {
        return false;
    }

    const QList<QVariantMap> previous = m_rows;
    const AbstractImporter::FieldInfo &field = m_fields.at(index.column());
    m_rows[index.row()].insert(field.key, value.toString());

    if (!_persist(previous)) {
        return false;
    }
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

Qt::ItemFlags RecordListTable::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QAbstractTableModel::flags(index);
    if (index.isValid()) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

QVariant RecordListTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal || section >= m_fields.size()) {
        return QVariant();
    }
    return m_fields.at(section).label;
}

void RecordListTable::addRow()
{
    const QList<QVariantMap> previous = m_rows;

    QVariantMap row;
    for (const auto &field : std::as_const(m_fields)) {
        row.insert(field.key, QString());
    }

    const int at = m_rows.size();
    beginInsertRows(QModelIndex(), at, at);
    m_rows.append(row);
    endInsertRows();

    if (!_persist(previous)) {
        // Roll back the visual insertion when persistence failed.
        beginRemoveRows(QModelIndex(), at, at);
        m_rows = previous;
        endRemoveRows();
    }
}

void RecordListTable::removeRow(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    const QList<QVariantMap> previous = m_rows;

    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();

    if (!_persist(previous)) {
        beginResetModel();
        m_rows = previous;
        endResetModel();
    }
}

bool RecordListTable::_persist(const QList<QVariantMap> &previous)
{
    if (!m_importer) {
        return false;
    }
    try {
        m_importer->setParamRecords(m_key, m_rows);
        return true;
    } catch (const ExceptionWithTitleText &e) {
        m_rows = previous;
        emit exceptionOccurred(e.errorTitle(), e.errorText());
        return false;
    }
}
