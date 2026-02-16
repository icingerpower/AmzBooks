#include "ProductFilterTable.h"
#include <QFile>
#include <QTextStream>

const QStringList ProductFilterTable::HEADER_IDS = {
    "Name", "Filters"
};

ProductFilterTable::ProductFilterTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("product_filters.csv");
    _load();
}

QStringList ProductFilterTable::getFilters(int row) const
{
    if (row < 0 || row >= m_data.size()) return {};
    
    // Column 1 is "Filters"
    QString raw = m_data[row].value(1);
    QStringList parts = raw.split(";", Qt::SkipEmptyParts);
    for (QString &p : parts) p = p.trimmed();
    return parts;
}

void ProductFilterTable::addFilter(const QString &name, const QString &filters)
{
    QStringList row;
    row << name << filters;
    
    beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
    m_data.append(row);
    endInsertRows();
    
    _save();
}

bool ProductFilterTable::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid()) return false;
    if (row < 0 || row + count > m_data.size()) return false;

    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_data.removeAt(row);
    }
    endRemoveRows();

    _save();
    return true;
}

QVariant ProductFilterTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == 0) return tr("Name");
        if (section == 1) return tr("Filters");
    }
    return QVariant();
}

int ProductFilterTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int ProductFilterTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 2;
}

QVariant ProductFilterTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_data.size() &&
            index.column() >= 0 && index.column() < m_data[index.row()].size()) {
            return m_data[index.row()][index.column()];
        }
    }
    return QVariant();
}

bool ProductFilterTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_data.size() &&
            index.column() >= 0 && index.column() < m_data[index.row()].size()) {
             if (m_data[index.row()][index.column()] != value.toString()) {
                m_data[index.row()][index.column()] = value.toString();
                _save();
                emit dataChanged(index, index, {role});
                return true;
             }
        }
    }
    return false;
}

Qt::ItemFlags ProductFilterTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void ProductFilterTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    
    QTextStream out(&file);
    out << HEADER_IDS.join(";") << "\n";
    for (const auto &row : m_data) {
        out << row.join(";") << "\n";
    }
}

void ProductFilterTable::_load()
{
    m_data.clear();
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    if (in.atEnd()) return;

    QString headerLine = in.readLine(); 
    // We assume header matches or we just ignore it.
    
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        
        QStringList parts = line.split(";");
        // Reconstruct columns if filters contained separators
        // Assuming Name is at 0, and Rest is Filters
        
        QString name;
        QString filters;
        
        if (!parts.isEmpty()) name = parts[0];
        if (parts.size() > 1) {
            filters = parts.mid(1).join(";");
        }
        
        QStringList row;
        row << name << filters;
        m_data.append(row);
    }
}
