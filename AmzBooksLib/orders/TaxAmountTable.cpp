#include "TaxAmountTable.h"
#include "orders/Shipment.h"
#include "CurrencyRateManager.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"

const QStringList TaxAmountTable::COL_NAMES = {
    QObject::tr("Declaring Country"),
    QObject::tr("Tax Scheme"),
    QObject::tr("Jurisdiction"),
    QObject::tr("VAT Paid To"),
    QObject::tr("Amount Untaxed"),
    QObject::tr("Taxes"),
    QObject::tr("Total")
};

TaxAmountTable::TaxAmountTable(
        const QList<QSharedPointer<Shipment>> &shipments
        , const CurrencyRateManager *currencyRateManager
        , const QString &destCurrency, QObject *parent)
    : QAbstractTableModel(parent)
    , m_currencyRateManager(currencyRateManager)
    , m_destCurrency(destCurrency)
{
    buildRows(shipments);
}

TaxAmountTable::TaxAmountTable(
        const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data
        , const CurrencyRateManager *currencyRateManager
        , const QString &destCurrency
        , QObject *parent)
    : QAbstractTableModel(parent)
    , m_currencyRateManager(currencyRateManager)
    , m_destCurrency(destCurrency)
{
    buildRows(data);
}

int TaxAmountTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int TaxAmountTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return COL_COUNT;
}

QVariant TaxAmountTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();

    const TaxRow &row = m_rows.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case COL_TAX_DECLARING_COUNTRY: return row.taxDeclaringCountry;
        case COL_TAX_SCHEME: return row.taxScheme;
        case COL_TAX_JURISDICTION: return row.taxJurisdiction;
        case COL_VAT_PAID_TO: return row.vatPaidTo;
        case COL_AMOUNT_UNTAXED: return row.amountUntaxed;
        case COL_AMOUNT_TAXES: return row.amountTaxes;
        case COL_AMOUNT_TOTAL: return row.amountTotal;
        }
    }
    return QVariant();
}

QVariant TaxAmountTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < COL_NAMES.size())
            return COL_NAMES[section];
    }
    return QVariant();
}

void TaxAmountTable::sort(int column, Qt::SortOrder order)
{
    emit layoutAboutToBeChanged();

    std::sort(m_rows.begin(), m_rows.end(), [column, order](const TaxRow &a, const TaxRow &b) {
        bool less = false;
        switch (column) {
        case COL_TAX_DECLARING_COUNTRY: less = a.taxDeclaringCountry.compare(b.taxDeclaringCountry, Qt::CaseInsensitive) < 0; break;
        case COL_TAX_SCHEME: less = a.taxScheme.compare(b.taxScheme, Qt::CaseInsensitive) < 0; break;
        case COL_TAX_JURISDICTION: less = a.taxJurisdiction.compare(b.taxJurisdiction, Qt::CaseInsensitive) < 0; break;
        case COL_VAT_PAID_TO: less = a.vatPaidTo.compare(b.vatPaidTo, Qt::CaseInsensitive) < 0; break;
        case COL_AMOUNT_UNTAXED: less = a.amountUntaxed < b.amountUntaxed; break;
        case COL_AMOUNT_TAXES: less = a.amountTaxes < b.amountTaxes; break;
        case COL_AMOUNT_TOTAL: less = a.amountTotal < b.amountTotal; break;
        default: less = a.amountTotal < b.amountTotal; break;
        }
        return (order == Qt::AscendingOrder) ? less : !less;
    });

    emit layoutChanged();
}

void TaxAmountTable::aggregate(const TaxResolver::TaxContext &ctx, const Shipment *shipment)
{
    if (!m_aggregationMap.contains(ctx)) {
        TaxRow row;
        row.taxDeclaringCountry = ctx.taxDeclaringCountryCode;
        row.taxScheme = taxSchemeToString(ctx.taxScheme);
        row.taxJurisdiction = taxJurisdictionLevelToString(ctx.taxJurisdictionLevel);
        row.vatPaidTo = ctx.countryCodeVatPaidTo;
        m_aggregationMap[ctx] = row;
    }

    TaxRow &row = m_aggregationMap[ctx];
    
    for (const auto &act : shipment->getActivities()) {
        double amountUntaxed = act.getAmountUntaxed();
        double amountTaxes = act.getAmountTaxes();
        
        // Convert if needed
        if (m_currencyRateManager) {
            if (act.getCurrency() != m_destCurrency) {
                 double rate = m_currencyRateManager->rate(act.getCurrency(), m_destCurrency, act.getDateTime().date());
                 amountUntaxed *= rate;
                 amountTaxes *= rate;
            }
        }
        
        row.amountUntaxed += amountUntaxed;
        row.amountTaxes += amountTaxes;
        row.amountTotal += (amountUntaxed + amountTaxes);
    }
}

void TaxAmountTable::buildRows(const QList<QSharedPointer<Shipment>> &shipments)
{
    m_aggregationMap.clear();
    m_rows.clear();

    for (const auto &ship : shipments) {
         if (ship->getActivities().isEmpty()) continue;
         const auto &act = ship->getActivities().first();
         
         TaxResolver::TaxContext ctx;
         ctx.taxDeclaringCountryCode = act.getTaxDeclaringCountryCode();
         ctx.taxScheme = act.getTaxScheme();
         ctx.taxJurisdictionLevel = act.getTaxJurisdictionLevel();
         ctx.countryCodeVatPaidTo = act.getCountryCodeVatPaidTo();
         
         aggregate(ctx, ship.data());
    }
    
    m_rows = m_aggregationMap.values();
}

void TaxAmountTable::buildRows(const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data)
{
    m_aggregationMap.clear();
    m_rows.clear();
    if (!data) return;

    for (auto itChannel = data->constBegin(); itChannel != data->constEnd(); ++itChannel) {
        for (auto itSite = itChannel.value().constBegin(); itSite != itChannel.value().constEnd(); ++itSite) {
            for (auto itContext = itSite.value().constBegin(); itContext != itSite.value().constEnd(); ++itContext) {
                 const TaxResolver::TaxContext &ctx = itContext.key();
                 const auto &shipments = itContext.value().shipmentsRefundsSameActivity;
                 
                 for (const auto &ship : shipments) {
                     aggregate(ctx, ship.data());
                 }
            }
        }
    }
    
    m_rows = m_aggregationMap.values();
}
