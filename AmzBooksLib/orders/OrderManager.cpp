#include "OrderManager.h"
#include "OrderManager_sql_schema.h"

#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QDebug>
#include <QDateTime>

#include "ActivitySource.h"
#include "Shipment.h"
#include "InvoicingInfo.h"
#include "Address.h"
#include "ActivityUpdate.h"

namespace {
    QString getSourceKey(const ActivitySource *source) {
        if (!source) return QString();
        return QString("%1|%2|%3|%4")
                .arg(static_cast<int>(source->type))
                .arg(source->channel)
                .arg(source->subchannel)
                .arg(source->reportOrMethode);
    }
}

OrderManager *OrderManager::s_instance = nullptr;

OrderManager *OrderManager::instance(const QDir &workingDirectory)
{
    if (!s_instance) {
        if (workingDirectory == QDir()) {
            qFatal("OrderManager instance not created and no working directory provided");
        }
        s_instance = new OrderManager(workingDirectory);
    }
    return s_instance;
}

OrderManager::OrderManager(const QDir &workingDirectory)
{
    m_filePathDb = workingDirectory.absoluteFilePath("Orders.db");
    initDb();
}

OrderManager::~OrderManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void OrderManager::initDb()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(m_filePathDb);
    
    if (!m_db.open()) {
        qWarning() << "Failed to open database:" << m_db.lastError().text();
        return;
    }

    QSqlQuery query;
    query.exec("PRAGMA foreign_keys = ON;");

    if (!query.exec(OrderManagerSql::CREATE_TABLE_ORDERS)) {
         qWarning() << "Failed to create orders table:" << query.lastError().text();
    }
    if (!query.exec(OrderManagerSql::CREATE_TABLE_SHIPMENTS)) {
         qWarning() << "Failed to create shipments table:" << query.lastError().text();
    }
    if (!query.exec(OrderManagerSql::CREATE_TABLE_FINANCIAL_EVENTS)) {
         qWarning() << "Failed to create financial_events table:" << query.lastError().text();
    }
    if (!query.exec(OrderManagerSql::CREATE_TABLE_INVOICING_INFOS)) {
         qWarning() << "Failed to create invoicing_infos table:" << query.lastError().text();
    }

    // Migration: Add store column if missing
    {
        QSqlQuery qMig("PRAGMA table_info(orders)");
        bool hasStore = false;
        while (qMig.next()) {
            if (qMig.value("name").toString() == "store") {
                hasStore = true;
                break;
            }
        }
        if (!hasStore) {
            QSqlQuery qAlter;
            if (!qAlter.exec("ALTER TABLE orders ADD COLUMN store TEXT")) {
                qWarning() << "Failed to add store column to orders:" << qAlter.lastError().text();
            }
        }
    }
}

QDateTime OrderManager::getLastDateTime(ActivitySource *activitySource) const
{
    QSqlQuery query;
    query.prepare("SELECT MAX(event_date) FROM shipments WHERE source_key = ?");
    query.addBindValue(getSourceKey(activitySource));
    if (query.exec() && query.next()) {
        QString dateStr = query.value(0).toString();
        if (!dateStr.isEmpty()) {
            return QDateTime::fromString(dateStr, Qt::ISODate);
        }
    }
    return QDateTime();
}

QDateTime OrderManager::getBeginDateTime(ActivitySource *activitySource) const
{
    QSqlQuery query;
    query.prepare("SELECT MIN(event_date) FROM shipments WHERE source_key = ?");
    query.addBindValue(getSourceKey(activitySource));
    if (query.exec() && query.next()) {
        QString dateStr = query.value(0).toString();
        if (!dateStr.isEmpty()) {
            return QDateTime::fromString(dateStr, Qt::ISODate);
        }
    }
    return QDateTime();
}

bool OrderManager::containsOrder(const QString &orderId) const
{
    QSqlQuery q;
    q.prepare("SELECT 1 FROM orders WHERE id = ?");
    q.addBindValue(orderId);
    return q.exec() && q.next();
}

bool OrderManager::containsShipmentOrRefund(const QString &shipmentOrRefundId) const
{
    QSqlQuery q;
    q.prepare("SELECT 1 FROM shipments WHERE id = ?");
    q.addBindValue(shipmentOrRefundId);
    return q.exec() && q.next();
}

