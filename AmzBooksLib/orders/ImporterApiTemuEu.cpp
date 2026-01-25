#include "ImporterApiTemuEu.h"
#include <QDateTime>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>

QString ImporterApiTemuEu::getEndpoint() const
{
    return "https://openapi-eu.temu.com";
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchShipments(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    result.orderInfos->dateMin = QDate();
    result.orderInfos->dateMax = QDate();
    
    QList<ShopConfig> shops = getShops();
    QStringList errors;
    
    for (const auto& shop : shops) {
        try {
            co_await fetchShopOrders(shop, dateFrom, result.orderInfos);
        } catch (const std::exception& e) {
            errors.append(QString("Shop '%1' error: %2").arg(shop.name, e.what()));
        }
    }
    
    if (!errors.isEmpty()) {
        if (shops.size() > 1) {
             result.errorReturned = QString("Partial failure: %1").arg(errors.join("; "));
        } else {
             result.errorReturned = errors.first();
        }
    }
    
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchRefunds(const QDateTime &dateFrom)
{
    // Placeholder: similar structure to shipments, fetching returns/refunds
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchAddresses(const QDateTime &dateFrom)
{
     ReturnOrderInfos result;
     result.orderInfos = QSharedPointer<OrderInfos>::create();
     co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiTemuEu::_fetchInvoiceInfos(const QDateTime &dateFrom)
{
     ReturnOrderInfos result;
     result.orderInfos = QSharedPointer<OrderInfos>::create();
     co_return result;
}
