#ifndef ENTRYSELFTABLE_H
#define ENTRYSELFTABLE_H

#include <QDir>
#include <QAbstractTableModel>

class EntrySelfTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    static const QStringList COL_NAMES;
    struct Row{
        QString label;
        QString account;
    };
    explicit EntrySelfTable(const QDir &workingDir, QObject *parent = nullptr);
    void addRow(const Row &row);
    void remove(const QModelIndex &index);
    QString getRowId(const QModelIndex &index) const;
    QString getId() const;
    QString getAccount(int row) const;

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;


    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    void _save();
    void _load();
    QString m_filePathCsv;
    QList<QStringList> m_listeOfStringList;
};

#endif // ENTRYSELFTABLE_H