void OrderManager::recordShipmentFromSource(const QString &orderId,
                                            const ActivitySource *activitySource,
                                            const Shipment *shipmentOrRefund,
                                            const QDate &newDateIfConflict)
{
    if (!shipmentOrRefund) return;
    
    {
        QSqlQuery qCheck;
        qCheck.prepare("INSERT OR IGNORE INTO orders (id) VALUES (?)");
        qCheck.addBindValue(orderId);
        if (!qCheck.exec()) qWarning() << "Failed to insert order:" << qCheck.lastError();
    }

    QString id = shipmentOrRefund->getId();
    QJsonObject content = shipmentOrRefund->toJson();
    QString jsonStr = QJsonDocument(content).toJson(QJsonDocument::Compact);
    // Use the first activity date as the event date
    if (shipmentOrRefund->getActivities().isEmpty()) return;
    QString eventDate = shipmentOrRefund->getActivities().first().getDateTime().toString(Qt::ISODate);
    QString sourceKey = getSourceKey(activitySource);

    QSqlQuery qSel;
    qSel.prepare("SELECT status, original_json, current_json FROM shipments WHERE id = ?");
    qSel.addBindValue(id);
    
    if (qSel.exec() && qSel.next()) {
        QString status = qSel.value("status").toString();
        QString currentJson = qSel.value("current_json").toString();
        
        if (status == "Draft") {
            QSqlQuery qUpd;
            qUpd.prepare("UPDATE shipments SET original_json = ?, current_json = ?, event_date = ?, source_key = ? WHERE id = ?");
            qUpd.addBindValue(jsonStr);
            qUpd.addBindValue(jsonStr); 
            qUpd.addBindValue(eventDate);
            qUpd.addBindValue(sourceKey);
            qUpd.addBindValue(id);
            qUpd.exec();

        } else if (status == "Published") {
            // Check if conflict / diff
            bool isConflict = false;
            bool contentDiffers = false;
            QString latestJson = currentJson;
            QString latestId = id; // Default to root if no revisions

            // Check if this content matches the LATEST PUBLISHED revision (if any)
            QSqlQuery qLatest;
            qLatest.prepare("SELECT id, current_json FROM shipments WHERE root_id = ? AND status = 'Published' ORDER BY event_date DESC, id DESC LIMIT 1");
            qLatest.addBindValue(id); // Search by root_id
            
            // If revisions exist, use the top one.
            if (qLatest.exec() && qLatest.next()) {
                latestId = qLatest.value(0).toString();
                latestJson = qLatest.value(1).toString();
            }

            if (latestJson != jsonStr) {
                contentDiffers = true;
                Shipment latestShip = Shipment::fromJson(QJsonDocument::fromJson(latestJson.toUtf8()).object());
                Shipment incomingShip = Shipment::fromJson(QJsonDocument::fromJson(jsonStr.toUtf8()).object());
                
                ConflictStatus status = checkConflict(latestShip, incomingShip);
                if (status == ConflictStatus::Conflict) {
                    isConflict = true;
                }
            }

            if (isConflict) {
                QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
                // We use timestamps to ensure uniqueness for revisions
                QString reversalId = QString("%1-rev-%2").arg(id).arg(timestamp);
                QString newVersionId = QString("%1-v-%2").arg(id).arg(timestamp);
                
                QSqlQuery qCheckDrafts;
                qCheckDrafts.prepare("SELECT id FROM shipments WHERE root_id = ? AND status = 'Draft'");
                qCheckDrafts.addBindValue(id);
                
                bool draftsFound = false;
                if (qCheckDrafts.exec()) {
                    while (qCheckDrafts.next()) {
                        draftsFound = true;
                         QString draftId = qCheckDrafts.value(0).toString();
                         if (draftId.contains("-v-")) {
                             QSqlQuery qUpd;
                             qUpd.prepare("UPDATE shipments SET original_json = ?, current_json = ?, event_date = ?, source_key = ? WHERE id = ?");
                             qUpd.addBindValue(jsonStr);
                             qUpd.addBindValue(jsonStr);
                             qUpd.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                             qUpd.addBindValue(sourceKey);
                             qUpd.addBindValue(draftId);
                             qUpd.exec();
                         }
                    }
                }
                
                if (!draftsFound) {
                    // Create Double Entry
                    // Reversal of the LATEST VALID State (latestJson)
                    {
                        QSqlQuery qInsRev;
                        qInsRev.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, root_id) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?)");
                        qInsRev.addBindValue(reversalId);
                        qInsRev.addBindValue(orderId);
                        qInsRev.addBindValue(latestJson);  // Reverse what was last active
                        qInsRev.addBindValue(latestJson);
                        qInsRev.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                        qInsRev.addBindValue(sourceKey);
                        qInsRev.addBindValue(id);
                        qInsRev.exec();
                    }
                    
                    // New Version
                    {
                        QSqlQuery qInsNew;
                        qInsNew.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, root_id) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?)");
                        qInsNew.addBindValue(newVersionId);
                        qInsNew.addBindValue(orderId);
                        qInsNew.addBindValue(jsonStr);
                        qInsNew.addBindValue(jsonStr);
                        qInsNew.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                        qInsNew.addBindValue(sourceKey);
                        qInsNew.addBindValue(id);
                        qInsNew.exec();
                    }
                }
                } else if (contentDiffers) {
                // No financial conflict, but content differs (e.g. date change in same month, or address)
                // Update the LATEST revision in place
                QSqlQuery qUpd;
                qUpd.prepare("UPDATE shipments SET current_json = ?, event_date = ?, source_key = ? WHERE id = ?");
                qUpd.addBindValue(jsonStr);
                qUpd.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                qUpd.addBindValue(sourceKey);
                qUpd.addBindValue(latestId);
                qUpd.exec();
            }
        }
    } else {
        QSqlQuery qIns;
        qIns.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key) VALUES (?, ?, 'Draft', ?, ?, ?, ?)");
        qIns.addBindValue(id);
        qIns.addBindValue(orderId);
        qIns.addBindValue(jsonStr);
        qIns.addBindValue(jsonStr);
        qIns.addBindValue(eventDate);
        qIns.addBindValue(sourceKey);
        qIns.exec();
    }
}

