#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include "SaleControlTable.h"

const QString SaleControlTable::COL_ID_STORE     = QStringLiteral("store_name");
const QString SaleControlTable::COL_ID_SALE_TYPE = QStringLiteral("sale_type");

QStringList SaleControlTable::allSaleTypeCodes()
{
    return {SALE_TYPE_BOTH, SALE_TYPE_SALE, SALE_TYPE_REFUND};
}

QString SaleControlTable::saleTypeDisplayText(const QString &code)
{
    if (code == SALE_TYPE_BOTH)   { return QCoreApplication::translate("SaleControlTable", "Sale & Refund"); }
    if (code == SALE_TYPE_SALE)   { return QCoreApplication::translate("SaleControlTable", "Sale"); }
    if (code == SALE_TYPE_REFUND) { return QCoreApplication::translate("SaleControlTable", "Refund"); }
    return code; // unknown code: display as-is
}

SaleControlTable::SaleControlTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{
    _load();
}

int SaleControlTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_data.size();
}

int SaleControlTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return 2;
}

QVariant SaleControlTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size()) {
        return QVariant();
    }
    const auto &entry = m_data.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return entry.storeName;
        case 1: return saleTypeDisplayText(entry.saleTypeCode);
        default: break;
        }
    } else if (role == Qt::EditRole) {
        switch (index.column()) {
        case 0: return entry.storeName;
        case 1: return entry.saleTypeCode; // stable code, not display text
        default: break;
        }
    }
    return QVariant();
}

bool SaleControlTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.row() >= m_data.size()) {
        return false;
    }
    auto &entry = m_data[index.row()];
    switch (index.column()) {
    case 0: entry.storeName    = value.toString(); break;
    case 1: entry.saleTypeCode = value.toString(); break;
    default: return false;
    }
    _save();
    emit dataChanged(index, index, {Qt::EditRole, Qt::DisplayRole});
    return true;
}

QVariant SaleControlTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case 0: return tr("Store name");
        case 1: return tr("Sale type");
        default: break;
        }
    }
    return QVariant();
}

Qt::ItemFlags SaleControlTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void SaleControlTable::addEntry(const QString &storeName, const QString &saleTypeCode)
{
    const int row = m_data.size();
    beginInsertRows(QModelIndex(), row, row);
    m_data.append({storeName, saleTypeCode});
    endInsertRows();
    _save();
}

bool SaleControlTable::removeRows(int row, int count, const QModelIndex &parent)
{
    if (row < 0 || count <= 0 || row + count > m_data.size()) {
        return false;
    }
    beginRemoveRows(parent, row, row + count - 1);
    m_data.remove(row, count);
    endRemoveRows();
    _save();
    return true;
}

QString SaleControlTable::_csvFilePath() const
{
    return m_workingDir.filePath(QStringLiteral("control_sales.csv"));
}

void SaleControlTable::_load()
{
    beginResetModel();
    m_data.clear();

    QFile file(_csvFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        endResetModel();
        return;
    }

    QTextStream in(&file);
    const QString headerLine = in.readLine();
    if (headerLine.isEmpty()) {
        endResetModel();
        return;
    }

    // Map stable column IDs to their position — survives reordering/additions.
    const QStringList cols = headerLine.split(';');
    const int idxStore    = cols.indexOf(COL_ID_STORE);
    const int idxSaleType = cols.indexOf(COL_ID_SALE_TYPE);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList fields = line.split(';');

        ControlEntry entry;
        if (idxStore >= 0 && idxStore < fields.size()) {
            entry.storeName = fields.at(idxStore);
        }
        if (idxSaleType >= 0 && idxSaleType < fields.size()) {
            entry.saleTypeCode = fields.at(idxSaleType);
        }
        if (!entry.storeName.isEmpty()) {
            m_data.append(entry);
        }
    }

    endResetModel();
}

void SaleControlTable::_save() const
{
    QFile file(_csvFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning("SaleControlTable: failed to save %s",
                 qPrintable(_csvFilePath()));
        return;
    }

    QTextStream out(&file);
    // Stable, untranslated header IDs
    out << COL_ID_STORE << ';' << COL_ID_SALE_TYPE << '\n';
    for (const auto &entry : std::as_const(m_data)) {
        out << entry.storeName << ';' << entry.saleTypeCode << '\n';
    }
}
