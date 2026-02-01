#include "AbstractImporterApi.h"

// QMap<QString, const AbstractImporterApi *> AbstractImporterApi::_IMPORTERS;

QMap<QString, const AbstractImporterApi *> &AbstractImporterApi::getImporters()
{
    static QMap<QString, const AbstractImporterApi *> importers;
    return importers;
}

const QMap<QString, const AbstractImporterApi *> &AbstractImporterApi::ALL_IMPORTERS()
{
    return getImporters();
}

AbstractImporterApi::Recorder::Recorder(const AbstractImporterApi *dataGetter)
{
    getImporters()[dataGetter->getLabel()] = dataGetter;
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToShipments() const
{
    auto s = _settings();
    return qMakePair(s->value("API/ShipmentsFrom").toDateTime(), s->value("API/ShipmentsTo").toDateTime());
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToRefunds() const
{
    auto s = _settings();
    return qMakePair(s->value("API/RefundsFrom").toDateTime(), s->value("API/RefundsTo").toDateTime());
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToAddresses() const
{
    auto s = _settings();
    return qMakePair(s->value("API/AddressesFrom").toDateTime(), s->value("API/AddressesTo").toDateTime());
}

QPair<QDateTime, QDateTime> AbstractImporterApi::datesFromToInvoiceInfos() const
{
    auto s = _settings();
    return qMakePair(s->value("API/InvoiceInfosFrom").toDateTime(), s->value("API/InvoiceInfosTo").toDateTime());
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchShipments(const QDateTime &dateFrom)
{
    auto result = co_await _fetchShipments(dateFrom);
    if (result.errorReturned.isEmpty()) {
        auto s = _settings();
        s->setValue("API/ShipmentsFrom", dateFrom);
        // We might want to update "To" as well based on result, but for now just mimicking existing
    }
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchRefunds(const QDateTime &dateFrom)
{
    auto result = co_await _fetchRefunds(dateFrom);
    if (result.errorReturned.isEmpty()) {
        auto s = _settings();
        s->setValue("API/RefundsFrom", dateFrom);
    }
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchAddresses(const QDateTime &dateFrom)
{
    auto result = co_await _fetchAddresses(dateFrom);
    if (result.errorReturned.isEmpty()) {
        auto s = _settings();
        s->setValue("API/AddressesFrom", dateFrom);
    }
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> AbstractImporterApi::fetchInvoiceInfos(const QDateTime &dateFrom)
{
    auto result = co_await _fetchInvoiceInfos(dateFrom);
    if (result.errorReturned.isEmpty()) {
        auto s = _settings();
        s->setValue("API/InvoiceInfosFrom", dateFrom);
    }
    co_return result;
}
