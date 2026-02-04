
void ServiceSalesBooksTable::load(int year)
{
    if (!m_orderManager) {
        return;
    }

    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    // Lambda to accept only our ManualService activities
    auto filter = [](const ActivitySource* source, const Shipment*) {
        if (!source) return false;
        return source->sourceType == ActivitySourceType::API
               && source->channel == "ManualService"
               && source->reportOrMethodType == "UserEntry";
    };

    auto shipmentsMap = m_orderManager->getShipmentAndRefunds(from, to, filter);

    // Iterate map (Key is DateTime, Value is Shipment)
    for (auto it = shipmentsMap.constBegin(); it != shipmentsMap.constEnd(); ++it) {
        const QSharedPointer<Shipment> &shipment = it.value();
        // We know these fit our criteria.
        // Reconstruct the add() call.
        // We need: orderId (shipment->orderId), invoiceId (?), date, amount, currency, label, etc.
        // In createSale we used:
        // orderId = "Service-{Date}-{ClientName}" (or similar)
        // invoiceId was passed as param. Where is it stored?
        // In Activity we might store things.
        // Let's check ServiceSalesBooksTable::createSale again to see how it records.
        
        // Actually, createSale called:
        // Activity::create(..., invoiceId (as externalId?), ...)
        // Let's look at createSale implementation in previous turn or logic.
        
        // Assuming Order structure:
        // Shipment has Activities.
        // We get the Shipment. We need to fetch its Activities.
        // Shipment contains a list of Activity?
        // Wait, OrderManager::getShipmentAndRefunds returns Shipments.
        // A Shipment has items.
        // ServiceSalesBooksTable::createSale constructs an Activity.
        // Let's assume the Shipment holds the Activity which holds the details.
        
        // I need to see Shipment class definition or how createSale builds the Shipment.
        // createSale uses Activity::create(SaleType::Service, date, amount, currency, invoiceId...?)
        
        // Let's pause writing this file and check ServiceSalesBooksTable.cpp and Shipment.h first.
    }
}
