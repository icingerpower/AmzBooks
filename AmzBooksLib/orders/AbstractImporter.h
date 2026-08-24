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
#include "OrderManager.h"

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
    struct RefundClue {
        double value;
        QString currency;
        QDate date;
    };
    struct OrderInfos{
        QList<InvoicingInfoWithId> invoicingInfos;
        QList<AddressToWithId> orderAddresses;
        QList<Shipment> shipments;
        QList<Refund> refunds;
        // One order may have multiple refund lines (one per item/shipment).
        QHash<QString, QList<RefundClue>> orderId_refundClues;
        QHash<QString, OrderManager::OrderInfo> orderId_infos;
        // year → month → countryFrom → countryTo → eventId → sku → units
        // One eventId can cover multiple SKUs; each (eventId, sku) pair is stored separately.
        QHash<int, QHash<int, QHash<QString, QHash<QString, QHash<QString, QHash<QString, int>>>>>> year_month_countryFrom_countryTo_eventId_sku_units;
        QDate dateMin;
        QDate dateMax;

        int countAll() const {
            int clueCount = 0;
            for (const auto &list : std::as_const(orderId_refundClues)) {
                clueCount += list.size();
            }
            return shipments.size() + refunds.size() + clueCount;
        }
    };
    struct ReturnOrderInfos{
        QSharedPointer<OrderInfos> orderInfos;
        QString errorReturned;
    };


    AbstractImporter(const QDir &workingDirectory);

    // Scalar  → a single value edited in the 2-column params form.
    // RecordList → a table of rows (one map per row); edited by the dedicated
    // record-list widget, persisted via QSettings begin/endWriteArray.
    enum class ParamType { Scalar, RecordList };

    // Column definition for one field of a RecordList row.
    struct FieldInfo {
        QString key;          // stable key used in the per-row QVariantMap
        QString label;        // human column header for UI
        bool secret = false;  // stored in the OS keychain, masked in the UI
    };

    struct ParamInfo {
        QString key;                  // stable key used in code
        QString label;                // human label for UI
        QString description;          // tooltip/help
        QVariant defaultValue;        // optional default
        QVariant value;               // current value
        // When true, the value is a credential: stored in the OS keychain
        // (Secret Service) rather than in plaintext importer.ini.
        bool secret = false;
        // return {ok, errorMessage}
        std::function<std::pair<bool, QString>(const QVariant&)> validator;
        // Scalar by default. When RecordList, `value` holds a QVariantList of
        // QVariantMap rows (fieldKey → QString) and `fields` describes columns.
        ParamType type = ParamType::Scalar;
        QList<FieldInfo> fields; // used only when type == RecordList
    };
    virtual ActivitySource getActivitySource() const = 0;
    virtual QString getId() const = 0; // Won't be translated while getLabel will
    virtual QString getLabel() const = 0;
    virtual QMap<QString, ParamInfo> getRequiredParams() const = 0;
    virtual bool recomputeTaxes() const = 0;
    virtual bool isWrongIfConflict() const = 0; // If 2 drafts conflicts, if one is wrong if conflict, we take data of the one not wrong
    virtual bool fixRefundDate() const = 0; //The refund tax date of refund is wrong and need to be fixed when the refund is added in the OrderManager, but using the tax date of the original order
    virtual bool isGroupedOrders() const = 0; //If not grouped, in book keeping generation, one entry set is done / shipment & refund instead of gathering by month and vat

    const QMap<QString, ParamInfo> &getLoadedParamValues() const;

    void load(); // init m_params and load from settings if values were saved
    void setParam(const QString& key, const QVariant& value);
    QVariant getParam(const QString& key) const;

    // Record-list accessors. A RecordList param stores a QVariantList of
    // QVariantMap rows in ParamInfo::value.
    // getParamRecords: returns {} when the key is missing or not a RecordList.
    QList<QVariantMap> getParamRecords(const QString &key) const;
    // setParamRecords: validates the key exists and is a RecordList (else
    // ExceptionWithTitleText), persists rows (secret fields to the keychain),
    // then updates m_params[key].value.
    void setParamRecords(const QString &key, const QList<QVariantMap> &records);

    // Shared config directory for tables that must be shared across importers
    // (e.g. fbacenters.csv). When set, importers should prefer this over
    // m_workingDirectory for those tables so the GUI and importer see the same data.
    void setSharedConfigDirectory(const QDir &dir);

    void setWorkingDirectory(const QDir &dir);
    QStringList getImportedIds() const;

protected:
    AbstractImporter() : m_workingDirectory(QDir::current()) {}
    QDir m_workingDirectory;
    QString m_sharedConfigDirectoryPath; // empty = use m_workingDirectory
    QString m_settingPath;
    QSharedPointer<QSettings> _settings() const;
    QMap<QString, ParamInfo> m_params;
};

#endif // ABSTRACTIMPORTER_H
