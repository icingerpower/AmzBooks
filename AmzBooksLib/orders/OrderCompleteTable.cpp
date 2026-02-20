#include "OrderCompleteTable.h"
#include "orders/Shipment.h"
#include "books/Activity.h"

const QStringList OrderCompleteTable::COL_NAMES = {
    QObject::tr("Date"),
    QObject::tr("Order ID"),
    QObject::tr("Activity ID"),
    QObject::tr("Orig. Activity ID"),
    QObject::tr("Channel"),
    QObject::tr("Store"),
    QObject::tr("Site"),
    QObject::tr("Sale Type"),
    QObject::tr("Type"),
    QObject::tr("From"),
    QObject::tr("To"),
    QObject::tr("VAT Paid To"),
    QObject::tr("Is Business"),
    QObject::tr("Tax Source"),
    QObject::tr("Tax Scheme"),
    QObject::tr("Jurisdiction"),
    QObject::tr("Currency"),
    QObject::tr("Amount Taxed"),
    QObject::tr("VAT Amount"),
    QObject::tr("Invoice ID")
};

OrderCompleteTable::OrderCompleteTable(
    const QSharedPointer<CompleteData> &channel_site_ShipmentAndRefunds,
    const QHash<QString, QString> &orderIdToSite,
    QObject *parent)
    : OrderTableAbstract(parent)
{
    buildRows(channel_site_ShipmentAndRefunds, orderIdToSite);
    _sort(COL_DATE, Qt::DescendingOrder);
}

int OrderCompleteTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return COL_COUNT;
}

QVariant OrderCompleteTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const OrderRow &row = m_rows.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case COL_DATE:             return row.date;
        case COL_ORDER_ID:         return row.orderId;
        case COL_ACTIVITY_ID:      return row.activityId;
        case COL_ACTIVITY_ID_ORIG: return row.activityIdOrig;
        case COL_CHANNEL:          return row.channel;
        case COL_STORE:            return row.store;
        case COL_SITE:             return row.site;
        case COL_SALE_TYPE:        return row.saleType;
        case COL_TYPE:             return row.amountTaxed >= 0 ? tr("Shipment") : tr("Refund");
        case COL_COUNTRY_FROM:     return row.countryFrom;
        case COL_COUNTRY_TO:       return row.countryTo;
        case COL_VAT_PAID_TO:      return row.vatPaidTo;
        case COL_IS_BUSINESS:      return row.isCompany ? tr("Yes") : tr("No");
        case COL_TAX_SOURCE:       return row.taxSource;
        case COL_TAX_SCHEME:       return row.taxScheme;
        case COL_TAX_JURISDICTION: return row.taxJurisdiction;
        case COL_CURRENCY:         return row.currency;
        case COL_AMOUNT_TAXED:     return row.amountTaxed;
        case COL_VAT_AMOUNT:       return row.vatAmount;
        case COL_INVOICE_ID:       return row.invoiceId;
        }
    }

    return QVariant();
}

QVariant OrderCompleteTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < COL_NAMES.size()) {
            return COL_NAMES[section];
        }
    }
    return QVariant();
}

