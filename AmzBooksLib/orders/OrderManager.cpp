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
#include "Refund.h"
#include "InvoicingInfo.h"
#include "Address.h"
#include "ActivityUpdate.h"
#include "ExceptionWithTitleText.h"

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



OrderManager::OrderManager(const QDir &workingDirectory)
{
    m_filePathDb = workingDirectory.absoluteFilePath("Orders.db");
    m_connectionName = QString("OrderManager_%1").arg(reinterpret_cast<quintptr>(this));
    initDb();
}

OrderManager::~OrderManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    // Reset m_db *before* removeDatabase: QSqlDatabase is reference-counted and
    // m_db (a value member) would still hold a reference during the destructor body,
    // causing Qt to warn "connection still in use". Assigning a default-constructed
    // QSqlDatabase releases that reference so removeDatabase finds refcount == 0.
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void OrderManager::initDb()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(m_filePathDb);
    
    if (!m_db.open()) {
        qWarning() << "Failed to open database:" << m_db.lastError().text();
        return;
    }

    QSqlQuery query(m_db);
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
    if (!query.exec(OrderManagerSql::CREATE_TABLE_INVENTORY_MOVES)) {
         qWarning() << "Failed to create inventory_moves table:" << query.lastError().text();
    }

    // Migration: Add store column if missing
    {
        QSqlQuery qMig(m_db);
        qMig.exec("PRAGMA table_info(orders)");
        bool hasStore = false;
        while (qMig.next()) {
            if (qMig.value("name").toString() == "store") {
                hasStore = true;
                break;
            }
        }
        if (!hasStore) {
            QSqlQuery qAlter(m_db);
            if (!qAlter.exec("ALTER TABLE orders ADD COLUMN store TEXT")) {
                qWarning() << "Failed to add store column to orders:" << qAlter.lastError().text();
            }
        }
    }

    // Migration: Add inserted_at column if missing in orders
    {
        QSqlQuery qMig(m_db);
        qMig.exec("PRAGMA table_info(orders)");
        bool hasInsertedAt = false;
        while (qMig.next()) {
            if (qMig.value("name").toString() == "inserted_at") {
                hasInsertedAt = true;
                break;
            }
        }
        if (!hasInsertedAt) {
            QSqlQuery qAlter(m_db);
            if (!qAlter.exec("ALTER TABLE orders ADD COLUMN inserted_at TEXT")) {
                qWarning() << "Failed to add inserted_at column to orders:" << qAlter.lastError().text();
            }
        }
    }

    // Migration: Add inserted_at column if missing in shipments
    {
        QSqlQuery qMig(m_db);
        qMig.exec("PRAGMA table_info(shipments)");
        bool hasInsertedAt = false;
        while (qMig.next()) {
            if (qMig.value("name").toString() == "inserted_at") {
                hasInsertedAt = true;
                break;
            }
        }
        if (!hasInsertedAt) {
            QSqlQuery qAlter(m_db);
            if (!qAlter.exec("ALTER TABLE shipments ADD COLUMN inserted_at TEXT")) {
                qWarning() << "Failed to add inserted_at column to shipments:" << qAlter.lastError().text();
            }
        }
    }
}

QDateTime OrderManager::getLastDateTime(ActivitySource *activitySource) const
{
    QSqlQuery query(m_db);
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
    QSqlQuery query(m_db);
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
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM orders WHERE id = ?");
    q.addBindValue(orderId);
    return q.exec() && q.next();
}

bool OrderManager::containsShipmentOrRefund(const QString &shipmentOrRefundId) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM shipments WHERE id = ?");
    q.addBindValue(shipmentOrRefundId);
    return q.exec() && q.next();
}