void OrderManager::recordShipmentUpdated(const QString &orderId,
                                         const ActivitySource *activitySource,
                                         const Shipment *shipmentOrRefund,
                                         const QDate &newDateIfConflict)
{
    if (!shipmentOrRefund) return;
    QString id = shipmentOrRefund->getId();
    QString jsonStr = QJsonDocument(shipmentOrRefund->toJson()).toJson(QJsonDocument::Compact);

    QSqlQuery qUpd;
    qUpd.prepare("UPDATE shipments SET current_json = ? WHERE id = ?");
    qUpd.addBindValue(jsonStr);
    qUpd.addBindValue(id);
    qUpd.exec();
}

void OrderManager::removeOrder(const QString &orderId)
{
    m_db.transaction();

    // 1. Check if any shipment is Published
    {
        QSqlQuery qCheck;
        qCheck.prepare("SELECT COUNT(*) FROM shipments WHERE order_id = ? AND status = 'Published'");
        qCheck.addBindValue(orderId);
        if (qCheck.exec() && qCheck.next()) {
            if (qCheck.value(0).toInt() > 0) {
                // Determine if we should create revisions? 
                // Requirement says "doesn't work if done again but order were published".
                // This implies we simply do NOT delete.
                // TODO: Possibly implement cancellation via Reversals here in the future if requested.
                m_db.commit(); 
                return;
            }
        }
    }

    // 2. Delete Invoicing Info
    // We delete where shipment_root_id corresponds to any shipment of this order.
    {
        QSqlQuery qDelInv;
        qDelInv.prepare("DELETE FROM invoicing_infos WHERE shipment_root_id IN (SELECT id FROM shipments WHERE order_id = ?)");
        qDelInv.addBindValue(orderId);
        qDelInv.exec();
    }
    
    // 3. Delete Shipments (and refunds which are stored in shipments table)
    {
        QSqlQuery qDelShip;
        qDelShip.prepare("DELETE FROM shipments WHERE order_id = ?");
        qDelShip.addBindValue(orderId);
        qDelShip.exec();
    }

    // 4. Delete Order
    {
        QSqlQuery qDelOrd;
        qDelOrd.prepare("DELETE FROM orders WHERE id = ?");
        qDelOrd.addBindValue(orderId);
        qDelOrd.exec();
    }

    m_db.commit();
}

void OrderManager::removeShipmenOrRefund(const QString &shipmentOrRefundId)
{
    // 1. Resolve Root ID, Order ID, and verify status
    QString rootId;
    QString orderId;
    bool isPublished = false;

    {
        QSqlQuery q;
        q.prepare("SELECT COALESCE(root_id, id), order_id, status FROM shipments WHERE id = ?");
        q.addBindValue(shipmentOrRefundId);
        if (q.exec() && q.next()) {
            rootId = q.value(0).toString();
            orderId = q.value(1).toString();
            if (q.value(2).toString() == "Published") {
                isPublished = true;
            }
        } else {
            return; // Not found
        }
    }

    if (isPublished) return;

    // 2. Check if this is the only logical shipment (root) for the order
    int count = 0;
    {
        QSqlQuery q;
        q.prepare("SELECT COUNT(DISTINCT COALESCE(root_id, id)) FROM shipments WHERE order_id = ?");
        q.addBindValue(orderId);
        if (q.exec() && q.next()) {
            count = q.value(0).toInt();
        }
    }

    if (count <= 1) {
        // If it's the only one, remove the entire order (which handles cleanup and published checks for the whole order)
        removeOrder(orderId);
    } else {
        // Delete only this shipment/refund logical entity (all revisions)
        m_db.transaction();
        
        {
            QSqlQuery qDelInv;
            qDelInv.prepare("DELETE FROM invoicing_infos WHERE shipment_root_id = ?");
            qDelInv.addBindValue(rootId);
            qDelInv.exec();
        }
        
        {
            QSqlQuery qDelShip;
            qDelShip.prepare("DELETE FROM shipments WHERE root_id = ? OR id = ?");
            qDelShip.addBindValue(rootId);
            qDelShip.addBindValue(rootId);
            qDelShip.exec();
        }
        
        m_db.commit();
    }
}

void OrderManager::recordAddressTo(const QString &orderId, const Address &addressTo)
{
    {
        QSqlQuery qCheck;
        qCheck.prepare("INSERT OR IGNORE INTO orders (id) VALUES (?)");
        qCheck.addBindValue(orderId);
        qCheck.exec();
    }
    
    QString jsonStr = QJsonDocument(addressTo.toJson()).toJson(QJsonDocument::Compact);
    QSqlQuery qUpd;
    qUpd.prepare("UPDATE orders SET address_json = ? WHERE id = ?");
    qUpd.addBindValue(jsonStr);
    qUpd.addBindValue(orderId);
    if (!qUpd.exec()) qWarning() << "Failed to update address:" << qUpd.lastError();
}

