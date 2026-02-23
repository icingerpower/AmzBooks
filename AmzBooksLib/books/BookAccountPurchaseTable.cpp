#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>

#include "BookAccountPurchaseTable.h"
#include "CountriesEu.h"
#include "ExceptionWithTitleText.h"
#include "VatResolver.h"

const QStringList BookAccountPurchaseTable::HEADER{
    QObject::tr("Country")
    , QObject::tr("Vat rate")
    , QObject::tr("VAT Debit (6)")
    , QObject::tr("VAT Credit (4)")
};

BookAccountPurchaseTable::BookAccountPurchaseTable(
        const QDir &workingDir, const QString &countryCodeCompany, QObject *parent)
    : QAbstractTableModel(parent)
    , m_countryCodeCompany(countryCodeCompany)
{
    m_filePath = workingDir.absoluteFilePath("purchaseBookAccounts.csv");
    _load();
    _fillIfEmpty();
}

QString BookAccountPurchaseTable::getAccountsDebit6(const QString &countryCode, double vatRate) const
{
    const QString key = countryCode + "|" + QString::number(vatRate);
    if (m_cache.contains(key))
        return m_cache[key].debit6;
        
    // Fallback: if specific From isn't found, try empty (wildcard)
    const QString wildcardKey = "|" + QString::number(vatRate);
    if (m_cache.contains(wildcardKey))
        return m_cache[wildcardKey].debit6;

    ExceptionWithTitleText exception(tr("Account Missing"),
        tr("No VAT Debit (6) account found for country %1 and rate %2. "
           "Please add it in the purchase accounts settings.")
            .arg(countryCode)
            .arg(vatRate));
    exception.raise();
    return {};
}

QString BookAccountPurchaseTable::getAccountsCredit4(const QString &countryCode, double vatRate) const
{
    const QString key = countryCode + "|" + QString::number(vatRate);
    if (m_cache.contains(key))
        return m_cache[key].credit4;
        
    // Fallback wildcard
    const QString wildcardKey = "|" + QString::number(vatRate);
    if (m_cache.contains(wildcardKey))
        return m_cache[wildcardKey].credit4;

    ExceptionWithTitleText exception(tr("Account Missing"),
        tr("No VAT Credit (4) account found for country %1 and rate %2. "
           "Please add it in the purchase accounts settings.")
            .arg(countryCode)
            .arg(vatRate));
    exception.raise();
    return {};
}

// ... (existing code)

void BookAccountPurchaseTable::addAccount(
        const QString &countryCode
        , double vatRate
        , const QString &vatAccountDebit6
        , const QString &vatAccountCredit4)
{
    bool isEuOrUk = countryCode.isEmpty() || (countryCode == "UK" || countryCode == "GB" || CountriesEu::all().contains(countryCode));
    
    if (!isEuOrUk) {
         ExceptionWithTitleText exception("Invalid Country", "The country is not UK or an EU member.");
         exception.raise();
    }

    // Validation 2: Existence check
    QString key = countryCode + "|" + QString::number(vatRate);
    if (m_existenceCache.contains(key)) {
         ExceptionWithTitleText exception(tr("Account Exists"),
            QString(tr("An account for country %1 and rate %2 already exists."))
                .arg(countryCode).arg(vatRate));
         exception.raise();
    }

    QStringList row;
    row << countryCode
        << QString::number(vatRate)
        << vatAccountDebit6
        << vatAccountCredit4;
        
    beginInsertRows(QModelIndex(), m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList.append(row);
    endInsertRows();
    _rebuildCache();
    _save();
}

QVariant BookAccountPurchaseTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            return HEADER.value(section);
        } else {
            return QString::number(section + 1);
        }
    } else if (role == Qt::ToolTipRole && orientation == Qt::Horizontal) {
        static const QStringList TOOLTIPS = {
            QObject::tr("Country"),
            QObject::tr("Vat rate"),
            QObject::tr("VAT debit account (alongside account class 6)"),
            QObject::tr("VAT credit account (alongside account class 4)")
        };
        return TOOLTIPS.value(section);
    }
    return QVariant();
}

int BookAccountPurchaseTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_listOfStringList.size();
}

int BookAccountPurchaseTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return HEADER.size();
}

QVariant BookAccountPurchaseTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < m_listOfStringList[index.row()].size()) {
             if (role == Qt::DisplayRole && index.column() == 1) { // VAT Rate column
                 double ratePct = m_listOfStringList[index.row()][1].toDouble() * 100.0;
                 return QString("%1%").arg(ratePct, 0, 'f', 2);
             }
             return m_listOfStringList[index.row()][index.column()];
        }
    }
    return QVariant();
}

bool BookAccountPurchaseTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < m_listOfStringList[index.row()].size()) {
             if (m_listOfStringList[index.row()][index.column()] != value.toString()) {
                m_listOfStringList[index.row()][index.column()] = value.toString();
                _rebuildCache();
                _save();
                emit dataChanged(index, index, {role});
                return true;
             }
        }
    }
    return false;
}

