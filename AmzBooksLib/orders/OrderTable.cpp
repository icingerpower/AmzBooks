#include "OrderTable.h"
#include "orders/Shipment.h"
#include "orders/ActivitySource.h"
#include "orders/SaleType.h"
#include "orders/TaxSource.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"

const QStringList OrderTable::COL_NAMES = {
    QObject::tr("Date"),
    QObject::tr("Channel"),
    QObject::tr("Site"),
    QObject::tr("Order ID"),
    QObject::tr("Activity ID"),
    QObject::tr("Sale Type"),
    QObject::tr("From"),
    QObject::tr("To"),
    QObject::tr("VAT Paid To"),
    QObject::tr("Tax Source"),
    QObject::tr("Tax Scheme"),
    QObject::tr("Jurisdiction"),
    QObject::tr("Currency"),
    QObject::tr("Amount Taxed"),
    QObject::tr("VAT Amount"),
    QObject::tr("Invoice ID")
};

OrderTable::OrderTable(const QList<QSharedPointer<Shipment>> &shipments, QObject *parent)
    : QAbstractTableModel(parent)
{
    buildRows(shipments);
    sort(COL_DATE, Qt::DescendingOrder);
}

OrderTable::OrderTable(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data, QObject *parent)
    : QAbstractTableModel(parent)
{
    buildRows(data);
    sort(COL_DATE, Qt::DescendingOrder);
}

int OrderTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int OrderTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return COL_COUNT;
}

QVariant OrderTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const OrderRow &row = m_rows.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case COL_DATE: return row.date;
        case COL_CHANNEL: return row.channel;
        case COL_SITE: return row.site;
        case COL_ORDER_ID: return row.orderId;
        case COL_ACTIVITY_ID: return row.activityId;
        case COL_SALE_TYPE: return row.saleType;
        case COL_COUNTRY_FROM: return row.countryFrom;
        case COL_COUNTRY_TO: return row.countryTo;
        case COL_VAT_PAID_TO: return row.vatPaidTo;
        case COL_TAX_SOURCE: return row.taxSource;
        case COL_TAX_SCHEME: return row.taxScheme;
        case COL_TAX_JURISDICTION: return row.taxJurisdiction;
        case COL_CURRENCY: return row.currency;
        case COL_AMOUNT_TAXED: return row.amountTaxed;
        case COL_VAT_AMOUNT: return row.vatAmount;
        case COL_INVOICE_ID: return row.invoiceId;
        }
    }

    return QVariant();
}

QVariant OrderTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < COL_NAMES.size())
            return COL_NAMES[section];
    }
    return QVariant();
}

void OrderTable::sort(int column, Qt::SortOrder order)
{
    emit layoutAboutToBeChanged();

    std::sort(m_rows.begin(), m_rows.end(), [column, order](const OrderRow &a, const OrderRow &b) {
        bool less = false;
        switch (column) {
        case COL_DATE: less = a.date < b.date; break;
        case COL_CHANNEL: less = a.channel.compare(b.channel, Qt::CaseInsensitive) < 0; break;
        case COL_SITE: less = a.site.compare(b.site, Qt::CaseInsensitive) < 0; break;
        case COL_ORDER_ID: less = a.orderId.compare(b.orderId, Qt::CaseInsensitive) < 0; break;
        case COL_ACTIVITY_ID: less = a.activityId.compare(b.activityId, Qt::CaseInsensitive) < 0; break;
        case COL_SALE_TYPE: less = a.saleType.compare(b.saleType, Qt::CaseInsensitive) < 0; break;
        case COL_COUNTRY_FROM: less = a.countryFrom.compare(b.countryFrom, Qt::CaseInsensitive) < 0; break;
        case COL_COUNTRY_TO: less = a.countryTo.compare(b.countryTo, Qt::CaseInsensitive) < 0; break;
        case COL_VAT_PAID_TO: less = a.vatPaidTo.compare(b.vatPaidTo, Qt::CaseInsensitive) < 0; break;
        case COL_TAX_SOURCE: less = a.taxSource.compare(b.taxSource, Qt::CaseInsensitive) < 0; break;
        case COL_TAX_SCHEME: less = a.taxScheme.compare(b.taxScheme, Qt::CaseInsensitive) < 0; break;
        case COL_TAX_JURISDICTION: less = a.taxJurisdiction.compare(b.taxJurisdiction, Qt::CaseInsensitive) < 0; break;
        case COL_CURRENCY: less = a.currency.compare(b.currency, Qt::CaseInsensitive) < 0; break;
        case COL_AMOUNT_TAXED: less = a.amountTaxed < b.amountTaxed; break;
        case COL_VAT_AMOUNT: less = a.vatAmount < b.vatAmount; break;
        case COL_INVOICE_ID: less = a.invoiceId.compare(b.invoiceId, Qt::CaseInsensitive) < 0; break;
        default: less = a.date < b.date; break;
        }
        return (order == Qt::AscendingOrder) ? less : !less;
    });

    emit layoutChanged();
}

