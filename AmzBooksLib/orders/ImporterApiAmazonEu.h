#ifndef IMPORTERAPIAMAZONEU_H
#define IMPORTERAPIAMAZONEU_H

#include "ImporterApiAmazon.h"

// Unit Testing Strategy:
// =====================
// To test this API without burning API quota or requiring real credentials:
//
// 1. **Network Mocking** (Recommended):
//    - Use QNetworkAccessManager subclass/mock that intercepts requests
//    - Mock responses based on captured real API responses
//    - Libraries like Qt's QTest with custom QNetworkAccessManager
//
// 2. **Dependency Injection**:
//    - Extract HTTP layer into injectable interface
//    - Provide mock implementation for tests
//    - Real implementation uses QNetworkAccessManager + QCoro
//
// 3. **Record/Replay**:
//    - Capture real API responses once (in dev environment)
//    - Store as fixture files (JSON)
//    - Replay during tests
//
// 4. **Amazon SP-API Sandbox** (Limited):
//    - Amazon provides sandbox endpoints for some API calls
//    - Still requires network and credentials
//    - Not true "unit" tests but good for integration testing
//
// Recommended approach: Network mocking with dependency injection
// This allows fast, deterministic tests without external dependencies.

class ImporterApiAmazonEu : public ImporterApiAmazon
{
public:
    using ImporterApiAmazon::ImporterApiAmazon; // Inherit constructor

protected:
    QString getEndpoint() const override;
    QString getRegion() const override;
    QString getMarketplaceId() const override;
    
    QCoro::Task<ReturnOrderInfos> _fetchShipments(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchRefunds(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchAddresses(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchInvoiceInfos(const QDateTime &dateFrom) override;
};

#endif // IMPORTERAPIAMAZONEU_H
