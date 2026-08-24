#ifndef IMPORTERAPIAMAZON_H
#define IMPORTERAPIAMAZON_H

#include "AbstractImporterApi.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

// Unit Testing Strategy for Amazon SP-API Importers:
// ==================================================
// To ensure reliability and efficiency without consuming API quotas or requiring live credentials:
//
// 1. **Network Mocking (Recommended)**:
//    - The `ImporterApiAmazon` class uses `QNetworkAccessManager` (NAM) for all requests.
//    - For unit tests, subclass or mock `QNetworkAccessManager` (or intercept requests via a custom `QNetworkAccessManagerFactory` if using QML, but here C++ direct usage).
//    - Better yet, inject a "NetworkService" interface, or since we use `m_nam` directly:
//      provide a way to set a custom NAM or use a local HTTP test server.
//    - The tests should simulate Amazon's responses (JSON payloads) for:
//      - Token Exchange (Success/Failure)
//      - Orders Response (Empty, Single Page, Multi-Page with NextToken)
//      - Throttling (429 Too Many Requests) - verify retry logic and error handling.
//
// 2. **Dependency Injection**:
//    - Although `m_nam` is private, we could add a protected `setNetworkAccessManager` for testing purposes.
//
// 3. **Fixture-Based Testing**:
//    - Save real API JSON responses as files (anonymized).
//    - The mock network layer reads these files and returns them as `QNetworkReply`.
//    - This ensures parsing logic covers real-world data structures without hitting the API.
//
// This approach allows running thousands of tests in seconds with zero API cost.

// Authentication: SP-API only requires the LWA access token
// (header x-amz-access-token). AWS SigV4 signing and IAM keys were
// retired by Amazon in October 2023 and must NOT be sent anymore.
class ImporterApiAmazon : public AbstractImporterApi
{
public:
    using AbstractImporterApi::AbstractImporterApi; // Inherit constructor

    ActivitySource getActivitySource() const override;
    QString getLabel() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;
    bool recomputeTaxes() const override;
    bool isWrongIfConflict() const override;
    bool fixRefundDate() const override;
    bool isGroupedOrders() const override;

protected:
    // Common helper methods for Amazon SP-API
    virtual QString getEndpoint() const = 0; // e.g., https://sellingpartnerapi-eu.amazon.com
    virtual QString getMarketplaceId() const = 0; // e.g., A1PA6795UKMFR9 for DE

    // Auth (LWA)
    QCoro::Task<QString> getAccessToken();

    // Generic Request — LWA-authenticated, retries on 429 throttling
    QCoro::Task<QByteArray> sendApiRequest(const QString& method,
                                           const QString& path,
                                           const QUrlQuery& query,
                                           const QByteArray& payload = QByteArray());

private:
    struct TokenInfo {
        QString accessToken;
        QDateTime expiration;
    };
    TokenInfo m_tokenCache;

    // LWA
    QCoro::Task<void> refreshAccessToken();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager *nam();
};

#endif // IMPORTERAPIAMAZON_H
