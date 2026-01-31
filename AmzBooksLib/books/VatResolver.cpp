#include "VatResolver.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QStandardPaths>

const QStringList VatResolver::HEADER{
    QObject::tr("Country")
            , QObject::tr("Date from")
            , QObject::tr("Date to")
            , QObject::tr("Sale Type")
            , QObject::tr("Product type")
            , QObject::tr("Territory ID")
            , QObject::tr("Rate")
};

VatResolver::VatResolver(const QDir &workingDir, QObject *parent, bool usePersistence)
    : QAbstractTableModel(parent)
    , m_usePersistence(usePersistence)
{
    if (m_usePersistence) {
        m_filePath = workingDir.absoluteFilePath("vatRates.csv");
        _load();
    }
    _fillIfEmpty();
}

QVariant VatResolver::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < HEADER.size())
            return HEADER[section];
    }
    return QVariant();
}

int VatResolver::rowCount(const QModelIndex &parent) const
{
    return m_listOfStringList.size();
}

int VatResolver::columnCount(const QModelIndex &parent) const
{
    return HEADER.size();
}

QVariant VatResolver::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
         if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
             index.column() >= 0 && index.column() < m_listOfStringList[index.row()].size()) {
                 return m_listOfStringList[index.row()][index.column()];
         }
    }
    return QVariant();
}

bool VatResolver::setData(const QModelIndex &index, const QVariant &value, int role)
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

