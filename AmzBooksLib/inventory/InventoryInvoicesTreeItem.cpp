#include "InventoryInvoicesTreeItem.h"
#include <algorithm>

InventoryInvoicesTreeItem::InventoryInvoicesTreeItem(Type type, const QVariant &data, InventoryInvoicesTreeItem *parent)
    : m_itemData(data)
    , m_parentItem(parent)
    , m_type(type)
{}

InventoryInvoicesTreeItem::~InventoryInvoicesTreeItem()
{
    qDeleteAll(m_childItems);
}

void InventoryInvoicesTreeItem::appendChild(InventoryInvoicesTreeItem *child)
{
    m_childItems.append(child);
}

void InventoryInvoicesTreeItem::removeChild(InventoryInvoicesTreeItem *child)
{
    m_childItems.removeAll(child);
    delete child;
}

InventoryInvoicesTreeItem *InventoryInvoicesTreeItem::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int InventoryInvoicesTreeItem::childCount() const
{
    return m_childItems.count();
}

int InventoryInvoicesTreeItem::columnCount() const
{
    return 1;
}

QVariant InventoryInvoicesTreeItem::data(int column) const
{
    if (column != 0)
        return QVariant();
    return m_itemData;
}

int InventoryInvoicesTreeItem::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<InventoryInvoicesTreeItem*>(this));
    return 0;
}

InventoryInvoicesTreeItem *InventoryInvoicesTreeItem::parentItem()
{
    return m_parentItem;
}

InventoryInvoicesTreeItem::Type InventoryInvoicesTreeItem::getType() const
{
    return m_type;
}

void InventoryInvoicesTreeItem::sortChildren()
{
    // Sort descending
    std::sort(m_childItems.begin(), m_childItems.end(), [](InventoryInvoicesTreeItem *a, InventoryInvoicesTreeItem *b) {
        return a->data(0).toString() > b->data(0).toString();
    });
}
