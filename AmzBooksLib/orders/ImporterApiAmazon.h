#ifndef IMPORTERAPIAMAZON_H
#define IMPORTERAPIAMAZON_H

#include "AbstractImporterApi.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QMessageAuthenticationCode>

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
//      - Throttling (429 Too Many Requests) - verify retry logic (if implemented) or error handling.
//
// 2. **Dependency Injection**:
//    - Although `m_nam` is private, we could add a protected `setNetworkAccessManager` for testing purposes.
//
// 3. **Fixture-Based Testing**:
//    - Save real API JSON responses as files (anonymized).
//    - The mock network layer reads these files and returns them as `QNetworkReply`.
//    - This ensures parsing logic covers real-world data structures without hitting the API.
//
// 4. **Date & SigV4 Verification**:
//    - Unit tests should verify that `signRequest` produces the correct canonical string and signature for a known set of inputs (fixed date, keys, and params).
//    - This ensures authentication logic doesn't break.
//
// This approach allows running thousands of tests in seconds with zero API cost.

class ImporterApiAmazon : public AbstractImporterApi
{
public:
    using AbstractImporterApi::AbstractImporterApi; // Inherit constructor
    
    ActivitySource getActivitySource() const override;
    QString getLabel() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;

protected:
    // Common helper methods for Amazon SP-API
    virtual QString getEndpoint() const = 0; // e.g., https://sellingpartnerapi-eu.amazon.com
    virtual QString getRegion() const = 0;   // e.g., eu-west-1 for EU
    virtual QString getMarketplaceId() const = 0; // e.g., A1PA6795UKMFR9 for DE

    // Auth & Signing
    QCoro::Task<QString> getAccessToken();
    
    // Generic Request
    QCoro::Task<QByteArray> sendSignedRequest(const QString& method, 
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
    
    // SigV4
    void signRequest(QNetworkRequest& request, 
                     const QString& method, 
                     const QString& path, 
                     const QUrlQuery& query, 
                     const QByteArray& payload) const;
                     
    QByteArray calculateSignatureKey(const QString& secret, 
                                     const QString& date, 
                                     const QString& region, 
                                     const QString& service) const;

    QNetworkAccessManager m_nam;
};

#endif // IMPORTERAPIAMAZON_H