Qt::ItemFlags VatResolver::flags(const QModelIndex &index) const
{
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void VatResolver::_fillIfEmpty()
{
    if (!m_listOfStringList.isEmpty()) return;
    
    // Format: Country, DateFrom, DateTo, SaleType, ProductType, Territory, Rate
    // SaleType: 0=Products, 1=Service
    // ProductType: "" = Standard
    
    // DE Standard 19%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "DE", "", "", 0.19);
    // DE Reduced 7% (e.g. Books)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "DE", "A_BOOKS_GEN", "", 0.07);
    
    // FR Standard 20%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "FR", "", "", 0.20);
    // FR Reduced 5.5% (Books)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "FR", "A_BOOKS_GEN", "", 0.055);

    // MC (Monaco) - follows FR
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "MC", "", "", 0.20);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "MC", "A_BOOKS_GEN", "", 0.055);
    
    // IT Standard 22%
    recordRate(QDate(), QDate(), SaleType::Products, "IT", "", "", 0.22);
    // IT Reduced 4% (Books)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "IT", "A_BOOKS_GEN", "", 0.04);
    
    // ES Standard 21%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "ES", "", "", 0.21);
    // ES Reduced 4% (Books)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "ES", "A_BOOKS_GEN", "", 0.04);

    // PL Standard 23%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "PL", "", "", 0.23);
    // PL Reduced 5% (Books)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "PL", "A_BOOKS_GEN", "", 0.05);
    
    // CZ Standard 21%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "CZ", "", "", 0.21);
    // CZ Reduced 10% (Books) until 2023-12-31? Need to check. 
    // For now assuming 10%
     recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "CZ", "A_BOOKS_GEN", "", 0.10);

    // NL Standard 21%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "NL", "", "", 0.21);
    // NL Reduced 9%
     recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "NL", "A_BOOKS_GEN", "", 0.09);

    // BE Standard 21%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "BE", "", "", 0.21);
    // BE Reduced 6%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "BE", "A_BOOKS_GEN", "", 0.06);

    // SE Standard 25%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "SE", "", "", 0.25);
    // SE Reduced 6%
     recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "SE", "A_BOOKS_GEN", "", 0.06);

    // GB Standard 20%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "GB", "", "", 0.20);
    // GB Zero 0% (Books)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "GB", "A_BOOKS_GEN", "", 0.0);

    // IE Standard 23%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "IE", "", "", 0.23);
    
    // XI (Northern Ireland) - aligned with GB VAT Rules for rates usually
    // Standard 20%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "XI", "", "", 0.20);
    // XI Zero 0% (Books)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "XI", "A_BOOKS_GEN", "", 0.0);

    // AT Standard 20%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "AT", "", "", 0.20);
    // PT Standard 23%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "PT", "", "", 0.23);
    // LU Standard: 17% usually, but 16% in 2023
    recordRate(QDate(2023, 1, 1), QDate(2023, 12, 31), SaleType::Products, "LU", "", "", 0.16);
    recordRate(QDate(2024, 1, 1), QDate(), SaleType::Products, "LU", "", "", 0.17); // Revert to 17% from 2024
    recordRate(QDate(), QDate(), SaleType::Products, "LU", "", "", 0.17); // Fallback / Pre-2023

    // HU Standard 27%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "HU", "", "", 0.27);
    
    // FI Standard: 24% until 31 Aug 2024, 25.5% from 1 Sep 2024
    recordRate(QDate(2024, 9, 1), QDate(), SaleType::Products, "FI", "", "", 0.255);
    recordRate(QDate(), QDate(2024, 8, 31), SaleType::Products, "FI", "", "", 0.24);

    // SI Standard 22%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "SI", "", "", 0.22);
    // RO Standard: 19% until 31 Jul 2025, 21% from 1 Aug 2025
    recordRate(QDate(2025, 8, 1), QDate(), SaleType::Products, "RO", "", "", 0.21);
    recordRate(QDate(), QDate(2025, 7, 31), SaleType::Products, "RO", "", "", 0.19);
    // GR Standard 24%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "GR", "", "", 0.24);
    // SK Standard: 20% until 31 Dec 2024, 23% from 1 Jan 2025
    recordRate(QDate(2025, 1, 1), QDate(), SaleType::Products, "SK", "", "", 0.23);
    recordRate(QDate(), QDate(2024, 12, 31), SaleType::Products, "SK", "", "", 0.20);
    // DK Standard 25%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "DK", "", "", 0.25);
    
    // EE Standard: 20% until 2023-12-31, 22% from 2024-01-01
    recordRate(QDate(2024, 1, 1), QDate(), SaleType::Products, "EE", "", "", 0.22);
    recordRate(QDate(), QDate(2023, 12, 31), SaleType::Products, "EE", "", "", 0.20);

    // HR Standard 25%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "HR", "", "", 0.25);
    // BG Standard 20%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "BG", "", "", 0.20);
    // LT Standard 21%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "LT", "", "", 0.21);
    // LV Standard 21%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "LV", "", "", 0.21);
    // MT Standard 18%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "MT", "", "", 0.18);
    // CY Standard 19%
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "CY", "", "", 0.19);
    
    // Non-EU / Territories
    // CH 0% (Standard) in Amazon Reports context
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "CH", "", "", 0.0);
    // SM (San Marino) 0% in Amazon Reports? (Usually import/exempt logic)
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "SM", "", "", 0.0);
    // RE, GP, MQ (French Overseas) - Often 0% or specific local VAT not always in Amazon system as standard EU VAT
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "RE", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "GP", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "MQ", "", "", 0.0);
    // CA, HK, JP, US, IL - Non EU, 0% for our context
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "CA", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "HK", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "JP", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "US", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "IL", "", "", 0.0);
    // TH
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "TH", "", "", 0.0);
    // JE, VA, IS, AR, GF, NO
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "JE", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "VA", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "IS", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "AR", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "GF", "", "", 0.0);
    recordRate(QDate(2021, 1, 1), QDate(), SaleType::Products, "NO", "", "", 0.0);
}

void VatResolver::_load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    
    QTextStream in(&file);
    m_listOfStringList.clear();
    bool first = true;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (first) { first = false; continue; } // Skip header
        if (line.trimmed().isEmpty()) continue;
        m_listOfStringList.append(line.split(","));
    }
    _rebuildCache();
}

void VatResolver::_save()
{
    if (!m_usePersistence) return;
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    out << HEADER.join(",") << "\n";
    for (const auto &row : m_listOfStringList) {
        out << row.join(",") << "\n";
    }
}

