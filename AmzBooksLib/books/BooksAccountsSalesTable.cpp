#include "BooksAccountsSalesTable.h"
#include "CountriesEu.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>
#include <stdexcept>
#include <optional>

const QStringList BooksAccountsSalesTable::HEADER{
    QObject::tr("Tax Scheme")
    , QObject::tr("Country From")
    , QObject::tr("Country To")
    , QObject::tr("VAT Rate")
    , QObject::tr("Sale Account")
    , QObject::tr("VAT Account")
    , QObject::tr("VAT Account To Pay")
    , QObject::tr("Customer Account")
};

BooksAccountsSalesTable::BooksAccountsSalesTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("saleBookAccounts.csv");
    _load();
    _fillIfEmpty();
}

VatCountries BooksAccountsSalesTable::resolveVatCountries(
        TaxScheme taxScheme
        , const QString &companyCountryFrom
        , const QString &countryFrom
        , const QString &countryCodeTo) const
{
    auto normalize = [](const QString &s) { return s.trimmed().toUpper(); };
    QString nFrom = normalize(countryFrom);
    QString nTo = normalize(countryCodeTo);
    QString nCompany = normalize(companyCountryFrom);

    switch (taxScheme) {
    case TaxScheme::DomesticVat: // FR > FR or DE > DE
        // Declaring = From (The country where VAT is paid/declared locally)
        return {taxScheme, nFrom, nFrom, ""};
    
    case TaxScheme::EuOssNonUnion: // Map to EuOssUnion
    case TaxScheme::EuOssUnion:
        // VAT due in member state of consumption but declared in Company Country (OSS)
        return {TaxScheme::EuOssUnion, nCompany, nCompany, nTo};
    
    case TaxScheme::EuIoss:
         // Declared in Company Country (IOSS)
         return {TaxScheme::EuIoss, nCompany, nCompany, nTo};

    case TaxScheme::Exempt:
        // Export: Declaring = From (Origin)
        return {taxScheme, nFrom, nFrom, ""};
        
    case TaxScheme::OutOfScope:
    case TaxScheme::MarketplaceDeemedSupplier:
        return {TaxScheme::OutOfScope, "", "", ""};

    default:
        ExceptionWithTitleText exception("Invalid Tax Scheme", 
                                        "The tax scheme " + taxSchemeToString(taxScheme) + " is not supported for account resolution.");
        exception.raise();
    }
    return {TaxScheme::Unknown, "", "", ""};
}

QCoro::Task<BooksAccountsSalesTable::Accounts> BooksAccountsSalesTable::getAccounts(
        const VatCountries &vatCountries
        , double vatRate
        , std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) const
{
    // Helper to look up in cache
    auto lookup = [&](const VatCountries &orgVc, double rate) -> std::optional<Accounts> {
        auto tryMatch = [&](const VatCountries &key) -> std::optional<Accounts> {
            if (m_vatCountries_vatRate_accountsCache.contains(key)) {
                const auto &rateMap = m_vatCountries_vatRate_accountsCache[key];
                QString rateStr = QString::number(rate);
                
                // Level 2: VAT Rate
                // Try exact match first
                if (rateMap.contains(rateStr)) {
                    return rateMap[rateStr];
                }
            }
            return std::nullopt;
        };

        if (auto res = tryMatch(orgVc)) return res;
        
        // Fallback: Try with empty From (Generic)
        if (!orgVc.countryCodeFrom.isEmpty()) {
            VatCountries genericFrom = orgVc;
            genericFrom.countryCodeFrom = "";
            if (auto res = tryMatch(genericFrom)) return res;
            
            // Fallback: Try with empty From AND empty Declaring (Generic OSS)
            if (!orgVc.countryCodeDeclaring.isEmpty()) {
                genericFrom.countryCodeDeclaring = "";
                if (auto res = tryMatch(genericFrom)) return res;
            }
        }
        
        return std::nullopt;
    };

    while (true) {
        if (auto acc = lookup(vatCountries, vatRate)) {
            co_return *acc;
        }
        
        if (!callbackAddIfMissing) {
            break;
        }

        // Not found - Prepare error message for callback (or exception)
        QString errorTitle = tr("Missing Account");
        QString errorText = tr("No account found for TaxScheme %1, From %2, To %3, Rate %4")
                                  .arg(taxSchemeToString(vatCountries.taxScheme), 
                                       vatCountries.countryCodeFrom, 
                                       vatCountries.countryCodeTo, 
                                       QString::number(vatRate));

        // Run callback (e.g. UI dialog to add account)
        // Default callback usually returns false unless user implements one that adds it.
        // Default callback usually returns false unless user implements one that adds it.
        bool retry = co_await callbackAddIfMissing(errorTitle, errorText);
        if (retry) {
            if (auto acc = lookup(vatCountries, vatRate)) {
                co_return *acc;
            }
        }
        break;
        break;
    }
    // Should not reach here if loop breaks only on !callback or handled inside, 
    // but if !callbackAddIfMissing break:
    QString errorTitle = tr("Missing Account");
    QString errorText = tr("No account found for TaxScheme %1, From %2, To %3, Rate %4")
                              .arg(taxSchemeToString(vatCountries.taxScheme), 
                                   vatCountries.countryCodeFrom, 
                                   vatCountries.countryCodeTo, 
                                   QString::number(vatRate));
    ExceptionWithTitleText exception(errorTitle, errorText);
    exception.raise();
    co_return {};
}


