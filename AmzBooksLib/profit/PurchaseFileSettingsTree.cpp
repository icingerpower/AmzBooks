#include "PurchaseFileSettingsTree.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include "orders/ExceptionParamValue.h"

const QString PurchaseFileSettingsTree::COL_ORDER_ID = "Order ID";
const QString PurchaseFileSettingsTree::COL_TITLE = "Title";
const QString PurchaseFileSettingsTree::COL_SKU = "SKU";
const QString PurchaseFileSettingsTree::COL_QUANTITY = "Quantity";
const QString PurchaseFileSettingsTree::COL_UNIT_WEIGHT = "Unit Weight";
const QString PurchaseFileSettingsTree::COL_UNIT_PRICE = "Unit Price";
const QString PurchaseFileSettingsTree::COL_CURRENCY = "Currency";

const QStringList PurchaseFileSettingsTree::FIXED_ROW_IDS = {
    PurchaseFileSettingsTree::COL_ORDER_ID,
    PurchaseFileSettingsTree::COL_TITLE,
    PurchaseFileSettingsTree::COL_SKU,
    PurchaseFileSettingsTree::COL_QUANTITY,
    PurchaseFileSettingsTree::COL_UNIT_WEIGHT,
    PurchaseFileSettingsTree::COL_UNIT_PRICE,
    PurchaseFileSettingsTree::COL_CURRENCY
};

const QStringList PurchaseFileSettingsTree::FIXED_ROW_NAMES()
{
    static const QStringList names = {
        tr("Order ID"),
        tr("Title"),
        tr("SKU"),
        tr("Quantity"),
        tr("Unit Weight"),
        tr("Unit Price"),
        tr("Currency")
    };
    return names;
}

PurchaseFileSettingsTree::PurchaseFileSettingsTree(const QDir &workingDir, QObject *parent)
    : QAbstractItemModel(parent)
{
    m_filePath = workingDir.filePath("purchaseFileSettings.csv");
    m_rootItem = new PurchaseFileSettingsTreeItem("Root");
    _setupFixedRows();
    _load();
}

PurchaseFileSettingsTree::~PurchaseFileSettingsTree()
{
    delete m_rootItem;
}

int PurchaseFileSettingsTree::getColPos(const QStringList &colNames, const QString &id) const
{
    // Find the fixed row with the given ID
    PurchaseFileSettingsTreeItem *fixedRow = nullptr;
    for (int i = 0; i < m_rootItem->childCount(); ++i) {
        PurchaseFileSettingsTreeItem *child = m_rootItem->child(i);
        if (child->id() == id) {
            fixedRow = child;
            break;
        }
    }

    if (!fixedRow) return -1;

    // "Leftmost Header Wins" priority
    // Iterate through available columns in the order they appear in the file (or provided list)
    for (int i = 0; i < colNames.size(); ++i) {
        QString colName = colNames[i];
        
        // 1. Check Fixed Row Name (tr)
        if (colName == fixedRow->name()) return i;
        
        // 2. Check Hidden ID
        if (colName == fixedRow->id()) return i;
        
        // 3. Check Candidates
        for (int j = 0; j < fixedRow->childCount(); ++j) {
            if (fixedRow->child(j)->name() == colName) return i;
        }
    }

    return -1;
}

void PurchaseFileSettingsTree::_setupFixedRows()
{
    QStringList ids = FIXED_ROW_IDS;
    QStringList names = FIXED_ROW_NAMES();
    
    // Ensure lists align
    Q_ASSERT(ids.size() == names.size());
    
    for (int i = 0; i < ids.size(); ++i) {
        QString id = ids[i];
        QString name = names[i];
        
        PurchaseFileSettingsTreeItem *item = new PurchaseFileSettingsTreeItem(name, m_rootItem);
        item->setId(id);
        item->setIsFixed(true);
        m_rootItem->appendChild(item);
    }
}

QModelIndex PurchaseFileSettingsTree::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    PurchaseFileSettingsTreeItem *parentItem = getItem(parent);
    PurchaseFileSettingsTreeItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex PurchaseFileSettingsTree::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();

    PurchaseFileSettingsTreeItem *childItem = static_cast<PurchaseFileSettingsTreeItem*>(child.internalPointer());
    PurchaseFileSettingsTreeItem *parentItem = childItem->parentItem();

    if (parentItem == m_rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int PurchaseFileSettingsTree::rowCount(const QModelIndex &parent) const
{
    PurchaseFileSettingsTreeItem *parentItem = getItem(parent);
    return parentItem->childCount();
}

int PurchaseFileSettingsTree::columnCount(const QModelIndex &parent) const
{
    return m_rootItem->columnCount();
}

QVariant PurchaseFileSettingsTree::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    PurchaseFileSettingsTreeItem *item = static_cast<PurchaseFileSettingsTreeItem*>(index.internalPointer());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return item->data(index.column());
    } 
    
    // Hidden ID
    if (role == Qt::UserRole && item->isFixed()) {
        return item->id();
    }

    return QVariant();
}

