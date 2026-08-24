#include "ImporterApiAmazon.h"
#include "ExceptionWithTitleText.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QTimer>
#include <QCoroNetworkReply>
#include <QCoroSignal>
#include <QUrlQuery>

ActivitySource ImporterApiAmazon::getActivitySource() const
{
    return ActivitySource {
        .type = ActivitySourceType::API,
        .channel = CHANNEL_AMAZON,
        .subchannel = getMarketplaceId(),
        .reportOrMethode = "SP-API"
    };
}

QString ImporterApiAmazon::getLabel() const
{
    return "Amazon SP-API";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterApiAmazon::getRequiredParams() const
{
    QMap<QString, ParamInfo> params;

    params["refreshToken"] = ParamInfo {
        .key = "refreshToken",
        .label = "Refresh Token",
        .description = "Amazon SP-API Refresh Token for LWA authentication",
        .defaultValue = QVariant(),
        .value = QVariant(),
        .secret = true,
        .validator = [](const QVariant& v) -> std::pair<bool, QString> {
            if (v.toString().isEmpty()) return {false, "Refresh token cannot be empty"};
            return {true, ""};
        }
    };

    params["clientId"] = ParamInfo {
        .key = "clientId",
        .label = "LWA Client ID",
        .description = "Login with Amazon (LWA) Client Identifier",
        .defaultValue = QVariant(),
        .value = QVariant(),
        .validator = [](const QVariant& v) -> std::pair<bool, QString> {
            if (v.toString().isEmpty()) return {false, "Client ID cannot be empty"};
            return {true, ""};
        }
    };

    params["clientSecret"] = ParamInfo {
        .key = "clientSecret",
        .label = "LWA Client Secret",
        .description = "Login with Amazon (LWA) Client Secret",
        .defaultValue = QVariant(),
        .value = QVariant(),
        .secret = true,
        .validator = [](const QVariant& v) -> std::pair<bool, QString> {
            if (v.toString().isEmpty()) return {false, "Client Secret cannot be empty"};
            return {true, ""};
        }
    };

    // Note: SP-API no longer requires AWS IAM keys (awsAccessKey/awsSecretKey).
    // Amazon removed the AWS SigV4 signing requirement in October 2023 —
    // the LWA access token is the only credential sent with each request.

    return params;
}

bool ImporterApiAmazon::recomputeTaxes() const
{
    return false;
}

bool ImporterApiAmazon::isWrongIfConflict() const
{
    return false;
}

bool ImporterApiAmazon::fixRefundDate() const
{
    return false;
}

bool ImporterApiAmazon::isGroupedOrders() const
{
    return true;
}

QCoro::Task<QString> ImporterApiAmazon::getAccessToken()
{
    if (!m_tokenCache.accessToken.isEmpty() && m_tokenCache.expiration > QDateTime::currentDateTimeUtc()) {
        co_return m_tokenCache.accessToken;
    }

    co_await refreshAccessToken();
    co_return m_tokenCache.accessToken;
}

QCoro::Task<void> ImporterApiAmazon::refreshAccessToken()
{
    QUrl url("https://api.amazon.com/auth/o2/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery query;
    query.addQueryItem("grant_type", "refresh_token");
    query.addQueryItem("refresh_token", getParam("refreshToken").toString());
    query.addQueryItem("client_id", getParam("clientId").toString());
    query.addQueryItem("client_secret", getParam("clientSecret").toString());

    QNetworkReply *reply = nam()->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    co_await reply;

    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    reply->deleteLater();

    if (netError != QNetworkReply::NoError) {
        ExceptionWithTitleText exception("Token Refresh Failed", "Failed to refresh access token: " + netErrorString);
        exception.raise();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();

    if (root.contains("access_token")) {
        m_tokenCache.accessToken = root["access_token"].toString();
        const int expiresIn = root["expires_in"].toInt(3600);
        // Refresh 5 min before expiry, and never trust a token beyond 55 min
        const int cacheSecs = qMax(qMin(expiresIn - 300, 55 * 60), 60);
        m_tokenCache.expiration = QDateTime::currentDateTimeUtc().addSecs(cacheSecs);
    } else {
        ExceptionWithTitleText exception("Invalid Token Response", "Invalid token response: " + QString::fromUtf8(data));
        exception.raise();
    }
}

QCoro::Task<QByteArray> ImporterApiAmazon::sendApiRequest(const QString& method,
                                                          const QString& path,
                                                          const QUrlQuery& query,
                                                          const QByteArray& payload)
{
    QUrl url(getEndpoint() + path);
    url.setQuery(query);

    // Retry on 429 throttling (SP-API rate limits are low on the orders endpoints)
    const int maxAttempts = 3;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        const QString accessToken = co_await getAccessToken();

        QNetworkRequest request(url);
        request.setRawHeader("x-amz-access-token", accessToken.toUtf8());
        request.setRawHeader("Accept", "application/json");
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = nullptr;
        if (method == "GET") {
            reply = nam()->get(request);
        } else if (method == "POST") {
            reply = nam()->post(request, payload);
        } else {
            ExceptionWithTitleText exception("Unsupported Method", "Unsupported method: " + method);
            exception.raise();
        }

        co_await reply;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString netErrorString = reply->errorString();
        reply->deleteLater();

        if (status == 429 && attempt < maxAttempts - 1) {
            QTimer retryTimer;
            retryTimer.setSingleShot(true);
            retryTimer.start(2000);
            co_await qCoro(&retryTimer, &QTimer::timeout);
            continue;
        }

        if (netError != QNetworkReply::NoError) {
            ExceptionWithTitleText exception("API Request Failed",
                "API Request failed (" + netErrorString + "): " + QString::fromUtf8(data));
            exception.raise();
        }

        co_return data;
    }

    // Unreachable: the last attempt either returns or throws
    co_return QByteArray();
}

QNetworkAccessManager *ImporterApiAmazon::nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(nullptr);
        m_nam->setTransferTimeout(30'000); // 30 s — aborts silently-hanging requests
    }
    return m_nam;
}
