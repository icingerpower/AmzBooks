#include "BookAccountAmzBalanceTable.h"
#include "ExceptionAccountMissing.h"
#include "CountriesEu.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

const QStringList BookAccountAmzBalanceTable::HEADER_IDS = {
    "Amazon", "Balance", "Account"
};

BookAccountAmzBalanceTable::BookAccountAmzBalanceTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("amazon_balance_accounts.csv");
    _load();
    _fillIfEmpty();
}

QCoro::Task<BookAccountAmzBalanceTable::Accounts> BookAccountAmzBalanceTable::getAccount(
// ... (rest of getAccount unchanged generally, but ensuring reference context if needed) ...
// Actually easier to just replace the top block containing the definition and constructor.
    const QString &amazonSite,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing) const
{
    while (true) {
        int rowIdx = _findRow(amazonSite);
        if (rowIdx != -1) {
            BookAccountAmzBalanceTable::Accounts accs;
            accs.balanceAccount = m_listOfStringList[rowIdx][1];
            accs.account = m_listOfStringList[rowIdx][2];
            
             if (!accs.balanceAccount.isEmpty() && !accs.account.isEmpty()) {
                co_return accs;
             }
             // Even if empty, if the row exists, we return it so user can edit it UI side.
             co_return accs; 
        }

        if (!callbackAddIfMissing) {
            break;
        }

        QString errorTitle = tr("Missing Amazon Site");
        QString errorText = tr("The Amazon site %1 is missing from the Balance table. Would you like to add it?").arg(amazonSite);

        bool retry = co_await callbackAddIfMissing(errorTitle, errorText);
        if (!retry) {
            break;
        }
    }

    throw ExceptionAccountMissing(amazonSite);
}

void BookAccountAmzBalanceTable::addAmazon(const QString &amazonSite)
{
    if (_findRow(amazonSite) != -1) {
        return;
    }

    QStringList row;
    row << amazonSite << "" << "";

    beginInsertRows(QModelIndex(), m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList.append(row);
    endInsertRows();

    _save();
}

bool BookAccountAmzBalanceTable::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid()) return false;
    if (row < 0 || row + count > m_listOfStringList.size()) return false;

    // Check if any row to remove is a default one
    for (int i = 0; i < count; ++i) {
        QString site = m_listOfStringList[row + i][0];
        if (CountriesEu::DEFAULT_AMAZON_SITES.contains(site)) {
            return false; // Cannot remove default sites
        }
    }

    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_listOfStringList.removeAt(row);
    }
    endRemoveRows();

    _save();
    return true;
}

QVariant BookAccountAmzBalanceTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < HEADER_IDS.size()) {
            // Return translated names if desired
            return HEADER_IDS[section];
        }
    }
    return QVariant();
}

int BookAccountAmzBalanceTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_listOfStringList.size();
}

int BookAccountAmzBalanceTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return HEADER_IDS.size();
}

QVariant BookAccountAmzBalanceTable::data(const QModelIndex &index, int role) const
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

bool BookAccountAmzBalanceTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 1 && index.column() <= 2) { // Column 0 (Amazon) is read-only ideally? 
             // "we can't remove the original lines" - implies read-only or protected.
             // Let's allow editing accounts (1, 2) but maybe not 0?
             // User didn't stricter specify 0 is read-only, but usually keys are.
             // Implemented as editable for now, but logical constraint is handled in add.
             
             if (m_listOfStringList[index.row()][index.column()] != value.toString()) {
                m_listOfStringList[index.row()][index.column()] = value.toString();
                _save();
                emit dataChanged(index, index, {role});
                return true;
             }
        }
    }
    return false;
}

Qt::ItemFlags BookAccountAmzBalanceTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() > 0) { // Allow editing Balance(1) and Account(2), lock Amazon(0)?
        f |= Qt::ItemIsEditable;
    }
    return f;
}

void BookAccountAmzBalanceTable::_fillIfEmpty()
{
    // Auto-populate specific Amazons
    for (const auto &site : CountriesEu::DEFAULT_AMAZON_SITES) {
        addAmazon(site); // Checks existence inside
    }
}

void BookAccountAmzBalanceTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out << HEADER_IDS.join(";") << "\n";
    for (const auto &row : m_listOfStringList) {
         out << row.join(";") << "\n";
    }
}

void BookAccountAmzBalanceTable::_load()
{
    m_listOfStringList.clear();
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream in(&file);
    if (in.atEnd()) return;

    QString headerLine = in.readLine(); 
    // We assume standard order for now or strict format
    
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(";");
        // Ensure 3 columns
        while (parts.size() < 3) parts << "";
        m_listOfStringList.append(parts);
    }
}

int BookAccountAmzBalanceTable::_findRow(const QString &amazonSite) const
{
    for (int i = 0; i < m_listOfStringList.size(); ++i) {
        if (m_listOfStringList[i][0] == amazonSite) {
            return i;
        }
    }
    return -1;
}
