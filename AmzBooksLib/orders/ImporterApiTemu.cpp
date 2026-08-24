#include "ImporterApiTemu.h"
#include "ExceptionWithTitleText.h"
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCoroNetworkReply>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDateTime>
#include <algorithm>

namespace {
// Stringification of JSON values for the Temu signature base string.
// Integral doubles are formatted without decimals; arrays/objects as compact JSON.
QString jsonValueToStringForSign(const QJsonValue &val)
{
    if (val.isString()) {
        return val.toString();
    } else if (val.isDouble()) {
        const double d = val.toDouble();
        if (d == static_cast<qint64>(d)) {
            return QString::number(static_cast<qint64>(d));
        }
        return QString::number(d, 'f', 6).replace(
            QRegularExpression(QStringLiteral("\\.?0+$")), QString());
    } else if (val.isBool()) {
        return val.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    } else if (val.isNull()) {
        return QStringLiteral("null");
    } else if (val.isArray()) {
        const QJsonDocument doc(val.toArray());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    } else if (val.isObject()) {
        const QJsonDocument doc(val.toObject());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    return QString();
}

// Temu Open Platform signature: MD5 hex (uppercase) of
// appSecret + concat(sortedKey + value) + appSecret
QString generateSign(const QJsonObject &params, const QString &appSecret)
{
    QStringList keys = params.keys();
    std::sort(keys.begin(), keys.end());

    QString baseString;
    for (const QString &key : std::as_const(keys)) {
        baseString += key + jsonValueToStringForSign(params.value(key));
    }

    const QString signString = appSecret + baseString + appSecret;
    const QByteArray hash = QCryptographicHash::hash(signString.toUtf8(),
                                                     QCryptographicHash::Md5);
    return QString::fromLatin1(hash.toHex().toUpper());
}
} // namespace

ActivitySource ImporterApiTemu::getActivitySource() const
{
    // Temu source. Subchannel will be dynamic or generic.
    // Since we aggregate multiple shops, subchannel might be "Aggregated" or we rely on
    // individual activities having specific source data if Activity supported it.
    // For now, generic.
    return ActivitySource {
        .type = ActivitySourceType::API,
        .channel = CHANNEL_TEMU,
        .subchannel = "EU", // Default, specific subclasses might override if needed but here we share base
        .reportOrMethode = "OpenAPI"
    };
}

QString ImporterApiTemu::getLabel() const
{
    return "Temu API";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterApiTemu::getRequiredParams() const
{
    QMap<QString, ParamInfo> params;

    params["appKey"] = ParamInfo {
        .key = "appKey",
        .label = QObject::tr("App Key"),
        .description = QObject::tr("Temu Open Platform App Key shared by all stores."),
        .defaultValue = "",
        .value = QVariant(),
        .secret = false
    };

    params["appSecret"] = ParamInfo {
        .key = "appSecret",
        .label = QObject::tr("App Secret"),
        .description = QObject::tr("Temu Open Platform App Secret shared by all stores."),
        .defaultValue = "",
        .value = QVariant(),
        .secret = true
    };

    ParamInfo stores;
    stores.key = "stores";
    stores.label = QObject::tr("Stores");
    stores.description = QObject::tr("One row per store: name, country and access token.");
    stores.type = ParamType::RecordList;
    stores.fields = QList<FieldInfo>{
        FieldInfo{ .key = "label",   .label = QObject::tr("Store name"),   .secret = false },
        FieldInfo{ .key = "country", .label = QObject::tr("Country"),      .secret = false },
        FieldInfo{ .key = "token",   .label = QObject::tr("Access token"), .secret = true },
    };
    params["stores"] = stores;

    return params;
}

bool ImporterApiTemu::recomputeTaxes() const
{
    return true;
}

bool ImporterApiTemu::isWrongIfConflict() const
{
    return false;
}

bool ImporterApiTemu::fixRefundDate() const
{
    return true;
}

bool ImporterApiTemu::isGroupedOrders() const
{
    return true;
}

QList<ImporterApiTemu::ShopConfig> ImporterApiTemu::getShops() const
{
    QList<ShopConfig> shops;

    // The App Key / App Secret are shared by every store.
    const QString appKey = getParam("appKey").toString();
    const QString appSecret = getParam("appSecret").toString();

    const QList<QVariantMap> rows = getParamRecords("stores");
    for (const auto &row : std::as_const(rows)) {
        ShopConfig shop;
        shop.name = row.value("label").toString();
        shop.appId = appKey;
        shop.appSecret = appSecret;
        shop.accessToken = row.value("token").toString();
        shop.country = row.value("country").toString();

        if (!shop.name.isEmpty() && !shop.appId.isEmpty()) {
            shops.append(shop);
        }
    }
    return shops;
}

QCoro::Task<QJsonObject> ImporterApiTemu::sendTemuRequest(const ShopConfig &shop,
                                                          const QString &method,
                                                          const QJsonObject &businessParams)
{
    QJsonObject reqObj = businessParams;

    // API version is encoded in the method name (bg.order.list.v2.get → V2)
    QString apiVersion = QStringLiteral("V1");
    const QRegularExpression verReg(QStringLiteral("\\.v([1-9]+)\\."));
    const QRegularExpressionMatch match = verReg.match(method);
    if (match.hasMatch()) {
        apiVersion = QStringLiteral("V") + match.captured(1);
    }

    reqObj.insert(QStringLiteral("type"), method);
    reqObj.insert(QStringLiteral("app_key"), shop.appId);
    reqObj.insert(QStringLiteral("access_token"), shop.accessToken);
    reqObj.insert(QStringLiteral("data_type"), QStringLiteral("JSON"));
    reqObj.insert(QStringLiteral("version"), apiVersion);
    reqObj.insert(QStringLiteral("timestamp"),
                  QDateTime::currentDateTimeUtc().toSecsSinceEpoch());
    reqObj.insert(QStringLiteral("sign"), generateSign(reqObj, shop.appSecret));

    QUrl url(getEndpoint() + QStringLiteral("/openapi/router"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QByteArray payload = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = nam()->post(req, payload);
    co_await qCoro(reply).waitForFinished();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    reply->deleteLater();

    if (netError != QNetworkReply::NoError && status == 0) {
        ExceptionWithTitleText ex("Temu API Request Failed",
                                  QString("Network error calling %1: %2").arg(method, netErrorString));
        ex.raise();
    }
    if (status != 200) {
        ExceptionWithTitleText ex("Temu API Request Failed",
                                  QString("HTTP %1 calling %2: %3")
                                      .arg(QString::number(status), method,
                                           QString::fromUtf8(data.left(800))));
        ex.raise();
    }

    QJsonParseError parseErr;
    const QJsonDocument respDoc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        ExceptionWithTitleText ex("Temu API Invalid Response",
                                  QString("JSON parse error for %1: %2")
                                      .arg(method, parseErr.errorString()));
        ex.raise();
    }

    const QJsonObject respObj = respDoc.object();
    QJsonObject responseVal = respObj.value(QStringLiteral("response")).toObject();
    if (responseVal.isEmpty()) {
        responseVal = respObj;
    }

    if (!responseVal.value(QStringLiteral("success")).toBool(false)) {
        const QString errCode = responseVal.value(QStringLiteral("errorCode")).toVariant().toString();
        const QString errMsg = responseVal.value(QStringLiteral("errorMsg")).toString();
        ExceptionWithTitleText ex("Temu API Error",
                                  QString("%1 returned error %2: %3").arg(method, errCode, errMsg));
        ex.raise();
    }

    // Some endpoints return "result" as an array — wrap it under "result"
    const QJsonValue resultVal = responseVal.value(QStringLiteral("result"));
    if (resultVal.isArray()) {
        QJsonObject wrap;
        wrap.insert(QStringLiteral("result"), resultVal.toArray());
        co_return wrap;
    }
    co_return resultVal.toObject();
}

QNetworkAccessManager *ImporterApiTemu::nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(nullptr);
        m_nam->setTransferTimeout(30'000); // 30 s — aborts silently-hanging requests
    }
    return m_nam;
}
