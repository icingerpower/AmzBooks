#ifndef ORDERCOMPLETETABLE_H
#define ORDERCOMPLETETABLE_H

#include <QList>
#include <QSharedPointer>
#include <QHash>
#include "orders/OrderTableAbstract.h"
#include "orders/OrderManager.h"
#include "books/TaxResolver.h"

class OrderCompleteTable : public OrderTableAbstract
{
    Q_OBJECT

public:
    enum Columns {
        COL_DATE = 0,
        COL_ORDER_ID,
        COL_ACTIVITY_ID,
        COL_ACTIVITY_ID_ORIG,   // activityId of the first shipment in the group, if different from current row
        COL_CHANNEL,            // channel key from the hash
        COL_STORE,              // site/store key from the hash (subchannel)
        COL_SITE,               // orders.store value (marketplace site, e.g. "Amazon.fr")
        COL_SALE_TYPE,
        COL_TYPE,
        COL_COUNTRY_FROM,
        COL_COUNTRY_TO,
        COL_VAT_PAID_TO,
        COL_IS_BUSINESS,
        COL_TAX_SOURCE,
        COL_TAX_SCHEME,
        COL_TAX_JURISDICTION,
        COL_CURRENCY,
        COL_AMOUNT_TAXED,
        COL_VAT_AMOUNT,
        COL_INVOICE_ID,
        COL_COUNT
    };

    // Stable index constants for external code that needs to identify
    // the order-ID and activity-ID columns without importing the full enum.
    static const int IND_COL_ORDER_ID    = COL_ORDER_ID;
    static const int IND_COL_ACTIVITY_ID = COL_ACTIVITY_ID;

    using CompleteData = QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>;

    explicit OrderCompleteTable(
        const QSharedPointer<CompleteData> &channel_site_ShipmentAndRefunds,
        const QHash<QString, QString> &orderIdToSite,
        QObject *parent = nullptr);

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    static const QStringList COL_NAMES;

    void buildRows(const QSharedPointer<CompleteData> &data, const QHash<QString, QString> &orderIdToSite);
    void _sort(int column, Qt::SortOrder order); // non-virtual, safe to call from constructor
};

#endif // ORDERCOMPLETETABLE_H
