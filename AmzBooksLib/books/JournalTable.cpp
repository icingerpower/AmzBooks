#include "JournalTable.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QSet>

#include "orders/AbstractImporterApi.h"
#include "orders/AbstractImporterFile.h"
#include "banks/AbstractBankStatement.h"


JournalTable::JournalTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_workingDir(workingDir)
{
    m_filePath = m_workingDir.absoluteFilePath("journals.csv");
    _init();
    _load();
    _save(); // Ensure CSV is saved after initialization
}

int JournalTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int JournalTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 2; // Name, Code (Id is hidden)
}

QVariant JournalTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size()) return QVariant();
    
    const auto &item = m_data.at(index.row());
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0) return item.code; // Col 0 is Code
        if (index.column() == 1) return item.name; // Col 1 is Name
    }
    return QVariant();
}

QVariant JournalTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section == 0) return tr("Journal"); // "Journal" (Code)
        if (section == 1) return tr("Name");    // "Name" (Description)
    }
    return QVariant();
}

bool JournalTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        int row = index.row();
        if (row >= 0 && row < m_data.size()) {
            bool changed = false;
            // Allow editing ONLY Column 1 (Name)
            if (index.column() == 1) {
                if (m_data[row].name != value.toString()) {
                    m_data[row].name = value.toString();
                    changed = true;
                }
            } else if (index.column() == 0) {
                 // Column 0 (Code) is NOT editable per user request ("only second column")
                 // Although inserting new rows might require setting code?
                 // User: "We can edit only the second column to change journal names"
                 // If we create a new row, it starts empty. If we can't edit Col 0, how do we set the Code?
                 // Maybe "Journal" column is fixed/preset or managed elsewhere? 
                 // OR maybe user meant "Modify existing". 
                 // For now I strictly follow "Edit only second column".
                 // But for `insertRows`, I might need to allow setting it programmatically? 
                 // `setData` is used by View. Programmatic access can modify m_data directly or use a specific method.
                 // But I'll stick to view restriction.
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

Qt::ItemFlags JournalTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    if (index.column() == 1) {
        flags |= Qt::ItemIsEditable;
    }
    return flags;
}

bool JournalTable::insertRows(int row, int count, const QModelIndex &parent)
{
    beginInsertRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_data.insert(row, {QString(), QString(), QString()});
    }
    endInsertRows();
    _save();
    return true;
}

bool JournalTable::removeRows(int row, int count, const QModelIndex &parent)
{
    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_data.removeAt(row);
    }
    endRemoveRows();
    _save();
    return true;
}

QString JournalTable::getCustomerAccount(const ActivitySource *activitySource) const
{
    Q_UNUSED(activitySource);
    return "CAMAZON";
}

QString JournalTable::getJournal(const ActivitySource *activitySource) const
{
    Q_UNUSED(activitySource);
    return "AMZTODO";
}

JournalItem JournalTable::getJournalPurchaseInvoice() const
{
    // Return the one with id "purchase"
    for (const auto &item : m_data) {
        if (item.id == "purchase") {
            return item;
        }
    }
    return { tr("Purchase"), tr("AC"), "purchase" };
}

void JournalTable::_load()
{
    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        // Skip header
        in.readLine();
        
        // Read CSV and update existing entries based on ID
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.trimmed().isEmpty()) continue;
            QStringList parts = line.split(";");
            if (parts.size() >= 3) {
                QString csvName = parts[0];
                QString csvCode = parts[1];
                QString csvId = parts[2];
                
                // Find and replace existing entry with same ID
                for (auto &item : m_data) {
                    if (item.id == csvId) {
                        item.name = csvName;
                        item.code = csvCode;
                        break;
                    }
                }
            }
        }
    }
}

void JournalTable::_save()
{
    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "Journal;Code;Id\n";
        for (const auto &item : m_data) {
            out << item.name << ";" << item.code << ";" << item.id << "\n";
        }
    }
}

void JournalTable::_init()
{
    // Auto-populate default entries from importers (unique channels)
    QSet<QString> existingIds;
    QSet<QString> channels;
    
    // Collect unique channels from AbstractImporterApi
    for (const auto *importer : AbstractImporterApi::ALL_IMPORTERS()) {
        QString channel = importer->getActivitySource().channel;
        if (!channel.isEmpty()) {
            channels.insert(channel);
        }
    }
    
    // Collect unique channels from AbstractImporterFile
    for (const auto *importer : AbstractImporterFile::ALL_IMPORTERS()) {
        QString channel = importer->getActivitySource().channel;
        if (!channel.isEmpty()) {
            channels.insert(channel);
        }
    }
    
    // Add channel entries
    for (const QString &channel : channels) {
        m_data.append({channel, channel, channel});
        existingIds.insert(channel);
    }
    
    // Add entries from AbstractBankStatement
    for (const auto *bank : AbstractBankStatement::ALL_BANKS()) {
        QString id = bank->getId();
        if (!existingIds.contains(id)) {
            m_data.append({bank->getName(), bank->getId(), id});
            existingIds.insert(id);
        }
    }
    
    // Add "purchase" journal if not already present
    if (!existingIds.contains("purchase")) {
        m_data.append({ tr("Purchase"), tr("AC"), "purchase" });
    }
}
