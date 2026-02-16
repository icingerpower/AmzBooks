#ifndef INVENTORYINVOICESTREE_H
#define INVENTORYINVOICESTREE_H

#include <QAbstractItemModel>
#include <QDir>
#include <QVariant>

class InventoryInvoicesTreeItem;

class InventoryInvoicesTree : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit InventoryInvoicesTree(const QDir &workingDir, QObject *parent = nullptr);
    ~InventoryInvoicesTree() override;

    void addFile(const QString &filePath);
    void removeFile(const QModelIndex &index);
    QStringList getCsvInvoices(int year) const;
    void load();

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QDir m_workingDir;
    InventoryInvoicesTreeItem *m_rootItem;
    
    void _saveFile(const QString &sourcePath, int year);
    void _removeFile(const QString &fileName, int year);
    InventoryInvoicesTreeItem* _findYearItem(int year);
    InventoryInvoicesTreeItem* _getOrCreateYearItem(int year);
    
    QDir _getInventoryDir() const;
    QDir _getYearDir(int year) const;
};

#endif // INVENTORYINVOICESTREE_H
