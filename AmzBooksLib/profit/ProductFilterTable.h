#ifndef PRODUCTFILTERTABLE_H
#define PRODUCTFILTERTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QStringList>

class ProductFilterTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ProductFilterTable(const QDir &workingDir, QObject *parent = nullptr);

    // Filter retrieval
    // return the value of the column filters (split on ;, then trimming all values)
    QStringList getFilters(int row) const;

    // Row management
    void addFilter(const QString &name, const QString &filters);
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    // Header
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    static const QStringList HEADER_IDS;
    
    QList<QStringList> m_data; // [0]=Name, [1]=Filters
    QString m_filePath;

    void _save();
    void _load();
};

#endif // PRODUCTFILTERTABLE_H
