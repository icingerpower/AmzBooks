#include "ImporterApiAmazonAmerica.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDebug>

DECLARE_IMPORTER_API(ImporterApiAmazonAmerica)

QString ImporterApiAmazonAmerica::getEndpoint() const
{
    return "https://sellingpartnerapi-na.amazon.com";
}

QString ImporterApiAmazonAmerica::getLabel() const
{
    return "Amazon SP-API (NA)";
}

QString ImporterApiAmazonAmerica::getId() const
{
    return "AmazonSpApiNa";
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

    const QString path = "/orders/v0/orders";

    QUrlQuery query;
    query.addQueryItem("MarketplaceIds", getMarketplaceId());
    query.addQueryItem("CreatedAfter", dateFrom.toUTC().toString(Qt::ISODate));

    try {
        bool hasMore = true;
        while (hasMore) {
            QByteArray response = co_await sendApiRequest("GET", path, query);
            const QJsonObject root = QJsonDocument::fromJson(response).object();

            if (!root.contains("payload")) {
                result.errorReturned = "Invalid response format: missing payload";
                break;
            }

            const QJsonObject payload = root["payload"].toObject();
            const QJsonArray orders = payload["Orders"].toArray();

            for (const QJsonValue &val : std::as_const(orders)) {
                const QJsonObject order = val.toObject();
                const QString orderId = order["AmazonOrderId"].toString();
                if (orderId.isEmpty()) {
                    continue;
                }
                result.orderInfos->orderId_infos[orderId] =
                    OrderManager::OrderInfo{getMarketplaceId(), isGroupedOrders(), ""};
            }

            // Per SP-API spec, when paging with NextToken only pass NextToken
            const QString nextToken = payload["NextToken"].toString();
            if (nextToken.isEmpty() || orders.isEmpty()) {
                hasMore = false;
            } else {
                query.clear();
                query.addQueryItem("NextToken", nextToken);
            }
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

    const QString path = "/finances/v0/financialEvents";
    QUrlQuery query;
    query.addQueryItem("PostedAfter", dateFrom.toUTC().toString(Qt::ISODate));

    try {
        QByteArray response = co_await sendApiRequest("GET", path, query);
        const QJsonObject root = QJsonDocument::fromJson(response).object();

        if (root.contains("payload")) {
            const QJsonObject payload = root["payload"].toObject();
            const QJsonObject events = payload["FinancialEvents"].toObject();
            const QJsonArray refundEvents = events["RefundEventList"].toArray();

            for (const QJsonValue &val : std::as_const(refundEvents)) {
                const QJsonObject ev = val.toObject();
                const QString orderId = ev["AmazonOrderId"].toString();
                if (orderId.isEmpty()) {
                    continue;
                }

                const QDate date = QDate::fromString(
                    ev["PostedDate"].toString().left(10), "yyyy-MM-dd");

                // For NA (US/CA/MX), Amazon acts as marketplace facilitator for taxes.
                // Only the Principal (net product charge) is the seller's responsibility;
                // any Tax charge must NOT be included.
                double principal = 0.0;
                QString currency;
                const QJsonArray items = ev["ShipmentItemAdjustmentList"].toArray();
                for (const QJsonValue &itemVal : std::as_const(items)) {
                    const QJsonArray charges =
                        itemVal.toObject()["ItemChargeAdjustmentList"].toArray();
                    for (const QJsonValue &chargeVal : std::as_const(charges)) {
                        const QJsonObject charge = chargeVal.toObject();
                        if (charge["ChargeType"].toString() == "Principal") {
                            const QJsonObject amt = charge["ChargeAmount"].toObject();
                            principal += amt["Amount"].toString().toDouble();
                            if (currency.isEmpty()) {
                                currency = amt["CurrencyCode"].toString();
                            }
                        }
                    }
                }

                if (!qFuzzyIsNull(principal)) {
                    result.orderInfos->orderId_refundClues[orderId].append(
                        {qAbs(principal), currency, date});
                }
            }
        }
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

