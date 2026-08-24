#ifndef IMPORTERAPITEMU_H
#define IMPORTERAPITEMU_H

#include "AbstractImporterApi.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

class QNetworkAccessManager;

// Unit Testing Strategy for Temu Importers:
// =========================================
// Similar to Amazon, use Network Mocking.
// The complexity here is the iteration over multiple shops.
// Tests should verify:
// 1. Reading of the "stores" record-list parameter (one row per store) plus the
//    shared "appKey" / "appSecret" scalars.
// 2. Iteration: Ensure requests are sent for EACH shop.
// 3. Aggregation: Ensure orders from all shops are combined into the result.
// 4. Error Handling: Partial failures (one shop fails) vs Total failure.

// Temu Open Platform protocol (validated live against the EU gateway):
// every call is a POST of a single JSON object to <endpoint>/openapi/router.
// The method name goes in the "type" field (e.g. "bg.order.list.v2.get");
// the signature is MD5 (uppercase hex) of appSecret + sorted(key+value) + appSecret.
class ImporterApiTemu : public AbstractImporterApi
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
    struct ShopConfig {
        QString name;
        QString appId;
        QString appSecret;
        QString accessToken;
        QString country; // ISO code of the shop's site (FR, DE…) — builds the "temu.fr" store id
    };

    QList<ShopConfig> getShops() const;

    // POST <endpoint>/openapi/router with the given API method ("type") and
    // business parameters. Returns the "result" object of a successful reply;
    // raises ExceptionWithTitleText on HTTP, transport or API-level errors.
    // When "result" is an array, it is wrapped under the "result" key.
    QCoro::Task<QJsonObject> sendTemuRequest(const ShopConfig &shop,
                                             const QString &method,
                                             const QJsonObject &businessParams);

    // To be implemented by subclasses — gateway base URL without path,
    // e.g. "https://openapi-b-eu.temu.com"
    virtual QString getEndpoint() const = 0;

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager *nam();
};

#endif // IMPORTERAPITEMU_H
