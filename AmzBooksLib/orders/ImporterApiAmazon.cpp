#include "ImporterApiAmazon.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QDateTime>
#include <QCoroNetworkReply>
#include <QUrlQuery>
#include <algorithm>

ActivitySource ImporterApiAmazon::getActivitySource() const
{
    return ActivitySource {
        .type = ActivitySourceType::API,
        .channel = "Amazon",
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
        .validator = [](const QVariant& v) -> std::pair<bool, QString> {
            if (v.toString().isEmpty()) return {false, "Client Secret cannot be empty"};
            return {true, ""};
        }
    };

    params["awsAccessKey"] = ParamInfo {
        .key = "awsAccessKey",
        .label = "AWS Access Key ID",
        .description = "IAM User Access Key ID",
        .defaultValue = QVariant(),
        .value = QVariant(),
        .validator = [](const QVariant& v) -> std::pair<bool, QString> {
            if (v.toString().isEmpty()) return {false, "AWS Access Key cannot be empty"};
            return {true, ""};
        }
    };

    params["awsSecretKey"] = ParamInfo {
        .key = "awsSecretKey",
        .label = "AWS Secret Access Key",
        .description = "IAM User Secret Access Key",
        .defaultValue = QVariant(),
        .value = QVariant(),
        .validator = [](const QVariant& v) -> std::pair<bool, QString> {
            if (v.toString().isEmpty()) return {false, "AWS Secret Key cannot be empty"};
            return {true, ""};
        }
    };
    
    return params;
}

QCoro::Task<QString> ImporterApiAmazon::getAccessToken()
{
    if (!m_tokenCache.accessToken.isEmpty() && m_tokenCache.expiration > QDateTime::currentDateTime().addSecs(300)) {
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
    
    QNetworkReply *reply = m_nam.post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    co_await reply;
    
    if (reply->error() != QNetworkReply::NoError) {
        throw std::runtime_error("Failed to refresh access token: " + reply->errorString().toStdString());
    }
    
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    
    if (root.contains("access_token")) {
        m_tokenCache.accessToken = root["access_token"].toString();
        int expiresIn = root["expires_in"].toInt(3600);
        m_tokenCache.expiration = QDateTime::currentDateTime().addSecs(expiresIn);
    } else {
        throw std::runtime_error("Invalid token response: " + data.toStdString());
    }
}

QByteArray ImporterApiAmazon::calculateSignatureKey(const QString& secret, 
                                                    const QString& date, 
                                                    const QString& region, 
                                                    const QString& service) const
{
    QByteArray kSecret = "AWS4" + secret.toUtf8();
    QByteArray kDate = QMessageAuthenticationCode::hash(date.toUtf8(), kSecret, QCryptographicHash::Sha256);
    QByteArray kRegion = QMessageAuthenticationCode::hash(region.toUtf8(), kDate, QCryptographicHash::Sha256);
    QByteArray kService = QMessageAuthenticationCode::hash(service.toUtf8(), kRegion, QCryptographicHash::Sha256);
    QByteArray kSigning = QMessageAuthenticationCode::hash("aws4_request", kService, QCryptographicHash::Sha256);
    return kSigning;
}

void ImporterApiAmazon::signRequest(QNetworkRequest& request, 
                                    const QString& method, 
                                    const QString& path, 
                                    const QUrlQuery& query, 
                                    const QByteArray& payload) const
{
    QString accessKey = getParam("awsAccessKey").toString();
    QString secretKey = getParam("awsSecretKey").toString();
    QString region = getRegion();
    QString service = "execute-api";     
    
    QDateTime now = QDateTime::currentDateTimeUtc();
    QString amzDate = now.toString("yyyyMMdd'T'HHmmss'Z'");
    QString dateStamp = now.toString("yyyyMMdd");
    
    request.setRawHeader("x-amz-date", amzDate.toUtf8());
    QByteArray host = request.url().host().toUtf8();
    request.setRawHeader("host", host);
    
    // Canonical Request
    QString canonicalUri = path;
    // Canonical Query must be sorted
    QList<QPair<QString, QString>> queryItems = query.queryItems(QUrl::FullyDecoded);
    std::sort(queryItems.begin(), queryItems.end(), [](const auto& a, const auto& b) {
        return a.first < b.first || (a.first == b.first && a.second < b.second);
    });
    
    QString canonicalQueryString;
    for (const auto& item : queryItems) {
        if (!canonicalQueryString.isEmpty()) canonicalQueryString += "&";
        canonicalQueryString += QUrl::toPercentEncoding(item.first) + "=" + QUrl::toPercentEncoding(item.second);
    }
    
    QString canonicalHeaders = "host:" + host + "\n" + "x-amz-date:" + amzDate + "\n";
    if (request.hasRawHeader("x-amz-access-token")) {
        canonicalHeaders += "x-amz-access-token:" + request.rawHeader("x-amz-access-token") + "\n";
    }
    
    QString signedHeaders = "host;x-amz-date";
    if (request.hasRawHeader("x-amz-access-token")) {
        signedHeaders += ";x-amz-access-token";
    }
    
    QByteArray payloadHash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    
    QString canonicalRequest = method + "\n" +
                               canonicalUri + "\n" +
                               canonicalQueryString + "\n" +
                               canonicalHeaders + "\n" +
                               signedHeaders + "\n" +
                               payloadHash;
                               
    // String to Sign
    QString algorithm = "AWS4-HMAC-SHA256";
    QString credentialScope = dateStamp + "/" + region + "/" + service + "/aws4_request";
    QString stringToSign = algorithm + "\n" +
                           amzDate + "\n" +
                           credentialScope + "\n" +
                           QCryptographicHash::hash(canonicalRequest.toUtf8(), QCryptographicHash::Sha256).toHex();
                           
    // Signature
    QByteArray signingKey = calculateSignatureKey(secretKey, dateStamp, region, service);
    QByteArray signature = QMessageAuthenticationCode::hash(stringToSign.toUtf8(), signingKey, QCryptographicHash::Sha256).toHex();
    
    // Authorization Header
    QString authorization = algorithm + " Credential=" + accessKey + "/" + credentialScope + 
                            ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;
                            
    request.setRawHeader("Authorization", authorization.toUtf8());
}

QCoro::Task<QByteArray> ImporterApiAmazon::sendSignedRequest(const QString& method, 
                                                             const QString& path, 
                                                             const QUrlQuery& query, 
                                                             const QByteArray& payload)
{
    QString accessToken = co_await getAccessToken();
    
    QUrl url(getEndpoint() + path);
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setRawHeader("x-amz-access-token", accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    signRequest(request, method, path, query, payload);
    
    QNetworkReply* reply = nullptr;
    if (method == "GET") {
        reply = m_nam.get(request);
    } else if (method == "POST") {
        reply = m_nam.post(request, payload);
    } else {
        throw std::runtime_error("Unsupported method: " + method.toStdString());
    }
    
    auto response = co_await reply;
    
    if (reply->error() != QNetworkReply::NoError) {
        QByteArray errorBody = reply->readAll();
        throw std::runtime_error("API Request failed (" + reply->errorString().toStdString() + "): " + errorBody.toStdString());
    }
    
    co_return reply->readAll();
}
