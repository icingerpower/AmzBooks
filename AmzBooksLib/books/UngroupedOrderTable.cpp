#include "UngroupedOrderTable.h"
#include "books/Activity.h"
#include "books/ServiceSalesBooksTable.h"
#include "books/BooksAccountsSalesTable.h"
#include "CountriesEu.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/ActivitySource.h"

UngroupedOrderTable::UngroupedOrderTable(
        const BooksConnections *bookConnections,
        OrderManager *orderManager,
        const QDir &workingDir,
        const BooksAccountsSalesTable *salesTable,
        const QString &companyCountry,
        const QString &companyCurrency,
        QObject *parent)
    : AbstractBooksTable(bookConnections, workingDir, parent)
    , m_orderManager(orderManager)
    , m_salesTable(salesTable)
    , m_companyCountry(companyCountry)
    , m_companyCurrency(companyCurrency)
{
    init();
}

QString UngroupedOrderTable::getId() const
{
    return "UngroupedOrders";
}

void UngroupedOrderTable::load(int year)
{
    if (!m_orderManager) return;

    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    auto filter = [](const ActivitySource *source, const Shipment *shipment) {
        if (!source || !shipment) return false;
        if (AbstractBooksTable::isGroupedOrders(shipment)) return false;
        // Exclude entries created by ServiceSalesBooksTable::createSale
        if (source->channel == ServiceSalesBooksTable::CHANNEL_SALE
                && source->reportOrMethode == ActivitySource::METHOD_USER_ENTRY)
            return false;
        return true;
    };

    auto sourceMap = m_orderManager->getActivitySource_ShipmentAndRefunds(from, to, filter);

    for (auto it = sourceMap.constBegin(); it != sourceMap.constEnd(); ++it) {
        for (auto jt = it.value().constBegin(); jt != it.value().constEnd(); ++jt) {
            const QSharedPointer<Shipment> &shipment = jt.value();
            if (!shipment) continue;

            const QList<Activity> &activities = shipment->getActivities();
            if (activities.isEmpty()) continue;

            const Activity &act = activities.first();
            const QDate orderDate = act.getDateTime().date();

            // Build label: orderId | countryFrom > countryTo [| foreignCurrency] | subActivityId
            QString label = act.getEventId()
                          + QStringLiteral(" | ") + act.getCountryCodeFrom()
                          + QStringLiteral(" > ") + act.getCountryCodeTo();
            if (!m_companyCurrency.isEmpty() && act.getCurrency() != m_companyCurrency) {
                label += QStringLiteral(" | ") + act.getCurrency();
            }
            label += QStringLiteral(" | ") + act.getSubActivityId();

            // Resolve account2 = clientAccount from BooksAccountsSalesTable
            QString clientAccount;
            if (m_salesTable && !m_companyCountry.isEmpty()) {
                const VatCountries vc = m_salesTable->resolveVatCountries(
                    act.getTaxScheme(), m_companyCountry,
                    act.getCountryCodeFrom(), act.getCountryCodeTo());
                const double vatRatePct = act.getVatRate() * 100.0;
                const auto acc = m_salesTable->getAccountsIfPresent(vc, vatRatePct, act.getSaleType());
                clientAccount = acc.clientAccount;
            }
            if (clientAccount.isEmpty()) {
                clientAccount = CountriesEu::all().contains(act.getCountryCodeTo())
                                ? QStringLiteral("CCLIENTEU")
                                : QStringLiteral("CLIENTDOM");
            }

            add(act.getEventId(),
                act.getEventId(),
                orderDate,
                act.getAmountTaxed(),
                act.getCurrency(),
                label,
                shipment->customerAccount(),
                clientAccount,
                act.getAmountTaxes(),
                act.getCountryCodeTo(),
                act.getCurrency());
        }
    }
}