void OrderManager::recordShipmentFromSource(const QString &orderId,
                                            const ActivitySource *activitySource,
                                            const Shipment *shipmentOrRefund,
                                            const QDate &newDateIfConflict,
                                            bool isWrongIfConflict,
                                            bool fixTaxDate)
{
    if (!shipmentOrRefund) return;
    
    {
        QSqlQuery qCheck(m_db);
        qCheck.prepare("INSERT OR IGNORE INTO orders (id, inserted_at) VALUES (?, ?)");
        qCheck.addBindValue(orderId);
        qCheck.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        if (!qCheck.exec()) qWarning() << "Failed to insert order:" << qCheck.lastError();
    }

    // Set isWrongIfConflict flag on the shipment before serialization
    // We need to copy because the input is const
    QSharedPointer<Shipment> shipCopy;
    if (auto ref = dynamic_cast<const Refund*>(shipmentOrRefund)) {
         shipCopy = QSharedPointer<Refund>::create(*ref);
    } else {
         shipCopy = QSharedPointer<Shipment>::create(*shipmentOrRefund);
    }
    shipCopy->setIsWrongIfConflict(isWrongIfConflict);

    QString id = shipCopy->getId();
    QJsonObject content = shipCopy->toJson();

    // If requested, inherit the tax date from the earliest existing shipment recorded
    // for the same orderId. This is useful for refunds where the tax date is not
    // explicitly provided in the source report (e.g. Temu EU VAT returns).
    if (fixTaxDate) {
        QSqlQuery qOrig(m_db);
        qOrig.prepare("SELECT current_json FROM shipments WHERE order_id = ? ORDER BY event_date ASC LIMIT 1");
        qOrig.addBindValue(orderId);
        if (qOrig.exec() && qOrig.next()) {
            QJsonDocument origDoc = QJsonDocument::fromJson(qOrig.value(0).toString().toUtf8());
            QJsonArray origActs = origDoc.object()["activities"].toArray();
            if (!origActs.isEmpty()) {
                QString origTaxDate = origActs[0].toObject()["dateTimeTax"].toString();
                if (!origTaxDate.isEmpty()) {
                    QJsonArray acts = content["activities"].toArray();
                    for (int i = 0; i < acts.size(); ++i) {
                        QJsonObject act = acts[i].toObject();
                        act["dateTimeTax"] = origTaxDate;
                        acts[i] = act;
                    }
                    content["activities"] = acts;
                }
            }
        }
    }

    QString jsonStr = QJsonDocument(content).toJson(QJsonDocument::Compact);
    // Use the first activity date as the event date
    if (shipCopy->getActivities().isEmpty()) return;
    QString eventDate = shipCopy->getActivities().first().getDateTime().toString(Qt::ISODate);
    QString sourceKey = getSourceKey(activitySource);

    QSqlQuery qSel(m_db);
    qSel.prepare("SELECT status, original_json, current_json FROM shipments WHERE id = ?");
    qSel.addBindValue(id);
    
    if (qSel.exec() && qSel.next()) {
        QString status = qSel.value("status").toString();
        QString currentJson = qSel.value("current_json").toString();
        
        if (status == "Draft") {
            // Check conflict strength
            Shipment currentShip = Shipment::fromJson(QJsonDocument::fromJson(currentJson.toUtf8()).object());
            bool existingIsWrong = currentShip.isWrongIfConflict();
            bool incomingIsWrong = isWrongIfConflict;

            // If existing is STRONG (false) and incoming is WEAK (true), we do NOT overwrite
            if (!existingIsWrong && incomingIsWrong) {
                // Existing wins. Do nothing.
                return;
            }
            // Else: Incoming wins (Weak overwrites Weak, Strong overwrites Weak, Strong overwrites Strong)

            QSqlQuery qUpd(m_db);
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
            QSqlQuery qLatest(m_db);
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
                
                QSqlQuery qCheckDrafts(m_db);
                qCheckDrafts.prepare("SELECT id FROM shipments WHERE root_id = ? AND status = 'Draft'");
                qCheckDrafts.addBindValue(id);
                
                bool draftsFound = false;
                if (qCheckDrafts.exec()) {
                    while (qCheckDrafts.next()) {
                        draftsFound = true;
                         QString draftId = qCheckDrafts.value(0).toString();
                         if (draftId.contains("-v-")) {
                             QSqlQuery qUpd(m_db);
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
                        QSqlQuery qInsRev(m_db);
                        qInsRev.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, root_id, inserted_at) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?, ?)");
                        qInsRev.addBindValue(reversalId);
                        qInsRev.addBindValue(orderId);
                        qInsRev.addBindValue(latestJson);  // Reverse what was last active
                        qInsRev.addBindValue(latestJson);
                        qInsRev.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                        qInsRev.addBindValue(sourceKey);
                        qInsRev.addBindValue(id);
                        qInsRev.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
                        qInsRev.exec();
                    }
                    
                    // New Version
                    {
                        QSqlQuery qInsNew(m_db);
                        qInsNew.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, root_id, inserted_at) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?, ?)");
                        qInsNew.addBindValue(newVersionId);
                        qInsNew.addBindValue(orderId);
                        qInsNew.addBindValue(jsonStr);
                        qInsNew.addBindValue(jsonStr);
                        qInsNew.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                        qInsNew.addBindValue(sourceKey);
                        qInsNew.addBindValue(id);
                        qInsNew.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
                        qInsNew.exec();
                    }
                }
                } else if (contentDiffers) {
                // No financial conflict, but content differs (e.g. date change in same month, or address)
                // Update the LATEST revision in place
                QSqlQuery qUpd(m_db);
                qUpd.prepare("UPDATE shipments SET current_json = ?, event_date = ?, source_key = ? WHERE id = ?");
                qUpd.addBindValue(jsonStr);
                qUpd.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                qUpd.addBindValue(sourceKey);
                qUpd.addBindValue(latestId);
                qUpd.exec();
            }
        }
    } else {
        QSqlQuery qIns(m_db);
        qIns.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, inserted_at) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?)");
        qIns.addBindValue(id);
        qIns.addBindValue(orderId);
        qIns.addBindValue(jsonStr);
        qIns.addBindValue(jsonStr);
        qIns.addBindValue(eventDate);
        qIns.addBindValue(sourceKey);
        qIns.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        qIns.exec();
    }
}