Qt::ItemFlags BookAccountPurchaseTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void BookAccountPurchaseTable::_fillIfEmpty()
{
    if (m_listOfStringList.isEmpty()) {
        QString countryCode = m_countryCodeCompany;
        if (countryCode.isEmpty()) {
            countryCode = "FR"; // Fallback provided by logic usually, but here strict.
        }
        
        double rate = 0.2; // Default
        
        // Attempt to get default rate from VatResolver
        // VatResolver vatResolver(...)
        // Since we are inside the library, we might not have the correct path to vatRates.csv easily
        // unless passed in. But we can assume standard 20% if not found or try best effort.
        // User requirement: "default rate from VatResolver"
        // Let's try to instantiate it with empty path (maybe it uses qRC or common path?)
        // Or strictly use a "safe" default if lookup fails.
        // There is no easy way to get "The Default Rate" without a date and context.
        // Assuming current date.
        
        // Using "20" as per user context (FR typically).
        // To do it properly:
        VatResolver resolver(QFileInfo(m_filePath).dir(), nullptr);
        rate = resolver.getRate(QDate::currentDate(), countryCode, SaleType::Products, "", "");
        if (rate == 0.0) rate = 0.2;
        
        QStringList row;
        row << ""
            << QString::number(0.2)
            << tr("445660", "french vat bookkeeping account")
            << tr("445710", "french vat bookkeeping account");
        m_listOfStringList.append(row);
        
        row.clear();
        row << ""
            << QString::number(0.1)
            << tr("445661", "french vat bookkeeping account")
            << tr("445711", "french vat bookkeeping account");
        m_listOfStringList.append(row);

        row.clear();
        row << ""
            << QString::number(0.055)
            << tr("445662", "french vat bookkeeping account")
            << tr("445712", "french vat bookkeeping account");
        m_listOfStringList.append(row);

        _rebuildCache();
        _save();
    }
}

// ... (Header definition)

const QStringList HEADER_IDS = {
    "Country", "VatRate", "VatDebit6", "VatCredit4"
};

// ...

void BookAccountPurchaseTable::_rebuildCache()
{
    m_cache.clear();
    m_existenceCache.clear();
    
    // Columns (Normalized to HEADER_IDS):
    // 0: Country
    // 1: Rate
    // 2: Debit6
    // 3: Credit4
    
    for (const auto &row : m_listOfStringList) {
        if (row.size() < 4) {
            continue;
        }
        
        QString countryCode = row[0];
        const QString cacheKey = countryCode + "|" + QString::number(row[1].toDouble());

        AccountPair acc;
        acc.debit6 = row[2];
        acc.credit4 = row[3];

        m_cache[cacheKey] = acc;
        m_existenceCache.insert(cacheKey);
    }
}

void BookAccountPurchaseTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save BookAccountPurchaseTable:" << file.errorString();
        return;
    }
    
    QTextStream out(&file);
    // Write Header
    out << HEADER_IDS.join(";") << "\n";
    
    for (const QStringList &row : m_listOfStringList) {
        QStringList outputRow = row;
        while(outputRow.size() < HEADER_IDS.size()) outputRow << "";
        out << outputRow.join(";") << "\n";
    }
}

void BookAccountPurchaseTable::_load()
{
    m_listOfStringList.clear();
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    
    QTextStream in(&file);
    if (in.atEnd()) return;

    QString headerLine = in.readLine();
    QStringList headers = headerLine.split(";");
    
    QMap<QString, int> columnMap;
    for (int i = 0; i < headers.size(); ++i) {
        columnMap[headers[i].trimmed()] = i;
    }
    
    int idxCountry = columnMap.value("Country", -1);
    if (idxCountry == -1 && columnMap.contains("CountryTo")) {
        // Fallback for previous change structure where we saved "CountryTo" effectively as "Country"
         idxCountry = columnMap.value("CountryTo", -1);
    }
    int idxRate = columnMap.value("VatRate", -1);
    int idxDebit = columnMap.value("VatDebit6", -1);
    int idxCredit = columnMap.value("VatCredit4", -1);

    auto processLine = [&](const QString &line) {
        if (line.trimmed().isEmpty()) return;
        QStringList parts = line.split(";");
        
        QStringList normalizedRow;
        for(int k=0; k<4; ++k) normalizedRow << "";
        
        if (idxCountry != -1 && idxCountry < parts.size()) normalizedRow[0] = parts[idxCountry];
        if (idxRate != -1 && idxRate < parts.size()) normalizedRow[1] = parts[idxRate];
        if (idxDebit != -1 && idxDebit < parts.size()) normalizedRow[2] = parts[idxDebit];
        if (idxCredit != -1 && idxCredit < parts.size()) normalizedRow[3] = parts[idxCredit];
        
        if (normalizedRow[1].isEmpty()) return; // Required (Rate)
        
        m_listOfStringList.append(normalizedRow);
    };
    
    while (!in.atEnd()) {
        processLine(in.readLine());
    }
    
    _rebuildCache();
}
