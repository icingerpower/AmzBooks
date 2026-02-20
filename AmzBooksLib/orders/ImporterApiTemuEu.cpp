#include "ImporterApiTemuEu.h"
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCoroNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

DECLARE_IMPORTER_API(ImporterApiTemuEu)

QString ImporterApiTemuEu::getEndpoint() const
{
    return "https://openapi-eu.temu.com";
}

QString ImporterApiTemuEu::getId() const
{
    return "TemuEu";
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

bool ImporterApiTemuEu::recomputeTaxes() const
{
    return true;
}

QCoro::Task<void> ImporterApiTemuEu::fetchShopOrders(const ShopConfig& shop, const QDateTime& dateFrom, QSharedPointer<OrderInfos> targetInfos)
{
    // Temu EU Open API — query all order pages since dateFrom and populate orderId_store.
    //
    // Sign algorithm (Temu Open Platform):
    //   1. Collect all non-sign request parameters as key=value pairs
    //   2. Sort them alphabetically by key
    //   3. Prepend and append the App Secret
    //   4. SHA-256 hex of the resulting string (uppercase)

    const QString baseUrl     = getEndpoint();
    const QString apiPath     = "/open_api/order/query_order_list";
    const int     pageSize    = 50;
    int           page        = 1;
    bool          hasMore     = true;

    while (hasMore) {
        const qint64 timestamp = QDateTime::currentSecsSinceEpoch();

        // Collect parameters (sorted below for the signature)
        QMap<QString, QString> params;
        params["app_key"]      = shop.appId;
        params["access_token"] = shop.accessToken;
        params["timestamp"]    = QString::number(timestamp);
        params["start_time"]   = QString::number(dateFrom.toSecsSinceEpoch());
        params["page_no"]      = QString::number(page);
        params["page_size"]    = QString::number(pageSize);

        // Build signature: secret + sorted(k+v) + secret → SHA-256 hex uppercase
        QString signInput = shop.appSecret;
        for (auto it = params.cbegin(); it != params.cend(); ++it)
            signInput += it.key() + it.value();
        signInput += shop.appSecret;

        const QString sign = QString::fromLatin1(
            QCryptographicHash::hash(signInput.toUtf8(), QCryptographicHash::Sha256).toHex()).toUpper();

        // Build query string
        QUrlQuery query;
        for (auto it = params.cbegin(); it != params.cend(); ++it)
            query.addQueryItem(it.key(), it.value());
        query.addQueryItem("sign", sign);

        QUrl url(baseUrl + apiPath);
        url.setQuery(query);

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *reply = nam()->get(req);
        co_await qCoro(reply).waitForFinished();
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError)
            throw std::runtime_error(reply->errorString().toStdString());

        const QJsonObject resp   = QJsonDocument::fromJson(raw).object();
        const int retCode        = resp["ret_code"].toInt(-1);
        if (retCode != 0)
            throw std::runtime_error(resp["ret_msg"].toString("Temu API error").toStdString());

        const QJsonObject result = resp["result"].toObject();
        const QJsonArray  orders = result["order_list"].toArray();

        for (const QJsonValue &v : orders) {
            const QJsonObject order       = v.toObject();
            const QString     orderId     = order["order_no"].toString();
            const QString     marketplace = order["marketplace"].toString();

            if (!orderId.isEmpty() && !marketplace.isEmpty())
                targetInfos->orderId_store[orderId] = "temu." + marketplace.toLower();
        }

        // Continue paging until the API signals end-of-results or returns an empty page
        hasMore = !result["is_end"].toBool(true) && !orders.isEmpty();
        ++page;
    }

    co_return;
}


QNetworkAccessManager *ImporterApiTemuEu::nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(nullptr);
    }
    return m_nam;
}