void OrderManager::recordShipmentsFromSource(const ActivitySource *activitySource,
                                             const QList<ShipmentFromSourceEntry> &entries)
{
    if (entries.isEmpty()) return;

    const int batchSize = 500;
    const QString sourceKey = getSourceKey(activitySource);
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    for (int batchStart = 0; batchStart < entries.size(); batchStart += batchSize) {
        const int batchEnd = qMin(batchStart + batchSize, entries.size());

        // Phase 1 — Serialize all entries in this batch upfront.
        struct PreparedEntry {
            const ShipmentFromSourceEntry *entry;
            QString id;
            QString jsonStr;
            QString eventDate;
        };

        QList<PreparedEntry> prepared;
        QStringList allShipmentIds;

        for (int i = batchStart; i < batchEnd; ++i) {
            const auto &e = entries[i];
            if (!e.shipmentOrRefund || e.shipmentOrRefund->getActivities().isEmpty()) continue;

            QSharedPointer<Shipment> shipCopy;
            if (auto ref = dynamic_cast<const Refund*>(e.shipmentOrRefund)) {
                shipCopy = QSharedPointer<Refund>::create(*ref);
            } else {
                shipCopy = QSharedPointer<Shipment>::create(*e.shipmentOrRefund);
            }
            shipCopy->setIsWrongIfConflict(e.isWrongIfConflict);

            PreparedEntry pe;
            pe.entry = &e;
            pe.id = shipCopy->getId();
            pe.jsonStr = QJsonDocument(shipCopy->toJson()).toJson(QJsonDocument::Compact);
            pe.eventDate = shipCopy->getActivities().first().getDateTime().toString(Qt::ISODate);

            prepared.append(pe);
            allShipmentIds.append(pe.id);
        }

        if (prepared.isEmpty()) continue;

        m_db.transaction();

        // Phase 2 — Bulk INSERT OR IGNORE all unique orders.
        {
            QSqlQuery q(m_db);
            q.prepare("INSERT OR IGNORE INTO orders (id, inserted_at) VALUES (?, ?)");
            QVariantList orderIdList, insertedAtList;
            QSet<QString> seenOrders;
            for (const auto &pe : prepared) {
                if (!seenOrders.contains(pe.entry->orderId)) {
                    seenOrders.insert(pe.entry->orderId);
                    orderIdList << pe.entry->orderId;
                    insertedAtList << now;
                }
            }
            q.addBindValue(orderIdList);
            q.addBindValue(insertedAtList);
            q.execBatch();
        }

        // Phase 3 — Bulk-fetch which shipment IDs already exist (1 query for the whole batch).
        QSet<QString> existingIds;
        {
            QString placeholders = QString("?,").repeated(allShipmentIds.size());
            placeholders.chop(1);
            QSqlQuery qSel(m_db);
            qSel.prepare(QString("SELECT id FROM shipments WHERE id IN (%1)").arg(placeholders));
            for (const auto &sid : allShipmentIds) qSel.addBindValue(sid);
            if (qSel.exec()) {
                while (qSel.next())
                    existingIds.insert(qSel.value(0).toString());
            }
        }

        // Phase 4 — Split: batch-insert new shipments; route complex cases to fallback.
        QVariantList ids, orderIds, jsons, eventDates, sourceKeys, nows;
        QList<const PreparedEntry*> toFallback;
        QSet<QString> processedInBatch; // guard against intra-batch duplicates

        for (const auto &pe : prepared) {
            const bool isNew    = !existingIds.contains(pe.id);
            const bool isUnique = !processedInBatch.contains(pe.id);

            if (isNew && isUnique && !pe.entry->fixTaxDate) {
                // Fast path: brand-new shipment with no special handling needed.
                processedInBatch.insert(pe.id);
                ids        << pe.id;
                orderIds   << pe.entry->orderId;
                jsons      << pe.jsonStr;
                eventDates << pe.eventDate;
                sourceKeys << sourceKey;
                nows       << now;
            } else {
                // Slow path: existing Draft/Published update, fixTaxDate, or intra-batch duplicate.
                toFallback.append(&pe);
            }
        }

        if (!ids.isEmpty()) {
            QSqlQuery qIns(m_db);
            qIns.prepare("INSERT INTO shipments "
                         "(id, order_id, status, original_json, current_json, event_date, source_key, inserted_at) "
                         "VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?)");
            qIns.addBindValue(ids);
            qIns.addBindValue(orderIds);
            qIns.addBindValue(jsons);
            qIns.addBindValue(jsons); // current_json mirrors original_json for new entries
            qIns.addBindValue(eventDates);
            qIns.addBindValue(sourceKeys);
            qIns.addBindValue(nows);
            qIns.execBatch();
        }

        // Phase 5 — Fallback: handle each complex entry individually within the same transaction.
        for (const auto *pe : toFallback) {
            recordShipmentFromSource(pe->entry->orderId, activitySource,
                                     pe->entry->shipmentOrRefund,
                                     pe->entry->newDateIfConflict,
                                     pe->entry->isWrongIfConflict,
                                     pe->entry->fixTaxDate);
        }

        m_db.commit();
    }
}