void OrderTable::buildRows(const QList<QSharedPointer<Shipment>> &shipments)
{
    m_rows.clear();
    m_rows.reserve(shipments.size());

    for (const auto &ship : shipments) {
        for (const auto &act : ship->getActivities()) {
            OrderRow row;
            row.date = act.getDateTime().date();
            row.channel = ""; // Not available in simple shipment list
            row.site = "";    // Not available in simple shipment list
            row.orderId = act.getEventId();
            row.activityId = act.getActivityId();
            row.saleType = toString(act.getSaleType());
            row.countryFrom = act.getCountryCodeFrom();
            row.countryTo = act.getCountryCodeTo();
            row.vatPaidTo = act.getCountryCodeVatPaidTo();
            row.taxSource = taxSourceToString(act.getTaxSource());
            row.taxScheme = taxSchemeToString(act.getTaxScheme());
            row.taxJurisdiction = taxJurisdictionLevelToString(act.getTaxJurisdictionLevel());
            row.currency = act.getCurrency();
            row.amountTaxed = act.getAmountTaxed();
            row.vatAmount = act.getAmountTaxesComputed();
            row.invoiceId = act.getInvoiceId();
            row.sourceShipment = ship;
            m_rows.append(row);
        }
    }
}

void OrderTable::buildRows(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data)
{
    m_rows.clear();
    if (!data) return;

    for (auto itChannel = data->constBegin(); itChannel != data->constEnd(); ++itChannel) {
        QString channel = itChannel.key();
        for (auto itSite = itChannel.value().constBegin(); itSite != itChannel.value().constEnd(); ++itSite) {
            QString site = itSite.key();
            for (auto itContext = itSite.value().constBegin(); itContext != itSite.value().constEnd(); ++itContext) {
                // Should we use context for row data? It might be redundant with activity data but consistent.
                
                const auto &shipments = itContext.value().shipmentsRefundsSameActivity;
                for (const auto &ship : shipments) {
                    for (const auto &act : ship->getActivities()) {
                        OrderRow row;
                        row.date = act.getDateTime().date();
                        row.channel = channel;
                        row.site = site;
                        row.orderId = act.getEventId();
                        row.activityId = act.getActivityId();
                        row.saleType = toString(act.getSaleType());
                        row.countryFrom = act.getCountryCodeFrom();
                        row.countryTo = act.getCountryCodeTo();
                        row.vatPaidTo = act.getCountryCodeVatPaidTo();
                        row.taxSource = taxSourceToString(act.getTaxSource());
                        row.taxScheme = taxSchemeToString(act.getTaxScheme());
                        row.taxJurisdiction = taxJurisdictionLevelToString(act.getTaxJurisdictionLevel());
                        row.currency = act.getCurrency();
                        row.amountTaxed = act.getAmountTaxed();
                        row.vatAmount = act.getAmountTaxesComputed();
                        row.invoiceId = act.getInvoiceId();
                        row.sourceShipment = ship;
                        m_rows.append(row);
                    }
                }
            }
        }
    }
}
