#include "OrderTable.h"
#include "orders/Shipment.h"
#include "orders/ActivitySource.h"
#include "orders/SaleType.h"
#include "orders/TaxSource.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"
#include <QDebug>

const QStringList OrderTable::COL_NAMES = {
    QObject::tr("Date"),
    QObject::tr("Order ID"),
    QObject::tr("Activity ID"),
    QObject::tr("Sale Type"),
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

OrderTable::OrderTable(const QList<QSharedPointer<Shipment>> &shipments, QObject *parent)
    : QAbstractTableModel(parent)
{
    buildRows(shipments);
    sort(COL_DATE, Qt::DescendingOrder);
}

OrderTable::OrderTable(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &channel_site_ShipmentAndRefunds, QObject *parent)
    : QAbstractTableModel(parent)
{
    buildRows(channel_site_ShipmentAndRefunds);
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

    int rowIndex = index.row();
    const OrderRow &row = m_rows.at(rowIndex);
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case COL_DATE: return row.date;
        case COL_ORDER_ID: return row.orderId;
        case COL_ACTIVITY_ID: return row.activityId;
        case COL_SALE_TYPE: return row.saleType;
        case COL_COUNTRY_FROM: return row.countryFrom;
        case COL_COUNTRY_TO: return row.countryTo;
        case COL_VAT_PAID_TO: return row.vatPaidTo;
        case COL_IS_BUSINESS: return row.isCompany ? tr("Yes") : tr("No");
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
        // For descending order, swap the operands instead of negating the result.
        // Negating (!less) violates strict weak ordering when a == b,
        // because comp(a,b) and comp(b,a) would both return true → UB in std::sort.
        const OrderRow &lhs = (order == Qt::AscendingOrder) ? a : b;
        const OrderRow &rhs = (order == Qt::AscendingOrder) ? b : a;

        switch (column) {
        case COL_DATE: return lhs.date < rhs.date;
        case COL_ORDER_ID: return lhs.orderId.compare(rhs.orderId, Qt::CaseInsensitive) < 0;
        case COL_ACTIVITY_ID: return lhs.activityId.compare(rhs.activityId, Qt::CaseInsensitive) < 0;
        case COL_SALE_TYPE: return lhs.saleType.compare(rhs.saleType, Qt::CaseInsensitive) < 0;
        case COL_COUNTRY_FROM: return lhs.countryFrom.compare(rhs.countryFrom, Qt::CaseInsensitive) < 0;
        case COL_COUNTRY_TO: return lhs.countryTo.compare(rhs.countryTo, Qt::CaseInsensitive) < 0;
        case COL_VAT_PAID_TO: return lhs.vatPaidTo.compare(rhs.vatPaidTo, Qt::CaseInsensitive) < 0;
        case COL_IS_BUSINESS: return lhs.isCompany < rhs.isCompany;
        case COL_TAX_SOURCE: return lhs.taxSource.compare(rhs.taxSource, Qt::CaseInsensitive) < 0;
        case COL_TAX_SCHEME: return lhs.taxScheme.compare(rhs.taxScheme, Qt::CaseInsensitive) < 0;
        case COL_TAX_JURISDICTION: return lhs.taxJurisdiction.compare(rhs.taxJurisdiction, Qt::CaseInsensitive) < 0;
        case COL_CURRENCY: return lhs.currency.compare(rhs.currency, Qt::CaseInsensitive) < 0;
        case COL_AMOUNT_TAXED: return lhs.amountTaxed < rhs.amountTaxed;
        case COL_VAT_AMOUNT: return lhs.vatAmount < rhs.vatAmount;
        case COL_INVOICE_ID: return lhs.invoiceId.compare(rhs.invoiceId, Qt::CaseInsensitive) < 0;
        default: return lhs.date < rhs.date;
        }
    });

    emit layoutChanged();
}

Qt::ItemFlags OrderTable::flags(const QModelIndex &index) const
{
    return Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled;
}

void OrderTable::buildRows(const QList<QSharedPointer<Shipment>> &shipments)
{
    m_rows.clear();
    m_rows.reserve(shipments.size());
    addRows(shipments);
}

void OrderTable::addRows(const QList<QSharedPointer<Shipment> > &shipments)
{
    for (const auto &ship : shipments) {
        for (const auto &act : ship->getActivities()) {
            OrderRow row;
            row.date = act.getDateTime().date();
            row.orderId = act.getEventId();
            row.activityId = act.getActivityId();
            row.saleType = toString(act.getSaleType());
            row.countryFrom = act.getCountryCodeFrom();
            row.countryTo = act.getCountryCodeTo();
            row.vatPaidTo = act.getCountryCodeVatPaidTo();
            row.isCompany = act.getIsCompany();
            row.taxSource = taxSourceToString(act.getTaxSource());
            row.taxScheme = taxSchemeToString(act.getTaxScheme());
            row.taxJurisdiction = taxJurisdictionLevelToString(act.getTaxJurisdictionLevel());
            row.currency = act.getCurrency();
            row.amountTaxed = act.getAmountTaxed();
            row.vatAmount = act.getAmountTaxes();
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
                addRows(shipments);
            }
        }
    }
}
