#include "OrderInvoicingTable.h"
#include "orders/LineItem.h"

const QStringList OrderInvoicingTable::COL_NAMES = {
    QObject::tr("Shipment/Refund ID"),
    QObject::tr("Invoice Number"),
    QObject::tr("Payment Date"),
    QObject::tr("Link"),
    QObject::tr("SKU"),
    QObject::tr("Item Name"),
    QObject::tr("Qty"),
    QObject::tr("Unit Price HT"),
    QObject::tr("Total TTC")
};

OrderInvoicingTable::OrderInvoicingTable(const QList<AbstractImporter::InvoicingInfoWithId> &infos, QObject *parent)
    : QAbstractTableModel(parent)
    , m_data(infos)
{
    for (int i = 0; i < m_data.size(); ++i) {
        const auto &items = m_data[i].invoicingInfo.getItems();
        if (items.isEmpty()) {
            m_rows.append({i, -1});
        } else {
            for (int j = 0; j < items.size(); ++j) {
                m_rows.append({i, j});
            }
        }
    }
}

int OrderInvoicingTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int OrderInvoicingTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return COL_COUNT;
}

QVariant OrderInvoicingTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const auto &row  = m_rows.at(index.row());
    const auto &inv  = m_data.at(row.dataIdx);
    const auto &info = inv.invoicingInfo;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case COL_ID:            return inv.shipmentOrRefundId;
        case COL_INVOICE_NUMBER: return info.getInvoiceNumber().value_or("");
        case COL_PAYMENT_DATE:  return info.getPaymentDate(QDate()).toString(Qt::ISODate);
        case COL_LINK:          return info.getInvoiceLink().value_or("");
        case COL_ITEM_SKU:
        case COL_ITEM_NAME:
        case COL_ITEM_QUANTITY:
        case COL_ITEM_UNIT_PRICE_HT:
        case COL_ITEM_TOTAL_TTC: {
            if (row.itemIdx < 0)
                return QVariant();
            const auto &item = info.getItems().at(row.itemIdx);
            switch (index.column()) {
            case COL_ITEM_SKU:           return item.getSku();
            case COL_ITEM_NAME:          return item.getName();
            case COL_ITEM_QUANTITY:      return item.getQuantity();
            case COL_ITEM_UNIT_PRICE_HT: return item.getAmountUntaxed();
            case COL_ITEM_TOTAL_TTC:     return item.getTotalTaxed();
            }
        }
        }
    }
    return QVariant();
}

QVariant OrderInvoicingTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < COL_NAMES.size())
            return COL_NAMES[section];
    }
    return QVariant();
}
