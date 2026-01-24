#ifndef JOURNALTABLE_H
#define JOURNALTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

class ActivitySource;

struct JournalItem {
    QString name;
    QString code;
    QString id; // Hidden, used for stability
};

class JournalTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit JournalTable(const QDir &workingDir, QObject *parent = nullptr);

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    // Specific methods
    QString getJournal(const ActivitySource *activitySource) const;
    JournalItem getJournalPurchaseInvoice() const;

private:
    QDir m_workingDir;
    QString m_filePath;
    QList<JournalItem> m_data;

    void _load();
    void _save();
    void _init(); // Ensure default rows exist
};

#endif // JOURNALTABLE_H
