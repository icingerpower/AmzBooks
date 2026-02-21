#include "InventoryMoveTreeItem.h"

// Column indices mirroring InventoryMoveTree::Columns to avoid a circular dependency
static const int COL_FROM          = 0;
static const int COL_TO            = 1;
static const int COL_SKU           = 2;
static const int COL_PRODUCT_NAME  = 3;
static const int COL_UNITS         = 4;
static const int COL_UNIT_PRICE    = 5;
static const int COL_TOTAL_PRICE   = 6;
static const int COL_CURRENCY      = 7;
static const int COL_ORIG_AMOUNT   = 8;
static const int COL_ORIG_CURRENCY = 9;
static const int COL_PURCHASE_FILE = 10;
static const int COL_COUNT         = 11;

// Root
InventoryMoveTreeItem::InventoryMoveTreeItem()
    : m_parentItem(nullptr)
    , m_type(Root)
{}

// Parent
InventoryMoveTreeItem::InventoryMoveTreeItem(const QString &from, const QString &to,
                                             const QString &currency,
                                             InventoryMoveTreeItem *parentItem)
    : m_parentItem(parentItem)
    , m_type(Parent)
    , m_from(from)
    , m_to(to)
    , m_currency(currency)
{}

// Child
InventoryMoveTreeItem::InventoryMoveTreeItem(const QString &from, const QString &to,
                                             const QString &sku, const QString &productName,
                                             int units, double unitPrice,
                                             const QString &currency,
                                             double origAmount, const QString &origCurrency,
                                             const QString &purchaseFile,
                                             InventoryMoveTreeItem *parentItem)
    : m_parentItem(parentItem)
    , m_type(Child)
    , m_from(from)
    , m_to(to)
    , m_sku(sku)
    , m_productName(productName)
    , m_units(units)
    , m_unitPrice(unitPrice)
    , m_totalPrice(units * unitPrice)
    , m_currency(currency)
    , m_origAmount(origAmount)
    , m_origCurrency(origCurrency)
    , m_purchaseFile(purchaseFile)
{}

InventoryMoveTreeItem::~InventoryMoveTreeItem()
{
    qDeleteAll(m_childItems);
}

void InventoryMoveTreeItem::appendChild(InventoryMoveTreeItem *child)
{
    m_childItems.append(child);
}

void InventoryMoveTreeItem::detachChildren()
{
    m_childItems.clear();
}

InventoryMoveTreeItem *InventoryMoveTreeItem::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int InventoryMoveTreeItem::childCount() const
{
    return m_childItems.count();
}

int InventoryMoveTreeItem::columnCount() const
{
    return COL_COUNT;
}

QVariant InventoryMoveTreeItem::data(int column) const
{
    switch (m_type) {
    case Root:
        return QVariant();

    case Parent:
        switch (column) {
        case COL_FROM:          return m_from;
        case COL_TO:            return m_to;
        case COL_SKU:           return m_childItems.count();
        case COL_PRODUCT_NAME:  return QVariant();
        case COL_UNITS:         return m_units;
        case COL_UNIT_PRICE:    return m_unitPrice;
        case COL_TOTAL_PRICE:   return m_totalPrice;
        case COL_CURRENCY:      return m_currency;
        case COL_ORIG_AMOUNT:   return QVariant(); // empty: no single original currency for aggregates
        case COL_ORIG_CURRENCY: return QVariant(); // empty: idem
        case COL_PURCHASE_FILE: return QVariant();
        default: break;
        }
        break;

    case Child:
        switch (column) {
        case COL_FROM:          return m_from;
        case COL_TO:            return m_to;
        case COL_SKU:           return m_sku;
        case COL_PRODUCT_NAME:  return m_productName;
        case COL_UNITS:         return m_units;
        case COL_UNIT_PRICE:    return m_unitPrice;
        case COL_TOTAL_PRICE:   return m_totalPrice;
        case COL_CURRENCY:      return m_currency;
        case COL_ORIG_AMOUNT:   return (m_origAmount != 0.0) ? QVariant(m_origAmount) : QVariant();
        case COL_ORIG_CURRENCY: return m_origCurrency.isEmpty() ? QVariant() : QVariant(m_origCurrency);
        case COL_PURCHASE_FILE: return m_purchaseFile;
        default: break;
        }
        break;
    }
    return QVariant();
}

int InventoryMoveTreeItem::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<InventoryMoveTreeItem*>(this));
    return 0;
}

InventoryMoveTreeItem *InventoryMoveTreeItem::parentItem()
{
    return m_parentItem;
}

InventoryMoveTreeItem::Type InventoryMoveTreeItem::getType() const
{
    return m_type;
}

void InventoryMoveTreeItem::aggregate()
{
    if (m_type != Parent)
        return;

    m_units = 0;
    m_totalPrice = 0.0;

    for (const InventoryMoveTreeItem *child : m_childItems) {
        m_units += child->m_units;
        m_totalPrice += child->m_totalPrice;
    }

    m_unitPrice = (m_units > 0) ? (m_totalPrice / m_units) : 0.0;
}
