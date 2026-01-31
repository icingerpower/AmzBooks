#include "VatNumbersTable.h"

#include <QFile>
#include <QTextStream>
#include <QCoreApplication> 
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include "ExceptionCompanyInfo.h"

const QStringList VatNumbersTable::HEADER_IDS = { "Country", "VatNumber", "Id" };

VatNumbersTable::VatNumbersTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.filePath("vatNumbers.csv");
    _load();
}

const QString &VatNumbersTable::getVatNumber(const QString &countryCode) const
{
    for (const auto &item : m_data) {
        if (item.country == countryCode) {
            return item.vatNumber;
        }
    }
    return m_emptyString; 
}

bool VatNumbersTable::hasVatNumber(const QString &countryCode) const
{
    for (const auto &item : m_data) {
        if (item.country == countryCode) {
            return true;
        }
    }
    return false;
}

void VatNumbersTable::addVatNumber(const QString &country, const QString &vatNumber)
{
    // Check exist
    for (const auto &item : m_data) {
        if (item.country == country) {
            throw ExceptionCompanyInfo(tr("Duplicate Country"), tr("VAT Number for country %1 already exists").arg(country));
        }
    }
    
    int row = m_data.size();
    beginInsertRows(QModelIndex(), row, row);
    VatItem item;
    item.country = country;
    item.vatNumber = vatNumber;
    item.id = country; 
    m_data.append(item);
    endInsertRows();
    _save();
}

int VatNumbersTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int VatNumbersTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 2; // Country, Value (ID is Hidden)
}

QVariant VatNumbersTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_data.size()) return QVariant();

    const auto &item = m_data[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0) return item.country;
        if (index.column() == 1) return item.vatNumber;
    }
    return QVariant();
}

QVariant VatNumbersTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == 0) return tr("Country");
        if (section == 1) return tr("VAT Number");
    }
    return QVariant();
}

bool VatNumbersTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_data.size()) {
            bool changed = false;
            if (index.column() == 0) { // Country
                 QString newCountry = value.toString();
                 if (newCountry != m_data[index.row()].country) {
                     // Check duplicate
                     for (int i=0; i<m_data.size(); ++i) {
                         if (i != index.row() && m_data[i].country == newCountry) {
                              throw ExceptionCompanyInfo(tr("Duplicate Country"), tr("VAT Number for country %1 already exists").arg(newCountry));
                         }
                     }
                     m_data[index.row()].country = newCountry;
                     m_data[index.row()].id = newCountry; // Update ID as well for consistency?
                     changed = true;
                 }
            } else if (index.column() == 1) { // VAT Number
                if (m_data[index.row()].vatNumber != value.toString()) {
                    m_data[index.row()].vatNumber = value.toString();
                    changed = true;
                }
            }
            // Col 2 is ID, not editable manually usually
            
            if (changed) {
                _save();
                emit dataChanged(index, index, {role});
                return true;
            }
        }
    }
    return false;
}

Qt::ItemFlags VatNumbersTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

bool VatNumbersTable::insertRows(int row, int count, const QModelIndex &parent)
{
    beginInsertRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_data.insert(row, {QString(), QString(), QString()});
    }
    endInsertRows();
    _save();
    return true;
}

bool VatNumbersTable::removeRows(int row, int count, const QModelIndex &parent)
{
    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_data.removeAt(row);
    }
    endRemoveRows();
    _save();
    return true;
}

void VatNumbersTable::_load()
{
    beginResetModel();
    m_data.clear();
    
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        endResetModel();
        return;
    }

    QTextStream in(&file);
    QString headerLine = in.readLine();
    QStringList headers = headerLine.split(";");
    
    QMap<QString, int> columnMap;
    for (int i = 0; i < headers.size(); ++i) {
        columnMap[headers[i].trimmed()] = i;
    }

    int idxCountry = columnMap.value("Country", -1);
    int idxVat = columnMap.value("VatNumber", -1);
    int idxId = columnMap.value("Id", -1);

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        
        QStringList parts = line.split(";");
        
        VatItem item;
        if (idxCountry != -1 && idxCountry < parts.size()) item.country = parts[idxCountry];
        if (idxVat != -1 && idxVat < parts.size()) item.vatNumber = parts[idxVat];
        if (idxId != -1 && idxId < parts.size()) item.id = parts[idxId];
        
        if (item.id.isEmpty()) item.id = item.country; // Fallback
        
        m_data.append(item);
    }
    
    endResetModel();
}

void VatNumbersTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save VatNumbersTable:" << file.errorString();
        return;
    }
    
    QTextStream out(&file);
    out << HEADER_IDS.join(";") << "\n";
    for (const auto &item : m_data) {
        QStringList row;
        row << item.country << item.vatNumber << item.id;
        out << row.join(";") << "\n";
    }
}