void OrderCompleteTable::_sort(int column, Qt::SortOrder order)
{
    std::sort(m_rows.begin(), m_rows.end(), [column, order](const OrderRow &a, const OrderRow &b) {
        const OrderRow &lhs = (order == Qt::AscendingOrder) ? a : b;
        const OrderRow &rhs = (order == Qt::AscendingOrder) ? b : a;

        switch (column) {
        case COL_DATE:             return lhs.date < rhs.date;
        case COL_ORDER_ID:         return lhs.orderId.compare(rhs.orderId, Qt::CaseInsensitive) < 0;
        case COL_ACTIVITY_ID:      return lhs.activityId.compare(rhs.activityId, Qt::CaseInsensitive) < 0;
        case COL_ACTIVITY_ID_ORIG: return lhs.activityIdOrig.compare(rhs.activityIdOrig, Qt::CaseInsensitive) < 0;
        case COL_CHANNEL:          return lhs.channel.compare(rhs.channel, Qt::CaseInsensitive) < 0;
        case COL_STORE:            return lhs.store.compare(rhs.store, Qt::CaseInsensitive) < 0;
        case COL_SITE:             return lhs.site.compare(rhs.site, Qt::CaseInsensitive) < 0;
        case COL_SALE_TYPE:        return lhs.saleType.compare(rhs.saleType, Qt::CaseInsensitive) < 0;
        case COL_TYPE:             return (lhs.amountTaxed >= 0) < (rhs.amountTaxed >= 0);
        case COL_COUNTRY_FROM:     return lhs.countryFrom.compare(rhs.countryFrom, Qt::CaseInsensitive) < 0;
        case COL_COUNTRY_TO:       return lhs.countryTo.compare(rhs.countryTo, Qt::CaseInsensitive) < 0;
        case COL_VAT_PAID_TO:      return lhs.vatPaidTo.compare(rhs.vatPaidTo, Qt::CaseInsensitive) < 0;
        case COL_IS_BUSINESS:      return lhs.isCompany < rhs.isCompany;
        case COL_TAX_SOURCE:       return lhs.taxSource.compare(rhs.taxSource, Qt::CaseInsensitive) < 0;
        case COL_TAX_SCHEME:       return lhs.taxScheme.compare(rhs.taxScheme, Qt::CaseInsensitive) < 0;
        case COL_TAX_JURISDICTION: return lhs.taxJurisdiction.compare(rhs.taxJurisdiction, Qt::CaseInsensitive) < 0;
        case COL_CURRENCY:         return lhs.currency.compare(rhs.currency, Qt::CaseInsensitive) < 0;
        case COL_AMOUNT_TAXED:     return lhs.amountTaxed < rhs.amountTaxed;
        case COL_VAT_AMOUNT:       return lhs.vatAmount < rhs.vatAmount;
        case COL_INVOICE_ID:       return lhs.invoiceId.compare(rhs.invoiceId, Qt::CaseInsensitive) < 0;
        default:                   return lhs.date < rhs.date;
        }
    });
}

void OrderCompleteTable::sort(int column, Qt::SortOrder order)
{
    emit layoutAboutToBeChanged();
    _sort(column, order);
    emit layoutChanged();
}

void OrderCompleteTable::buildRows(
    const QSharedPointer<CompleteData> &data,
    const QHash<QString, QString> &orderIdToSite)
{
    m_rows.clear();
    if (!data) return;

    for (auto itChannel = data->constBegin(); itChannel != data->constEnd(); ++itChannel) {
        const QString &channel = itChannel.key();
        for (auto itSite = itChannel.value().constBegin(); itSite != itChannel.value().constEnd(); ++itSite) {
            const QString &store = itSite.key();
            for (auto itContext = itSite.value().constBegin(); itContext != itSite.value().constEnd(); ++itContext) {
                const auto &shipments = itContext.value().shipmentsRefundsSameActivity;

                // Pre-build per-order queues of original activity IDs (positive amounts).
                // Each refund claims the next unclaimed original from its own order's queue,
                // preventing cross-order contamination when multiple orders share a TaxContext.
                QHash<QString, QList<QString>> unclaimedOrigsByOrder;
                for (const auto &ship : shipments)
                    for (const auto &act : ship->getActivities())
                        if (act.getAmountTaxed() >= 0)
                            unclaimedOrigsByOrder[act.getEventId()].append(act.getActivityId());

                for (const auto &ship : shipments) {
                    for (const auto &act : ship->getActivities()) {
                        OrderRow row = makeRow(act, ship);
                        row.channel = channel;
                        row.store = store;
                        row.site = orderIdToSite.value(act.getEventId());
                        if (act.getAmountTaxed() < 0) {
                            auto &origQueue = unclaimedOrigsByOrder[act.getEventId()];
                            if (!origQueue.isEmpty())
                                row.activityIdOrig = origQueue.takeFirst();
                        }
                        m_rows.append(row);
                    }
                }
            }
        }
    }
}
