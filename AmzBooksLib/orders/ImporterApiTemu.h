#ifndef IMPORTERAPITEMU_H
#define IMPORTERAPITEMU_H

#include "AbstractImporterApi.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Unit Testing Strategy for Temu Importers:
// =========================================
// Similar to Amazon, use Network Mocking.
// The complexity here is the iteration over multiple shops.
// Tests should verify:
// 1. Parsing of the "shops" JSON parameter.
// 2. Iteration: Ensure requests are sent for EACH shop.
// 3. Aggregation: Ensure orders from all shops are combined into the result.
// 4. Error Handling: Partial failures (one shop fails) vs Total failure.

class ImporterApiTemu : public AbstractImporterApi
{
public:
    using AbstractImporterApi::AbstractImporterApi; // Inherit constructor
    
    ActivitySource getActivitySource() const override;
    QString getLabel() const override;
    QMap<QString, ParamInfo> getRequiredParams() const override;

protected:
    struct ShopConfig {
        QString name;
        QString appId;
        QString appSecret;
        QString accessToken;
    };
    
    QList<ShopConfig> getShops() const;
    
    // To be implemented by subclasses
    virtual QString getEndpoint() const = 0;
};

#endif // IMPORTERAPITEMU_H
