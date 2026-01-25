#include "AbstractImporterApi.h"
#include <algorithm>

// ... helper ...
static void updateDateRange(AbstractImporter::OrderInfos& infos) {
    QDateTime minDateTime;
    QDateTime maxDateTime;
    
    for (const auto& shipment : infos.shipments) {
        for (const auto& activity : shipment.getActivities()) {
             QDateTime dt = activity.getDateTime();
             if (dt.isValid()) {
                 if (!minDateTime.isValid() || dt < minDateTime) minDateTime = dt;
                 if (!maxDateTime.isValid() || dt > maxDateTime) maxDateTime = dt;
             }
        }
    }
    for (const auto& refund : infos.refunds) {
        for (const auto& activity : refund.getActivities()) {
             QDateTime dt = activity.getDateTime();
             if (dt.isValid()) {
                 if (!minDateTime.isValid() || dt < minDateTime) minDateTime = dt;
                 if (!maxDateTime.isValid() || dt > maxDateTime) maxDateTime = dt;
             }
        }
    }
    
    if (minDateTime.isValid()) infos.dateMin = minDateTime.date();
    if (maxDateTime.isValid()) infos.dateMax = maxDateTime.date();
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToShipments() const
{
    auto s = _settings();
    return qMakePair(s->value("Shipments/ImportedFrom").toDateTime(), s->value("Shipments/ImportedTo").toDateTime());
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToRefunds() const
{
    auto s = _settings();
    return qMakePair(s->value("Refunds/ImportedFrom").toDateTime(), s->value("Refunds/ImportedTo").toDateTime());
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToAddresses() const
{
    auto s = _settings();
    return qMakePair(s->value("Addresses/ImportedFrom").toDateTime(), s->value("Addresses/ImportedTo").toDateTime());
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToInvoiceInfos() const
{
    auto s = _settings();
    return qMakePair(s->value("InvoiceInfos/ImportedFrom").toDateTime(), s->value("InvoiceInfos/ImportedTo").toDateTime());
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchShipments(const QDateTime &dateFrom)
{
    auto result = co_await _fetchShipments(dateFrom);
    if (result.errorReturned.isEmpty() && result.orderInfos && !result.orderInfos->shipments.isEmpty()) {
        updateDateRange(*result.orderInfos);
        
        auto s = _settings();
        if (!s->value("Shipments/ImportedFrom").isValid()) {
             s->setValue("Shipments/ImportedFrom", dateFrom);
        } else {
             QDateTime currentFrom = s->value("Shipments/ImportedFrom").toDateTime();
             if (dateFrom < currentFrom) s->setValue("Shipments/ImportedFrom", dateFrom);
        }
        
        if (result.orderInfos->dateMax.isValid()) {
            QDateTime maxDate = result.orderInfos->dateMax.startOfDay();
            QDateTime currentTo = s->value("Shipments/ImportedTo").toDateTime();
            if (!currentTo.isValid() || maxDate > currentTo) {
                s->setValue("Shipments/ImportedTo", maxDate);
            }
        }
    }
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchRefunds(const QDateTime &dateFrom)
{
    auto result = co_await _fetchRefunds(dateFrom);
    if (result.errorReturned.isEmpty() && result.orderInfos && !result.orderInfos->refunds.isEmpty()) {
        updateDateRange(*result.orderInfos);
        
        auto s = _settings();
        if (!s->value("Refunds/ImportedFrom").isValid()) {
             s->setValue("Refunds/ImportedFrom", dateFrom);
        } else {
             QDateTime currentFrom = s->value("Refunds/ImportedFrom").toDateTime();
             if (dateFrom < currentFrom) s->setValue("Refunds/ImportedFrom", dateFrom);
        }
        
        if (result.orderInfos->dateMax.isValid()) {
            QDateTime maxDate = result.orderInfos->dateMax.startOfDay();
            QDateTime currentTo = s->value("Refunds/ImportedTo").toDateTime();
            if (!currentTo.isValid() || maxDate > currentTo) {
                s->setValue("Refunds/ImportedTo", maxDate);
            }
        }
    }
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchAddresses(const QDateTime &dateFrom)
{
    auto result = co_await _fetchAddresses(dateFrom);
    if (result.errorReturned.isEmpty() && result.orderInfos && !result.orderInfos->orderAddresses.isEmpty()) {
        updateDateRange(*result.orderInfos);
        
        auto s = _settings();
        if (!s->value("Addresses/ImportedFrom").isValid()) {
             s->setValue("Addresses/ImportedFrom", dateFrom);
        } else {
             QDateTime currentFrom = s->value("Addresses/ImportedFrom").toDateTime();
             if (dateFrom < currentFrom) s->setValue("Addresses/ImportedFrom", dateFrom);
        }
        // Since Address has no date, we fall back to currentDateTime or rely on strict API behavior?
        // User instruction was: "check the last date time in OrderInfos in case there is a latency."
        // For addresses, we can't check. 
        // We will assume currentDateTime is the best we can do for now, 
        // OR we don't update ImportedTo if we can't verify.
        // But preventing updates might break incremental fetch.
        // For now, retaining currentDateTime but using getMaxDate if partially available.
         
        if (result.orderInfos->dateMax.isValid()) {
             QDateTime maxDate = result.orderInfos->dateMax.startOfDay();
             QDateTime currentTo = s->value("Addresses/ImportedTo").toDateTime();
             if (!currentTo.isValid() || maxDate > currentTo) {
                 s->setValue("Addresses/ImportedTo", maxDate);
             }
        } else {
             // Fallback if no specific date found (e.g. only addresses returned)
             s->setValue("Addresses/ImportedTo", QDateTime::currentDateTime());
        }
    }
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchInvoiceInfos(const QDateTime &dateFrom)
{
    auto result = co_await _fetchInvoiceInfos(dateFrom);
    if (result.errorReturned.isEmpty() && result.orderInfos && !result.orderInfos->invoicingInfos.isEmpty()) {
        updateDateRange(*result.orderInfos);
        
        auto s = _settings();
        if (!s->value("InvoiceInfos/ImportedFrom").isValid()) {
             s->setValue("InvoiceInfos/ImportedFrom", dateFrom);
        } else {
             QDateTime currentFrom = s->value("InvoiceInfos/ImportedFrom").toDateTime();
             if (dateFrom < currentFrom) s->setValue("InvoiceInfos/ImportedFrom", dateFrom);
        }
        
        if (result.orderInfos->dateMax.isValid()) {
            QDateTime maxDate = result.orderInfos->dateMax.startOfDay();
            QDateTime currentTo = s->value("InvoiceInfos/ImportedTo").toDateTime();
            if (!currentTo.isValid() || maxDate > currentTo) {
                s->setValue("InvoiceInfos/ImportedTo", maxDate);
            }
        } else {
            // Fallback
            s->setValue("InvoiceInfos/ImportedTo", QDateTime::currentDateTime());
        }
    }
    co_return result;
}
