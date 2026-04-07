#include "TaxAmountTable.h"
#include "orders/Shipment.h"
#include "CurrencyRateManager.h"
#include "books/TaxScheme.h"
#include "books/TaxJurisdictionLevel.h"
#include <QFont>
#include <QColor>

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
        , const QString &destCurrency
        , const QString &companyCountryCode
        , QObject *parent)
    : QAbstractTableModel(parent)
    , m_currencyRateManager(currencyRateManager)
    , m_destCurrency(destCurrency)
    , m_companyCountryCode(companyCountryCode)
{
    buildRows(shipments);
}

TaxAmountTable::TaxAmountTable(
        const QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> &data
        , const CurrencyRateManager *currencyRateManager
        , const QString &destCurrency
        , const QString &companyCountryCode
        , QObject *parent)
    : QAbstractTableModel(parent)
    , m_currencyRateManager(currencyRateManager)
    , m_destCurrency(destCurrency)
    , m_companyCountryCode(companyCountryCode)
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

int TaxAmountTable::getNumberTotalRows() const
{
    return 3;
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
    if (role == Qt::FontRole && index.row() < getNumberTotalRows()) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::BackgroundRole && index.row() >= getNumberTotalRows()) {
        if (row.colorBand == 1)
            return QColor(55, 55, 60);
        return QVariant();
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
    const int nTotal = getNumberTotalRows();
    if (m_rows.size() <= nTotal)
        return;

    emit layoutAboutToBeChanged();

    // Keep the total rows (indices 0..nTotal-1) pinned; sort only the detail rows.
    QList<TaxRow> totalRows = m_rows.mid(0, nTotal);
    m_rows = m_rows.mid(nTotal);

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

    for (int i = nTotal - 1; i >= 0; --i)
        m_rows.prepend(totalRows[i]);

    assignColorBands();
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
        row.taxSchemeEnum = ctx.taxScheme;
        m_aggregationMap[ctx] = row;
    }

    TaxRow &row = m_aggregationMap[ctx];

    for (const auto &act : shipment->getActivities()) {
        double amountUntaxed = act.getAmountUntaxed();
        double amountTaxes = act.getAmountTaxes();

        // Convert if needed.
        // Use the last day of the activity's month as the exchange-rate date so
        // that the displayed totals match what the bookkeeping CSV will contain
        // (createEntryOssIoss also uses the month-end date for conversions).
        if (m_currencyRateManager) {
            if (act.getCurrency() != m_destCurrency) {
                const QDate actDate = act.getDateTime().date();
                const QDate rateDate(actDate.year(), actDate.month(), actDate.daysInMonth());
                const double rate = m_currencyRateManager->rate(act.getCurrency(), m_destCurrency, rateDate);
                amountUntaxed *= rate;
                amountTaxes *= rate;
            }
        }

        row.amountUntaxed += amountUntaxed;
        row.amountTaxes += amountTaxes;
        row.amountTotal += (amountUntaxed + amountTaxes);
    }
}

void TaxAmountTable::applyDefaultSort()
{
    const QString &companyCC = m_companyCountryCode;
    std::sort(m_rows.begin(), m_rows.end(), [&companyCC](const TaxRow &a, const TaxRow &b) {
        // Priority: 0 = DomesticVat of company country, 1 = other DomesticVat, 2 = everything else
        auto priority = [&companyCC](const TaxRow &r) -> int {
            if (r.taxSchemeEnum == TaxScheme::DomesticVat) {
                if (!companyCC.isEmpty() && r.taxDeclaringCountry == companyCC)
                    return 0;
                return 1;
            }
            return 2;
        };
        int pa = priority(a);
        int pb = priority(b);
        if (pa != pb)
            return pa < pb;
        // Within the same priority group: highest VAT to pay first
        return a.amountTaxes > b.amountTaxes;
    });
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
    applyDefaultSort();
    prependTotalRows();
    assignColorBands();
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
    applyDefaultSort();
    prependTotalRows();
    assignColorBands();
}

void TaxAmountTable::prependTotalRows()
{
    TaxRow iossTotal;
    iossTotal.taxDeclaringCountry = tr("Total IOSS");
    iossTotal.isTotalRow = true;

    TaxRow ossTotal;
    ossTotal.taxDeclaringCountry = tr("Total OSS");
    ossTotal.isTotalRow = true;

    TaxRow total;
    total.taxDeclaringCountry = tr("Total");
    total.isTotalRow = true;

    for (const TaxRow &r : m_rows) {
        total.amountUntaxed += r.amountUntaxed;
        total.amountTaxes   += r.amountTaxes;
        total.amountTotal   += r.amountTotal;

        if (r.taxSchemeEnum == TaxScheme::EuOssUnion || r.taxSchemeEnum == TaxScheme::EuOssNonUnion) {
            ossTotal.amountUntaxed += r.amountUntaxed;
            ossTotal.amountTaxes   += r.amountTaxes;
            ossTotal.amountTotal   += r.amountTotal;
        } else if (r.taxSchemeEnum == TaxScheme::EuIoss) {
            iossTotal.amountUntaxed += r.amountUntaxed;
            iossTotal.amountTaxes   += r.amountTaxes;
            iossTotal.amountTotal   += r.amountTotal;
        }
    }

    // Prepend in reverse order so final layout is: Total(0), Total OSS(1), Total IOSS(2)
    m_rows.prepend(iossTotal);
    m_rows.prepend(ossTotal);
    m_rows.prepend(total);
}

void TaxAmountTable::assignColorBands()
{
    const int nTotal = getNumberTotalRows();
    int band = 0;
    TaxScheme lastScheme = TaxScheme::Unknown;
    bool first = true;
    for (int i = nTotal; i < m_rows.size(); ++i) {
        TaxRow &row = m_rows[i];
        if (!first && row.taxSchemeEnum != lastScheme)
            band = 1 - band;
        lastScheme = row.taxSchemeEnum;
        first = false;
        row.colorBand = band;
    }
}
