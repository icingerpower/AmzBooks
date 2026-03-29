#include "BookAccountsGroupedSalesTable.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

const QStringList BookAccountsGroupedSalesTable::HEADER{
    QObject::tr("Sale channel"),
    QObject::tr("Grouped Client Account")
};

const QStringList BookAccountsGroupedSalesTable::CSV_HEADER_IDS{
    QStringLiteral("Id"),
    QStringLiteral("Channel"),
    QStringLiteral("GroupedClientAccount")
};

BookAccountsGroupedSalesTable::BookAccountsGroupedSalesTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = workingDir.absoluteFilePath("groupedSaleAccounts.csv");
    _load();
    _fillIfEmpty();
}

void BookAccountsGroupedSalesTable::populateChannels(const QList<ImporterChannelInfo> &importerChannels)
{
    bool changed = false;
    QSet<QString> seenInBatch;
    for (const auto &info : importerChannels) {
        if (m_channelToRowIndex.contains(info.channel) || seenInBatch.contains(info.channel)) {
            continue;
        }
        seenInBatch.insert(info.channel);
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
        m_rows.append({info.id, info.channel, QStringLiteral("")});
        endInsertRows();
        changed = true;
    }
    if (changed) {
        _rebuildCache();
        _save();
    }
}

QString BookAccountsGroupedSalesTable::getGroupedClientAccount(const QString &channel) const
{
    auto it = m_channelToRowIndex.constFind(channel);
    if (it == m_channelToRowIndex.constEnd()) {
        return {};
    }
    const int row = it.value();
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    return m_rows[row][IDX_ACCOUNT];
}

QVariant BookAccountsGroupedSalesTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal
            && section >= 0 && section < HEADER.size()) {
        return HEADER.at(section);
    }
    return QVariant();
}

int BookAccountsGroupedSalesTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int BookAccountsGroupedSalesTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return HEADER.size(); // 2 visible columns: channel + account
}

QVariant BookAccountsGroupedSalesTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if ((role == Qt::DisplayRole || role == Qt::EditRole) && index.row() < m_rows.size()) {
        // Visible col 0 → IDX_CHANNEL (=1), visible col 1 → IDX_ACCOUNT (=2)
        const int internalIdx = index.column() + 1; // skip IDX_ID (=0)
        if (internalIdx < m_rows[index.row()].size()) {
            return m_rows[index.row()][internalIdx];
        }
    }
    return QVariant();
}

bool BookAccountsGroupedSalesTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    // Only the grouped client account column (visible col 1) is editable
    if (index.column() != 1) {
        return false;
    }
    if (index.row() >= m_rows.size()) {
        return false;
    }
    if (m_rows[index.row()][IDX_ACCOUNT] == value.toString()) {
        return false;
    }
    m_rows[index.row()][IDX_ACCOUNT] = value.toString();
    _rebuildCache();
    _save();
    emit dataChanged(index, index, {role});
    return true;
}

Qt::ItemFlags BookAccountsGroupedSalesTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    // Only the grouped client account column is editable; channel column is read-only
    if (index.column() == 1) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

void BookAccountsGroupedSalesTable::_fillIfEmpty()
{
    if (!m_rows.isEmpty()) {
        return;
    }
    // Pre-populate with known sale channels; use channel name as stable Id.
    // These cover the built-in importers. Additional channels are added via populateChannels().
    m_rows.append({QStringLiteral("Amazon"),     QStringLiteral("Amazon"),     QStringLiteral("")});
    m_rows.append({QStringLiteral("Temu"),        QStringLiteral("Temu"),        QStringLiteral("")});
    m_rows.append({QStringLiteral("CommerceHQ"), QStringLiteral("CommerceHQ"), QStringLiteral("")});
    m_rows.append({QStringLiteral("Service"),    QStringLiteral("Service"),    QStringLiteral("")});
    _rebuildCache();
    _save();
}

void BookAccountsGroupedSalesTable::_rebuildCache()
{
    m_channelToRowIndex.clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].size() > IDX_CHANNEL && !m_rows[i][IDX_CHANNEL].isEmpty()) {
            m_channelToRowIndex[m_rows[i][IDX_CHANNEL]] = i;
        }
    }
}

void BookAccountsGroupedSalesTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save BookAccountsGroupedSalesTable:" << file.errorString();
        return;
    }
    QTextStream out(&file);
    out << CSV_HEADER_IDS.join(";") << "\n";
    for (const QStringList &row : std::as_const(m_rows)) {
        QStringList r = row;
        while (r.size() < CSV_HEADER_IDS.size()) {
            r << QString();
        }
        out << r.join(";") << "\n";
    }
}

void BookAccountsGroupedSalesTable::_load()
{
    m_rows.clear();
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream in(&file);
    if (in.atEnd()) {
        return;
    }
    const QStringList headers = in.readLine().split(";");
    QMap<QString, int> colMap;
    for (int i = 0; i < headers.size(); ++i) {
        colMap[headers[i].trimmed()] = i;
    }
    const int idxId      = colMap.value(QStringLiteral("Id"),                   -1);
    const int idxChannel = colMap.value(QStringLiteral("Channel"),               -1);
    const int idxAccount = colMap.value(QStringLiteral("GroupedClientAccount"), -1);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(";");
        QStringList row(3);
        if (idxId      >= 0 && idxId      < parts.size()) {
            row[IDX_ID]      = parts[idxId];
        }
        if (idxChannel >= 0 && idxChannel < parts.size()) {
            row[IDX_CHANNEL] = parts[idxChannel];
        }
        if (idxAccount >= 0 && idxAccount < parts.size()) {
            row[IDX_ACCOUNT] = parts[idxAccount];
        }
        if (row[IDX_CHANNEL].isEmpty()) {
            continue;
        }
        m_rows.append(row);
    }
    _rebuildCache();
}