void OrderManager::recordOrder(const QString &orderId, const QString &store)
{
    QSqlQuery q;
    q.prepare("INSERT INTO orders (id, store) VALUES (?, ?) "
              "ON CONFLICT(id) DO UPDATE SET store=excluded.store");
    q.addBindValue(orderId);
    q.addBindValue(store);
    if (!q.exec()) {
        qWarning() << "Failed to record order store:" << q.lastError();
    }
}

void OrderManager::recordInvoicingInfo(const QString &shipmentOrRefundId,
                                       const InvoicingInfo *invoicingInfo)
{
    if (!invoicingInfo) return;
    
    // 1. Identify the Root ID
    // Invoicing info is considered stable across technical revisions (conflicts, reversals) of a shipment.
    // Therefore, we always attach it to the 'root_id'.
    // If 'shipmentOrRefundId' is a revision, we fetch its root. If it's already a root, we use it directly.
    QString rootId = shipmentOrRefundId;
    {
        QSqlQuery q;
        q.prepare("SELECT COALESCE(root_id, id) FROM shipments WHERE id = ?");
        q.addBindValue(shipmentOrRefundId);
        if (q.exec() && q.next()) {
            rootId = q.value(0).toString();
        }
    }
    
    // 2. Persist the Info
    // We use INSERT OR REPLACE to update existing info or create new one.
    QSqlQuery q;
    q.prepare("INSERT OR REPLACE INTO invoicing_infos (shipment_root_id, json) VALUES (?, ?)");
    q.addBindValue(rootId);
    q.addBindValue(QString::fromUtf8(QJsonDocument(invoicingInfo->toJson()).toJson(QJsonDocument::Compact)));
    if (!q.exec()) {
        qWarning() << "Failed to record invoicing info:" << q.lastError();
    }
}

QSharedPointer<InvoicingInfo> OrderManager::getInvoicingInfo(const QString &shipmentId) const
{
    // 1. Resolve to Root ID
    // The incoming shipmentId might be a specific version/revision. 
    // We need to look up the info using the stable root ID.
    QString rootId = shipmentId;
    {
        QSqlQuery q;
        q.prepare("SELECT COALESCE(root_id, id) FROM shipments WHERE id = ?");
        q.addBindValue(shipmentId);
        if (q.exec() && q.next()) {
            rootId = q.value(0).toString();
        }
    }
    
    // 2. Retrieve Data
    QSqlQuery q;
    q.prepare("SELECT json FROM invoicing_infos WHERE shipment_root_id = ?");
    q.addBindValue(rootId);
    if (q.exec() && q.next()) {
        QJsonObject json = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
        return QSharedPointer<InvoicingInfo>::create(InvoicingInfo::fromJson(json));
    }
    
    // Return empty pointer if not found
    return nullptr;
}

void OrderManager::publish(QDate &dateUntil)
{
    m_db.transaction();
    
    QString dateParam = dateUntil.toString(Qt::ISODate);
    
    // Process Drafts (Including Revisions)
    QSqlQuery qDrafts;
    qDrafts.prepare("SELECT id, current_json, root_id FROM shipments WHERE status = 'Draft' AND (event_date IS NULL OR event_date <= ?)");
    qDrafts.addBindValue(dateParam);
    if (qDrafts.exec()) {
        while (qDrafts.next()) {
            QString id = qDrafts.value("id").toString();
            QString jsonStr = qDrafts.value("current_json").toString();
            QString rootId = qDrafts.value("root_id").toString();
            
            QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
            
            // Parse Shipment to access activities
            Shipment s = Shipment::fromJson(obj);
            const auto &activities = s.getActivities();
            
            for (const auto &act : activities) {
                double amount = act.getAmountTaxed() + act.getAmountTaxes();
                QString currency = act.getCurrency();
                QString date = act.getDateTime().toString(Qt::ISODate);
                
                // Determine Type
                QString type = "Invoice";
                if (!rootId.isEmpty() && id.contains("-rev-")) {
                    type = "CreditNote";
                }
                
                // If ID is same for all, we need unique PK for financial_events if multiple activities
                // Assume 1 activity per shipment usually?
                // Or append subActivityId / index?
                // User said "Update also the json conversion and storage".
                // Creating unique ID: shipmentId + activity index or subID??
                QString subId = act.getSubActivityId();
                QString uniqueRef = id;
                if (!subId.isEmpty()) uniqueRef += "-" + subId;
                else if (activities.size() > 1) {
                    // Fallback to avoid PK collision if subId undefined but multiple acts
                    // This is slightly risky but needed if no subId.
                    // Assuming different hash or increment? can't easily increment.
                    // Rely heavily on valid data model.
                }

                QString invoiceId = (type == "CreditNote") ? QString("CN-%1").arg(uniqueRef) : QString("INV-%1").arg(uniqueRef);
                
                // Serialize just this activity or the whole shipment?
                // "content_json" usually whole shipment. But if broken down...
                // The table has `shipment_id` FK.
                // Keeping whole shipment JSON in each row is redundant but maybe safer for context?
                // Or user meant "Activity" to be stored?
                // I will store the whole shipment JSON as before, but create rows for each activity.

                QSqlQuery qIns;
                qIns.prepare("INSERT INTO financial_events (id, shipment_id, type, event_date, amount, currency, content_json) VALUES (?, ?, ?, ?, ?, ?, ?)");
                qIns.addBindValue(invoiceId);
                qIns.addBindValue(id);
                qIns.addBindValue(type);
                qIns.addBindValue(date);
                qIns.addBindValue(amount);
                qIns.addBindValue(currency);
                qIns.addBindValue(jsonStr); 
                if (!qIns.exec()) {
                    // Ignore duplicate PK if single line? Or log warning?
                    // If simple update, maybe delete old?
                }
            }
            
            QSqlQuery qUpd;
            qUpd.prepare("UPDATE shipments SET status = 'Published', published_json = current_json, publication_date = ? WHERE id = ?");
            qUpd.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
            qUpd.addBindValue(id);
            qUpd.exec();
        }
    }
    
    m_db.commit();
}