void VatResolver::_rebuildCache()
{
    m_countryCode_saleType_specialProductType_vatTerritory_dateFrom_vatRateCached.clear();
    // Header mappings:
    // 0: Country, 1: DateFrom, 2: DateTo, 3: SaleType, 4: ProductType, 5: Territory, 6: Rate
    
    for (const auto &row : m_listOfStringList) {
        if (row.size() < 7) continue;
        QString country = row[0];
        QDate dateFrom = QDate::fromString(row[1], "yyyy-MM-dd");
        // QDate dateTo = QDate::fromString(row[2], "yyyy-MM-dd"); 
        QString saleTypeStr = row[3];
        QString productType = row[4];
        QString territory = row[5];
        double rate = row[6].toDouble();
        
        m_countryCode_saleType_specialProductType_vatTerritory_dateFrom_vatRateCached[country][saleTypeStr][productType][territory][dateFrom] = rate;
    }
}

void VatResolver::recordRate(const QDate &dateFrom, const QDate &dateTo, SaleType saleType, const QString &countryCode, double vatRate)
{
    recordRate(dateFrom, dateTo, saleType, countryCode, "", "", vatRate);
}

void VatResolver::recordRate(const QDate &dateFrom, const QDate &dateTo, SaleType saleType, const QString &countryCode, const QString &specialProducttype, const QString &vatTerritory, double vatRate)
{
    QStringList row;
    row << countryCode
        << dateFrom.toString("yyyy-MM-dd")
        << (dateTo.isValid() ? dateTo.toString("yyyy-MM-dd") : "")
        << QString::number(static_cast<int>(saleType))
        << specialProducttype
        << vatTerritory
        << QString::number(vatRate);
        
    beginInsertRows(QModelIndex(), m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList.append(row);
    endInsertRows();
    _rebuildCache();
    _save();
}

bool VatResolver::hasRate(const QDate &date, const QString &countryCode, SaleType saleType, const QString &specialProducttype, const QString &vatTerritory) const
{
    return getRate(date, countryCode, saleType, specialProducttype, vatTerritory) >= 0;
}

double VatResolver::getRate(const QDate &date, const QString &countryCode, SaleType saleType, const QString &specialProducttype, const QString &vatTerritory) const
{
    QString saleTypeStr = QString::number(static_cast<int>(saleType));
    
    if (!m_countryCode_saleType_specialProductType_vatTerritory_dateFrom_vatRateCached.contains(countryCode)) return -1.0;
    const auto &stMap = m_countryCode_saleType_specialProductType_vatTerritory_dateFrom_vatRateCached[countryCode];
    
    if (!stMap.contains(saleTypeStr)) return -1.0;
    const auto &ptMap = stMap[saleTypeStr];
    
    QString ptKey = specialProducttype;
    if (!ptMap.contains(ptKey)) {
         if (!specialProducttype.isEmpty() && ptMap.contains("")) {
             ptKey = ""; 
         } else {
             return -1.0;
         }
    }
    
    const auto &terrMap = ptMap[ptKey];
    
    QString tKey = vatTerritory;
    if (!terrMap.contains(tKey)) {
        if (!vatTerritory.isEmpty() && terrMap.contains("")) {
            tKey = "";
        } else {
            return -1.0;
        }
    }
    
    const auto &dateMap = terrMap[tKey];
    
    auto it = dateMap.upperBound(date);
    if (it == dateMap.begin()) {
        return -1.0; 
    }
    --it;
    return it.value();
}

void VatResolver::remove(const QModelIndex &index)
{
    if (index.isValid() && index.row() < m_listOfStringList.size()) {
       beginRemoveRows(QModelIndex(), index.row(), index.row());
       m_listOfStringList.removeAt(index.row());
       endRemoveRows();
       _rebuildCache();
       _save();
    }
}

void VatResolver::addRate(const QDate &date, const QString &country, SaleType type, double rate, const QString &specialCode)
{
    recordRate(date, QDate(), type, country, specialCode, "", rate);
}

bool VatResolver::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid()) return false;
    if (row < 0 || row + count > m_listOfStringList.size()) return false;
    
    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_listOfStringList.removeAt(row);
    }
    endRemoveRows();
    _rebuildCache();
    _save();
    return true;
}
