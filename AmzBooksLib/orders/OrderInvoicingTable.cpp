#include "OrderInvoicingTable.h"

const QStringList OrderInvoicingTable::COL_NAMES = {
    QObject::tr("Shipment/Refund ID"),
    QObject::tr("Invoice Number"),
    QObject::tr("Payment Date"),
    QObject::tr("Link")
};

OrderInvoicingTable::OrderInvoicingTable(const QList<AbstractImporter::InvoicingInfoWithId> &infos, QObject *parent)
    : QAbstractTableModel(parent)
    , m_data(infos)
{
}

int OrderInvoicingTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_data.size();
}

int OrderInvoicingTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return COL_COUNT;
}

QVariant OrderInvoicingTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size())
        return QVariant();

    const auto &item = m_data.at(index.row());
    const auto &info = item.invoicingInfo;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case COL_ID: return item.shipmentOrRefundId;
        case COL_INVOICE_NUMBER: return info.getInvoiceNumber().value_or("");
        case COL_PAYMENT_DATE: return info.getPaymentDate(QDate()).toString(Qt::ISODate); // Using default QDate if not set, logic might need adjustment
        case COL_LINK: return info.getInvoiceLink().value_or("");
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
