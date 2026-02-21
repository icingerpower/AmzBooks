#ifndef INVENTORYMOVETREEITEM_H
#define INVENTORYMOVETREEITEM_H

#include <QList>
#include <QVariant>
#include <QString>

class InventoryMoveTreeItem
{
public:
    enum Type {
        Root,
        Parent,
        Child
    };

    explicit InventoryMoveTreeItem(); // Root
    explicit InventoryMoveTreeItem(const QString &from, const QString &to,
                                   const QString &currency,
                                   InventoryMoveTreeItem *parentItem); // Parent
    explicit InventoryMoveTreeItem(const QString &from, const QString &to,
                                   const QString &sku, const QString &productName,
                                   int units, double unitPrice,
                                   const QString &currency,
                                   double origAmount, const QString &origCurrency,
                                   const QString &purchaseFile,
                                   InventoryMoveTreeItem *parentItem); // Child
    ~InventoryMoveTreeItem();

    void appendChild(InventoryMoveTreeItem *child);
    void detachChildren(); // Clear list without deleting (used for sort)

    InventoryMoveTreeItem *child(int row);
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    int row() const;
    InventoryMoveTreeItem *parentItem();

    Type getType() const;

    // Compute aggregated values (units total, weighted avg unit price, total price) from children.
    // Call after all children have been appended.
    void aggregate();

private:
    QList<InventoryMoveTreeItem*> m_childItems;
    InventoryMoveTreeItem *m_parentItem;
    Type m_type;

    QString m_from;
    QString m_to;
    QString m_sku;              // actual SKU for Child; unused for Parent/Root
    QString m_productName;      // product name for Child; unused for Parent/Root
    int m_units = 0;            // actual units for Child; aggregated total for Parent
    double m_unitPrice = 0.0;   // unit price for Child; weighted average for Parent
    double m_totalPrice = 0.0;  // units * unitPrice for Child; sum for Parent
    QString m_currency;         // company currency (e.g. "EUR") for both Parent and Child
    double m_origAmount = 0.0;  // original total in invoice currency (0 when no conversion)
    QString m_origCurrency;     // original invoice currency (empty when no conversion)
    QString m_purchaseFile;     // source filename for Child; empty for Parent
};

#endif // INVENTORYMOVETREEITEM_H
