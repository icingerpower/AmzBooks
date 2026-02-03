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

AbstractBooksTable::AbstractBooksTable(
        const BooksConnections *bookConnections
        , const QDir &workingDir
        , QObject *parent)
    : QAbstractTableModel(parent)
{
    m_settingsFilePath = workingDir.absoluteFilePath("booksTables.ini");
    m_bookConnections = bookConnections;
    _loadFromSettings();
}

QString AbstractBooksTable::getRowId(const QModelIndex &index) const
{
    return m_listOfVariantList[index.row()].last().toString();
}

void AbstractBooksTable::_saveInSettings()
{
    QSettings settings(m_settingsFilePath, QSettings::IniFormat);
    // TODO only save m_fixedData
}

void AbstractBooksTable::_loadFromSettings()
{
    QSettings settings(m_settingsFilePath, QSettings::IniFormat);
    // TODO only load m_fixedData
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
    m_listOfVariantList <<
     QVariantList{
        date,
        amountFullOrig,
        currencyAmount,
        label,
        account1,
        account2,
        vatOrig,
        vatCountry,
        vatCurrency,
        rowId
    };
    if (m_fixedData.contains(rowId))
    {
        // TODO update the last row added in m_listOfVariantList
    }
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
    // TODO fill m_fixedData[rowId] = …
    // You need to create static const QString for each key of the QHash
    // You need to create static const QString for each key of the QHash
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
