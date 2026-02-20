#ifndef ABSTRACTIMPORTER_H
#define ABSTRACTIMPORTER_H

#include <QVariant>
#include <QDir>
#include <QSharedPointer>
#include <QSettings>

#include "InvoicingInfo.h"
#include "Address.h"
#include "Refund.h"
#include "InventoryMove.h"

#include "ActivitySource.h"

#include <functional>
#include <utility>

class AbstractImporter
{
public:
    static const QString CHANNEL_AMAZON;
    static const QString CHANNEL_TEMU;
    struct InvoicingInfoWithId{
        QString shipmentOrRefundId;
        InvoicingInfo invoicingInfo;
    };
    struct AddressToWithId{
        QString orderId;
        Address address;
    };
    struct Amount{
        double value;
        QString currency;
    };
    using InventoryMove = ::InventoryMove; // defined in InventoryMove.h

    struct OrderInfos{
        QList<InvoicingInfoWithId> invoicingInfos;
        QList<AddressToWithId> orderAddresses;
        QList<Shipment> shipments;
        QList<Refund> refunds;
        QHash<QString, Amount> orderId_refundClue;
        QHash<QString, QString> orderId_store;
        QHash<int, QHash<int, QHash<QString, QHash<QString, QHash<QString, InventoryMove>>>>> year_month_countryFrom_countryTo_id_SkuMovedUnits;
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
    virtual QString getId() const = 0; // Won't be translated while getLabel will
    virtual QString getLabel() const = 0;
    virtual QMap<QString, ParamInfo> getRequiredParams() const = 0;
    virtual bool recomputeTaxes() const = 0;
    virtual bool isWrongIfConflict() const = 0; // If 2 drafts conflicts, if one is wrong if conflict, we take data of the one not wrong
    virtual bool fixRefundDate() const = 0; //The refund tax date of refund is wrong and need to be fixed when the refund is added in the OrderManager, but using the tax date of the original order

    const QMap<QString, ParamInfo> &getLoadedParamValues() const;

    void load(); // init m_params and load from settings if values were saved
    void setParam(const QString& key, const QVariant& value);
    QVariant getParam(const QString& key) const;

protected:
    AbstractImporter() : m_workingDirectory(QDir::current()) {}
    QDir m_workingDirectory;
    QString m_settingPath;
    QSharedPointer<QSettings> _settings() const;
    QMap<QString, ParamInfo> m_params;
};

#endif // ABSTRACTIMPORTER_H
