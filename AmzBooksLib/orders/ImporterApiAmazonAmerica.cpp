#include "ImporterApiAmazonAmerica.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>

QString ImporterApiAmazonAmerica::getEndpoint() const
{
    return "https://sellingpartnerapi-na.amazon.com";
}

QString ImporterApiAmazonAmerica::getRegion() const
{
    // NA region includes US, CA, MX, BR. Endpoint is usually us-east-1 for NA.
    return "us-east-1"; 
}

QString ImporterApiAmazonAmerica::getMarketplaceId() const
{
    // Default to US
    return "ATVPDKIKX0DER"; 
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonAmerica::_fetchShipments(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    
    QString path = "/orders/v0/orders";
    
    QUrlQuery query;
    query.addQueryItem("MarketplaceIds", getMarketplaceId());
    query.addQueryItem("CreatedAfter", dateFrom.toUTC().toString(Qt::ISODate));
    
    try {
        QByteArray response = co_await sendSignedRequest("GET", path, query);
        QJsonDocument doc = QJsonDocument::fromJson(response);
        QJsonObject root = doc.object();
        
        if (root.contains("payload")) {
            QJsonObject payload = root["payload"].toObject();
            QJsonArray orders = payload["Orders"].toArray();
            
            for (const auto& val : orders) {
                QJsonObject order = val.toObject();
                // Placeholder for conversion logic
            }
             // Pagination logic would mirror EU logic
        } else {
             result.errorReturned = "Invalid response format: missing payload";
        }
        
    } catch (const std::exception& e) {
        result.errorReturned = QString::fromStdString(e.what());
    }
    
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonAmerica::_fetchRefunds(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    
    QString path = "/finances/v0/financialEvents";
    QUrlQuery query;
    query.addQueryItem("PostedAfter", dateFrom.toUTC().toString(Qt::ISODate));
    
    try {
        QByteArray response = co_await sendSignedRequest("GET", path, query);
        // Parse finances...
    } catch (const std::exception& e) {
        result.errorReturned = QString::fromStdString(e.what());
    }
    
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonAmerica::_fetchAddresses(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    // Leaving empty as PII requires Restricted Data Token
    co_return result; 
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonAmerica::_fetchInvoiceInfos(const QDateTime &dateFrom)
{
     ReturnOrderInfos result;
     result.orderInfos = QSharedPointer<OrderInfos>::create();
     co_return result;
}
