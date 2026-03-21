#ifndef ORDERINVOICINGTABLE_H
#define ORDERINVOICINGTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include "orders/AbstractImporter.h"

class OrderInvoicingTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        COL_ID = 0,
        COL_INVOICE_NUMBER,
        COL_PAYMENT_DATE,
        COL_LINK,
        // Line-item columns — one row per item; repeated from the parent InvoicingInfo.
        // Rows whose InvoicingInfo has no items use these columns with empty/zero values.
        COL_ITEM_SKU,
        COL_ITEM_NAME,
        COL_ITEM_QUANTITY,
        COL_ITEM_UNIT_PRICE_HT,  // per-unit untaxed amount
        COL_ITEM_TOTAL_TTC,      // qty × taxed amount (total incl. tax)
        COL_COUNT
    };

    explicit OrderInvoicingTable(const QList<AbstractImporter::InvoicingInfoWithId> &infos, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<AbstractImporter::InvoicingInfoWithId> m_data;

    // Flat row index: one entry per line item (or one entry per info when items is empty).
    struct Row {
        int dataIdx; // index into m_data
        int itemIdx; // index into m_data[dataIdx].invoicingInfo.getItems(), or -1 if none
    };
    QList<Row> m_rows;

    static const QStringList COL_NAMES;
};

#endif // ORDERINVOICINGTABLE_H
