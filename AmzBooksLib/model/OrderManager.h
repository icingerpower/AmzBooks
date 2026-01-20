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

#include "Address.h"

class ActivitySource;
class Shipment;
class InvoicingInfo;

class OrderManager
{
public:
    OrderManager(const QString &workingDirectory);
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
    void recordInvoicingInfo(const QString &shipmentOrRefundId,
                             const InvoicingInfo *invoicingInfo);
    void publish(QDate &dateUntil); //Shipment updated are published and the original from source are ignored (when replaced) except if they were published already
    void clearUnpublished(); // Usefull if data were loaded with a bug. It will clear all unpublished
    void deleteDatabase(); // Usefull to reset + also for unit tests
    // QMultiMap<QDateTime, QSharedPointer<Shipment>> getShipmentAndRefunds(const QDate &dateFrom, const QDate &dateTo) const; // Here updated shipment replace original shipment
    // void publish(const QDate &dateUntil);
    // TODO invoices / orders / linkage to accounting generation
    // Link shipment by ActivitySource
    void copyDatabase(const QString &filePath, int yearUntil); // To archive all orders
    void removeInDatabase(int yearUntil); // To remove old data

private:
    QString m_filePathDb;
};

#endif // ORDERMANAGER_H