// ... existing code ...

void BooksAccountsSalesTable::addAccount(
        const VatCountries &vatCountries, double vatRate, const BooksAccountsSalesTable::Accounts &accounts)
{
    QString schemeStr = taxSchemeToString(vatCountries.taxScheme);
    QString rateStr = QString::number(vatRate);

    // Validation: Check for duplicates using cache
    if (m_vatCountries_vatRate_accountsCache.contains(vatCountries)) {
        if (m_vatCountries_vatRate_accountsCache[vatCountries].contains(rateStr)) {
             ExceptionWithTitleText exception(tr("Account Exists"), 
                QString(tr("An account for scheme %1, from %2, to %3, rate %4 already exists."))
                    .arg(schemeStr)
                    .arg(vatCountries.countryCodeFrom)
                    .arg(vatCountries.countryCodeTo)
                    .arg(rateStr));
             exception.raise();
        }
    }

    QStringList row;
    row << taxSchemeToString(vatCountries.taxScheme)
        << vatCountries.countryCodeFrom
        << vatCountries.countryCodeTo
        << QString::number(vatRate)
        << accounts.saleAccount
        << accounts.vatAccount
        << accounts.vatAccountToPay
        << accounts.customerAccount;
        
    beginInsertRows(QModelIndex(), m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList.insert(0, row);
    endInsertRows();
    _rebuildCache();
    _save();
}

QVariant BooksAccountsSalesTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            return HEADER.value(section);
        } else {
            return QString::number(section + 1);
        }
    }
    return QVariant();
}

int BooksAccountsSalesTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_listOfStringList.size();
}

int BooksAccountsSalesTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return HEADER.size();
}

QVariant BooksAccountsSalesTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < m_listOfStringList[index.row()].size()) {
             return m_listOfStringList[index.row()][index.column()];
        }
    }
    return QVariant();
}

bool BooksAccountsSalesTable::setData(const QModelIndex &index, const QVariant &value, int role)
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

Qt::ItemFlags BooksAccountsSalesTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

#include "books/VatResolver.h"
#include <QSet>

