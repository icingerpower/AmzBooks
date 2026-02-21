#include "CompanyInfosTable.h"

#include <QFile>
#include <QTextStream>
#include <QLocale>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>



const QStringList CompanyInfosTable::HEADER_IDS = { "Parameter", "Value", "Id" };
const QString CompanyInfosTable::ID_COUNTRY = "Country";
const QString CompanyInfosTable::ID_CURRENCY = "Currency";
const QString CompanyInfosTable::ID_FIXER_API_KEY = "FixerApiKey";
const QString CompanyInfosTable::ID_LEGAL_SHARE_CAPITAL = "Legal_ShareCapital";
const QString CompanyInfosTable::ID_LEGAL_SIRET = "Legal_Siret";
const QString CompanyInfosTable::ID_LEGAL_RCS = "Legal_RCS";
const QString CompanyInfosTable::ID_LEGAL_VAT_INTRACOMMUNITY = "Legal_VATIntracommunity";

CompanyInfosTable::CompanyInfosTable(
    const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("company.csv");
    m_hadData = QFile::exists(m_filePath);
    _load();
    if (m_data.isEmpty()) {
        _ensureDefaults();
    }
}

QColor CompanyInfosTable::getHighlightColorDark()
{
    return QColor(180, 80, 0); // dark orange
}

const QString &CompanyInfosTable::getCompanyCountryCode() const
{
    for (const auto &item : m_data) {
        if (item.id == ID_COUNTRY) {
            return item.value;
        }
    }
    static QString empty;
    return empty;
}

const QString &CompanyInfosTable::getCurrency() const
{
    for (const auto &item : m_data) {
        if (item.id == ID_CURRENCY) {
            return item.value;
        }
    }
    static QString empty;
    return empty;
}

const QString &CompanyInfosTable::getApiKeyFixer() const
{
    for (const auto &item : m_data) {
        if (item.id == ID_FIXER_API_KEY) {
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

int CompanyInfosTable::getRowById(const QString &id) const
{
    for (int i = 0; i < m_data.size(); ++i) {
        if (m_data[i].id == id) {
            return i;
        }
    }
    return -1;
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
    _initDefaultRows();
    _save();
}

void CompanyInfosTable::_initDefaultRows()
{
    m_data.clear();
    
    // Row 1: Country
    {
        InfoItem item;
        item.id = ID_COUNTRY;
        item.parameter = tr("Company Country Code");
        item.value = QLocale::system().name().split('_').last();
        item.encrypt = false;
        m_data.append(item);
    }
    
    // Row 2: Currency
    {
        InfoItem item;
        item.id = ID_CURRENCY;
        item.parameter = tr("Currency");
        item.value = QLocale::system().currencySymbol(QLocale::CurrencyIsoCode);
        item.encrypt = false;
        m_data.append(item);
    }
    
    // Row 3: Fixer.io API Key
    {
        InfoItem item;
        item.id = ID_FIXER_API_KEY;
        item.parameter = tr("Fixer.io API Key");
        item.value = "";
        item.encrypt = true;
        m_data.append(item);
    }
    
    // Row 4: Share Capital
    {
        InfoItem item;
        item.id = ID_LEGAL_SHARE_CAPITAL;
        item.parameter = tr("Legal Share Capital");
        item.value = "1000 €";
        item.encrypt = false;
        m_data.append(item);
    }

    // Row 5: SIRET
    {
        InfoItem item;
        item.id = ID_LEGAL_SIRET;
        item.parameter = tr("Legal SIRET");
        item.value = "";
        item.encrypt = false;
        m_data.append(item);
    }

    // Row 6: RCS
    {
        InfoItem item;
        item.id = ID_LEGAL_RCS;
        item.parameter = tr("Legal RCS");
        item.value = "";
        item.encrypt = false;
        m_data.append(item);
    }

    // Row 7: VAT Intracommunity
    {
        InfoItem item;
        item.id = ID_LEGAL_VAT_INTRACOMMUNITY;
        item.parameter = tr("Legal VAT Intracommunity");
        item.value = "";
        item.encrypt = false;
        m_data.append(item);
    }
}

void CompanyInfosTable::_load()
{
    beginResetModel();
    
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // If file doesn't exist, we will create defaults later (in init)
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
    int idxParam = columnMap.value("Parameter", -1); // Unused for logic, but good to have
    int idxValue = columnMap.value("Value", -1);

    QMap<QString, QString> loadedValues;
    
    if (idxId != -1 && idxValue != -1) {
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.trimmed().isEmpty()) continue;
            
            QStringList parts = line.split(";");
            if (idxId < parts.size() && idxValue < parts.size()) {
                QString id = parts[idxId];
                QString value = parts[idxValue];
                loadedValues[id] = value;
            }
        }
    }
    
    // Enforce 7 rows structure
    _initDefaultRows();
    
    // Update values from loaded file
    for (int i = 0; i < m_data.size(); ++i) {
        if (loadedValues.contains(m_data[i].id)) {
            QString val = loadedValues[m_data[i].id];
            
            // Decrypt if needed
            if (m_data[i].id == ID_FIXER_API_KEY && m_data[i].encrypt && !val.isEmpty()) {
                val = _decrypt(val);
            }
            
            m_data[i].value = val;
        }
    }
    
    endResetModel();
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
        QString valueToSave = item.value;
        if (item.encrypt && !valueToSave.isEmpty()) {
            valueToSave = _encrypt(valueToSave);
        }
        row << item.parameter << valueToSave << item.id;
        out << row.join(";") << "\n";
    }
}

QString CompanyInfosTable::_encrypt(const QString &value) const
{
    // Simple XOR-based encryption with a fixed key
    // This is NOT cryptographically secure, but provides basic obfuscation
    const QString key = "AmzBooksSecretKey2026";
    QString encrypted;
    
    for (int i = 0; i < value.length(); ++i) {
        QChar c = value[i];
        QChar k = key[i % key.length()];
        encrypted.append(QChar(c.unicode() ^ k.unicode()));
    }
    
    // Convert to hex for safe CSV storage
    QString result;
    for (const QChar &ch : encrypted) {
        result.append(QString::number(ch.unicode(), 16).rightJustified(4, '0'));
    }
    
    return result;
}

QString CompanyInfosTable::_decrypt(const QString &value) const
{
    // Reverse the hex encoding
    QString encrypted;
    for (int i = 0; i < value.length(); i += 4) {
        QString hex = value.mid(i, 4);
        bool ok;
        ushort unicode = hex.toUShort(&ok, 16);
        if (ok) {
            encrypted.append(QChar(unicode));
        }
    }
    
    // XOR decryption (same as encryption)
    const QString key = "AmzBooksSecretKey2026";
    QString decrypted;
    
    for (int i = 0; i < encrypted.length(); ++i) {
        QChar c = encrypted[i];
        QChar k = key[i % key.length()];
        decrypted.append(QChar(c.unicode() ^ k.unicode()));
    }
    
    return decrypted;
}
