#ifndef IMPORTERAPIAMAZONAMERICA_H
#define IMPORTERAPIAMAZONAMERICA_H

#include "ImporterApiAmazon.h"

class ImporterApiAmazonAmerica : public ImporterApiAmazon
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

#endif // IMPORTERAPIAMAZONAMERICA_H