void BooksAccountsSalesTable::_fillIfEmpty()
{
    if (m_listOfStringList.isEmpty()) {
        // Use in-memory VatResolver (no file dependency) for deterministic defaults.
        // This ensures the same rates are used regardless of the working directory state,
        // and covers countries with multiple rates across time (e.g. RO: 19% until Jul 2025,
        // then 21% from Aug 2025).
        VatResolver vatResolver(QDir(), nullptr, false);

        // Helper: format a decimal rate as a percentage string ("0.20" → "20", "0.255" → "25.5")
        auto getRateStr = [](double rate) {
            double pct = rate * 100.0;
            if (qAbs(pct - qRound(pct)) < 0.001) return QString::number(qRound(pct));
            return QString::number(pct);
        };

        auto createRow = [&](TaxScheme scheme, const QString &from, const QString &to, double rate,
                             const QString &saleAcc, const QString &vatAcc, const QString &vatPayAcc = "") {
            QStringList row;
            row << taxSchemeToString(scheme)
                << from
                << to
                << QString::number(rate)
                << saleAcc
                << vatAcc
                << vatPayAcc
                << QString("411%1").arg(to.isEmpty() ? from : to);
            m_listOfStringList.append(row);
        };

        // Collect every unique standard-product rate per country from VatResolver.
        // VatResolver column layout: 0=country, 1=dateFrom, 2=dateTo, 3=saleType,
        //                            4=productType, 5=territory, 6=rate
        QMap<QString, QSet<double>> countryRates;
        for (int i = 0; i < vatResolver.rowCount(); ++i) {
            if (vatResolver.data(vatResolver.index(i, 3)).toString().toInt()
                    != static_cast<int>(SaleType::Products))
                continue;
            if (!vatResolver.data(vatResolver.index(i, 4)).toString().isEmpty())
                continue; // skip special product types
            if (!vatResolver.data(vatResolver.index(i, 5)).toString().isEmpty())
                continue; // skip territory-specific rates
            const QString country = vatResolver.data(vatResolver.index(i, 0)).toString();
            const double  rate    = vatResolver.data(vatResolver.index(i, 6)).toString().toDouble();
            countryRates[country].insert(rate);
        }

        const QStringList panEu = CountriesEu::getAmazonPanEuCountryCodes();

        // Iterate all EU countries (and associated territories) to populate OSS/IOSS entries,
        // plus DomesticVat entries for PanEU countries.
        for (const QString &c : CountriesEu::all()) {
            if (c == CountriesEu::GB) continue; // GB is not in the OSS scheme

            QSet<double> rates = countryRates.value(c);
            if (rates.isEmpty())
                rates.insert(0.20); // fallback if the country has no VatResolver entry

            // DomesticVat zero-rate entry (PanEU only, once per country)
            if (panEu.contains(c)) {
                createRow(TaxScheme::DomesticVat, c, "", 0.0,
                          QString("7070DOM%1%2").arg(c, "0"),
                          QString("4457DOM%1%2").arg(c, "0"),
                          QString("4457DOM%1%2_PAY").arg(c, "0"));
            }

            for (double rate : std::as_const(rates)) {
                const double  ratePct = rate * 100.0;
                const QString rStr    = getRateStr(rate);

                // DomesticVat (PanEU only)
                if (panEu.contains(c)) {
                    createRow(TaxScheme::DomesticVat, c, "", ratePct,
                              QString("7070DOM%1%2").arg(c, rStr),
                              QString("4457DOM%1%2").arg(c, rStr),
                              QString("4457DOM%1%2_PAY").arg(c, rStr));
                }

                // OSS (Union)
                createRow(TaxScheme::EuOssUnion, "", c, ratePct,
                          QString("7070OSS%1%2").arg(c, rStr),
                          QString("4457OSS%1%2").arg(c, rStr),
                          QString("4457OSS%1%2_PAY").arg(c, rStr));

                // IOSS
                createRow(TaxScheme::EuIoss, "", c, ratePct,
                          QString("7070IOSS%1%2").arg(c, rStr),
                          QString("4457IOSS%1%2").arg(c, rStr),
                          QString("4457IOSS%1%2_PAY").arg(c, rStr));
            }
        }

        // Exempt (Export) — keyed by PanEU origin country
        for (const QString &c : panEu) {
            createRow(TaxScheme::Exempt, c, "", 0.0,
                      QString("7073EXP%1").arg(c), "", "");
        }

        // OutOfScope — empty key (resolveVatCountries maps to {"", "", ""})
        createRow(TaxScheme::OutOfScope, "", "", 0.0, "7079OUT", "", "");

        _rebuildCache();
        _save();
    }
}
// ... (skip save/load)
// ... (Header definition updates if needed, but I'll add HEADER_IDS locally or static)



const QStringList HEADER_IDS = {
    "TaxScheme", "CountryFrom", "CountryTo", "VatRate", "SaleAccount", "VatAccount", "VatAccountToPay", "CustomerAccount"
};

// ...

void BooksAccountsSalesTable::_rebuildCache()
{
    m_vatCountries_vatRate_accountsCache.clear();
    
    // Columns:
    // 0: TaxScheme
    // 1: From
    // 2: To
    // 3: Rate
    // 4: SaleAccount
    // 5: VatAccount
    
    // m_listOfStringList now stores data in order of HEADER_IDS (Canonical Order) after load/save normalization?
    // OR m_listOfStringList stores raw rows and we need to normalize?
    // Best approach: Normalize on load. m_listOfStringList ALWAYS strictly follows 0..5 index of HEADER_IDS.
    
    for (const auto &row : m_listOfStringList) {
        if (row.size() < 8) {
            continue;
        }
        
        TaxScheme scheme = toTaxScheme(row[0]);
        
        VatCountries vc;
        vc.taxScheme = scheme;
        vc.countryCodeFrom = row[1];
        vc.countryCodeTo = row[2];
        // For Domestic/Exempt, Declaring is From. For others, default to empty (generic)
        if (scheme == TaxScheme::DomesticVat || scheme == TaxScheme::Exempt) {
            vc.countryCodeDeclaring = row[1];
        } else {
            vc.countryCodeDeclaring = "";
        }
        
        QString rate = row[3];
        
        Accounts acc;
        acc.saleAccount = row[4];
        acc.vatAccount = row[5];
        acc.vatAccountToPay = row[6];
        acc.customerAccount = row[7];
        
        m_vatCountries_vatRate_accountsCache[vc][rate] = acc;
    }
}

