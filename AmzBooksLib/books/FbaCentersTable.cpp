#include "FbaCentersTable.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include "ExceptionVatAccount.h" // Reusing exception type or similar if needed, else std::runtime_error

const QStringList FbaCentersTable::HEADER_IDS = {
    "FbaCenter", "CountryCode", "PostalCode", "City"
};

FbaCentersTable::FbaCentersTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("fbacenters.csv");
    _load();
    _fillIfEmpty();
}

QCoro::Task<QString> FbaCentersTable::getCountryCode(
    const QString &fbaCenterId,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) const
{
    // Helper lambda to lookup in cache
    auto lookup = [&](const QString &id) -> QString {
        if (m_cache.contains(id)) {
            return m_cache[id].countryCode;
        }
        return QString();
    };

    while (true) {
        QString country = lookup(fbaCenterId);
        if (!country.isEmpty()) {
            co_return country;
        }

        if (!callbackAddIfMissing) {
            break;
        }

        // Prepare error text
        QString errorTitle = tr("Missing FBA Center");
        QString errorText = tr("The FBA Center %1 is missing. Please add the Country Code, Postal Code, and City locally.").arg(fbaCenterId);
        // Note: The user requested "indicate the amazon (amazon.de for instance) and fulfillement center id missing"
        // But we only get fbaCenterId here. Usually FBA center IDs like "ORY1" don't strictly imply a marketplace, 
        // but often context is known. The prompt implies just showing the ID is key.
        // User said: "indicate the amazon (amazon.de for instance) and fulfillement center id missing".
        // If I don't have the marketplace context passed in, I can only clearly state the Center ID is unknown.
        // I will stick to Center ID for now as per signature.
        
        bool retry = co_await callbackAddIfMissing(errorTitle, errorText);
        
        // Check again
        country = lookup(fbaCenterId);
        if (!country.isEmpty()) {
            co_return country;
        }
        
        if (!retry) {
            // User cancelled
            // Return empty or throw? SaleBookAccountsTable throws ExceptionVatAccount.
            // Let's return empty string to indicate failure if we don't want to throw, 
            // but consistency suggests throwing or returning empty if valid. 
            // We'll throw simple exception for now to abort flow if critical.
            throw std::runtime_error(tr("FBA Center %1 not found").arg(fbaCenterId).toStdString());
        }
    }

    throw std::runtime_error(tr("FBA Center %1 not found").arg(fbaCenterId).toStdString());
}

void FbaCentersTable::addCenter(const FbaCenter &center)
{
    if (m_cache.contains(center.centerId)) {
        return; // Already exists
    }

    QStringList row;
    row << center.centerId << center.countryCode << center.postalCode << center.city;

    beginInsertRows(QModelIndex(), m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList.append(row);
    endInsertRows();
    
    _rebuildCache();
    _save();
}

QVariant FbaCentersTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < HEADER_IDS.size()) {
            // Return translated names
            if (HEADER_IDS[section] == "FbaCenter") return tr("FBA Center");
            if (HEADER_IDS[section] == "CountryCode") return tr("Country Code");
            if (HEADER_IDS[section] == "PostalCode") return tr("Postal Code");
            if (HEADER_IDS[section] == "City") return tr("City");
            return HEADER_IDS[section];
        }
    }
    return QVariant();
}

int FbaCentersTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_listOfStringList.size();
}

int FbaCentersTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return HEADER_IDS.size();
}

QVariant FbaCentersTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < m_listOfStringList[index.row()].size()) {
            return m_listOfStringList[index.row()][index.column()];
        }
    }
    return QVariant();
}

bool FbaCentersTable::setData(const QModelIndex &index, const QVariant &value, int role)
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

Qt::ItemFlags FbaCentersTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

