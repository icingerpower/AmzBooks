#include <QFile>
#include <QTextStream>
#include <QDebug>

#include "BookAccountSelfVatTable.h"
#include "CountriesEu.h"

const QStringList BookAccountSelfVatTable::HEADER{
    QObject::tr("Country"),
    QObject::tr("Deductible VAT"),
    QObject::tr("Due VAT")
};

BookAccountSelfVatTable::BookAccountSelfVatTable(const QDir &workingDir,
                                                 const QString &companyCountryCode,
                                                 QObject *parent)
    : QAbstractTableModel(parent)
    , m_companyCountryCode(companyCountryCode)
{
    m_filePath = workingDir.absoluteFilePath("selfVatPurchaseAccount.csv");
    _load();
    _fillIfEmpty();
}

QString BookAccountSelfVatTable::_resolveCategory(const QString &countryFrom,
                                                   const QString &countryTo) const
{
    // Only applicable when the purchase arrives at the company's country
    if (countryTo != m_companyCountryCode)
        return {};

    // Domestic: no self-VAT
    if (countryFrom == m_companyCountryCode)
        return {};

    if (CountriesEu::isEuMember(countryFrom, QDate::currentDate()) || countryFrom == "EU")
        return QStringLiteral("EU");

    return QStringLiteral("NON_EU");
}

QString BookAccountSelfVatTable::getAccountVatDeductible(const QString &countryFrom,
                                                          const QString &countryTo) const
{
    const QString category = _resolveCategory(countryFrom, countryTo);
    if (category.isEmpty())
        return {};

    int row = (category == QStringLiteral("EU")) ? ROW_EU : ROW_NON_EU;
    return m_rows[row][COL_DEDUCTIBLE];
}

QString BookAccountSelfVatTable::getAccountVatDue(const QString &countryFrom,
                                                   const QString &countryTo) const
{
    const QString category = _resolveCategory(countryFrom, countryTo);
    if (category.isEmpty())
        return {};

    int row = (category == QStringLiteral("EU")) ? ROW_EU : ROW_NON_EU;
    return m_rows[row][COL_DUE];
}

QVariant BookAccountSelfVatTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal
            && section >= 0 && section < HEADER.size())
        return HEADER.at(section);
    return QVariant();
}

int BookAccountSelfVatTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int BookAccountSelfVatTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return HEADER.size();
}

QVariant BookAccountSelfVatTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if ((role == Qt::DisplayRole || role == Qt::EditRole)
            && index.row() < m_rows.size()
            && index.column() < m_rows[index.row()].size()) {
        return m_rows[index.row()][index.column()];
    }
    return QVariant();
}

bool BookAccountSelfVatTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole)
        return false;

    // Only columns 1 and 2 are editable
    if (index.column() == COL_COUNTRY)
        return false;

    if (index.row() >= m_rows.size() || index.column() >= m_rows[index.row()].size())
        return false;

    if (m_rows[index.row()][index.column()] == value.toString())
        return false;

    m_rows[index.row()][index.column()] = value.toString();
    _save();
    emit dataChanged(index, index, {role});
    return true;
}

Qt::ItemFlags BookAccountSelfVatTable::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() != COL_COUNTRY)
        f |= Qt::ItemIsEditable;
    return f;
}

void BookAccountSelfVatTable::_fillIfEmpty()
{
    if (!m_rows.isEmpty())
        return;

    m_rows.append({tr("EU"),     QStringLiteral("445662"), QStringLiteral("445200")});
    m_rows.append({tr("non-EU"), QStringLiteral("445663"), QStringLiteral("445300")});
    _save();
}

static const QStringList CSV_HEADER_IDS = {
    QStringLiteral("Country"),
    QStringLiteral("DeductibleVat"),
    QStringLiteral("DueVat")
};

void BookAccountSelfVatTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save BookAccountSelfVatTable:" << file.errorString();
        return;
    }
    QTextStream out(&file);
    out << CSV_HEADER_IDS.join(";") << "\n";
    for (const QStringList &row : std::as_const(m_rows)) {
        QStringList r = row;
        while (r.size() < CSV_HEADER_IDS.size())
            r << QString();
        out << r.join(";") << "\n";
    }
}

void BookAccountSelfVatTable::_load()
{
    m_rows.clear();
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    if (in.atEnd())
        return;

    const QStringList headers = in.readLine().split(";");
    QMap<QString, int> colMap;
    for (int i = 0; i < headers.size(); ++i)
        colMap[headers[i].trimmed()] = i;

    const int idxCountry    = colMap.value(QStringLiteral("Country"),      -1);
    const int idxDeductible = colMap.value(QStringLiteral("DeductibleVat"), -1);
    const int idxDue        = colMap.value(QStringLiteral("DueVat"),        -1);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty())
            continue;
        const QStringList parts = line.split(";");

        QStringList row(3);
        if (idxCountry    >= 0 && idxCountry    < parts.size()) row[COL_COUNTRY]    = parts[idxCountry];
        if (idxDeductible >= 0 && idxDeductible < parts.size()) row[COL_DEDUCTIBLE] = parts[idxDeductible];
        if (idxDue        >= 0 && idxDue        < parts.size()) row[COL_DUE]        = parts[idxDue];

        m_rows.append(row);
    }
}