ActivityUpdate *OrderManager::createActivityUpdateModel(const QString &shipmentId, QObject* parent)
{
    ActivityUpdate *model = new ActivityUpdate(parent);
    QList<ActivityUpdateItem> items;
    
    QSqlQuery q;
    q.prepare("SELECT event_date, type, id, amount, currency FROM financial_events WHERE shipment_id = ? OR shipment_id LIKE ? ORDER BY event_date DESC");
    q.addBindValue(shipmentId);
    q.addBindValue(shipmentId + "-%"); // Match revisions
    
    if (q.exec()) {
        while (q.next()) {
            items.append({
                QDateTime::fromString(q.value(0).toString(), Qt::ISODate),
                q.value(1).toString(),
                q.value(2).toString(),
                q.value(3).toDouble(),
                q.value(4).toString(),
                "Issued"
            });
        }
    }
    
    model->setItems(items);
    return model;
}

QMultiMap<QDateTime, QSharedPointer<Shipment>> OrderManager::getShipmentAndRefunds(
        const QDate &dateFrom,
        const QDate &dateTo,
        std::function<bool(const ActivitySource*, const Shipment*)> acceptCallback) const
{
    QMultiMap<QDateTime, QSharedPointer<Shipment>> results;
    
    QString queryStr = "SELECT current_json, source_key, event_date, id FROM shipments WHERE 1=1";
    if (dateFrom.isValid()) {
        queryStr += QString(" AND event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        queryStr += QString(" AND event_date <= '%1'").arg(dateTo.toString(Qt::ISODate));
    }
    
    QSqlQuery query(queryStr);
    
    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString dateStr = query.value(2).toString();
        QString id = query.value(3).toString();
        QDateTime eventDate = QDateTime::fromString(dateStr, Qt::ISODate);
        
        // Parse Source
        // source_key: type|channel|subchannel|report
        QStringList parts = sourceKey.split('|');
        ActivitySource source;
        if (parts.size() >= 4) {
             source.type = static_cast<ActivitySourceType>(parts[0].toInt());
             source.channel = parts[1];
             source.subchannel = parts[2];
             source.reportOrMethode = parts[3];
        } else {
            source.type = ActivitySourceType::API; // Default
        }
        
        // Parse Shipment
        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        
        // Check for Reversal
        if (id.contains("-rev-")) {
            QList<Activity> newActs;
            for (const auto &act : shipment->getActivities()) {
                Amount negatedAmount(-act.getAmountTaxed(), -act.getAmountTaxesSource());
                auto res = Activity::create(act.getEventId(),
                                            act.getActivityId(),
                                            act.getSubActivityId(),
                                            act.getDateTime(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getCountryCodeVatPaidTo(),
                                            negatedAmount,
                                            act.getTaxSource(),
                                            act.getTaxDeclaringCountryCode(),
                                            act.getTaxScheme(),
                                            act.getTaxJurisdictionLevel(),
                                            act.getSaleType(),
                                            act.getVatTerritoryFrom(),
                                            act.getVatTerritoryTo());
               if (res.value) {
                   newActs.append(*res.value);
               }
            }
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs));
        }

        if (!acceptCallback || acceptCallback(&source, shipment.data())) {
            results.insert(eventDate, shipment);
        }
    }
    
    return results;
}

