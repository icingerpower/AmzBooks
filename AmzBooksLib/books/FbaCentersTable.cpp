#include "FbaCentersTable.h"
#include "ExceptionWithTitleText.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

const QStringList FbaCentersTable::HEADER_IDS = {
    "FbaCenter", "CountryCode", "PostalCode", "City"
};

FbaCentersTable::FbaCentersTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("fbacenters.csv");
    _load();
    _fillIfMissing();
}

QCoro::Task<QString> FbaCentersTable::getCountryCode(
    const QString &fbaCenterId,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing,
    const QString &marketplaceHint) const
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
        QString errorText = marketplaceHint.isEmpty()
            ? tr("The FBA Center %1 is missing. Please add the Country Code, Postal Code, and City locally.").arg(fbaCenterId)
            : tr("The FBA Center %1 (%2) is missing. Please add the Country Code, Postal Code, and City locally.").arg(fbaCenterId, marketplaceHint);
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
            QString detail = marketplaceHint.isEmpty() ? fbaCenterId : tr("%1 (%2)").arg(fbaCenterId, marketplaceHint);
            ExceptionWithTitleText exception(tr("FBA Center Not Found"), tr("FBA Center %1 not found").arg(detail));
            exception.raise();
        }
    }

    QString detail = marketplaceHint.isEmpty() ? fbaCenterId : tr("%1 (%2)").arg(fbaCenterId, marketplaceHint);
    ExceptionWithTitleText exception(tr("FBA Center Not Found"), tr("FBA Center %1 not found").arg(detail));
    exception.raise();
}

