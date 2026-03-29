#ifndef BOOKACCOUNTSGROUPEDSALESTABLE_H
#define BOOKACCOUNTSGROUPEDSALESTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QHash>

// Maps sale channels (e.g. "Amazon", "Temu") to a grouped receivable account
// used when multiple orders from that channel are aggregated into one journal entry.
//
// Each channel appears exactly once. The hidden Id column stores a stable identifier
// (from AbstractImporter::getId()) so rows survive order changes or app language changes.
// populateChannels() deduplicates by channel: if a channel is already present it is
// not added again even if a different importer Id maps to the same channel.

class BookAccountsGroupedSalesTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Stable (id, channel) pair used to populate channels from importers.
    // id   = AbstractImporter::getId()   (not translated, stable key)
    // channel = ActivitySource::channel  (display value, e.g. "Amazon")
    struct ImporterChannelInfo {
        QString id;
        QString channel;
    };

    explicit BookAccountsGroupedSalesTable(const QDir &workingDir, QObject *parent = nullptr);

    // Adds rows for channels not yet present; deduplicates by channel.
    // Call this with the full list of (id, channel) pairs from all registered importers.
    void populateChannels(const QList<ImporterChannelInfo> &importerChannels);

    // Returns the grouped client account for the given channel, or "" if not found/empty.
    QString getGroupedClientAccount(const QString &channel) const;

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    // Internal storage: m_rows[i] = {id, channel, groupedClientAccount}
    // The id field is NOT exposed as a model column; it is persisted in the CSV as a stable key.
    // Visible col 0 → channel (read-only), visible col 1 → groupedClientAccount (editable).
    QList<QStringList> m_rows;
    QHash<QString, int> m_channelToRowIndex; ///< channel → index in m_rows

    QString m_filePath;

    static const QStringList HEADER;
    static const QStringList CSV_HEADER_IDS;
    static const int IDX_ID      = 0;
    static const int IDX_CHANNEL = 1;
    static const int IDX_ACCOUNT = 2;

    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
};

#endif // BOOKACCOUNTSGROUPEDSALESTABLE_H
