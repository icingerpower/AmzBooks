#include "BookAccountBankTable.h"
#include "banks/AbstractBankStatement.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

BookAccountBankTable::BookAccountBankTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{
    m_filePath = m_workingDir.absoluteFilePath("accountsBanks.csv");
    _init(); // Fill defaults first
    _load(); // Overwrite with saved data
    _save(); // Ensure complete CSV exists
}

int BookAccountBankTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int BookAccountBankTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 3; // Bank, Account, Fees Account
}

QVariant BookAccountBankTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size()) return QVariant();

    const auto &item = m_data.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0) return item.bankName;
        if (index.column() == 1) return item.account;
        if (index.column() == 2) return item.feesAccount;
    }
    return QVariant();
}

QVariant BookAccountBankTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == 0) return tr("Bank");
        if (section == 1) return tr("Account");
        if (section == 2) return tr("Fees Account");
    }
    return QVariant();
}

bool BookAccountBankTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        int row = index.row();
        if (row >= 0 && row < m_data.size()) {
            bool changed = false;
            if (index.column() == 1) {
                if (m_data[row].account != value.toString()) {
                    m_data[row].account = value.toString();
                    changed = true;
                }
            } else if (index.column() == 2) {
                if (m_data[row].feesAccount != value.toString()) {
                    m_data[row].feesAccount = value.toString();
                    changed = true;
                }
            }

            if (changed) {
                _save();
                emit dataChanged(index, index, {role});
                return true;
            }
        }
    }
    return false;
}

Qt::ItemFlags BookAccountBankTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    // Allow editing Account (1) and Fees Account (2)
    if (index.column() == 1 || index.column() == 2) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

void BookAccountBankTable::_init()
{
    m_data.clear();
    const auto &banks = AbstractBankStatement::ALL_BANKS();
    for (const auto *bank : banks) {
        BankAccountItem item;
        item.bankName = bank->getName();
        item.account = bank->defaultAccount();
        item.feesAccount = bank->defaultAccountFees();
        item.id = bank->getId();
        m_data.append(item);
    }
}

void BookAccountBankTable::_load()
{
    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        // Skip header
        in.readLine();

        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.trimmed().isEmpty()) continue;
            QStringList parts = line.split(";");
            // Expecting: BankName;Account;FeesAccount;Id
            if (parts.size() >= 4) {
                QString csvId = parts[3];

                // Find matching item by ID
                for (auto &item : m_data) {
                    if (item.id == csvId) {
                        item.account = parts[1];
                        item.feesAccount = parts[2];
                        // BankName (parts[0]) is ignored, we trust code initialization for name
                        break;
                    }
                }
            }
        }
    }
}

void BookAccountBankTable::_save()
{
    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "Bank;Account;Fees Account;Id\n";
        for (const auto &item : m_data) {
            out << item.bankName << ";" 
                << item.account << ";" 
                << item.feesAccount << ";" 
                << item.id << "\n";
        }
    }
}
