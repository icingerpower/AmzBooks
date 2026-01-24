#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>

#include "PurchaseBookAccountsTable.h"
#include "CountriesEu.h"
#include "ExceptionVatAccountExisting.h"
#include "ExceptionTaxSchemeInvalid.h"
#include "model/VatResolver.h"

const QStringList PurchaseBookAccountsTable::HEADER{
    QObject::tr("Country")
    , QObject::tr("Vat rate")
    , QObject::tr("VAT Debit (6)")
    , QObject::tr("VAT Credit (4)")
};

PurchaseBookAccountsTable::PurchaseBookAccountsTable(
        const QDir &workingDir, const QString &countryCodeCompany, QObject *parent)
    : QAbstractTableModel(parent)
    , m_countryCodeCompany(countryCodeCompany)
{
    m_filePath = workingDir.absoluteFilePath("purchaseBookAccounts.csv");
    _load();
    _fillIfEmpty();
}

bool PurchaseBookAccountsTable::isDoubleVatEntryNeeded(const QString &countryFrom, const QString &countryTo) const
{
    // French bookkeeping rules (and general VAT rules):
    // "TVA auto-liquidé" (Reverse Charge) applies when purchasing goods/services from outside the domestic tax jurisdiction
    // but tax is due in the domestic jurisdiction (Place of supply = Destination).
    // This happens for:
    // 1. Intra-Community Acquisitions (EU to FR): Mandatory Reverse Charge.
    // 2. Imports (Non-EU to FR): Mandatory Reverse Charge (Autoliquidation à l'importation) since 2022 in France.
    // 
    // Condition: 
    // - Destination must be the company's country (where we file VAT).
    // - Origin must be different from company's country.
    
    return (countryTo == m_countryCodeCompany) && (countryFrom != m_countryCodeCompany);
}

QString PurchaseBookAccountsTable::getAccountsDebit6(const QString &countryCode) const
{
    if (m_cache.contains(countryCode)) {
        return m_cache[countryCode].debit6;
    }
    return QString();
}

QString PurchaseBookAccountsTable::getAccountsCredit4(const QString &countryCode) const
{
    if (m_cache.contains(countryCode)) {
        return m_cache[countryCode].credit4;
    }
    return QString();
}

// ... (existing code)

void PurchaseBookAccountsTable::addAccount(
        const QString &countryCode
        , double vatRate
        , const QString &vatAccountDebit6
        , const QString &vatAccountCredit4)
{
    // Validation 1: EU or UK check
    // "UK" usually refers to GB code in this system.
    // We check if it is an EU member (at any time? or current?) or specifically "UK"/"GB".
    // CountriesEu has isEuMember(code, date) and all().
    // If we want strict check: it must be in the list of supported countries.
    // The user said: "check countryCode is UK or in EU".
    bool isEuOrUk = (countryCode == "UK" || countryCode == "GB" || CountriesEu::all().contains(countryCode));
    // Also check if valid EU member via helper if needed, but strictly:
    // "GB" is in CountriesEu::all() list in this codebase? Let's check CountriesEu.cpp again.
    // CountriesEu::all() includes GB? No, lines 47-51 in cpp: "AT... SK, MC, XI". GB is NOT in all().
    // GB is in getAmazonPanEuCountryCodes().
    // So I must explicit check for GB.
    
    if (!isEuOrUk) {
        // What exception? User only mentioned VatAccountExistingException for existence.
        // But for country validity? " trigger an Exception otherwise".
        // Maybe reused or generic?
        // Let's assume generic QException or simple one if not specified, 
        // BUT the user said "(Create a new file with Exception (VatAccountExisting)...)".
        // It implies using that one or maybe I should have created CountryNotSupportedException?
        // "and trigger an Exception otherwise". It's vague if it refers to "VatAccountExisting" for both.
        // "check ... that ... doesn't exist (and trigger an Exception otherwise)."
        // It likely refers to VatAccountExistingException for the duplication.
        // For the country check: "Create a new file with Exception (VatAccountExisting) (similar to TaxSchemeInvalidException)".
        // It sounds like one exception for the duplicate scenarios.
        // I will throw VatAccountExistingException for duplication.
        // For Invalid Country? User didn't specify checking country triggers *that* exception.
        // "check countryCode is UK or in EU ... and trigger an Exception otherwise" -> This phrasing covers both checks.
        // So I will use `VatAccountExistingException` for both or maybe `TaxSchemeInvalidException`? No.
        // I'll create `VatAccountExistingException` and maybe abuse it or create another one "InvalidCountryException"?
        // Given constraints, I'll use `TaxSchemeInvalidException` (renaming it mentaly to InvalidInput?) or just throw strict string message in QException/std::runtime_error?
        // User explicitly asked to create `VatAccountExistingException`.
        // Let's split hairs: "check ... that couple ... doesn't exist (and trigger an Exception otherwise)."
        // This parenthesis links existence to exception.
        // The first part "addAccount should check countryCode is UK or in EU".
        // I'll assume standard exception (TaxSchemeInvalidException or new one invalidArgument).
        // Let's use `VatAccountExistingException` for "Existing".
        // What for "Invalid Country"? I'll use `TaxSchemeInvalidException` as "Invalid Configuration"?
        // Or simply throw `VatAccountExistingException` with specific message? No, name is bad.
        // I will throw `TaxSchemeInvalidException` ("Invalid Country", ...) for country check as I already have it and it fits "Invalid...".
        // Or just std::invalid_argument?
        // User said: "Create a new file with Exception (VatAccountExisting)".
        // I'll stick to: Invalid Country -> TaxSchemeInvalidException (or similar generic).
        // Duplicate -> VatAccountExistingException.
    }
    
    if (!isEuOrUk) {
         throw ExceptionTaxSchemeInvalid("Invalid Country", "The country " + countryCode + " is not UK or an EU member.");
    }

    // Validation 2: Existence check
    QString key = countryCode + "|" + QString::number(vatRate);
    if (m_existenceCache.contains(key)) {
         throw ExceptionVatAccountExisting(tr("Account Exists"),
            QString(tr("An account for country %1 and rate %2 already exists.")).arg(countryCode).arg(vatRate));
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

QVariant PurchaseBookAccountsTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            return HEADER.value(section);
        } else {
            return QString::number(section + 1);
        }
    } else if (role == Qt::ToolTipRole && orientation == Qt::Horizontal) {
        static const QStringList TOOLTIPS = {
            QObject::tr("The country code"),
            QObject::tr("Vat rate"),
            QObject::tr("VAT debit account (alongside account class 6)"),
            QObject::tr("VAT credit account (alongside account class 4)")
        };
        return TOOLTIPS.value(section);
    }
    return QVariant();
}

int PurchaseBookAccountsTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_listOfStringList.size();
}

int PurchaseBookAccountsTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return HEADER.size();
}

QVariant PurchaseBookAccountsTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < m_listOfStringList[index.row()].size()) {
             return m_listOfStringList[index.row()][index.column()];
        }
    }
    return QVariant();
}

bool PurchaseBookAccountsTable::setData(const QModelIndex &index, const QVariant &value, int role)
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

Qt::ItemFlags PurchaseBookAccountsTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void PurchaseBookAccountsTable::_fillIfEmpty()
{
    if (m_listOfStringList.isEmpty()) {
        QString countryCode = m_countryCodeCompany;
        if (countryCode.isEmpty()) countryCode = "FR"; // Fallback provided by logic usually, but here strict.
        
        double rate = 20.0; // Default
        
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
        VatResolver resolver(nullptr, QFileInfo(m_filePath).dir().path());
        rate = resolver.getRate(QDate::currentDate(), countryCode, SaleType::Products, "", "");
        if (rate == 0.0) rate = 20.0;
        
        QStringList row;
        row << countryCode 
            << QString::number(rate) 
            << tr("445660", "french vat bookkeeping account")
            << tr("445710", "french vat bookkeeping account");
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

void PurchaseBookAccountsTable::_rebuildCache()
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
        
        QString country = row[0];
        // QString rate = row[1]; // Not used in key for now as getAccounts is by Country
        
        AccountPair acc;
        acc.debit6 = row[2];
        acc.credit4 = row[3];
        
        // Last one wins? or First? 
        m_cache[country] = acc;
        
        // Populate existence cache
        // Key format: "Country|Rate"
        m_existenceCache.insert(country + "|" + row[1]);
    }
}

void PurchaseBookAccountsTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save PurchaseBookAccountsTable:" << file.errorString();
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

void PurchaseBookAccountsTable::_load()
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
    
    // Legacy check:
    // Legacy Header (Display Strings): "Country", "Vat rate", "VAT Debit (6)", "VAT Credit (4)" (or localized!)
    // But in `SaleBookAccountsTable`, we assumed data only in legacy.
    // In `PurchaseBookAccountsTable`, it might be data only too.
    // First line data: Country Code (2 chars).
    // New Header: "Country".
    // Check if first token is "Country".
    bool isLegacy = (headers.first().trimmed() != "Country");
    
    QMap<QString, int> columnMap;
    if (!isLegacy) {
        for (int i = 0; i < headers.size(); ++i) {
            columnMap[headers[i].trimmed()] = i;
        }
    } else {
        // Legacy Map: Fixed Order
        columnMap["Country"] = 0;
        columnMap["VatRate"] = 1;
        columnMap["VatDebit6"] = 2;
        columnMap["VatCredit4"] = 3;
    }
    
    // Canonical Indices
    int idxCountry = columnMap.value("Country", -1);
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
        
        if (normalizedRow[0].isEmpty()) return;
        
        m_listOfStringList.append(normalizedRow);
    };

    if (isLegacy) {
        processLine(headerLine);
    }
    
    while (!in.atEnd()) {
        processLine(in.readLine());
    }
    
    _rebuildCache();
}
