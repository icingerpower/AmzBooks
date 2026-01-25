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

#include <QSqlDatabase>
#include <QJsonObject>

#include "ActivitySource.h"

class Address;
class ActivitySource;
class Shipment;
class InvoicingInfo;
class ActivityUpdate;

class OrderManager
{
    friend class TestOrderManager;
public:
    OrderManager(const QDir &workingDirectory);
    ~OrderManager();
    // TODO add source (SourceType such as report or API, main channel / subchannel, reportOrMethodType
    QDateTime getLastDateTime(ActivitySource *activitySource) const;
    QDateTime getBeginDateTime(ActivitySource *activitySource) const;
    void recordShipmentFromSource(const QString &orderId,
                                  const ActivitySource *activitySource
                                  , const Shipment *shipmentOrRefund
                                  , const QDate &newDateIfConflict); // Save if new. Replace if not published OR not Activity::isDifferentTaxese. Otherwise create double entry (refund / re-invoicing).
    void recordShipmentUpdated(const QString &orderId,
                               const ActivitySource *activitySource
                               , const Shipment *shipmentOrRefund
                               , const QDate &newDateIfConflict); // Will record without erasing the original shipment (exception if shipment doesn't exist). // Save if new. Replace if not published OR not Activity::isDifferentTaxese. Otherwise create double entry (refund / re-invoicing).
    void recordAddressTo(const QString &orderId,
                         const Address &addressTo); // Replace if exists
    // Records invoicing information (number, link, items) for a given shipment (or its root).
    // The info is stored by shipment root ID, ensuring access across all revisions/conflicts.
    void recordInvoicingInfo(const QString &shipmentOrRefundId,
                             const InvoicingInfo *invoicingInfo);

    // Retrieves the invoicing info associated with a shipment's root ID.
    QSharedPointer<InvoicingInfo> getInvoicingInfo(const QString &shipmentId) const;
    void publish(QDate &dateUntil); //Shipment updated are published and the original from source are ignored (when replaced) except if they were published already
    void clearUnpublished(); // Usefull if data were loaded with a bug. It will clear all unpublished
    void deleteDatabase(); // Usefull to reset + also for unit tests
    QMultiMap<QDateTime, QSharedPointer<Shipment>> getShipmentAndRefunds(
            const QDate &dateFrom
            , const QDate &dateTo
            , std::function<bool(const ActivitySource*, const Shipment*)> acceptCallback) const;
    QHash<ActivitySource, QMultiMap<QDateTime, QSharedPointer<Shipment>>> getActivitySource_ShipmentAndRefunds(
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