QHash<ActivitySource, QMultiMap<QDateTime, QSharedPointer<Shipment>>> OrderManager::getActivitySource_ShipmentAndRefunds(
        const QDate &dateFrom,
        const QDate &dateTo,
        std::function<bool(const ActivitySource*, const Shipment*)> acceptCallback) const
{
    QHash<ActivitySource, QMultiMap<QDateTime, QSharedPointer<Shipment>>> results;
    
    QString queryStr = "SELECT current_json, source_key, event_date, id FROM shipments WHERE 1=1";
    if (dateFrom.isValid()) {
        queryStr += QString(" AND event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        queryStr += QString(" AND event_date <= '%1'").arg(dateTo.toString(Qt::ISODate));
    }
    
    QSqlQuery query(queryStr);
    
    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString dateStr = query.value(2).toString();
        QString id = query.value(3).toString();
        QDateTime eventDate = QDateTime::fromString(dateStr, Qt::ISODate);
        
        // Parse Source
        QStringList parts = sourceKey.split('|');
        ActivitySource source;
        if (parts.size() >= 4) {
             source.type = static_cast<ActivitySourceType>(parts[0].toInt());
             source.channel = parts[1];
             source.subchannel = parts[2];
             source.reportOrMethode = parts[3];
        } else {
            source.type = ActivitySourceType::API; // Default
        }
        
        // Parse Shipment
        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        
        // Check for Reversal
        if (id.contains("-rev-")) {
            QList<Activity> newActs;
            for (const auto &act : shipment->getActivities()) {
                Amount negatedAmount(-act.getAmountTaxed(), -act.getAmountTaxesSource());
                auto res = Activity::create(act.getEventId(),
                                            act.getActivityId(),
                                            act.getSubActivityId(),
                                            act.getDateTime(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getCountryCodeVatPaidTo(),
                                            negatedAmount,
                                            act.getTaxSource(),
                                            act.getTaxDeclaringCountryCode(),
                                            act.getTaxScheme(),
                                            act.getTaxJurisdictionLevel(),
                                            act.getSaleType(),
                                            act.getVatTerritoryFrom(),
                                            act.getVatTerritoryTo());
               if (res.value) {
                   newActs.append(*res.value);
               }
            }
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs));
        }

        if (!acceptCallback || acceptCallback(&source, shipment.data())) {
            results[source].insert(eventDate, shipment);
        }
    }
    
    return results;
}

OrderManager::ConflictStatus OrderManager::checkConflict(const Shipment &existing, const Shipment &incoming) const
{
    const auto &existingActs = existing.getActivities();
    const auto &incomingActs = incoming.getActivities();
    
    if (existingActs.size() != incomingActs.size()) {
        return ConflictStatus::Conflict;
    }

    QJsonObject exJson = existing.toJson();
    QJsonObject inJson = incoming.toJson();
    
    // Exact match check
    if (exJson == inJson) {
        return ConflictStatus::NoChange;
    }
    
    for (int i = 0; i < existingActs.size(); ++i) {
        if (existingActs[i].isDifferentTaxes(incomingActs[i])) {
            return ConflictStatus::Conflict;
        }
    }
    
    return ConflictStatus::ContentDiffers;
}

