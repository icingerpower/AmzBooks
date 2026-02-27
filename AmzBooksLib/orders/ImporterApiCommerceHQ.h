#ifndef IMPORTERAPICOMMERCEHQ_H
#define IMPORTERAPICOMMERCEHQ_H

#include "AbstractImporterApi.h"
#include <QNetworkAccessManager>

// Unit Testing Strategy for CommerceHQ Importers:
// ==============================================
// 1. Mocking: Use QNetworkAccessManager mocking.
// 2. Multi-Store: Verify "stores" JSON parsing and iteration.
// 3. Auth: Verify correct headers/params are sent for each store.

class ImporterApiCommerceHQ : public AbstractImporterApi
{
public:
    using AbstractImporterApi::AbstractImporterApi;
    
    ActivitySource getActivitySource() const override;

    QString getLabel() const override;
    QString getId() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;
    bool isGroupedOrders() const override;

protected:
    QString getEndpoint() const;
    
    QCoro::Task<ReturnOrderInfos> _fetchShipments(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchRefunds(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchAddresses(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchInvoiceInfos(const QDateTime &dateFrom) override;

private:
    struct StoreConfig {
        QString name;
        QString storeId;
        QString apiKey;
        QString apiPassword;
    };
    
    QList<StoreConfig> getStores() const;
    QCoro::Task<void> fetchStoreOrders(const StoreConfig& store, const QDateTime& dateFrom, QSharedPointer<OrderInfos> targetInfos);
    
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager *nam();
};

#endif // IMPORTERAPICOMMERCEHQ_H
