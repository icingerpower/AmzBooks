#include "CompanyInfosTable.h"

#include <QSettings>
#include <QLocale>
#include <QCoreApplication> 

const char* KEY_COUNTRY = "Country";
const char* KEY_CURRENCY = "Currency";

CompanyInfosTable::CompanyInfosTable(const QString &iniFilePath, QObject *parent)
    : QAbstractTableModel(parent)
    , m_iniFilePath(iniFilePath)
{
    _load();
}

int CompanyInfosTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int CompanyInfosTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 3; // Parameter, Value, ID (Hidden)
}

QVariant CompanyInfosTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_data.size()) return QVariant();

    const auto &item = m_data[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0) return item.parameter;
        if (index.column() == 1) return item.value;
        if (index.column() == 2) return item.id;
    }
    return QVariant();
}

QVariant CompanyInfosTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == 0) return tr("Parameter");
        if (section == 1) return tr("Value");
        if (section == 2) return tr("ID");
    }
    return QVariant();
}

bool CompanyInfosTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_data.size()) {
            if (index.column() == 1) { // Only Value is editable
                if (m_data[index.row()].value != value.toString()) {
                     m_data[index.row()].value = value.toString();
                     _save();
                     emit dataChanged(index, index, {role});
                     return true;
                }
            }
        }
    }
    return false;
}

Qt::ItemFlags CompanyInfosTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    if (index.column() == 1) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

void CompanyInfosTable::_load()
{
    beginResetModel();
    m_data.clear();
    
    QSettings settings(m_iniFilePath, QSettings::IniFormat);
    
    // We load by expected keys to ensure structure, but we also want to preserve what was saved.
    // However, requirements say: "The rows are Company country code, Currency".
    // And "Parameter id (hidden, so if the user change app language... you can still load correctly data)".
    
    // Strategy: m_data is fixed structure (2 rows). We read values from settings using the ID keys.
    
    QString countryVal = settings.value(KEY_COUNTRY, QLocale::system().name().split('_').last()).toString();
    QString currencyVal = settings.value(KEY_CURRENCY, QLocale::system().currencySymbol(QLocale::CurrencyIsoCode)).toString();
    
    // Row 1: Country
    {
        InfoItem item;
        item.id = KEY_COUNTRY;
        item.parameter = tr("Company Country Code");
        item.value = countryVal;
        m_data.append(item);
    }
    
    // Row 2: Currency
    {
        InfoItem item;
        item.id = KEY_CURRENCY;
        item.parameter = tr("Currency");
        item.value = currencyVal;
        m_data.append(item);
    }
    
    endResetModel();
}

void CompanyInfosTable::_save()
{
    QSettings settings(m_iniFilePath, QSettings::IniFormat);
    for (const auto &item : m_data) {
        settings.setValue(item.id, item.value);
    }
}