bool PurchaseFileSettingsTree::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && role == Qt::EditRole) {
        PurchaseFileSettingsTreeItem *item = static_cast<PurchaseFileSettingsTreeItem*>(index.internalPointer());
        if (!item->isFixed()) { // Only candidates are editable
            if (item->name() != value.toString()) {
                item->setName(value.toString());
                emit dataChanged(index, index, {role});
                _save();
                return true;
            }
        }
    }
    return false;
}

QVariant PurchaseFileSettingsTree::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == 0) return tr("Name");
    }
    return QVariant();
}

Qt::ItemFlags PurchaseFileSettingsTree::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    PurchaseFileSettingsTreeItem *item = static_cast<PurchaseFileSettingsTreeItem*>(index.internalPointer());

    if (!item->isFixed()) {
        flags |= Qt::ItemIsEditable;
    }

    return flags;
}

bool PurchaseFileSettingsTree::insertRows(int row, int count, const QModelIndex &parent)
{
    PurchaseFileSettingsTreeItem *parentItem = getItem(parent);
    if (!parentItem) return false;
    
    if (parentItem == m_rootItem) return false; 
    if (!parentItem->isFixed()) return false; 

    beginInsertRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        PurchaseFileSettingsTreeItem *item = new PurchaseFileSettingsTreeItem("New Value", parentItem);
        parentItem->insertChild(row + i, item);
    }
    endInsertRows();
    _save();
    return true;
}

bool PurchaseFileSettingsTree::removeRows(int row, int count, const QModelIndex &parent)
{
    PurchaseFileSettingsTreeItem *parentItem = getItem(parent);
    if (!parentItem) return false;

    if (parentItem == m_rootItem) return false; 
    if (!parentItem->isFixed()) return false;

    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        parentItem->removeChild(row);
    }
    endRemoveRows();
    _save();
    return true;
}

void PurchaseFileSettingsTree::addCandidate(const QModelIndex &parentIndex, const QString &candidate)
{
    if (!parentIndex.isValid()) return;
    
    // Check for duplicates
    for (int i = 0; i < m_rootItem->childCount(); ++i) {
        PurchaseFileSettingsTreeItem *fixedRow = m_rootItem->child(i);
        // Check fixed row name (though usually it's different concept, but safe to check)
        if (fixedRow->name() == candidate) {
             throw ExceptionParamValue(tr("Duplicate Name"), tr("The name '%1' is already used by a fixed row.").arg(candidate));
        }
        
        for (int j = 0; j < fixedRow->childCount(); ++j) {
            PurchaseFileSettingsTreeItem *existing = fixedRow->child(j);
            if (existing->name() == candidate) {
                throw ExceptionParamValue(tr("Duplicate Name"), tr("The name '%1' is already used.").arg(candidate));
            }
        }
    }
    
    PurchaseFileSettingsTreeItem *parentItem = static_cast<PurchaseFileSettingsTreeItem*>(parentIndex.internalPointer());
    if (!parentItem->isFixed()) return;

    int row = parentItem->childCount();
    beginInsertRows(parentIndex, row, row);
    PurchaseFileSettingsTreeItem *item = new PurchaseFileSettingsTreeItem(candidate, parentItem);
    parentItem->appendChild(item);
    endInsertRows();
    _save();
}

PurchaseFileSettingsTreeItem *PurchaseFileSettingsTree::getItem(const QModelIndex &index) const
{
    if (index.isValid()) {
        PurchaseFileSettingsTreeItem *item = static_cast<PurchaseFileSettingsTreeItem*>(index.internalPointer());
        if (item) return item;
    }
    return m_rootItem;
}

void PurchaseFileSettingsTree::_load()
{
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList parts = line.split(";");
        if (parts.size() >= 2) {
            QString parentId = parts[0];
            QString candidate = parts[1];

            // Find parent
            for (int i=0; i < m_rootItem->childCount(); ++i) {
                PurchaseFileSettingsTreeItem *fixedRow = m_rootItem->child(i);
                if (fixedRow->id() == parentId) {
                    PurchaseFileSettingsTreeItem *item = new PurchaseFileSettingsTreeItem(candidate, fixedRow);
                    fixedRow->appendChild(item);
                    break;
                }
            }
        }
    }
}

void PurchaseFileSettingsTree::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to save PurchaseFileSettingsTree:" << file.errorString();
        return;
    }

    QTextStream out(&file);
    for (int i=0; i < m_rootItem->childCount(); ++i) {
        PurchaseFileSettingsTreeItem *fixedRow = m_rootItem->child(i);
        for (int j=0; j < fixedRow->childCount(); ++j) {
            PurchaseFileSettingsTreeItem *candidate = fixedRow->child(j);
            out << fixedRow->id() << ";" << candidate->name() << "\n";
        }
    }
}