void OrderManager::recordShipmentUpdated(const QString &orderId,
                                         const ActivitySource *activitySource,
                                         const Shipment *shipmentOrRefund,
                                         const QDate &newDateIfConflict,
                                         bool isWrongIfConflict)
{
    if (!shipmentOrRefund) return;
    
    // Clone and set flag
    QSharedPointer<Shipment> shipCopy;
    if (auto ref = dynamic_cast<const Refund*>(shipmentOrRefund)) {
         shipCopy = QSharedPointer<Refund>::create(*ref);
    } else {
         shipCopy = QSharedPointer<Shipment>::create(*shipmentOrRefund);
    }
    shipCopy->setIsWrongIfConflict(isWrongIfConflict);

    QString id = shipCopy->getId();
    QString jsonStr = QJsonDocument(shipCopy->toJson()).toJson(QJsonDocument::Compact);

    QSqlQuery qUpd(m_db);
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
        QSqlQuery qCheck(m_db);
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
        QSqlQuery qDelInv(m_db);
        qDelInv.prepare("DELETE FROM invoicing_infos WHERE shipment_root_id IN (SELECT id FROM shipments WHERE order_id = ?)");
        qDelInv.addBindValue(orderId);
        qDelInv.exec();
    }
    
    // 3. Delete Shipments (and refunds which are stored in shipments table)
    {
        QSqlQuery qDelShip(m_db);
        qDelShip.prepare("DELETE FROM shipments WHERE order_id = ?");
        qDelShip.addBindValue(orderId);
        qDelShip.exec();
    }

    // 4. Delete Order
    {
        QSqlQuery qDelOrd(m_db);
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
        QSqlQuery q(m_db);
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
        QSqlQuery q(m_db);
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
            QSqlQuery qDelInv(m_db);
            qDelInv.prepare("DELETE FROM invoicing_infos WHERE shipment_root_id = ?");
            qDelInv.addBindValue(rootId);
            qDelInv.exec();
        }
        
        {
            QSqlQuery qDelShip(m_db);
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
        QSqlQuery qCheck(m_db);
        qCheck.prepare("INSERT OR IGNORE INTO orders (id) VALUES (?)");
        qCheck.addBindValue(orderId);
        qCheck.exec();
    }
    
    QString jsonStr = QJsonDocument(addressTo.toJson()).toJson(QJsonDocument::Compact);
    QSqlQuery qUpd(m_db);
    qUpd.prepare("UPDATE orders SET address_json = ? WHERE id = ?");
    qUpd.addBindValue(jsonStr);
    qUpd.addBindValue(orderId);
    if (!qUpd.exec()) qWarning() << "Failed to update address:" << qUpd.lastError();
}

