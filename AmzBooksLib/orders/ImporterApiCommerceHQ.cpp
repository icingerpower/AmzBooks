#include "ImporterApiCommerceHQ.h"
#include "ExceptionWithTitleText.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QCoroNetworkReply>

DECLARE_IMPORTER_API(ImporterApiCommerceHQ)

ActivitySource ImporterApiCommerceHQ::getActivitySource() const
{
    return ActivitySource {
        .type = ActivitySourceType::API,
        .channel = "CommerceHQ",
        .subchannel = "",
        .reportOrMethode = "API"
    };
}

QString ImporterApiCommerceHQ::getLabel() const
{
    return "CommerceHQ API";
}

QString ImporterApiCommerceHQ::getId() const
{
    return "CommerceHQ";
}

bool ImporterApiCommerceHQ::isGroupedOrders() const
{
    return false;
}

bool ImporterApiCommerceHQ::recomputeTaxes() const
{
    return true;
}

bool ImporterApiCommerceHQ::isWrongIfConflict() const
{
    return true;
}

bool ImporterApiCommerceHQ::fixRefundDate() const
{
    return true;
}

QMap<QString, AbstractImporter::ParamInfo> ImporterApiCommerceHQ::getRequiredParams() const
{
    QMap<QString, ParamInfo> params;
    
    auto arrayValidator = [](const QVariant& v) -> std::pair<bool, QString> {
        QString str = v.toString();
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(str.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            return {false, "Invalid JSON: " + error.errorString()};
        }
        if (!doc.isArray()) {
            return {false, "Value must be a JSON array of strings"};
        }
         // Check if all elements are strings
        QJsonArray arr = doc.array();
        for (const auto& kv : arr) {
            if (!kv.isString()) {
                return {false, "All elements in the array must be strings"};
            }
        }
        return {true, ""};
    };

    params["storeNames"] = ParamInfo {
        .key = "storeNames",
        .label = "Store Names (JSON Array)",
        .description = "List of names: [\"Store1\", \"Store2\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };

    params["storeIds"] = ParamInfo {
        .key = "storeIds",
        .label = "Store IDs (JSON Array)",
        .description = "List of Store IDs: [\"999\", \"888\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };

    params["apiKeys"] = ParamInfo {
        .key = "apiKeys",
        .label = "API Keys (JSON Array)",
        .description = "List of Keys: [\"abc\", \"def\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };

    params["apiPasswords"] = ParamInfo {
        .key = "apiPasswords",
        .label = "API Passwords (JSON Array)",
        .description = "List of Passwords: [\"pass1\", \"pass2\"]",
        .defaultValue = "[]",
        .value = QVariant(),
        .validator = arrayValidator
    };
    
    return params;
}

QList<ImporterApiCommerceHQ::StoreConfig> ImporterApiCommerceHQ::getStores() const
{
    QList<StoreConfig> stores;
    
    auto parseArray = [this](const QString& key) -> QStringList {
        QString jsonStr = getParam(key).toString();
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QStringList list;
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const auto& val : arr) {
                list.append(val.toString());
            }
        }
        return list;
    };

    QStringList names = parseArray("storeNames");
    QStringList storeIds = parseArray("storeIds");
    QStringList apiKeys = parseArray("apiKeys");
    QStringList apiPasswords = parseArray("apiPasswords");

    qsizetype count = std::min({names.size(), storeIds.size(), apiKeys.size(), apiPasswords.size()});

    for (qsizetype i = 0; i < count; ++i) {
        StoreConfig store;
        store.name = names[i];
        store.storeId = storeIds[i];
        store.apiKey = apiKeys[i];
        store.apiPassword = apiPasswords[i];
        
        if (!store.name.isEmpty() && !store.apiKey.isEmpty()) {
            stores.append(store);
        }
    }
    return stores;
}

QString ImporterApiCommerceHQ::getEndpoint() const
{
    return "https://api.commercehq.com"; 
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiCommerceHQ::_fetchShipments(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    result.orderInfos->dateMin = QDate();
    result.orderInfos->dateMax = QDate();
    
    QList<StoreConfig> stores = getStores();
    QStringList errors;
    
    for (const auto& store : stores) {
        try {
            co_await fetchStoreOrders(store, dateFrom, result.orderInfos);
        } catch (const std::exception& e) {
            errors.append(QString("Store '%1' error: %2").arg(store.name, e.what()));
        }
    }
    
    if (!errors.isEmpty()) {
         if (stores.size() > 1) {
             result.errorReturned = QString("Partial failure: %1").arg(errors.join("; "));
         } else {
             result.errorReturned = errors.first();
         }
    }
    
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiCommerceHQ::_fetchRefunds(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiCommerceHQ::_fetchAddresses(const QDateTime &dateFrom)
{
     ReturnOrderInfos result;
     result.orderInfos = QSharedPointer<OrderInfos>::create();
     co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiCommerceHQ::_fetchInvoiceInfos(const QDateTime &dateFrom)
{
     ReturnOrderInfos result;
     result.orderInfos = QSharedPointer<OrderInfos>::create();
     co_return result;
}

QCoro::Task<void> ImporterApiCommerceHQ::fetchStoreOrders(const StoreConfig& store, const QDateTime& dateFrom, QSharedPointer<OrderInfos> targetInfos)
{
    const QString baseUrl = getEndpoint();
    const QString apiPath = "/orders";
    const int perPage = 50;
    int page = 1;
    bool hasMore = true;

    // Basic Auth — credentials must be UTF-8 encoded before base64 (RFC 7617)
    const QString credentials = store.apiKey + ":" + store.apiPassword;
    const QByteArray authHeader = "Basic " + credentials.toUtf8().toBase64();

    while (hasMore) {
        QUrl url(baseUrl + apiPath);
        QUrlQuery query;
        query.addQueryItem("store_id", store.storeId);
        query.addQueryItem("created_at_min", dateFrom.toUTC().toString(Qt::ISODate));
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("per_page", QString::number(perPage));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("Authorization", authHeader);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *reply = nam()->get(request);
        co_await reply;

        const QNetworkReply::NetworkError netErr = reply->error();
        const QString errStr = reply->errorString();
        const QByteArray responseData = reply->readAll();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError) {
            ExceptionWithTitleText exception("API Request Failed", "API Request failed: " + errStr);
            exception.raise();
        }

        const QJsonDocument doc = QJsonDocument::fromJson(responseData);

        QJsonArray orders;
        if (doc.isArray()) {
            orders = doc.array();
        } else if (doc.isObject()) {
            orders = doc.object()["items"].toArray();
        }

        for (const QJsonValue &val : std::as_const(orders)) {
            const QJsonObject order = val.toObject();
            // Prefer the human-facing "number" field; fall back to integer "id"
            QString orderId = order["number"].toString().trimmed();
            if (orderId.isEmpty()) {
                const int idInt = order["id"].toInt(0);
                if (idInt > 0) {
                    orderId = QString::number(idInt);
                }
            }
            if (!orderId.isEmpty()) {
                targetInfos->orderId_infos[orderId] =
                    OrderManager::OrderInfo{"CommerceHQ", isGroupedOrders(), ""};
            }
        }

        hasMore = orders.size() >= perPage;
        ++page;
    }
}


QNetworkAccessManager *ImporterApiCommerceHQ::nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(nullptr);
    }
    return m_nam;
}
