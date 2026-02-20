#ifndef ORDERMANAGER_H
#define ORDERMANAGER_H

// OrderManager = application service (SQLite-backed) that loads, upserts, and version-controls Orders and their SalesEvents (Shipments/Refunds)
// with idempotent deduplication by (marketplace, eventKind, externalEventId), maintains Draft vs Published revisions, applies manual adjustments
// as new revisions (never mutating published history), and when publishing computes a PostingSet of Activities; if the new PostingSet differs from
// the last published one, it automatically generates compensating reversal postings (refund/credit-note) and publishes a corrected set atomically
// within a database transaction; it is the single entry point for recordFromApi(), applyManualEdit(), and publish() workflows.
// If a shipment or refund is recorded again while published and different, we call this a conflit that usually leads to create a reverse / new transaction

#include <QString>
#include <QDate>
#include <QDir>
#include <QMultiMap>
#include <QSharedPointer>
#include <functional>
#include <QCoroTask>

#include <QSqlDatabase>
#include <QJsonObject>

#include "ActivitySource.h"
#include "books/TaxResolver.h"
#include "Address.h"
#include "InventoryMove.h"
class ActivitySource;
class Shipment;
class InvoicingInfo;
class ActivityUpdate;

class OrderManager
{
    friend class TestOrderManager;
    friend class TestServiceSales;
    friend class TestFileImportAmazonFbaInvoicing;
    friend class TestOrderCompleteTableRealData;
public:
    explicit OrderManager(const QDir &workingDirectory);

    ~OrderManager();
    QDateTime getLastDateTime(ActivitySource *activitySource) const;
    QDateTime getBeginDateTime(ActivitySource *activitySource) const;

    void recordShipmentFromSource(const QString &orderId,
                                  const ActivitySource *activitySource
                                  , const Shipment *shipmentOrRefund
                                  , const QDate &newDateIfConflict
                                  , bool isWrongIfConflict = false
                                  , bool fixTaxDate = false); // Save if new. Replace if not published OR not Activity::isDifferentTaxese. Otherwise create double entry (refund / re-invoicing).

    struct ShipmentFromSourceEntry {
        QString orderId;
        const Shipment *shipmentOrRefund = nullptr;
        QDate newDateIfConflict;
        bool isWrongIfConflict = false;
        bool fixTaxDate = false;
    };

    // Batch variant: records multiple shipments in one or more transactions (500 per batch).
    // New shipments are inserted with a single execBatch() call per batch.
    // Existing shipments (Draft/Published updates) and fixTaxDate entries fall back to
    // recordShipmentFromSource() within the same transaction.
    // Significantly faster than calling recordShipmentFromSource() in a loop (≥3× for all-new entries).
    void recordShipmentsFromSource(const ActivitySource *activitySource,
                                   const QList<ShipmentFromSourceEntry> &entries);
    void recordShipmentUpdated(const QString &orderId,
                               const ActivitySource *activitySource
                               , const Shipment *shipmentOrRefund
                               , const QDate &newDateIfConflict
                               , bool isWrongIfConflict = false); // Will record without erasing the original shipment (exception if shipment doesn't exist). // Save if new. Replace if not published OR not Activity::isDifferentTaxese. Otherwise create double entry (refund / re-invoicing).
    void removeOrder(const QString &orderId);
    void removeShipmenOrRefund(const QString &shipmentOrRefundId);
    bool containsOrder(const QString &orderId) const;
    bool containsShipmentOrRefund(const QString &shipmentOrRefundId) const;
    void recordOrders(const QHash<QString, QString> &orderId_store); // Batch upsert, 1000 at a time
    QHash<QString, QString> getStores(const QList<QSharedPointer<Shipment>> &shipments) const; // orderId → store
    void recordAddressesTo(const QHash<QString, Address> &orderId_addressTo); // Batch upsert, 1000 at a time
    void recordInventoryMove(const QHash<int, QHash<int, QHash<QString, QHash<QString, QHash<QString, InventoryMove>>>>> &year_month_countryFrom_countryTo_id_SkuMovedUnits); // Batch upsert 500 at a time; throws ExceptionWithTitleText if any transactionId is empty
    QHash<QString, int> getInventoryImported(int year, int month, const QString &countryCodeTo) const;
    QHash<QString, int> getInventoryExported(int year, int month, const QString &countryCodeFrom) const;
    // Records invoicing information (number, link, items) for a given shipment (or its root).
    // The info is stored by shipment root ID, ensuring access across all revisions/conflicts.
    void recordInvoicingInfo(const QString &shipmentOrRefundId,
                             const InvoicingInfo *invoicingInfo);
    QCoro::Task<QString> tryRecordRefund(
            const QString &orderId,
            double amount,
            const QString &currency,
            const QString &shipmentId,
            std::function<QCoro::Task<QString>(const QString &errorTitle,
                                               const QString &errorText,
                                               const QList<QSharedPointer<Shipment>> &shipmentsToPick)> callbackPickShipment);