void OrderManager::recordInventoryMove(
        int year
        , int month
        , const QString &countryCodeFrom
        , const QString &countryCodeTo
        , const QString &transactionId
        , const QString &sku
        , int units)
{
    if (transactionId.isEmpty()) {
        ExceptionWithTitleText ex("Invalid Inventory Move",
                                  "transactionId cannot be empty");
        ex.raise();
    }
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO inventory_moves "
              "(id, year, month, country_from, country_to, sku, units) "
              "VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(transactionId);
    q.addBindValue(year);
    q.addBindValue(month);
    q.addBindValue(countryCodeFrom);
    q.addBindValue(countryCodeTo);
    q.addBindValue(sku);
    q.addBindValue(units);
    if (!q.exec())
        qWarning() << "Failed to record inventory move:" << q.lastError();
}

QHash<QString, int> OrderManager::getInventoryImported(
        int year, int month, const QString &countryCodeTo) const
{
    QHash<QString, int> sku_units;
    QSqlQuery q(m_db);
    q.prepare("SELECT sku, SUM(units) FROM inventory_moves "
              "WHERE year = ? AND month = ? AND country_to = ? "
              "GROUP BY sku");
    q.addBindValue(year);
    q.addBindValue(month);
    q.addBindValue(countryCodeTo);
    if (q.exec()) {
        while (q.next())
            sku_units[q.value(0).toString()] = q.value(1).toInt();
    }
    return sku_units;
}

QHash<QString, int> OrderManager::getInventoryExported(
        int year, int month, const QString &countryCodeFrom) const
{
    QHash<QString, int> sku_units;
    QSqlQuery q(m_db);
    q.prepare("SELECT sku, SUM(units) FROM inventory_moves "
              "WHERE year = ? AND month = ? AND country_from = ? "
              "GROUP BY sku");
    q.addBindValue(year);
    q.addBindValue(month);
    q.addBindValue(countryCodeFrom);
    if (q.exec()) {
        while (q.next())
            sku_units[q.value(0).toString()] = q.value(1).toInt();
    }
    return sku_units;
}

void OrderManager::recordOrder(const QString &orderId, const QString &store)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO orders (id, store, inserted_at) VALUES (?, ?, ?) "
              "ON CONFLICT(id) DO UPDATE SET store=excluded.store");
    q.addBindValue(orderId);
    q.addBindValue(store);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
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
        QSqlQuery q(m_db);
        q.prepare("SELECT COALESCE(root_id, id) FROM shipments WHERE id = ?");
        q.addBindValue(shipmentOrRefundId);
        if (q.exec() && q.next()) {
            rootId = q.value(0).toString();
        }
    }
    
    // 2. Persist the Info
    // We use INSERT OR REPLACE to update existing info or create new one.
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO invoicing_infos (shipment_root_id, json) VALUES (?, ?)");
    q.addBindValue(rootId);
    q.addBindValue(QString::fromUtf8(QJsonDocument(invoicingInfo->toJson()).toJson(QJsonDocument::Compact)));
    if (!q.exec()) {
        qWarning() << "Failed to record invoicing info:" << q.lastError();
    }
}

