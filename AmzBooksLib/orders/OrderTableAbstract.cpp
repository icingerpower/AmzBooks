#include "OrderTableAbstract.h"
#include "orders/Shipment.h"
#include "orders/SaleType.h"
#include "orders/TaxSource.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"
#include "books/Activity.h"

OrderTableAbstract::OrderTableAbstract(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int OrderTableAbstract::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

Qt::ItemFlags OrderTableAbstract::flags(const QModelIndex &) const
{
    return Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled;
}

OrderTableAbstract::OrderRow OrderTableAbstract::makeRow(const Activity &act, const QSharedPointer<Shipment> &ship)
{
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
    row.vatAmount = act.getAmountTaxesSource();
    row.invoiceId = act.getInvoiceId();
    row.sourceShipment = ship;
    return row;
}