    // Retrieves the invoicing info associated with a shipment's root ID.
    QSharedPointer<InvoicingInfo> getInvoicingInfo(const QString &shipmentId) const;
    void publish(QDate &dateUntil); //Shipment updated are published and the original from source are ignored (when replaced) except if they were published already
    void clearUnpublished(); // Usefull if data were loaded with a bug. It will clear all unpublished
    void deleteDatabase(); // Usefull to reset + also for unit tests

    struct ShipmentRefundsWithUpdates{
        QSharedPointer<ActivityUpdate> activityUpdate;
        QList<QSharedPointer<Shipment>> shipmentsRefundsSameActivity;
        QList<bool> invoicesToDo;
        QSharedPointer<InvoicingInfo> invoicingInfo;
        QSharedPointer<Address> addressTo;
    };

    QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, ShipmentRefundsWithUpdates>>>> get_channel_site_ShipmentAndRefundsConflicts(
            const QDate &dateFrom
            , const QDate &dateTo) const;
    QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, ShipmentRefundsWithUpdates>>>> get_channel_site_ShipmentAndRefundsInsertedAt(
            const QDate &dateFromInsertedDb
            , const QDate &dateToInsertedDb) const;
    QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, ShipmentRefundsWithUpdates>>>> get_channel_site_ShipmentAndRefundsNoInvoices(
            const QDate &dateFrom
            , const QDate &dateTo) const;

    QSharedPointer<QList<OrderManager::ShipmentRefundsWithUpdates>> getShipmentAndRefundsNoInvoices(
            const QDate &dateFrom
            , const QDate &dateTo) const;

    QMultiMap<QDateTime, QSharedPointer<Shipment>> getShipmentAndRefundsRecentlyAdded(const QDate &minDateAdded) const;

    QMultiMap<QDateTime, QSharedPointer<Shipment>> getShipmentAndRefunds(
            const QDate &dateFrom
            , const QDate &dateTo
            , std::function<bool(const ActivitySource*, const Shipment*)> acceptCallback) const;
    QHash<ActivitySource, QMultiMap<QDateTime, QSharedPointer<Shipment>>> getActivitySource_ShipmentAndRefunds(
            const QDate &dateFrom
            , const QDate &dateTo
            , std::function<bool(const ActivitySource*, const Shipment*)> acceptCallback) const;
    QHash<ActivitySource, QHash<QString, QMultiMap<QDateTime, QSharedPointer<Shipment>>>> getActivitySource_store_ShipmentAndRefunds(
            const QDate &dateFrom
            , const QDate &dateTo
            , std::function<bool(const ActivitySource*, const Shipment*)> acceptCallback) const;
    void copyDatabase(const QString &filePath, int yearUntil); // To archive all orders
    void removeInDatabase(int yearUntil); // To remove old data
    
    // Returns a new model for specific view usage
    ActivityUpdate *createActivityUpdateModel(const QString &shipmentId, QObject* parent = nullptr); 

    // Returns a valid pointer if a shipment (or refund) exists for this orderId and IS DIFFERENT from the provided one.
    // If no shipment exists, or if the existing one is identical to the provided one, returns nullptr.
    // This allows the caller to decide whether to record a new/updated shipment.
    // The returned pointer is a COPY of the existing shipment.
    QSharedPointer<Shipment> getShipmentOrRefundIfDifferent(const QString &orderId,
                                                            const ActivitySource *activitySource
                                                            , const Shipment *shipmentOrRefund) const;

private:


    void initDb();
    
    QString m_filePathDb;
    QString m_connectionName;
    QSqlDatabase m_db;

    enum class ConflictStatus {
        NoChange,     // Content is identical
        ContentDiffers, // Content differs but no financial impact (e.g. internal date) or not requiring reversal
        Conflict      // Financial or significant conflict requiring Reversal + New Version
    };

    // Helper to check conflict between an existing shipment (from DB) and an incoming one.
    ConflictStatus checkConflict(const Shipment &existing, const Shipment &incoming) const;

    // Helper to retrieve the "current effective" shipment/refund for a given ID.
    // Priority: 1. Latest Draft (if any), 2. Latest Published.
    // Returns nullptr if not found.
    QSharedPointer<Shipment> getHeadShipment(const QString &id, QString *outStatus = nullptr, QString *outJson = nullptr) const;
};

#endif // ORDERMANAGER_H
