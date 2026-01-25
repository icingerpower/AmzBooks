#ifndef IMPORTERAPITEMUEU_H
#define IMPORTERAPITEMUEU_H

#include "ImporterApiTemu.h"
#include <QNetworkAccessManager>

class ImporterApiTemuEu : public ImporterApiTemu
{
public:
    using ImporterApiTemu::ImporterApiTemu;

protected:
    QString getEndpoint() const override;
    
    QCoro::Task<ReturnOrderInfos> _fetchShipments(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchRefunds(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchAddresses(const QDateTime &dateFrom) override;
    QCoro::Task<ReturnOrderInfos> _fetchInvoiceInfos(const QDateTime &dateFrom) override;

private:
    QCoro::Task<void> fetchShopOrders(const ShopConfig& shop, const QDateTime& dateFrom, QSharedPointer<OrderInfos> targetInfos);
    
    QNetworkAccessManager m_nam;
};

#endif // IMPORTERAPITEMUEU_H