void FbaCentersTable::_fillIfEmpty()
{
    if (m_listOfStringList.isEmpty()) {
        addCenter({"LYS4", "FR", "", ""});
        addCenter({"XWR3", "PL", "55-040", "Kryzowice"});
        addCenter({"EMA4", "UK", "B76 9AH", "Minworth"}); // Corrected to UK based on address
        addCenter({"OVD1", "ES", "33429", "Bobes"});
        addCenter({"PAD2", "DE", "", ""});
        addCenter({"NCL1", "UK", "NE10 8YA", "Gateshead"});
        addCenter({"NCL2", "UK", "TS22 5TH", "Billingham"});
        addCenter({"BRS2", "UK", "SN3 4DB", "Swindon"});
        addCenter({"DSA6", "UK", "LS26 9DN", "Wakefield"});
        addCenter({"ERF1", "DE", "", ""});
        addCenter({"BRQ2", "CZ", "", ""});
        addCenter({"BRE2", "FR", "", ""});
        addCenter({"BCN4", "ES", "17469", "Girona"});
        addCenter({"PSR2", "IT", "66050", "San Salvo"});
        addCenter({"SCN2", "DE", "67661", "Kaiserslautern"});
        addCenter({"XFRS", "FR", "", ""});
        addCenter({"XFRO", "FR", "", ""});
        addCenter({"VLC1", "ES", "12200", "Onda"});
        addCenter({"XPLE", "DE", "", ""});
        addCenter({"XPO1", "DE", "", ""});
        addCenter({"LEJ5", "DE", "", ""});
        addCenter({"NUE1", "DE", "95185", "Gattendorf"});
        addCenter({"XPLD", "FR", "", ""});
        addCenter({"SES1", "ES", "", ""});
        addCenter({"RMU1", "ES", "30156", "Murcia"});
        addCenter({"BGY1", "IT", "24050", "Cividate al Piano"});
        addCenter({"MXP6", "IT", "28100", "Agognate"});
        addCenter({"POZ2", "PL", "", "Chociule"});
        addCenter({"MAN1", "UK", "", "Manchester"});
        addCenter({"BER3", "DE", "", "Brieselang"});
        addCenter({"BTS2", "SK", "", "Bratislava"});
        addCenter({"BRE4", "DE", "", "Achim"});
        addCenter({"CGN1", "DE", "", "Koblenz"});
        addCenter({"DTM1", "DE", "", "Werne"});
        addCenter({"DTM2", "DE", "", "Dortmund"});
        addCenter({"DTM3", "DE", "", "Dortmund"});
        addCenter({"DUS2", "DE", "", "Rheinberg"});
        addCenter({"DUS4", "DE", "", "Moenchengladbach"});
        addCenter({"EDEA", "DE", "", "Dortmund"});
        addCenter({"EDE4", "DE", "", "Werne"});
        addCenter({"EDE5", "DE", "", "Werne"});
        addCenter({"FRA1", "DE", "", "Bad Hersfeld"});
        addCenter({"FRA3", "DE", "", "Bad Hersfeld"});
        addCenter({"FRA7", "DE", "", "Frankenthal Pfalz"});
        addCenter({"HAM2", "DE", "", "Winsen an der Luhe"});
        addCenter({"KTW1", "PL", "", "Sosnowiec"});
        addCenter({"KTW3", "PL", "", "Bojkowska"});
        addCenter({"LCJ2", "PL", "", "Pawlikowice"});
        addCenter({"LCJ3", "PL", "", "Łódź"});
        addCenter({"LCJ4", "PL", "", "Łódź"});
        addCenter({"LEJ1", "DE", "", "Leipzig"});
        addCenter({"LEJ3", "DE", "", "Suelzetal"});
        addCenter({"MUC3", "DE", "", "Graben"});
        addCenter({"PAD1", "DE", "", "Oelde"});
        addCenter({"POZ1", "PL", "", "Poznan"});
        addCenter({"PRG1", "CZ", "", "Dobroviz"});
        addCenter({"PRG2", "CZ", "", "Dobroviz"});
        addCenter({"STR1", "DE", "", "Pforzheim"});
        addCenter({"SZZ1", "PL", "", "Kołbaskowo"});
        addCenter({"WRO1", "PL", "", "Bielany Wroclawskie"});
        addCenter({"WRO2", "PL", "", "Bielany Wroclawskie"});
        addCenter({"WRO5", "PL", "", "Okmiany"});
        addCenter({"XDU1", "DE", "", "Malsfeld"});
        addCenter({"XDET", "DE", "", "Malsfeld"});
        addCenter({"XDU2", "DE", "", "Oberhausen"});
        addCenter({"XDEZ", "DE", "", "Oberhausen"});
        addCenter({"XFR1", "DE", "", "Hammersbach"});
        addCenter({"XDEY", "DE", "", "Hammersbach"});
        addCenter({"XFR2", "DE", "", "Rennerod"});
        addCenter({"XDEH", "DE", "", "Rennerod"});
        addCenter({"XFR3", "DE", "", "Michelstadt"});
        addCenter({"XDEW", "DE", "", "Michelstadt"});
        addCenter({"XHA1", "DE", "", "Neu Wulmsdorf"});
        addCenter({"XSC1", "DE", "", "Kaiserslautern"});
        addCenter({"XDEQ", "DE", "", "Kaiserslautern"});
        addCenter({"XWR1", "PL", "", "Krajków"});
        addCenter({"XPLA", "PL", "", "Krajków"});
        addCenter({"ORY1", "FR", "", "Saran"});
        addCenter({"ORY4", "FR", "", "Brétigny"});
        addCenter({"MRS1", "FR", "", "Montelimar"});
        addCenter({"LYS1", "FR", "", "Sevrey"});
        addCenter({"LIL1", "FR", "", "Lauwin"});
        addCenter({"BVA1", "FR", "", "Boves"});
        addCenter({"CDG7", "FR", "", "Senlis"});
        addCenter({"XVA1", "FR", "", "Bussy-Lettree"});
        addCenter({"XFRZ", "FR", "", "Bussy-Lettree"});
        addCenter({"ETZ2", "FR", "", "Augny"});
        addCenter({"VESK", "FR", "", "Savigny Le Temple"});
        addCenter({"XFRJ", "FR", "", "Savigny Le Temple"});
        addCenter({"XOR6", "FR", "", "Lisses"});
        addCenter({"XFRK", "FR", "", "Lisses"});
        addCenter({"XOR2", "FR", "", "Satolas-et-Bonce"});
        addCenter({"XFRE", "FR", "", "Satolas-et-Bonce"});
        addCenter({"XOS1", "FR", "", "Brebieres"});
        addCenter({"XFRL", "FR", "", "Brebieres"});
        addCenter({"TRN1", "IT", "", "Torrazza Piemonte"});
        addCenter({"BLQ1", "IT", "", "San Bellino"});
        addCenter({"FCO1", "IT", "", "Passo Corese"});
        addCenter({"FCO2", "IT", "", "Colleferro"});
        addCenter({"MXP5", "IT", "", "Castel San Giovanni"});
        addCenter({"MXP3", "IT", "", "Vercelli"});
        addCenter({"XITC", "IT", "", "Carpiano"});
        addCenter({"XMP2", "IT", "", "Carpiano"});
        addCenter({"XITD", "IT", "", "Rovigo"});
        addCenter({"XMP1", "IT", "", "Rovigo"});
        addCenter({"XITG", "IT", "", "Carpiano"});
        addCenter({"XLI1", "IT", "", "Carpiano"});
        addCenter({"XITI", "IT", "", "Marzano"});
        addCenter({"XLI3", "IT", "", "Marzano"});
        addCenter({"XITF", "IT", "", "Piacenza"});
        addCenter({"VEII ", "IT", "", "Piacenza"});
        addCenter({"MAD4", "ES", "", "Madrid"});
        addCenter({"MAD6", "ES", "", "Illescas"});
        addCenter({"MAD7", "ES", "", "Illescas"});
        addCenter({"MAD9", "ES", "", "Alcalá de Henares"});
        addCenter({"PESA", "ES", "", "Toledo"});
        addCenter({"BCN1", "ES", "", "Barcelona"});
        addCenter({"BCN2", "ES", "", "Martorelles"});
        addCenter({"BCN3", "ES", "", "Castellbisbal"});
        addCenter({"XMA1", "ES", "", "XESA"});
        addCenter({"XMA2", "ES", "19208", "Alovera"});
        addCenter({"XMA3", "ES", "", "XESF"});
        addCenter({"XRE1", "ES", "", "XESC"});
        addCenter({"SVQ1", "ES", "", " Sevilla"});
        addCenter({"XAR1", "SE", "", "Eskilstuna (SE)"});
        addCenter({"LTN1", "UK", "", "Marston Gate"});
        addCenter({"LTN2", "UK", "", "Hemel Hempstead"});
        addCenter({"LTN4", "UK", "", "Dunstable"});
        addCenter({"LTN7", "UK", "", "Bedford"});
        addCenter({"LTN9", "UK", "", "Dunstable"});
        addCenter({"BHX1", "UK", "", "Rugeley"});
        addCenter({"BHX2", "UK", "", "Coalville"});
        addCenter({"BHX3", "UK", "", "Daventry"});
        addCenter({"BHX4", "UK", "", "Coventry"});
        addCenter({"BHX5", "UK", "", "Rugby"});
        addCenter({"BHX7", "UK", "", "Hinckley"});
        addCenter({"BRS1", "UK", "", "Bristol"});
        addCenter({"CWL1", "UK", "", "Swansea"});
        addCenter({"EDI4", "UK", "", "Dunfermline"});
        addCenter({"EMA1", "UK", "", "Derby"});
        addCenter({"EMA2", "UK", "", "Mansfield"});
        addCenter({"EMA3", "UK", "", "Nottingham"});
        addCenter({"EUK5", "UK", "", "Peterborough"});
        addCenter({"GLA1", "UK", "", "Gourock"});
        addCenter({"LBA1", "UK", "", "Doncaster"});
        addCenter({"LBA2", "UK", "", "Doncaster"});
        addCenter({"LBA3", "UK", "", "Doncaster"});
        addCenter({"LBA4", "UK", "", "Doncaster"});
        addCenter({"MAN1", "UK", "", "Manchester"});
        addCenter({"MAN2", "UK", "", "Warrington"});
        addCenter({"MAN3", "UK", "", "Bolton"});
        addCenter({"MAN4", "UK", "", "Barlborough"});
        addCenter({"MME1", "UK", "", "Darlington"});
        addCenter({"MME2", "UK", "", "Bowburn"});
        addCenter({"LCY2", "UK", "", "Tilbury"});
        addCenter({"LCY3", "UK", "", "Dartford"});
        addCenter({"XLT1", "UK", "", "Peterborough"});
        addCenter({"EUKA", "UK", "", "Peterborough"});
        addCenter({"XLT2", "UK", "", "Peterborough"});
        addCenter({"EUKB", "UK", "", "Peterborough"});
        addCenter({"XPL1", "UK", "", "Widnes"});
        addCenter({"EUKD", "UK", "", "Widnes"});
        addCenter({"XUKA", "UK", "", " Runcorn"});
        addCenter({"XUKD", "UK", "", "Daventry"});
        addCenter({"XBH1", "UK", "", "Daventry"});
        addCenter({"XUKN", "UK", "", "Rugby"});
        addCenter({"XBH2", "UK", "", "Rugby"});
    }
}