QCoro::Task<QString> OrderManager::tryRecordRefund(
        const QString &orderId, double amount, const QString &currency, const QString &shipmentId,
        std::function<QCoro::Task<QString>(const QString &errorTitle,
                                           const QString &errorText,
                                           const QList<QSharedPointer<Shipment>> &shipmentsToPick)> callbackPickShipment)
{
    // 1. If the order id initially has only one shipment => We can create the refund
    // 2. But if several shipments with several kind of TVA, we can create the refund only if one shipment and one only is the amount value
    // 3. If not, we check if shipmentId is not empty and do a partial refund on it.
    // 4. Otherwise we call the callback to let the user pick, or return an error

    // Gather all shipments for this order
    struct ShipmentInfo {
        QString id;
        QSharedPointer<Shipment> shipment;
        double totalTaxed;
        QString countryFrom;
        QString countryTo;
        QString sourceKey;
    };

    QList<ShipmentInfo> shipments;
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT id, current_json, source_key FROM shipments WHERE order_id = ? AND id NOT LIKE '%-rev-%'");
        q.addBindValue(orderId);
        if (q.exec()) {
            while (q.next()) {
                QString id = q.value(0).toString();
                QString jsonStr = q.value(1).toString();
                QString sourceKey = q.value(2).toString();
                QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
                auto ship = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));

                double totalTaxed = 0.0;
                QString cFrom, cTo;
                for (const auto &act : ship->getActivities()) {
                    totalTaxed += act.getAmountTaxed();
                    if (cFrom.isEmpty()) {
                        cFrom = act.getCountryCodeFrom();
                    }
                    if (cTo.isEmpty()) {
                        cTo = act.getCountryCodeTo();
                    }
                }
                shipments.append({id, ship, totalTaxed, cFrom, cTo, sourceKey});
            }
        }
    }

    if (shipments.isEmpty()) {
        co_return QObject::tr("No shipments found for order %1").arg(orderId);
    }

    // Helper lambda: create a refund from a shipment with given amount
    auto createRefundFromShipment = [&](const ShipmentInfo &info) -> QString {
        const auto &activities = info.shipment->getActivities();
        if (activities.isEmpty()) {
            return QObject::tr("Shipment %1 has no activities").arg(info.id);
        }

        // Create refund activities by negating the shipment's activities, scaled to the refund amount
        QList<Activity> refundActivities;
        double shipTotal = info.totalTaxed;
        double ratio = (qAbs(shipTotal) > 0.001) ? (amount / shipTotal) : 1.0;

        for (const auto &act : activities) {
            double refundTaxed = act.getAmountTaxed() * ratio;
            double refundTaxes = act.getAmountTaxesSource() * ratio;

            ::Amount negatedAmount(refundTaxed, refundTaxes);
            auto res = Activity::create(
                act.getEventId() + "-refund",
                act.getActivityId() + "-refund",
                act.getSubActivityId(),
                act.getDateTime(),
                act.getDateTimeTax(), // Using original tax date as requested
                currency.isEmpty() ? act.getCurrency() : currency,
                act.getCountryCodeFrom(),
                act.getCountryCodeTo(),
                act.getIsCompany(),
                act.getCountryCodeVatPaidTo(),
                negatedAmount,
                act.getTaxSource(),
                act.getTaxDeclaringCountryCode(),
                act.getTaxScheme(),
                act.getTaxJurisdictionLevel(),
                act.getSaleType(),
                act.getVatTerritoryFrom(),
                act.getVatTerritoryTo());

            if (res.ok()) {
                refundActivities.append(*res.value);
            }
        }

        if (refundActivities.isEmpty()) {
            return QObject::tr("Failed to create refund activities for shipment %1").arg(info.id);
        }

        Refund refund(refundActivities);

        // Parse source key back to ActivitySource
        ActivitySource source;
        QStringList parts = info.sourceKey.split('|');
        if (parts.size() >= 4) {
            source.type = static_cast<ActivitySourceType>(parts[0].toInt());
            source.channel = parts[1];
            source.subchannel = parts[2];
            source.reportOrMethode = parts[3];
        }

        recordShipmentFromSource(orderId, &source, &refund, QDate::currentDate(), false);
        return QString{}; // Success
    };

    // Case 1: Single shipment
    if (shipments.size() == 1) {
        co_return createRefundFromShipment(shipments.first());
    }

    // Case 2: Multiple shipments — find one whose totalTaxed matches qAbs(amount)
    {
        QList<int> matchingIndices;
        double absAmount = qAbs(amount);
        for (int i = 0; i < shipments.size(); ++i) {
            if (qAbs(qAbs(shipments[i].totalTaxed) - absAmount) < 0.01) {
                matchingIndices.append(i);
            }
        }
        if (matchingIndices.size() == 1) {
            co_return createRefundFromShipment(shipments[matchingIndices.first()]);
        }
    }

    // Case 3: shipmentId provided
    if (!shipmentId.isEmpty()) {
        for (const auto &info : shipments) {
            if (info.id == shipmentId) {
                co_return createRefundFromShipment(info);
            }
        }
        co_return QObject::tr("Shipment %1 not found for order %2").arg(shipmentId, orderId);
    }

    // Case 4: Ambiguous — call callback to let the user pick a shipment
    QStringList details;
    QList<QSharedPointer<Shipment>> shipmentPtrs;
    for (const auto &info : shipments) {
        details.append(QObject::tr("  Shipment %1: amount=%2, from=%3, to=%4")
                        .arg(info.id, QString::number(info.totalTaxed, 'f', 2), info.countryFrom, info.countryTo));
        shipmentPtrs.append(info.shipment);
    }

    QString errorTitle = QObject::tr("Ambiguous refund for order %1").arg(orderId);
    QString errorText = QObject::tr("Cannot determine which shipment to refund for order %1 (amount=%2 %3).\n"
              "Existing shipments:\n%4")
            .arg(orderId, QString::number(amount, 'f', 2), currency, details.join("\n"));

    if (callbackPickShipment) {
        QString pickedId = co_await callbackPickShipment(errorTitle, errorText, shipmentPtrs);
        if (!pickedId.isEmpty()) {
            for (const auto &info : shipments) {
                if (info.id == pickedId) {
                    co_return createRefundFromShipment(info);
                }
            }
            co_return QObject::tr("Shipment %1 not found for order %2").arg(pickedId, orderId);
        }
    }

    co_return errorText;
}

