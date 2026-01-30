#include "CompanyInfosTable.h"

#include <QFile>
#include <QTextStream>
#include <QLocale>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

const char* KEY_COUNTRY = "Country";
const char* KEY_CURRENCY = "Currency";

const QStringList CompanyInfosTable::HEADER_IDS = { "Parameter", "Value", "Id" };

CompanyInfosTable::CompanyInfosTable(
    const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("company.csv");
    _load();
    if (m_data.isEmpty()) {
        m_hadData = false;
        _ensureDefaults();
    }
}

const QString &CompanyInfosTable::getCompanyCountryCode() const
{
    for (const auto &item : m_data) {
        if (item.id == KEY_COUNTRY) {
            return item.value;
        }
    }
    static QString empty;
    return empty;
}

const QString &CompanyInfosTable::getCurrency() const
{
    for (const auto &item : m_data) {
        if (item.id == KEY_CURRENCY) {
            return item.value;
        }
    }
    static QString empty;
    return empty;
}

bool CompanyInfosTable::hadData() const
{
    return m_hadData;
}

int CompanyInfosTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int CompanyInfosTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 2; // Parameter, Value (ID is Hidden)
}

QVariant CompanyInfosTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_data.size()) return QVariant();

    const auto &item = m_data[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0) return item.parameter;
        if (index.column() == 1) return item.value;
    }
    return QVariant();
}

QVariant CompanyInfosTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == 0) return tr("Parameter");
        if (section == 1) return tr("Value");
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

void CompanyInfosTable::_ensureDefaults()
{
    m_data.clear();
    
    // Row 1: Country
    {
        InfoItem item;
        item.id = KEY_COUNTRY;
        item.parameter = tr("Company Country Code");
        item.value = QLocale::system().name().split('_').last();
        m_data.append(item);
    }
    
    // Row 2: Currency
    {
        InfoItem item;
        item.id = KEY_CURRENCY;
        item.parameter = tr("Currency");
        item.value = QLocale::system().currencySymbol(QLocale::CurrencyIsoCode);
        m_data.append(item);
    }
    
    _save();
}

void CompanyInfosTable::_load()
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

    // Identify indices for our Technical IDs
    int idxId = columnMap.value("Id", -1);
    int idxParam = columnMap.value("Parameter", -1);
    int idxValue = columnMap.value("Value", -1);

    // If Id column is missing, we can't reliably map rows to our logic, maybe assume legacy order?
    // But this is a new file format. If missing, we might fail or load partial.
    
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        
        QStringList parts = line.split(";");
        
        InfoItem item;
        if (idxId != -1 && idxId < parts.size()) item.id = parts[idxId];
        if (idxParam != -1 && idxParam < parts.size()) item.parameter = parts[idxParam];
        if (idxValue != -1 && idxValue < parts.size()) item.value = parts[idxValue];
        
        // Fixup: parameter should be translated usually, but in CSV it might be static.
        // If we load by ID, we can force the correct Parameter Name from code if we want "translation update".
        if (item.id == KEY_COUNTRY) item.parameter = tr("Company Country Code");
        else if (item.id == KEY_CURRENCY) item.parameter = tr("Currency");
        
        if (!item.id.isEmpty()) {
            m_data.append(item);
        }
    }
    
    endResetModel();
    
    // Check if we have required rows, if not add defaults?
    // Current logic: _ensureDefaults calls clear(). Better only add missing if needed.
    // For now, if empty, constructor calls _ensureDefaults.
}

void CompanyInfosTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save CompanyInfosTable:" << file.errorString();
        return;
    }
    
    QTextStream out(&file);
    // Always write standard header: Parameter;Value;Id
    out << HEADER_IDS.join(";") << "\n";
    
    for (const auto &item : m_data) {
        QStringList row;
        // Map to HEADER_IDS order: Parameter, Value, Id
        // But wait, "If CSV column order change it works".
        // Loading is robust. Saving should be consistent (Standard order).
        // Standard: Parameter;Value;Id
        row << item.parameter << item.value << item.id;
        out << row.join(";") << "\n";
    }
}
