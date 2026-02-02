#include "EntrySelfTable.h"

const QStringList EntrySelfTable::COL_NAMES{QObject::tr("Name"), QObject::tr("Account")};

EntrySelfTable::EntrySelfTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePathCsv = workingDir.absoluteFilePath("selfBookAccounts.csv");
    _load();
}

void EntrySelfTable::addRow(const Row &row)
{
    beginInsertRows(QModelIndex{}, 0, 0);
    const QString &id = row.account + row.label + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    // Corrected: Use row.label for the first column (Name) instead of row.account
    m_listeOfStringList.insert(0, QStringList{row.label, row.account, id});
    _save();
    endInsertRows(); // Corrected: Was endRemoveRows
}

void EntrySelfTable::remove(const QModelIndex &index)
{
    beginRemoveRows(QModelIndex{}, index.row(), index.row());
    m_listeOfStringList.removeAt(index.row());
    _save();
    endRemoveRows();
}

QString EntrySelfTable::getId() const
{
    return "EntrySelfTable";
}

QString EntrySelfTable::getRowId(const QModelIndex &index) const
{
    if (index.row() < 0 || index.row() >= m_listeOfStringList.size()) {
        return QString();
    }
    return m_listeOfStringList[index.row()].last();
}

void EntrySelfTable::_save()
{
    QFile file(m_filePathCsv);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Could not open file for writing:" << m_filePathCsv;
        return;
    }
    QTextStream out(&file);
    out << "Name;Account;Id\n";
    for (const auto &row : m_listeOfStringList) {
        out << row.join(";") << "\n";
    }
}

void EntrySelfTable::_load()
{
    beginResetModel();
    m_listeOfStringList.clear();
    QFile file(m_filePathCsv);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        // Skip header
        if (!in.atEnd()) {
            in.readLine();
        }
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;
            QStringList parts = line.split(";");
            if (parts.size() >= 3) {
                 m_listeOfStringList.append(parts);
            }
        }
    }
    endResetModel();
}



QVariant EntrySelfTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (orientation == Qt::Horizontal)
        {
            return COL_NAMES[section];
        }
    }
    return QVariant{};
}

int EntrySelfTable::rowCount(const QModelIndex &parent) const
{
    return m_listeOfStringList.size();
}

int EntrySelfTable::columnCount(const QModelIndex &parent) const
{
    return COL_NAMES.size();
}

QVariant EntrySelfTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        return m_listeOfStringList[index.row()][index.column()];
    }
    return QVariant();
}

bool EntrySelfTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (data(index, role) != value) {
        m_listeOfStringList[index.row()][index.column()] = value.toString();
        _save();
        emit dataChanged(index, index, {role});
        return true;
    }
    return false;
}

Qt::ItemFlags EntrySelfTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}


