#ifndef INVENTORYINVOICESTREEITEM_H
#define INVENTORYINVOICESTREEITEM_H

#include <QList>
#include <QVariant>
#include <QString>

class InventoryInvoicesTreeItem
{
public:
    enum Type {
        Root,
        Year,
        File
    };

    explicit InventoryInvoicesTreeItem(Type type, const QVariant &data, InventoryInvoicesTreeItem *parent = nullptr);
    ~InventoryInvoicesTreeItem();

    void appendChild(InventoryInvoicesTreeItem *child);
    void removeChild(InventoryInvoicesTreeItem *child);
    
    InventoryInvoicesTreeItem *child(int row);
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    int row() const;
    InventoryInvoicesTreeItem *parentItem();
    
    Type getType() const;
    void sortChildren();

private:
    QList<InventoryInvoicesTreeItem*> m_childItems;
    QVariant m_itemData;
    InventoryInvoicesTreeItem *m_parentItem;
    Type m_type;
};

#endif // INVENTORYINVOICESTREEITEM_H
