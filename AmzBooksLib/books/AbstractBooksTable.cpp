#include <QDate>
#include <QBrush>
#include <QColor>
#include <QSettings>

#include "BooksConnections.h"

#include "AbstractBooksTable.h"

const QVariantList AbstractBooksTable::COL_NAMES = {
    AbstractBooksTable::tr("Date"),
    AbstractBooksTable::tr("Amount"),
    AbstractBooksTable::tr("Currency"),
    AbstractBooksTable::tr("Label"),
    AbstractBooksTable::tr("Account 1"),
    AbstractBooksTable::tr("Account 2"),
    AbstractBooksTable::tr("Original VAT"),
    AbstractBooksTable::tr("VAT Country"),
    AbstractBooksTable::tr("VAT Currency")
};

const QString AbstractBooksTable::KEY_AMOUNT_FULL_ORIG = "amountFullOrig";
const QString AbstractBooksTable::KEY_AMOUNT_FULL_CONVERTED = "amountFullConverted";
const QString AbstractBooksTable::KEY_CURRENCY_AMOUNT = "currencyAmount";
const QString AbstractBooksTable::KEY_VAT_CONVERTED = "vatConverted";
const QString AbstractBooksTable::KEY_VAT_ORIG = "vatOrig";
const QString AbstractBooksTable::KEY_VAT_COUNTRY = "vatCountry";
const QString AbstractBooksTable::KEY_VAT_CURRENCY = "vatCurrency";

AbstractBooksTable::AbstractBooksTable(
        const BooksConnections *bookConnections
        , const QDir &workingDir
        , QObject *parent)
    : QAbstractTableModel(parent)
{
    m_settingsFilePath = workingDir.absoluteFilePath("booksTables.ini");
    m_bookConnections = bookConnections;
}

void AbstractBooksTable::init()
{
    _loadFromSettings();
}

QString AbstractBooksTable::getRowId(const QModelIndex &index) const
{
    return m_listOfVariantList[index.row()].last().toString();
}

void AbstractBooksTable::_saveInSettings()
{
    QSettings settings(m_settingsFilePath, QSettings::IniFormat);
    settings.beginGroup(getId());
    for (auto it = m_fixedData.constBegin(); it != m_fixedData.constEnd(); ++it)
    {
        settings.beginGroup(it.key()); // Group by RowID
        const auto &map = it.value();
        for (auto it2 = map.constBegin(); it2 != map.constEnd(); ++it2)
        {
            settings.setValue(it2.key(), it2.value());
        }
        settings.endGroup();
    }
    settings.endGroup();
}

void AbstractBooksTable::_loadFromSettings()
{
    m_fixedData.clear();
    QSettings settings(m_settingsFilePath, QSettings::IniFormat);
    settings.beginGroup(getId());
    QStringList childGroups = settings.childGroups();
    for (const QString &rowId : childGroups)
    {
        settings.beginGroup(rowId);
        QStringList keys = settings.childKeys();
        QHash<QString, QVariant> rowData;
        for (const QString &key : keys)
        {
            rowData[key] = settings.value(key);
        }
        m_fixedData.insert(rowId, rowData);
        settings.endGroup();
    }
    settings.endGroup();
}