bool FbaCentersTable::_addCenterBatch(const FbaCenter &center)
{
    if (m_cache.contains(center.centerId)) {
        return false;
    }
    QStringList row;
    row << center.centerId << center.countryCode << center.postalCode << center.city;
    beginInsertRows(QModelIndex(), m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList.append(row);
    endInsertRows();
    m_cache[center.centerId] = center;
    return true;
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

bool FbaCentersTable::removeRows(int row, int count, const QModelIndex &parent)
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

void FbaCentersTable::_fillIfMissing()
{
    bool added = false;
    auto add = [&](const FbaCenter &c) { if (_addCenterBatch(c)) { added = true; } };

    add({"LYS4", "FR", "", ""});
    add({"XWR3", "PL", "55-040", "Kryzowice"});
    add({"EMA4", "UK", "B76 9AH", "Minworth"}); // Corrected to UK based on address
    add({"OVD1", "ES", "33429", "Bobes"});
    add({"PAD2", "DE", "", ""});
    add({"NCL1", "UK", "NE10 8YA", "Gateshead"});
    add({"NCL2", "UK", "TS22 5TH", "Billingham"});
    add({"BRS2", "UK", "SN3 4DB", "Swindon"});
    add({"DSA6", "UK", "LS26 9DN", "Wakefield"});
    add({"YEG2", "CA", "T7X 6A0", "Acheson"}); 
    add({"BFL1", "US", "93308", "Bakersfield"}); 

        // Canada
    add({"YHM1", "CA", "", ""});
    add({"YOW3", "CA", "", ""});
    add({"YVR4", "CA", "", ""});
    add({"YXU1", "CA", "", ""});
    add({"YXX2", "CA", "", ""});
    add({"YYC4", "CA", "", ""});
    add({"YYZ4", "CA", "", ""});
    add({"YYZ7", "CA", "", ""});
    add({"YYZ9", "CA", "", ""});

        // US
    add({"ABE2", "US", "", ""});
    add({"ABE3", "US", "", ""});
    add({"ABQ1", "US", "", ""});
    add({"ACY1", "US", "", ""});
    add({"AFW1", "US", "", ""});
    add({"AGS1", "US", "", ""});
    add({"AGS2", "US", "", ""});
    add({"AKC1", "US", "", ""});
    add({"ATL2", "US", "", ""});
    add({"AUS2", "US", "", ""});
    add({"AUS3", "US", "", ""});
    add({"BDL2", "US", "", ""});
    add({"BDL3", "US", "", ""});
    add({"BDL4", "US", "", ""});
    add({"BFI4", "US", "", ""});
    add({"BFL2", "US", "", ""});
    add({"BHM1", "US", "", ""});
    add({"BNA3", "US", "", ""});
    add({"BOI2", "US", "", ""});
    add({"BOS3", "US", "", ""});
    add({"BTR1", "US", "", ""});
    add({"BWI2", "US", "", ""});
    add({"CAE1", "US", "", ""});
    add({"CLE2", "US", "", ""});
    add({"CLE3", "US", "", ""});
    add({"CLT4", "US", "", ""});
    add({"CMH1", "US", "", "Columbus"});
    add({"CMH4", "US", "", ""});
    add({"CSG1", "US", "", ""});
    add({"DAB2", "US", "", ""});
    add({"DAL2", "US", "", ""});
    add({"DAL3", "US", "", ""});
    add({"DCA1", "US", "", ""});
    add({"DEN3", "US", "", ""});
    add({"DEN4", "US", "", ""});
    add({"DET3", "US", "", ""});
    add({"DET6", "US", "", ""});
    add({"DFW7", "US", "", ""});
    add({"DSM5", "US", "", ""});
    add({"DTW1", "US", "", ""});
    add({"ELP1", "US", "", ""});
    add({"EWR4", "US", "", ""});
    add({"EWR9", "US", "", ""});
    add({"FAT1", "US", "", ""});
    add({"FSD1", "US", "", ""});
    add({"FTW3", "US", "", ""});
    add({"FTW5", "US", "", ""});
    add({"FTW6", "US", "", ""});
    add({"FTW9", "US", "", ""});
    add({"FWA6", "US", "", ""});
    add({"GEG1", "US", "", ""});
    add({"GRR1", "US", "", ""});
    add({"GYR1", "US", "", ""});
    add({"HOU2", "US", "", ""});
    add({"HOU6", "US", "", ""});
    add({"IGQ1", "US", "", ""});
    add({"IND1", "US", "", ""});
    add({"IND4", "US", "", ""});
    add({"JAN1", "US", "", ""});
    add({"JAX2", "US", "", ""});
    add({"JAX7", "US", "", ""});
    add({"JFK8", "US", "", ""});
    add({"LAS7", "US", "", ""});
    add({"LGA9", "US", "", ""});
    add({"LGB3", "US", "", ""});
    add({"LGB7", "US", "", ""});
    add({"LIT1", "US", "", ""});
    add({"LUK2", "US", "", ""});
    add({"MCO1", "US", "", ""});
    add({"MDW4", "US", "", ""});
    add({"MIA1", "US", "", ""});
    add({"MDW7", "US", "", ""});
    add({"MEM4", "US", "", ""});
    add({"MGE1", "US", "", ""});
    add({"MKC6", "US", "", ""});
    add({"MKE1", "US", "", ""});
    add({"MKE2", "US", "", ""});
    add({"MLI1", "US", "", ""});
    add({"MQY1", "US", "", ""});
    add({"MSP1", "US", "", ""});
    add({"MTN1", "US", "", ""});
    add({"OAK4", "US", "", ""});
    add({"OKC1", "US", "", ""});
    add({"OMA2", "US", "", ""});
    add({"ONT2", "US", "", ""});
    add({"ONT6", "US", "", ""});
    add({"ORD5", "US", "", ""});
    add({"ORF3", "US", "", ""});
    add({"ORF4", "US", "", ""});
    add({"ORH3", "US", "", ""});
    add({"OXR1", "US", "", ""});
    add({"PAE2", "US", "", ""});
    add({"PCW1", "US", "", ""});
    add({"PDX8", "US", "", ""});
    add({"PDX9", "US", "", ""});
    add({"PHL7", "US", "", ""});
    add({"PHX3", "US", "", ""});
    add({"PSP1", "US", "", ""});
    add({"PVD2", "US", "", ""});
    add({"RDG1", "US", "", ""});
    add({"RDU1", "US", "", ""});
    add({"RIC2", "US", "", ""});
    add({"RIC4", "US", "", ""});
    add({"ROC1", "US", "", ""});
    add({"SAN3", "US", "", ""});
    add({"SAT2", "US", "", ""});
    add({"SAT3", "US", "", ""});
    add({"SAV4", "US", "", ""});
    add({"SBD6", "US", "", ""});
    add({"SBN1", "US", "", ""});
    add({"SCK1", "US", "", ""});
    add({"SCK6", "US", "", ""});
    add({"SDF1", "US", "", ""});
    add({"SDF4", "US", "", ""});
    add({"SDF8", "US", "", ""});
    add({"SHV1", "US", "", ""});
    add({"SLC1", "US", "", ""});
    add({"SMF1", "US", "", ""});
    add({"STL8", "US", "", ""});
    add({"SYR1", "US", "", ""});
    add({"TLH2", "US", "", ""});
    add({"TPA1", "US", "", ""});
    add({"TPA4", "US", "", ""});
    add({"TUL2", "US", "", ""});
    add({"TUS2", "US", "", ""});
    add({"TYS1", "US", "", ""});
    add({"VGT1", "US", "", ""});
        
    add({"ERF1", "DE", "", ""});
    add({"BRQ2", "CZ", "", ""});
    add({"BRE2", "FR", "", ""});
    add({"BCN4", "ES", "17469", "Girona"});
    add({"PSR2", "IT", "66050", "San Salvo"});
    add({"SCN2", "DE", "67661", "Kaiserslautern"});
    add({"XFRS", "FR", "", ""});
    add({"XFRO", "FR", "", ""});
    add({"VLC1", "ES", "12200", "Onda"});
    add({"XPLE", "DE", "", ""});
    add({"XPO1", "DE", "", ""});
    add({"LEJ5", "DE", "", ""});
    add({"NUE1", "DE", "95185", "Gattendorf"});
    add({"XPLD", "FR", "", ""});
    add({"SES1", "ES", "", ""});
    add({"RMU1", "ES", "30156", "Murcia"});
    add({"BGY1", "IT", "24050", "Cividate al Piano"});
    add({"MXP6", "IT", "28100", "Agognate"});
    add({"POZ2", "PL", "", "Chociule"});
    add({"MAN1", "UK", "", "Manchester"});
    add({"BER3", "DE", "", "Brieselang"});
    add({"BTS2", "SK", "", "Bratislava"});
    add({"BRE4", "DE", "", "Achim"});
    add({"CGN1", "DE", "", "Koblenz"});
    add({"DTM1", "DE", "", "Werne"});
    add({"DTM2", "DE", "", "Dortmund"});
    add({"DTM3", "DE", "", "Dortmund"});
    add({"DUS2", "DE", "", "Rheinberg"});
    add({"DUS4", "DE", "", "Moenchengladbach"});
    add({"EDEA", "DE", "", "Dortmund"});
    add({"EDE4", "DE", "", "Werne"});
    add({"EDE5", "DE", "", "Werne"});
    add({"FRA1", "DE", "", "Bad Hersfeld"});
    add({"FRA3", "DE", "", "Bad Hersfeld"});
    add({"FRA7", "DE", "", "Frankenthal Pfalz"});
    add({"HAM2", "DE", "", "Winsen an der Luhe"});
    add({"KTW1", "PL", "", "Sosnowiec"});
    add({"KTW3", "PL", "", "Bojkowska"});
    add({"LCJ2", "PL", "", "Pawlikowice"});
    add({"LCJ3", "PL", "", "Łódź"});
    add({"LCJ4", "PL", "", "Łódź"});
    add({"LEJ1", "DE", "", "Leipzig"});
    add({"LEJ3", "DE", "", "Suelzetal"});
    add({"MUC3", "DE", "", "Graben"});
    add({"PAD1", "DE", "", "Oelde"});
    add({"POZ1", "PL", "", "Poznan"});
    add({"PRG1", "CZ", "", "Dobroviz"});
    add({"PRG2", "CZ", "", "Dobroviz"});
    add({"STR1", "DE", "", "Pforzheim"});
    add({"SZZ1", "PL", "", "Kołbaskowo"});
    add({"WRO1", "PL", "", "Bielany Wroclawskie"});
    add({"WRO2", "PL", "", "Bielany Wroclawskie"});
    add({"WRO5", "PL", "", "Okmiany"});
    add({"XDU1", "DE", "", "Malsfeld"});
    add({"XDET", "DE", "", "Malsfeld"});
    add({"XDU2", "DE", "", "Oberhausen"});
    add({"XDEZ", "DE", "", "Oberhausen"});
    add({"XFR1", "DE", "", "Hammersbach"});
    add({"XDEY", "DE", "", "Hammersbach"});
    add({"XFR2", "DE", "", "Rennerod"});
    add({"XDEH", "DE", "", "Rennerod"});
    add({"XFR3", "DE", "", "Michelstadt"});
    add({"XDEW", "DE", "", "Michelstadt"});
    add({"XHA1", "DE", "", "Neu Wulmsdorf"});
    add({"XSC1", "DE", "", "Kaiserslautern"});
    add({"XDEQ", "DE", "", "Kaiserslautern"});
    add({"XWR1", "PL", "", "Krajków"});
    add({"XPLA", "PL", "", "Krajków"});
    add({"ORY1", "FR", "", "Saran"});
    add({"ORY4", "FR", "", "Brétigny"});
    add({"MRS1", "FR", "", "Montelimar"});
    add({"LYS1", "FR", "", "Sevrey"});
    add({"LIL1", "FR", "", "Lauwin"});
    add({"BVA1", "FR", "", "Boves"});
    add({"CDG7", "FR", "", "Senlis"});
    add({"XVA1", "FR", "", "Bussy-Lettree"});
    add({"XFRZ", "FR", "", "Bussy-Lettree"});
    add({"ETZ2", "FR", "", "Augny"});
    add({"VESK", "FR", "", "Savigny Le Temple"});
    add({"XFRJ", "FR", "", "Savigny Le Temple"});
    add({"XOR6", "FR", "", "Lisses"});
    add({"XFRK", "FR", "", "Lisses"});
    add({"XOR2", "FR", "", "Satolas-et-Bonce"});
    add({"XFRE", "FR", "", "Satolas-et-Bonce"});
    add({"XOS1", "FR", "", "Brebieres"});
    add({"XFRL", "FR", "", "Brebieres"});
    add({"TRN1", "IT", "", "Torrazza Piemonte"});
    add({"BLQ1", "IT", "", "San Bellino"});
    add({"FCO1", "IT", "", "Passo Corese"});
    add({"FCO2", "IT", "", "Colleferro"});
    add({"MXP5", "IT", "", "Castel San Giovanni"});
    add({"MXP3", "IT", "", "Vercelli"});
    add({"XITC", "IT", "", "Carpiano"});
    add({"XMP2", "IT", "", "Carpiano"});
    add({"XITD", "IT", "", "Rovigo"});
    add({"XMP1", "IT", "", "Rovigo"});
    add({"XITG", "IT", "", "Carpiano"});
    add({"XLI1", "IT", "", "Carpiano"});
    add({"XITI", "IT", "", "Marzano"});
    add({"XLI3", "IT", "", "Marzano"});
    add({"XITF", "IT", "", "Piacenza"});
    add({"VEII ", "IT", "", "Piacenza"});
    add({"MAD4", "ES", "", "Madrid"});
    add({"MAD6", "ES", "", "Illescas"});
    add({"MAD7", "ES", "", "Illescas"});
    add({"MAD9", "ES", "", "Alcalá de Henares"});
    add({"PESA", "ES", "", "Toledo"});
    add({"BCN1", "ES", "", "Barcelona"});
    add({"BCN2", "ES", "", "Martorelles"});
    add({"BCN3", "ES", "", "Castellbisbal"});
    add({"XMA1", "ES", "", "XESA"});
    add({"XMA2", "ES", "19208", "Alovera"});
    add({"XMA3", "ES", "", "XESF"});
    add({"XRE1", "ES", "", "XESC"});
    add({"SVQ1", "ES", "", " Sevilla"});
    add({"XAR1", "SE", "", "Eskilstuna (SE)"});
    add({"LTN1", "UK", "", "Marston Gate"});
    add({"LTN2", "UK", "", "Hemel Hempstead"});
    add({"LTN4", "UK", "", "Dunstable"});
    add({"LTN7", "UK", "", "Bedford"});
    add({"LTN9", "UK", "", "Dunstable"});
    add({"BHX1", "UK", "", "Rugeley"});
    add({"BHX2", "UK", "", "Coalville"});
    add({"BHX3", "UK", "", "Daventry"});
    add({"BHX4", "UK", "", "Coventry"});
    add({"BHX5", "UK", "", "Rugby"});
    add({"BHX7", "UK", "", "Hinckley"});
    add({"BRS1", "UK", "", "Bristol"});
    add({"CWL1", "UK", "", "Swansea"});
    add({"EDI4", "UK", "", "Dunfermline"});
    add({"EMA1", "UK", "", "Derby"});
    add({"EMA2", "UK", "", "Mansfield"});
    add({"EMA3", "UK", "", "Nottingham"});
    add({"EUK5", "UK", "", "Peterborough"});
    add({"GLA1", "UK", "", "Gourock"});
    add({"LBA1", "UK", "", "Doncaster"});
    add({"LBA2", "UK", "", "Doncaster"});
    add({"LBA3", "UK", "", "Doncaster"});
    add({"LBA4", "UK", "", "Doncaster"});
    add({"MAN1", "UK", "", "Manchester"});
    add({"MAN2", "UK", "", "Warrington"});
    add({"MAN3", "UK", "", "Bolton"});
    add({"MAN4", "UK", "", "Barlborough"});
    add({"MME1", "UK", "", "Darlington"});
    add({"MME2", "UK", "", "Bowburn"});
    add({"LCY2", "UK", "", "Tilbury"});
    add({"LCY3", "UK", "", "Dartford"});
    add({"XLT1", "UK", "", "Peterborough"});
    add({"EUKA", "UK", "", "Peterborough"});
    add({"XLT2", "UK", "", "Peterborough"});
    add({"EUKB", "UK", "", "Peterborough"});
    add({"XPL1", "UK", "", "Widnes"});
    add({"EUKD", "UK", "", "Widnes"});
    add({"XUKA", "UK", "", " Runcorn"});
    add({"XUKD", "UK", "", "Daventry"});
    add({"XBH1", "UK", "", "Daventry"});
    add({"XUKN", "UK", "", "Rugby"});
    add({"XBH2", "UK", "", "Rugby"});

    if (added) {
        _save();
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
