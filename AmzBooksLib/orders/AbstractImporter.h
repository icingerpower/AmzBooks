#ifndef ABSTRACTIMPORTER_H
#define ABSTRACTIMPORTER_H

#include <QVariant>
#include <QDir>
#include <QSharedPointer>
#include <QSettings>

#include "InvoicingInfo.h"
#include "Address.h"
#include "Refund.h"

#include "ActivitySource.h"

#include <functional>
#include <utility>

class AbstractImporter
{
public:
    struct InvoicingInfoWithId{
        QString shipmentOrRefundId;
        InvoicingInfo invoicingInfo;
    };
    struct AddressToWithId{
        QString orderId;
        Address address;
    };
    struct OrderInfos{
        QList<InvoicingInfoWithId> invoicingInfos;
        QList<AddressToWithId> orderAddresses;
        QList<Shipment> shipments;
        QList<Refund> refunds;
        QHash<QString, QString> orderId_store;
        QDate dateMin;
        QDate dateMax;
    };
    struct ReturnOrderInfos{
        QSharedPointer<OrderInfos> orderInfos;
        QString errorReturned;
    };


    AbstractImporter(const QDir &workingDirectory);
    struct ParamInfo {
        QString key;                  // stable key used in code
        QString label;                // human label for UI
        QString description;          // tooltip/help
        QVariant defaultValue;        // optional default
        QVariant value;               // current value
        // return {ok, errorMessage}
        std::function<std::pair<bool, QString>(const QVariant&)> validator;
    };
    virtual ActivitySource getActivitySource() const = 0;
    virtual QString getLabel() const = 0;
    virtual QMap<QString, ParamInfo> getRequiredParams() const = 0;

    const QMap<QString, ParamInfo> &getLoadedParamValues() const;

    void load(); // init m_params and load from settings if values were saved
    void setParam(const QString& key, const QVariant& value);
    QVariant getParam(const QString& key) const;

protected:
    QDir m_workingDirectory;
    QString m_settingPath;
    QSharedPointer<QSettings> _settings() const;
    QMap<QString, ParamInfo> m_params;
};

#endif // ABSTRACTIMPORTER_H