void AbstractBooksTable::add(
        const QString &rowId,
        const QDate &date
        , double amountFullOrig
        , const QString &currencyAmount
        , const QString &label
        , const QString &account1
        , const QString &account2
        , double vatOrig
        , const QString &vatCountry
        , const QString &vatCurrency)
{
    int row = m_listOfVariantList.size();
    beginInsertRows(QModelIndex{}, row, row);
    
    // Check if we have overrides in m_fixedData
    double finalAmountFullOrig = amountFullOrig;
    QString finalCurrencyAmount = currencyAmount;
    double finalVatOrig = vatOrig;
    QString finalVatCountry = vatCountry;
    QString finalVatCurrency = vatCurrency;

    if (m_fixedData.contains(rowId))
    {
        const auto &overrides = m_fixedData[rowId];
        if (overrides.contains(KEY_AMOUNT_FULL_ORIG))
        {
            finalAmountFullOrig = overrides[KEY_AMOUNT_FULL_ORIG].toDouble();
        }
        if (overrides.contains(KEY_CURRENCY_AMOUNT))
        {
            finalCurrencyAmount = overrides[KEY_CURRENCY_AMOUNT].toString();
        }
        if (overrides.contains(KEY_VAT_ORIG))
        {
            finalVatOrig = overrides[KEY_VAT_ORIG].toDouble();
        }
        if (overrides.contains(KEY_VAT_COUNTRY))
        {
            finalVatCountry = overrides[KEY_VAT_COUNTRY].toString();
        }
        if (overrides.contains(KEY_VAT_CURRENCY))
        {
            finalVatCurrency = overrides[KEY_VAT_CURRENCY].toString();
        }
    }

    m_listOfVariantList <<
     QVariantList{
        date,
        finalAmountFullOrig,
        finalCurrencyAmount,
        label,
        account1,
        account2,
        finalVatOrig,
        finalVatCountry,
        finalVatCurrency,
        rowId
    };
    endInsertRows();
}

void AbstractBooksTable::updateData(
        const QString &rowId
        , double amountFullOrig
        , double amountFullConverted
        , const QString &currencyAmount
        , double vatConverted
        , double vatOrig
        , const QString &vatCountry
        , const QString &vatCurrency)
{
    m_fixedData[rowId][KEY_AMOUNT_FULL_ORIG] = amountFullOrig;
    m_fixedData[rowId][KEY_AMOUNT_FULL_CONVERTED] = amountFullConverted;
    m_fixedData[rowId][KEY_CURRENCY_AMOUNT] = currencyAmount;
    m_fixedData[rowId][KEY_VAT_CONVERTED] = vatConverted;
    m_fixedData[rowId][KEY_VAT_ORIG] = vatOrig;
    m_fixedData[rowId][KEY_VAT_COUNTRY] = vatCountry;
    m_fixedData[rowId][KEY_VAT_CURRENCY] = vatCurrency;

    // Update in model if exists
    for (int i = 0; i < m_listOfVariantList.size(); ++i)
    {
        if (m_listOfVariantList[i].last().toString() == rowId)
        {
             m_listOfVariantList[i][1] = amountFullOrig;
             m_listOfVariantList[i][2] = currencyAmount;
             m_listOfVariantList[i][6] = vatOrig;
             m_listOfVariantList[i][7] = vatCountry;
             m_listOfVariantList[i][8] = vatCurrency;
             emit dataChanged(index(i, 0), index(i, 8));
             break;
        }
    }

    _saveInSettings();
}

void AbstractBooksTable::clear()
{
    beginResetModel();
    m_listOfVariantList.clear();
    endResetModel();
}

bool AbstractBooksTable::remove(const QString &rowId)
{
    for (int i = 0; i < m_listOfVariantList.size(); ++i) {
        if (m_listOfVariantList[i].last().toString() == rowId) {
            beginRemoveRows(QModelIndex(), i, i);
            m_listOfVariantList.removeAt(i);
            endRemoveRows();
            return true;
        }
    }
    return false;
}

QVariant AbstractBooksTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (orientation == Qt::Horizontal)
        {
            return COL_NAMES[section];
        }
        else
        {
            return QString::number(section + 1);
        }
    }
    return QVariant{};
}

int AbstractBooksTable::rowCount(const QModelIndex &parent) const
{
    return m_listOfVariantList.size();
}

int AbstractBooksTable::columnCount(const QModelIndex &parent) const
{
    return COL_NAMES.size();
}

QVariant AbstractBooksTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        return m_listOfVariantList[index.row()][index.column()];
    }
    else if (role == Qt::BackgroundRole)
    {
        const auto &rowId = getRowId(index);
        if (m_bookConnections->contains(getId(), rowId))
        {
            static QBrush greenColor{QColor{163, 177, 138}};
            return greenColor;
        }
    }
    return QVariant();
}
