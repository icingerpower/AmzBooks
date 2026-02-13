#ifndef PURCHASEFILESETTINGSTREE_H
#define PURCHASEFILESETTINGSTREE_H

#include <QAbstractItemModel>
#include <QDir>
#include <QList>
#include <QString>
#include "PurchaseFileSettingsTreeItem.h"

class PurchaseFileSettingsTree : public QAbstractItemModel
{
    Q_OBJECT

public:
    static const QString COL_ORDER_ID;
    static const QString COL_TITLE;
    static const QString COL_SKU;
    static const QString COL_QUANTITY;
    static const QString COL_UNIT_WEIGHT;
    static const QString COL_UNIT_PRICE;
    static const QString COL_CURRENCY;
    static const QStringList FIXED_ROW_IDS;
    static const QStringList FIXED_ROW_NAMES();

    explicit PurchaseFileSettingsTree(const QDir &workingDir, QObject *parent = nullptr);
    ~PurchaseFileSettingsTree() override;

    int getColPos(const QStringList &colNames, const QString &id) const;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

public slots:
    void addCandidate(const QModelIndex &parentIndex, const QString &candidate);

private:
    PurchaseFileSettingsTreeItem *m_rootItem;
    QString m_filePath;

    void _setupFixedRows();
    void _load();
    void _save();
    PurchaseFileSettingsTreeItem *getItem(const QModelIndex &index) const;
};

#endif // PURCHASEFILESETTINGSTREE_H