void BooksAccountsSalesTable::_sort()
{
    std::sort(m_listOfStringList.begin(), m_listOfStringList.end(), [](const QStringList &a, const QStringList &b) {
        // 0: TaxScheme
        if (a[0] != b[0]) return a[0] < b[0];
        // 1: Country From
        if (a[1] != b[1]) return a[1] < b[1];
        // 2: Country To
        if (a[2] != b[2]) return a[2] < b[2];
        // 3: Rate
        return a[3].toDouble() < b[3].toDouble();
    });
}

void BooksAccountsSalesTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save BooksAccountsSalesTable:" << file.errorString();
        return;
    }
    
    QTextStream out(&file);
    // Write Header
    out << HEADER_IDS.join(";") << "\n";
    
    for (const QStringList &row : m_listOfStringList) {
        // Ensure row has enough columns?
        QStringList outputRow = row;
        while(outputRow.size() < HEADER_IDS.size()) outputRow << "";
        out << outputRow.join(";") << "\n";
    }
}

void BooksAccountsSalesTable::_load()
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
    
    // Detect if this is a legacy file (No header, so first line is data)
    // Legacy first column is Tax Scheme String (e.g. "DomesticVat", "EuOssUnion" etc.)
    // New Header first col is "TaxScheme".
    // Check if first token is "TaxScheme".
    bool isLegacy = (headers.first().trimmed() != "TaxScheme");
    
    QMap<QString, int> columnMap;
    if (!isLegacy) {
        for (int i = 0; i < headers.size(); ++i) {
            columnMap[headers[i].trimmed()] = i;
        }
    } else {
        // Legacy Map: Fixed Order
        columnMap["TaxScheme"] = 0;
        columnMap["CountryFrom"] = 1;
        columnMap["CountryTo"] = 2;
        columnMap["VatRate"] = 3;
        columnMap["SaleAccount"] = 4;
        columnMap["VatAccount"] = 5;
        columnMap["CustomerAccount"] = 6;
    }
    
    // Canonical Indices
    int idxScheme = columnMap.value("TaxScheme", -1);
    int idxFrom = columnMap.value("CountryFrom", -1);
    int idxTo = columnMap.value("CountryTo", -1);
    int idxRate = columnMap.value("VatRate", -1);
    int idxSale = columnMap.value("SaleAccount", -1);
    int idxVat = columnMap.value("VatAccount", -1);
    int idxVatPay = columnMap.value("VatAccountToPay", -1);
    int idxCustomer = columnMap.value("CustomerAccount", -1);

    auto processLine = [&](const QString &line) {
        if (line.trimmed().isEmpty()) return;
        QStringList parts = line.split(";");
        
        QStringList normalizedRow;
        // Init with empty. Size 8 now.
        for(int k=0; k<8; ++k) normalizedRow << "";
        
        if (idxScheme != -1 && idxScheme < parts.size()) normalizedRow[0] = parts[idxScheme];
        if (idxFrom != -1 && idxFrom < parts.size()) normalizedRow[1] = parts[idxFrom];
        if (idxTo != -1 && idxTo < parts.size()) normalizedRow[2] = parts[idxTo];
        if (idxRate != -1 && idxRate < parts.size()) normalizedRow[3] = parts[idxRate];
        if (idxSale != -1 && idxSale < parts.size()) normalizedRow[4] = parts[idxSale];
        if (idxVat != -1 && idxVat < parts.size()) normalizedRow[5] = parts[idxVat];
        if (idxVatPay != -1 && idxVatPay < parts.size()) normalizedRow[6] = parts[idxVatPay];
        if (idxCustomer != -1 && idxCustomer < parts.size()) normalizedRow[7] = parts[idxCustomer];
        
        // Skip if empty scheme (invalid row)?
        if (normalizedRow[0].isEmpty()) return;
        
        m_listOfStringList.append(normalizedRow);
    };


    if (isLegacy) {
        // Process the read headerLine as data
        processLine(headerLine);
    }
    
    while (!in.atEnd()) {
        processLine(in.readLine());
    }
    _sort();
    
    _rebuildCache();
}
