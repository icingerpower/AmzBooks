#include "CompanyAddressTable.h"

#include <QSettings>
#include <QDebug>
#include <algorithm>
#include <QException>
#include <QCoreApplication> 
#include "CompanyInfoException.h"

CompanyAddressTable::CompanyAddressTable(const QString &iniFilePath, QObject *parent)
    : QAbstractTableModel(parent)
    , m_iniFilePath(iniFilePath)
{
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
    throw CompanyInfoException(tr("Missing Address"), tr("No company address found for date %1").arg(date.toString(Qt::ISODate)));
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
        m_data.insert(row, {QDate::currentDate(), "", "", "", "", ""});
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
    QSettings settings(m_iniFilePath, QSettings::IniFormat);
    int size = settings.beginReadArray("Addresses");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        AddressItem item;
        item.date = settings.value("date").toDate();
        // Support old format "address" (fallback) or new detailed keys
        // If "address" exists and "street1" missing, maybe migrate? 
        // User requested refactoring, assuming data migration or clean slate is fine or handled implicitly.
        // I will focus on new keys.
        item.companyName = settings.value("companyName").toString();
        item.street1 = settings.value("street1").toString();
        item.street2 = settings.value("street2").toString();
        item.postalCode = settings.value("postalCode").toString();
        item.city = settings.value("city").toString();
        
        m_data.append(item);
    }
    settings.endArray();
    _sort();
    endResetModel();
}

void CompanyAddressTable::_save()
{
    QSettings settings(m_iniFilePath, QSettings::IniFormat);
    settings.beginWriteArray("Addresses", m_data.size());
    for (int i = 0; i < m_data.size(); ++i) {
        settings.setArrayIndex(i);
        const auto &item = m_data[i];
        settings.setValue("date", item.date);
        settings.setValue("companyName", item.companyName);
        settings.setValue("street1", item.street1);
        settings.setValue("street2", item.street2);
        settings.setValue("postalCode", item.postalCode);
        settings.setValue("city", item.city);
    }
    settings.endArray();
}
