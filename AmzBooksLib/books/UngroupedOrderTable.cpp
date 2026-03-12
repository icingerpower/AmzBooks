#include "UngroupedOrderTable.h"
#include "books/Activity.h"
#include "books/ServiceSalesBooksTable.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/ActivitySource.h"

UngroupedOrderTable::UngroupedOrderTable(
        const BooksConnections *bookConnections,
        OrderManager *orderManager,
        const QDir &workingDir,
        QObject *parent)
    : AbstractBooksTable(bookConnections, workingDir, parent)
    , m_orderManager(orderManager)
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

            add(act.getEventId(),
                act.getEventId(),
                orderDate,
                act.getAmountTaxed(),
                act.getCurrency(),
                act.getSubActivityId(),
                shipment->customerAccount(),
                QString(),
                act.getAmountTaxes(),
                act.getCountryCodeTo(),
                act.getCurrency());
        }
    }
}