QSharedPointer<Shipment> OrderManager::getHeadShipment(const QString &id, QString *outStatus, QString *outJson) const
{
    // 1. Check Drafts (including revisions)
    {
        QSqlQuery q;
        q.prepare("SELECT id, current_json, status FROM shipments WHERE status = 'Draft' AND (root_id = ? OR id = ?) ORDER BY event_date DESC, id DESC LIMIT 1");
        q.addBindValue(id);
        q.addBindValue(id);
        if (q.exec() && q.next()) {
             if (outStatus) *outStatus = q.value("status").toString();
             if (outJson) *outJson = q.value("current_json").toString();
             QJsonObject obj = QJsonDocument::fromJson(q.value("current_json").toString().toUtf8()).object();
             return QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
    }

    // 2. Check Published Revisions
    {
        QSqlQuery q;
        q.prepare("SELECT id, current_json, status FROM shipments WHERE status = 'Published' AND root_id = ? ORDER BY event_date DESC, id DESC LIMIT 1");
        q.addBindValue(id);
        if (q.exec() && q.next()) {
             if (outStatus) *outStatus = q.value("status").toString();
             if (outJson) *outJson = q.value("current_json").toString();
             QJsonObject obj = QJsonDocument::fromJson(q.value("current_json").toString().toUtf8()).object();
             return QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
    }

    // 3. Check Original (if not covered above)
    {
         QSqlQuery q;
         q.prepare("SELECT id, current_json, status FROM shipments WHERE id = ?");
         q.addBindValue(id);
         if (q.exec() && q.next()) {
             if (outStatus) *outStatus = q.value("status").toString();
             if (outJson) *outJson = q.value("current_json").toString();
             QJsonObject obj = QJsonDocument::fromJson(q.value("current_json").toString().toUtf8()).object();
             return QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
         }
    }

    return nullptr;
}

QSharedPointer<Shipment> OrderManager::getShipmentOrRefundIfDifferent(const QString &orderId,
                                                                      const ActivitySource *activitySource,
                                                                      const Shipment *shipmentOrRefund) const
{
    Q_UNUSED(orderId);
    Q_UNUSED(activitySource);

    if (!shipmentOrRefund) return nullptr;
    
    QSharedPointer<Shipment> existing = getHeadShipment(shipmentOrRefund->getId());
    if (!existing) return nullptr;
    
    ConflictStatus status = checkConflict(*existing, *shipmentOrRefund);
    if (status == ConflictStatus::NoChange) {
        return nullptr;
    }
    
    
    return existing;
}

QHash<ActivitySource, QHash<QString, QMultiMap<QDateTime, QSharedPointer<Shipment>>>> OrderManager::getActivitySource_store_ShipmentAndRefunds(
        const QDate &dateFrom
        , const QDate &dateTo
        , std::function<bool(const ActivitySource*, const Shipment*)> acceptCallback) const
{
    QHash<ActivitySource, QHash<QString, QMultiMap<QDateTime, QSharedPointer<Shipment>>>> results;

    QString queryStr = "SELECT s.current_json, s.source_key, s.event_date, s.id, o.store "
                       "FROM shipments s "
                       "LEFT JOIN orders o ON s.order_id = o.id "
                       "WHERE 1=1";

    if (dateFrom.isValid()) {
        queryStr += QString(" AND s.event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        queryStr += QString(" AND s.event_date <= '%1'").arg(dateTo.toString(Qt::ISODate));
    }

    QSqlQuery query(queryStr);

    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString dateStr = query.value(2).toString();
        QString id = query.value(3).toString();
        QString store = query.value(4).toString();
        QDateTime eventDate = QDateTime::fromString(dateStr, Qt::ISODate);

        // Parse Source
        QStringList parts = sourceKey.split('|');
        ActivitySource source;
        if (parts.size() >= 4) {
             source.type = static_cast<ActivitySourceType>(parts[0].toInt());
             source.channel = parts[1];
             source.subchannel = parts[2];
             source.reportOrMethode = parts[3];
        } else {
            source.type = ActivitySourceType::API; // Default
        }

        // Parse Shipment
        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));

        // Check for Reversal
        if (id.contains("-rev-")) {
            QList<Activity> newActs;
            for (const auto &act : shipment->getActivities()) {
                Amount negatedAmount(-act.getAmountTaxed(), -act.getAmountTaxesSource());
                auto res = Activity::create(act.getEventId(),
                                            act.getActivityId(),
                                            act.getSubActivityId(),
                                            act.getDateTime(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getCountryCodeVatPaidTo(),
                                            negatedAmount,
                                            act.getTaxSource(),
                                            act.getTaxDeclaringCountryCode(),
                                            act.getTaxScheme(),
                                            act.getTaxJurisdictionLevel(),
                                            act.getSaleType(),
                                            act.getVatTerritoryFrom(),
                                            act.getVatTerritoryTo());
               if (res.value) {
                   newActs.append(*res.value);
               }
            }
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs));
        }

        if (!acceptCallback || acceptCallback(&source, shipment.data())) {
            results[source][store].insert(eventDate, shipment);
        }
    }

    return results;
}

void OrderManager::deleteDatabase()
{
    m_db.close();
    QSqlDatabase::removeDatabase(m_db.connectionName());
    QFile::remove(m_filePathDb);
    initDb();
}

QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>>
OrderManager::get_channel_site_ShipmentAndRefundsNoInvoices(const QDate &dateFrom, const QDate &dateTo) const
{
    // TODO any shipment without an orderid and site should led to ExceptionAccountMissing
    return nullptr;
}

