#ifndef PURCHASEFILESETTINGSTREEITEM_H
#define PURCHASEFILESETTINGSTREEITEM_H

#include <QString>
#include <QList>
#include <QVariant>

class PurchaseFileSettingsTreeItem
{
public:
    explicit PurchaseFileSettingsTreeItem(const QString &name, PurchaseFileSettingsTreeItem *parent = nullptr);
    ~PurchaseFileSettingsTreeItem();

    void appendChild(PurchaseFileSettingsTreeItem *child);
    void insertChild(int row, PurchaseFileSettingsTreeItem *child);
    void removeChild(int row);

    PurchaseFileSettingsTreeItem *child(int row);
    int childCount() const;
    int columnCount() const;
    QVariant data(int column) const;
    int row() const;
    PurchaseFileSettingsTreeItem *parentItem();

    void setName(const QString &name);
    QString name() const;

    void setId(const QString &id);
    QString id() const;

    void setIsFixed(bool isFixed);
    bool isFixed() const;
    
    // Find child by ID
    PurchaseFileSettingsTreeItem *findChildById(const QString &id) const;

private:
    QList<PurchaseFileSettingsTreeItem*> m_childItems;
    PurchaseFileSettingsTreeItem *m_parentItem;
    QString m_name;
    QString m_id; // Hidden ID
    bool m_isFixed;
};

#endif // PURCHASEFILESETTINGSTREEITEM_H