void FbaCentersTable::_rebuildCache()
{
    m_cache.clear();
    // Map columns by ID
    // 0: FbaCenter
    // 1: CountryCode
    // 2: PostalCode
    // 3: City
    
    // Safety check: if columns reordered in file vs code, we should map them eventually.
    // Ideally _load would handle column mapping.
    
    for (const auto &row : m_listOfStringList) {
        if (row.size() < 4) continue;
        FbaCenter c;
        c.centerId = row[0];
        c.countryCode = row[1];
        c.postalCode = row[2];
        c.city = row[3];
        m_cache[c.centerId] = c;
    }
}

void FbaCentersTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out << HEADER_IDS.join(";") << "\n";
    for (const auto &row : m_listOfStringList) {
         // Pad if needed?
         out << row.join(";") << "\n";
    }
}

void FbaCentersTable::_load()
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
    
    // Map columns
    QMap<QString, int> colMap;
    for (int i = 0; i < headers.size(); ++i) {
        colMap[headers[i].trimmed()] = i;
    }

    // Indices
    int idxId = colMap.value("FbaCenter", -1);
    int idxCC = colMap.value("CountryCode", -1);
    int idxPC = colMap.value("PostalCode", -1);
    int idxCity = colMap.value("City", -1);

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(";");
        
        QStringList row;
        row << "" << "" << "" << ""; // Init 4 empty
        
        if (idxId != -1 && idxId < parts.size()) row[0] = parts[idxId];
        else if (parts.size() > 0) row[0] = parts[0]; // Fallback legacy?

        if (idxCC != -1 && idxCC < parts.size()) row[1] = parts[idxCC];
        else if (parts.size() > 1) row[1] = parts[1];

        if (idxPC != -1 && idxPC < parts.size()) row[2] = parts[idxPC];
        else if (parts.size() > 2) row[2] = parts[2];
        
        if (idxCity != -1 && idxCity < parts.size()) row[3] = parts[idxCity];
        else if (parts.size() > 3) row[3] = parts[3];

        if (!row[0].isEmpty()) {
            m_listOfStringList.append(row);
        }
    }
    _rebuildCache();
}
