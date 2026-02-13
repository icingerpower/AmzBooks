#include "PurchaseFileSettingsTreeItem.h"

PurchaseFileSettingsTreeItem::PurchaseFileSettingsTreeItem(const QString &name, PurchaseFileSettingsTreeItem *parent)
    : m_parentItem(parent), m_name(name), m_isFixed(false)
{
}

PurchaseFileSettingsTreeItem::~PurchaseFileSettingsTreeItem()
{
    qDeleteAll(m_childItems);
}

void PurchaseFileSettingsTreeItem::appendChild(PurchaseFileSettingsTreeItem *child)
{
    m_childItems.append(child);
}

void PurchaseFileSettingsTreeItem::insertChild(int row, PurchaseFileSettingsTreeItem *child)
{
    if (row >= 0 && row <= m_childItems.size())
        m_childItems.insert(row, child);
}

void PurchaseFileSettingsTreeItem::removeChild(int row)
{
    if (row >= 0 && row < m_childItems.size())
        delete m_childItems.takeAt(row);
}

PurchaseFileSettingsTreeItem *PurchaseFileSettingsTreeItem::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int PurchaseFileSettingsTreeItem::childCount() const
{
    return m_childItems.count();
}

int PurchaseFileSettingsTreeItem::columnCount() const
{
    return 1;
}

QVariant PurchaseFileSettingsTreeItem::data(int column) const
{
    if (column == 0)
        return m_name;
    return QVariant();
}

int PurchaseFileSettingsTreeItem::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<PurchaseFileSettingsTreeItem*>(this));
    return 0;
}

PurchaseFileSettingsTreeItem *PurchaseFileSettingsTreeItem::parentItem()
{
    return m_parentItem;
}

void PurchaseFileSettingsTreeItem::setName(const QString &name)
{
    m_name = name;
}

QString PurchaseFileSettingsTreeItem::name() const
{
    return m_name;
}

void PurchaseFileSettingsTreeItem::setId(const QString &id)
{
    m_id = id;
}

QString PurchaseFileSettingsTreeItem::id() const
{
    return m_id;
}

void PurchaseFileSettingsTreeItem::setIsFixed(bool isFixed)
{
    m_isFixed = isFixed;
}

bool PurchaseFileSettingsTreeItem::isFixed() const
{
    return m_isFixed;
}

PurchaseFileSettingsTreeItem *PurchaseFileSettingsTreeItem::findChildById(const QString &id) const
{
    for (PurchaseFileSettingsTreeItem *child : m_childItems) {
        if (child->id() == id) {
            return child;
        }
    }
    return nullptr;
}
