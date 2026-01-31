#include "CompanyAddressTable.h"

#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <algorithm>
#include <QException>
#include <QCoreApplication> 
#include <QFileInfo>
#include <QDir>
#include "ExceptionCompanyInfo.h"

const QStringList CompanyAddressTable::HEADER_IDS = { "Date", "CompanyName", "Street1", "Street2", "PostalCode", "City" };

CompanyAddressTable::CompanyAddressTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("company-addresses.csv");
    _load();
    if (m_data.isEmpty()) {
        // Just empty, no defaults requested.
    }
}

// Helper
const CompanyAddressTable::AddressItem &CompanyAddressTable::_getItemForDate(const QDate &date) const
{
    for (const auto &entry : m_data) {
        if (entry.date <= date) {
            return entry;
        }
    }
    throw ExceptionCompanyInfo(tr("Missing Address"), tr("No company address found for date %1").arg(date.toString(Qt::ISODate)));
}

QString CompanyAddressTable::getCompanyAddress(const QDate &date) const
{
    const auto &item = _getItemForDate(date);
    QStringList parts;
    if (!item.companyName.isEmpty()) parts << item.companyName;
    if (!item.street1.isEmpty()) parts << item.street1;
    if (!item.street2.isEmpty()) parts << item.street2;
    
    QString location;
    if (!item.postalCode.isEmpty()) location += item.postalCode;
    if (!item.city.isEmpty()) {
        if (!location.isEmpty()) location += " ";
        location += item.city;
    }
    if (!location.isEmpty()) parts << location;
    
    return parts.join("\n");
}

QString CompanyAddressTable::getCompanyName(const QDate &date) const
{
    return _getItemForDate(date).companyName;
}

QString CompanyAddressTable::getStreet1(const QDate &date) const
{
    return _getItemForDate(date).street1;
}

QString CompanyAddressTable::getStreet2(const QDate &date) const
{
    return _getItemForDate(date).street2;
}

QString CompanyAddressTable::getPostalCode(const QDate &date) const
{
    return _getItemForDate(date).postalCode;
}

QString CompanyAddressTable::getCity(const QDate &date) const
{
    return _getItemForDate(date).city;
}


int CompanyAddressTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int CompanyAddressTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 6; 
    // 0: Date
    // 1: Company Name
    // 2: Street 1
    // 3: Street 2
    // 4: Postal Code
    // 5: City
}

QVariant CompanyAddressTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_data.size()) return QVariant();
    
    const auto &item = m_data[index.row()];
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch(index.column()) {
            case 0: return item.date;
            case 1: return item.companyName;
            case 2: return item.street1;
            case 3: return item.street2;
            case 4: return item.postalCode;
            case 5: return item.city;
        }
    }
    return QVariant();
}

QVariant CompanyAddressTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch(section) {
            case 0: return tr("Date");
            case 1: return tr("Company Name");
            case 2: return tr("Street 1");
            case 3: return tr("Street 2");
            case 4: return tr("Postal Code");
            case 5: return tr("City");
        }
    }
    return QVariant();
}

bool CompanyAddressTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_data.size()) {
            bool changed = false;
            auto &item = m_data[index.row()];
            
            switch(index.column()) {
                case 0: {
                    QDate d = value.toDate();
                    if (d.isValid() && d != item.date) {
                        item.date = d;
                        changed = true;
                    }
                    break;
                }
                case 1: {
                    if (item.companyName != value.toString()) {
                        item.companyName = value.toString();
                        changed = true;
                    }
                    break;
                }
                case 2: {
                    if (item.street1 != value.toString()) {
                        item.street1 = value.toString();
                        changed = true;
                    }
                    break;
                }
                case 3: {
                    if (item.street2 != value.toString()) {
                        item.street2 = value.toString();
                        changed = true;
                    }
                    break;
                }
                case 4: {
                    if (item.postalCode != value.toString()) {
                        item.postalCode = value.toString();
                        changed = true;
                    }
                    break;
                }
                case 5: {
                    if (item.city != value.toString()) {
                        item.city = value.toString();
                        changed = true;
                    }
                    break;
                }
            }
            
            if (changed) {
                if (index.column() == 0) _sort();
                _save();
                emit dataChanged(index, index, {role}); 
                if (index.column() == 0) emit layoutChanged();
                return true;
            }
        }
    }
    return false;
}

Qt::ItemFlags CompanyAddressTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

bool CompanyAddressTable::insertRows(int row, int count, const QModelIndex &parent)
{
    beginInsertRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        auto date = rowCount() == 0 ? QDate::currentDate().addYears(-10) : QDate::currentDate();
        m_data.insert(row, {date, "", "", "", "", ""});
    }
    endInsertRows();
    _sort(); 
    emit layoutChanged(); 
    _save();
    return true;
}

bool CompanyAddressTable::removeRows(int row, int count, const QModelIndex &parent)
{
    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_data.removeAt(row);
    }
    endRemoveRows();
    _save();
    return true;
}

void CompanyAddressTable::remove(const QModelIndex &index)
{
    if (index.isValid())
    {
        beginRemoveRows(QModelIndex{}, index.row(), index.row());
        m_data.removeAt(index.row());
        endRemoveRows();
    }
}

void CompanyAddressTable::_sort()
{
    std::sort(m_data.begin(), m_data.end(), [](const AddressItem &a, const AddressItem &b) {
        return a.date > b.date; // Descending
    });
}

void CompanyAddressTable::_load()
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
        // Handle "Date" specially? No, standard CSV logic.
        columnMap[headers[i].trimmed()] = i;
    }
    
    // Header IDs: Date, CompanyName, Street1, Street2, PostalCode, City
    int idxDate = columnMap.value("Date", -1);
    int idxName = columnMap.value("CompanyName", -1);
    int idxSt1 = columnMap.value("Street1", -1);
    int idxSt2 = columnMap.value("Street2", -1);
    int idxZip = columnMap.value("PostalCode", -1);
    int idxCity = columnMap.value("City", -1);

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        
        QStringList parts = line.split(";");
        
        AddressItem item;
        if (idxDate != -1 && idxDate < parts.size()) item.date = QDate::fromString(parts[idxDate], Qt::ISODate);
        if (idxName != -1 && idxName < parts.size()) item.companyName = parts[idxName];
        if (idxSt1 != -1 && idxSt1 < parts.size()) item.street1 = parts[idxSt1];
        if (idxSt2 != -1 && idxSt2 < parts.size()) item.street2 = parts[idxSt2];
        if (idxZip != -1 && idxZip < parts.size()) item.postalCode = parts[idxZip];
        if (idxCity != -1 && idxCity < parts.size()) item.city = parts[idxCity];
        
        // Provide Default Date if missing?
        if (!item.date.isValid()) item.date = QDate::currentDate(); // or skip? user code logic seems to rely on valid dates
        
        m_data.append(item);
    }
    
    _sort();
    endResetModel();
}

void CompanyAddressTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save CompanyAddressTable:" << file.errorString();
        return;
    }
    
    QTextStream out(&file);
    out << HEADER_IDS.join(";") << "\n";
    for (const auto &item : m_data) {
        QStringList row;
        row << item.date.toString(Qt::ISODate)
            << item.companyName
            << item.street1
            << item.street2
            << item.postalCode
            << item.city;
        out << row.join(";") << "\n";
    }
}