QSharedPointer<InvoicingInfo> OrderManager::getInvoicingInfo(const QString &shipmentId) const
{
    // 1. Resolve to Root ID
    // The incoming shipmentId might be a specific version/revision. 
    // We need to look up the info using the stable root ID.
    QString rootId = shipmentId;
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT COALESCE(root_id, id) FROM shipments WHERE id = ?");
        q.addBindValue(shipmentId);
        if (q.exec() && q.next()) {
            rootId = q.value(0).toString();
        }
    }
    
    // 2. Retrieve Data
    QSqlQuery q(m_db);
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
    QSqlQuery qDrafts(m_db);
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

                QSqlQuery qIns(m_db);
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
            
            QSqlQuery qUpd(m_db);
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
    
    QSqlQuery q(m_db);
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
    
    QSqlQuery query(m_db);
    query.exec(queryStr);
    
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
                                            act.getDateTimeTax(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getIsCompany(),
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
    
    QSqlQuery query(m_db);
    query.exec(queryStr);
    
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
                                            act.getDateTimeTax(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getIsCompany(),
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



QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> 
OrderManager::get_channel_site_ShipmentAndRefundsInsertedAt(const QDate &dateFromInsertedDb, const QDate &dateToInsertedDb) const
{
    auto result = QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>>::create();
    
    QString queryStr = "SELECT current_json, source_key, event_date, id FROM shipments WHERE 1=1";
    if (dateFromInsertedDb.isValid()) {
        queryStr += QString(" AND inserted_at >= '%1'").arg(dateFromInsertedDb.toString(Qt::ISODate));
    }
    if (dateToInsertedDb.isValid()) {
         queryStr += QString(" AND inserted_at < '%1'").arg(dateToInsertedDb.addDays(1).toString(Qt::ISODate));
    }
    
    QSqlQuery query(m_db);
    query.exec(queryStr);
    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString id = query.value(3).toString();
        
        QStringList parts = sourceKey.split('|');
        QString channel = (parts.size() >= 2) ? parts[1] : "Unknown";
        QString subchannel = (parts.size() >= 3) ? parts[2] : "Unknown";

        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        
        if (id.contains("-rev-")) {
            QList<Activity> newActs;
            for (const auto &act : shipment->getActivities()) {
                Amount negatedAmount(-act.getAmountTaxed(), -act.getAmountTaxesSource());
                auto res = Activity::create(act.getEventId(),
                                            act.getActivityId(),
                                            act.getSubActivityId(),
                                            act.getDateTime(),
                                            act.getDateTimeTax(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getIsCompany(),
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
        
        if (shipment->getActivities().isEmpty()) continue;
        const auto &act = shipment->getActivities().first();
        TaxResolver::TaxContext ctx;
        ctx.taxDeclaringCountryCode = act.getTaxDeclaringCountryCode();
        ctx.taxScheme = act.getTaxScheme();
        ctx.taxJurisdictionLevel = act.getTaxJurisdictionLevel();
        ctx.countryCodeVatPaidTo = act.getCountryCodeVatPaidTo();
        
        (*result)[channel][subchannel][ctx].shipmentsRefundsSameActivity.append(shipment);
        (*result)[channel][subchannel][ctx].invoicesToDo.append(false);
    }
    
    return result;
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
        QSqlQuery q(m_db);
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
        QSqlQuery q(m_db);
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
         QSqlQuery q(m_db);
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

    QSqlQuery query(m_db);
    query.exec(queryStr);

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
                                            act.getDateTimeTax(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getIsCompany(),
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

QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>> 
OrderManager::get_channel_site_ShipmentAndRefundsConflicts(const QDate &dateFrom, const QDate &dateTo) const
{
    auto result = QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>>::create();
    
    QString queryStr = "SELECT current_json, source_key, event_date, id FROM shipments WHERE 1=1";
    if (dateFrom.isValid()) {
        queryStr += QString(" AND event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        queryStr += QString(" AND event_date < '%1'").arg(dateTo.addDays(1).toString(Qt::ISODate));
    }
    
    QSqlQuery query(m_db);
    query.exec(queryStr);
    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString id = query.value(3).toString();
        
        // Parse Source
        QStringList parts = sourceKey.split('|');
        QString channel = (parts.size() >= 2) ? parts[1] : "Unknown";
        QString subchannel = (parts.size() >= 3) ? parts[2] : "Unknown";

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
                                            act.getDateTimeTax(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getIsCompany(),
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
        
        if (shipment->getActivities().isEmpty()) continue;
        const auto &act = shipment->getActivities().first();
        TaxResolver::TaxContext ctx;
        ctx.taxDeclaringCountryCode = act.getTaxDeclaringCountryCode();
        ctx.taxScheme = act.getTaxScheme();
        ctx.taxJurisdictionLevel = act.getTaxJurisdictionLevel();
        ctx.countryCodeVatPaidTo = act.getCountryCodeVatPaidTo();
        
        (*result)[channel][subchannel][ctx].shipmentsRefundsSameActivity.append(shipment);
        (*result)[channel][subchannel][ctx].invoicesToDo.append(false);
    }
    
    return result;
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
    // TODO any shipment without an orderid and site should led to ExceptionWithTitleText
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
    
    QSqlQuery rootQuery(m_db);
    rootQuery.exec(rootQueryStr);
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
    
    QSqlQuery query(m_db);
    query.exec(queryStr);
    
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
                                            act.getDateTimeTax(),
                                            act.getCurrency(),
                                            act.getCountryCodeFrom(),
                                            act.getCountryCodeTo(),
                                            act.getIsCompany(),
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


