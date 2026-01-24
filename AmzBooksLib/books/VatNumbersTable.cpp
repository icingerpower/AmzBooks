#include "VatNumbersTable.h"

#include <QSettings>
#include <QCoreApplication> 
#include "CompanyInfoException.h"

VatNumbersTable::VatNumbersTable(const QString &iniFilePath, QObject *parent)
    : QAbstractTableModel(parent)
    , m_iniFilePath(iniFilePath)
{
    _load();
}

const QString &VatNumbersTable::getVatNumber(const QString &countryCode) const
{
    for (const auto &item : m_data) {
        if (item.country == countryCode) {
            return item.vatNumber;
        }
    }
    // Return empty if not found (or throw? Prompt says "valid return const QString &", doesn't specify throw on get. CompanyAddressTable did throw, but here returning valid reference suggests empty is possible or handled elsewhere. Prompt says "add one vat number... always check it doesn't exist and raise CompanyInfoException if needed". That's for ADD.
    // I will return empty string reference.
    // Cast constness away if needed or use mutable member? 
    // m_emptyString is member.
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
            throw CompanyInfoException(tr("Duplicate Country"), tr("VAT Number for country %1 already exists").arg(country));
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
    return 3; // Country, Value, ID (Hidden)
}

QVariant VatNumbersTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_data.size()) return QVariant();

    const auto &item = m_data[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0) return item.country;
        if (index.column() == 1) return item.vatNumber;
        if (index.column() == 2) return item.id;
    }
    return QVariant();
}

QVariant VatNumbersTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == 0) return tr("Country");
        if (section == 1) return tr("VAT Number");
        // Hidden column ID, no header needed or just ID
        if (section == 2) return "ID"; // "Use tr except for hidden column ids"
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
                     // Check duplicate?
                     for (int i=0; i<m_data.size(); ++i) {
                         if (i != index.row() && m_data[i].country == newCountry) {
                              throw CompanyInfoException(tr("Duplicate Country"), tr("VAT Number for country %1 already exists").arg(newCountry));
                         }
                     }
                     m_data[index.row()].country = newCountry;
                     m_data[index.row()].id = newCountry; // Update ID too? Usually ID is stable. Prompt says "hidden column ids so if a column is added / removed we can still load data correctly". Wait, "rows are ... rows", check "if a column is added...". That usually refers to schema evolution.
                     // But for rows: "add hidden column ids so if a column is added..." assumes mapping rows to properties? No, this is a table of rows.
                     // Ah, maybe the user meant "hidden column ids" generally for schema, but here we have dynamic rows.
                     // Re-reading: "Columns are Country / VAT number... Add hidden column ids".
                     // Maybe it means "Column IDs" (Header IDs)?
                     // "The rows are Company country code, Currency" was for CompanyInfosTable (Fixed rows).
                     // Here "We can add one vat number / EU country". So dynamic rows.
                     // "Add hidden column ids" usually implies saving by Key/Value pairs in INI array usage?
                     // QSettings arrays usually use index.
                     // If we use beginWriteArray, we iterate.
                     // Maybe user means storing as `id=value` in INI instead of array?
                     // "Data are saved in .ini file (QSettings::IniFormat)"
                     // If we use beginWriteArray, ID isn't used for mapping unless manual management.
                     // But let's assume standard behavior: Store ID in the row data.
                     changed = true;
                 }
            } else if (index.column() == 1) { // VAT Number
                if (m_data[index.row()].vatNumber != value.toString()) {
                    m_data[index.row()].vatNumber = value.toString();
                    changed = true;
                }
            }
            // Col 2 is ID, usually read only or managed automatically.
            
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
    QSettings settings(m_iniFilePath, QSettings::IniFormat);
    int size = settings.beginReadArray("VatNumbers");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        VatItem item;
        item.country = settings.value("Country").toString(); // Key "Country"
        item.vatNumber = settings.value("VatNumber").toString(); // Key "VatNumber"
        item.id = settings.value("Id").toString(); // Key "Id"
        
        // If ID missing, fallback to country?
        if (item.id.isEmpty()) item.id = item.country;
        
        m_data.append(item);
    }
    settings.endArray();
    endResetModel();
}

void VatNumbersTable::_save()
{
    QSettings settings(m_iniFilePath, QSettings::IniFormat);
    settings.beginWriteArray("VatNumbers", m_data.size());
    for (int i = 0; i < m_data.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("Country", m_data[i].country);
        settings.setValue("VatNumber", m_data[i].vatNumber);
        settings.setValue("Id", m_data[i].id);
    }
    settings.endArray();
}