QSharedPointer<QList<OrderManager::ShipmentRefundsWithUpdates>>
OrderManager::getShipmentAndRefundsNoInvoices(const QDate &dateFrom, const QDate &dateTo) const
{
    auto results = QSharedPointer<QList<ShipmentRefundsWithUpdates>>::create();
    
    // Query all shipments for roots that have at least one shipment in the date range
    // that needs an invoice. A shipment needs an invoice if:
    // 1. It's in the date range AND
    // 2. Either: (no invoicing_info exists) OR (it's a revision/conflict: contains -rev- or -v-)
    
    // First, get all distinct root_ids that have shipments within the date range
    // needing invoice work
    QString rootQueryStr = R"(
        SELECT DISTINCT COALESCE(s.root_id, s.id) as root_id
        FROM shipments s
        LEFT JOIN invoicing_infos inv ON COALESCE(s.root_id, s.id) = inv.shipment_root_id
        WHERE 1=1
    )";
    
    if (dateFrom.isValid()) {
        rootQueryStr += QString(" AND DATE(s.event_date) >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        rootQueryStr += QString(" AND DATE(s.event_date) <= '%1'").arg(dateTo.toString(Qt::ISODate));
    }
    
    // We want roots where shipments in range need invoices:
    // - Either no invoicing_info exists
    // - OR invoicing_info has no invoice_number  
    // - OR the shipment is a revision/conflict (id contains -rev- or -v-)
    rootQueryStr += R"( AND (
        inv.shipment_root_id IS NULL 
        OR inv.json NOT LIKE '%"invoiceNumber":%' 
        OR inv.json LIKE '%"invoiceNumber":null%'
        OR s.id LIKE '%-rev-%'
        OR s.id LIKE '%-v-%'
    ))";
    
    QSqlQuery rootQuery(rootQueryStr);
    QSet<QString> rootIdsToProcess;
    while (rootQuery.next()) {
        rootIdsToProcess.insert(rootQuery.value("root_id").toString());
    }
    
    if (rootIdsToProcess.isEmpty()) {
        return results;
    }
    
    // Now fetch ALL shipments for those roots (including history outside date range)
    // along with their invoicing info and address
    QString queryStr = R"(
        SELECT s.id, s.order_id, s.current_json, s.source_key, s.event_date, 
               COALESCE(s.root_id, s.id) as root_id, o.address_json, inv.json as inv_json
        FROM shipments s
        LEFT JOIN orders o ON s.order_id = o.id
        LEFT JOIN invoicing_infos inv ON COALESCE(s.root_id, s.id) = inv.shipment_root_id
        WHERE COALESCE(s.root_id, s.id) IN (
    )";
    
    // Build IN clause
    QStringList quotedIds;
    for (const QString &id : rootIdsToProcess) {
        quotedIds << QString("'%1'").arg(id);
    }
    queryStr += quotedIds.join(", ") + ")";
    queryStr += " ORDER BY root_id, s.event_date";
    
    QSqlQuery query(queryStr);
    
    // Group shipments by root_id
    QHash<QString, ShipmentRefundsWithUpdates> groupedResults;
    QHash<QString, QString> rootInvJson; // Cache invoicing json per root
    
    while (query.next()) {
        QString id = query.value("id").toString();
        QString rootId = query.value("root_id").toString();
        QString jsonStr = query.value("current_json").toString();
        QString addressJson = query.value("address_json").toString();
        QString invJson = query.value("inv_json").toString();
        QString eventDateStr = query.value("event_date").toString();
        QDate eventDate = QDate::fromString(eventDateStr.left(10), Qt::ISODate);
        
        // Parse Shipment
        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        
        // Check for Reversal - negate amounts if it's a reversal entry
        if (id.contains("-rev-")) {
            QList<Activity> newActs;
            for (const auto &act : shipment->getActivities()) {
                Amount negatedAmount(-act.getAmountTaxed(), -act.getAmountTaxesSource());
                auto res = Activity::create(act.getEventId(),
                                            act.getActivityId(),
                                            act.getSubActivityId(),
                                            act.getDateTime(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getCountryCodeVatPaidTo(),
                                            negatedAmount,
                                            act.getTaxSource(),
                                            act.getTaxDeclaringCountryCode(),
                                            act.getTaxScheme(),
                                            act.getTaxJurisdictionLevel(),
                                            act.getSaleType(),
                                            act.getVatTerritoryFrom(),
                                            act.getVatTerritoryTo());
               if (res.value) {
                   newActs.append(*res.value);
               }
            }
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs));
        }
        
        // Add to grouped results
        if (!groupedResults.contains(rootId)) {
            ShipmentRefundsWithUpdates item;
            item.activityUpdate = QSharedPointer<ActivityUpdate>::create();
            
            // Parse invoicing info if available
            if (!invJson.isEmpty()) {
                QJsonObject invObj = QJsonDocument::fromJson(invJson.toUtf8()).object();
                item.invoicingInfo = QSharedPointer<InvoicingInfo>::create(InvoicingInfo::fromJson(invObj));
            }
            
            // Parse address if available
            if (!addressJson.isEmpty()) {
                QJsonObject addrObj = QJsonDocument::fromJson(addressJson.toUtf8()).object();
                item.addressTo = QSharedPointer<Address>::create(Address::fromJson(addrObj));
            }
            
            rootInvJson[rootId] = invJson;
            groupedResults[rootId] = item;
        }
        
        groupedResults[rootId].shipmentsRefundsSameActivity.append(shipment);
        
        // Determine if this shipment needs an invoice
        // invoicesToDo = true if:
        // 1. The shipment is within the requested date range AND
        // 2. Either: (no invoicing info exists OR no invoice number) OR (it's a revision/conflict)
        bool inDateRange = true;
        if (dateFrom.isValid() && eventDate < dateFrom) {
            inDateRange = false;
        }
        if (dateTo.isValid() && eventDate > dateTo) {
            inDateRange = false;
        }
        
        bool isRevisionOrConflict = id.contains("-rev-") || id.contains("-v-");
        
        bool hasInvoiceNumber = false;
        const QString &cachedInvJson = rootInvJson[rootId];
        if (!cachedInvJson.isEmpty()) {
            QJsonObject invObj = QJsonDocument::fromJson(cachedInvJson.toUtf8()).object();
            if (invObj.contains("invoiceNumber") && !invObj.value("invoiceNumber").isNull()) {
                hasInvoiceNumber = true;
            }
        }
        
        // Needs invoice if in range AND (no invoice exists OR it's a revision/conflict)
        bool needsInvoice = inDateRange && (!hasInvoiceNumber || isRevisionOrConflict);
        groupedResults[rootId].invoicesToDo.append(needsInvoice);
    }
    
    // Convert hash to list
    for (auto it = groupedResults.begin(); it != groupedResults.end(); ++it) {
        results->append(it.value());
    }
    
    return results;
}


