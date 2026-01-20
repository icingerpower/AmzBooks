#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

#include "VatTerritoryResolver.h"



const QStringList VatTerritoryResolver::HEADER{
    QObject::tr("Country code")
    , QObject::tr("City")
    , QObject::tr("Postal code")
    , QObject::tr("Territory ID")
};

VatTerritoryResolver::VatTerritoryResolver(const QString &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = QDir{workingDir}.absoluteFilePath("vatTerritories.csv");
    _load();
    _fillIfEmpty();
}

QString VatTerritoryResolver::getTerritoryId(
        const QString &countryCode, const QString &postalCode, const QString &city) const noexcept
{
    if (countryCode == "GB" && postalCode.startsWith("BT")) {
        return "XI";
    }

    if (m_countryCode_postalCode_cachedTerritory.contains(countryCode)) {
        const auto &postalMap = m_countryCode_postalCode_cachedTerritory[countryCode];
        if (postalMap.contains(postalCode)) {
            const auto &rules = postalMap[postalCode];
            for (const auto &rule : rules) {
                if (rule.cityPattern.isEmpty()) {
                    return rule.territoryId; // No city requirement
                }
                if (city.contains(rule.cityPattern, Qt::CaseInsensitive)) {
                    return rule.territoryId;
                }
            }
        }
    }
    return QString();
}

void VatTerritoryResolver::addTerritory(
        const QString &countryCode, const QString &postalCode, const QString &city, const QString &territoryId)
{
    QStringList row;
    row << countryCode << city << postalCode << territoryId;
    beginInsertRows(QModelIndex(), m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList.append(row);
    endInsertRows();
    _rebuildCache();
    _save();
}

QVariant VatTerritoryResolver::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            return HEADER.value(section);
        } else {
            return QString::number(section + 1);
        }
    }
    return QVariant{};
}

int VatTerritoryResolver::rowCount(const QModelIndex &) const
{
    return m_listOfStringList.size();
}

int VatTerritoryResolver::columnCount(const QModelIndex &) const
{
    return HEADER.size();
}

QVariant VatTerritoryResolver::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < m_listOfStringList[index.row()].size()) {
             return m_listOfStringList[index.row()][index.column()];
        }
    }
    return QVariant();
}

bool VatTerritoryResolver::setData(
        const QModelIndex &index, const QVariant &value, int role)
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

Qt::ItemFlags VatTerritoryResolver::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void VatTerritoryResolver::_fillIfEmpty()
{
    if (m_listOfStringList.size() == 0)
    {
        // --- ITALY (IT) ---
        addTerritory("IT", "23041", "Livigno", "IT-LIVIGNO");
        addTerritory("IT", "23030", "Livigno", "IT-LIVIGNO");
        addTerritory("IT", "22061", "Campione", "IT-CAMPIONE");
        addTerritory("IT", "22060", "Campione", "IT-CAMPIONE");
        addTerritory("IT", "00120", "Citta del Vaticano", "VA"); 
        for (int i = 47890; i <= 47899; ++i) {
            addTerritory("IT", QString::number(i), "San Marino", "SM");
        }

        // --- SPAIN (ES) ---
        // Canary Islands: 35xxx and 38xxx
        for (int i = 35000; i <= 35999; ++i) addTerritory("ES", QString::number(i), "", "ES-CANARY");
        for (int i = 38000; i <= 38999; ++i) addTerritory("ES", QString::number(i), "", "ES-CANARY");
        // Ceuta (51xxx), Melilla (52xxx)
        for (int i = 51000; i <= 51999; ++i) addTerritory("ES", QString::number(i), "Ceuta", "ES-CEUTA");
        for (int i = 52000; i <= 52999; ++i) addTerritory("ES", QString::number(i), "Melilla", "ES-MELILLA");

        // --- GREECE (GR) ---
        addTerritory("GR", "63086", "Karyes", "GR-ATHOS");
        addTerritory("GR", "63087", "Daphni", "GR-ATHOS");

        // --- GERMANY (DE) ---
        addTerritory("DE", "27498", "Helgoland", "DE-HELGOLAND");
        addTerritory("DE", "78266", "Busingen", "DE-BUSINGEN");

        // --- FRANCE (FR) ---
        // DOM-TOM treated as Non-EU for VAT purposes
        for (int i = 97100; i <= 97199; ++i) addTerritory("FR", QString::number(i), "Guadeloupe", "GP");
        for (int i = 97200; i <= 97299; ++i) addTerritory("FR", QString::number(i), "Martinique", "MQ");
        for (int i = 97300; i <= 97399; ++i) addTerritory("FR", QString::number(i), "Guyane", "GF");
        for (int i = 97400; i <= 97499; ++i) addTerritory("FR", QString::number(i), "Reunion", "RE");
        addTerritory("FR", "97500", "Saint-Pierre-et-Miquelon", "PM");
        for (int i = 97600; i <= 97699; ++i) addTerritory("FR", QString::number(i), "Mayotte", "YT");
        addTerritory("FR", "97133", "Saint-Barthelemy", "BL"); // Technically non-EU
        addTerritory("FR", "97150", "Saint-Martin", "MF"); // Treated as non-EU VAT-wise in some contexts or distinctive
        // Monaco? usually treated as FR.

        // --- FINLAND (FI) ---
        // Aland Islands (AX)
        for (int i = 22000; i <= 22999; ++i) {
            addTerritory("FI", QString::number(i), "", "FI-ALAND");
        }

        // --- UNITED KINGDOM (GB) ---
        // Channel Islands
        addTerritory("GB", "JE1 1AA", "Jersey", "JE"); 
        addTerritory("GB", "GY1 1AA", "Guernsey", "GG"); 
    }
}

void VatTerritoryResolver::_save()
{
    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        // Use a CSV writer logic if complex fields, but here basic join is likely used
        for (const auto &row : m_listOfStringList) {
            out << row.join(',') << "\n";
        }
        file.close();
    }
}

void VatTerritoryResolver::_load()
{
    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_listOfStringList.clear();
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                m_listOfStringList.append(line.split(','));
            }
        }
        file.close();
    }
    _rebuildCache();
}

void VatTerritoryResolver::_rebuildCache()
{
    m_countryCode_postalCode_cachedTerritory.clear();

    // ID CountryCode = 0
    // ID City = 1
    // ID PostalCode = 2
    // ID TerritoryId = 3
    
    for(const auto &row : m_listOfStringList) {
        if (row.size() < 4) continue; // Require Territory ID
        
        QString country = row[0];
        QString city = row[1];
        QString postal = row[2];
        QString tid = row[3];
        
        TerritoryRule rule;
        rule.cityPattern = city;
        rule.territoryId = tid;
        
        m_countryCode_postalCode_cachedTerritory[country][postal].append(rule);
    }
}
