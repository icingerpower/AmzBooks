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
#include <QAtomicInteger>

#include "ActivitySource.h"
#include "Shipment.h"
#include "Refund.h"
#include "InvoicingInfo.h"
#include "Address.h"
#include "ActivityUpdate.h"
#include "ExceptionWithTitleText.h"

namespace {
    // Monotonically increasing counter ensures revision IDs sort correctly under ORDER BY id DESC
    // even when two revisions are created within the same millisecond.
    Q_GLOBAL_STATIC(QAtomicInteger<quint32>, s_revCounter)

    QString makeRevisionSuffix() {
        quint32 seq = s_revCounter->fetchAndAddOrdered(1);
        return QString("%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(seq, 8, 10, QChar('0'));
    }

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
    query.exec("PRAGMA journal_mode = WAL;");
    query.exec("PRAGMA synchronous = NORMAL;");
    query.exec("PRAGMA foreign_keys = ON;");

    m_db.transaction();

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

    // Migration: recreate inventory_moves whenever it lacks the current composite PK.
    // History:
    //   v1 — PRIMARY KEY (id)          → too strict: same eventId can appear for different
    //                                     (country_from, country_to) pairs.
    //   v2 — no primary key            → too loose: no DB-level uniqueness guarantee.
    //   v3 — PRIMARY KEY (id, country_from, country_to, sku)  ← current.
    //         Same event can appear for different directions AND carry multiple SKUs,
    //         but (id, from, to, sku) must be unique.
    // Trigger: table exists but DDL does not contain the current PK signature.
    {
        QSqlQuery qMig(m_db);
        if (qMig.exec(QStringLiteral(
                "SELECT sql FROM sqlite_master WHERE type='table' AND name='inventory_moves'"))
                && qMig.next()) {
            const QString tableSql = qMig.value(0).toString();
            if (!tableSql.contains(QStringLiteral("PRIMARY KEY (id, country_from, country_to, sku)"))) {
                QSqlQuery qDrop(m_db);
                if (!qDrop.exec(QStringLiteral("DROP TABLE inventory_moves"))) {
                    qWarning() << "Failed to drop old inventory_moves table:" << qDrop.lastError();
                } else {
                    QSqlQuery qCreate(m_db);
                    if (!qCreate.exec(OrderManagerSql::CREATE_TABLE_INVENTORY_MOVES)) {
                        qWarning() << "Failed to recreate inventory_moves table:" << qCreate.lastError();
                    }
                }
            }
        }
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

    // Migration: Add is_ungrouped and customer_account columns if missing in orders
    {
        QSqlQuery qMig(m_db);
        qMig.exec("PRAGMA table_info(orders)");
        bool hasIsUngrouped = false;
        bool hasCustomerAccount = false;
        while (qMig.next()) {
            const QString col = qMig.value("name").toString();
            if (col == "is_ungrouped")    hasIsUngrouped = true;
            if (col == "customer_account") hasCustomerAccount = true;
        }
        if (!hasIsUngrouped) {
            QSqlQuery qAlter(m_db);
            if (!qAlter.exec("ALTER TABLE orders ADD COLUMN is_ungrouped INTEGER NOT NULL DEFAULT 0")) {
                qWarning() << "Failed to add is_ungrouped column to orders:" << qAlter.lastError().text();
            }
        }
        if (!hasCustomerAccount) {
            QSqlQuery qAlter(m_db);
            if (!qAlter.exec("ALTER TABLE orders ADD COLUMN customer_account TEXT DEFAULT ''")) {
                qWarning() << "Failed to add customer_account column to orders:" << qAlter.lastError().text();
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

    // Migration: Add is_refund column if missing in shipments
    {
        QSqlQuery qMig(m_db);
        qMig.exec("PRAGMA table_info(shipments)");
        bool hasIsRefund = false;
        while (qMig.next()) {
            if (qMig.value("name").toString() == "is_refund") {
                hasIsRefund = true;
                break;
            }
        }
        if (!hasIsRefund) {
            QSqlQuery qAlter(m_db);
            if (!qAlter.exec("ALTER TABLE shipments ADD COLUMN is_refund INTEGER NOT NULL DEFAULT 0")) {
                qWarning() << "Failed to add is_refund column to shipments:" << qAlter.lastError().text();
            }
        }
    }

    m_db.commit();
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

bool OrderManager::isOrderPublished(const QString &orderId) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM shipments WHERE order_id = ? AND status = 'Published'");
    q.addBindValue(orderId);
    return q.exec() && q.next() && q.value(0).toInt() > 0;
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
                QString uniqueSuffix = makeRevisionSuffix();
                QString reversalId = QString("%1-rev-%2").arg(id).arg(uniqueSuffix);
                QString newVersionId = QString("%1-v-%2").arg(id).arg(uniqueSuffix);
                
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
                        qInsRev.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, root_id, inserted_at, is_refund) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?, ?, 0)");
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
                        qInsNew.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, root_id, inserted_at, is_refund) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?, ?, ?)");
                        qInsNew.addBindValue(newVersionId);
                        qInsNew.addBindValue(orderId);
                        qInsNew.addBindValue(jsonStr);
                        qInsNew.addBindValue(jsonStr);
                        qInsNew.addBindValue(newDateIfConflict.isValid() ? newDateIfConflict.toString(Qt::ISODate) : eventDate);
                        qInsNew.addBindValue(sourceKey);
                        qInsNew.addBindValue(id);
                        qInsNew.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
                        qInsNew.addBindValue(dynamic_cast<const Refund*>(shipCopy.data()) ? 1 : 0);
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
        qIns.prepare("INSERT INTO shipments (id, order_id, status, original_json, current_json, event_date, source_key, inserted_at, is_refund) VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?, ?)");
        qIns.addBindValue(id);
        qIns.addBindValue(orderId);
        qIns.addBindValue(jsonStr);
        qIns.addBindValue(jsonStr);
        qIns.addBindValue(eventDate);
        qIns.addBindValue(sourceKey);
        qIns.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        qIns.addBindValue(dynamic_cast<const Refund*>(shipCopy.data()) ? 1 : 0);
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
            bool isRefund = false;
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
            pe.isRefund = dynamic_cast<const Refund*>(shipCopy.data()) != nullptr;

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
        QVariantList ids, orderIds, jsons, eventDates, sourceKeys, nows, isRefunds;
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
                isRefunds  << (pe.isRefund ? 1 : 0);
            } else {
                // Slow path: existing Draft/Published update, fixTaxDate, or intra-batch duplicate.
                toFallback.append(&pe);
            }
        }

        if (!ids.isEmpty()) {
            QSqlQuery qIns(m_db);
            qIns.prepare("INSERT INTO shipments "
                         "(id, order_id, status, original_json, current_json, event_date, source_key, inserted_at, is_refund) "
                         "VALUES (?, ?, 'Draft', ?, ?, ?, ?, ?, ?)");
            qIns.addBindValue(ids);
            qIns.addBindValue(orderIds);
            qIns.addBindValue(jsons);
            qIns.addBindValue(jsons); // current_json mirrors original_json for new entries
            qIns.addBindValue(eventDates);
            qIns.addBindValue(sourceKeys);
            qIns.addBindValue(nows);
            qIns.addBindValue(isRefunds);
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

void OrderManager::removeShipmentOrRefund(const QString &shipmentOrRefundId)
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

void OrderManager::removeShipmentsRefunds(const QDate &dateFrom, const QDate &dateTo)
{
    m_db.transaction();

    // Find all root_ids in the given date range that DO NOT have any 'Published' revision
    QStringList safeRootIds;
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT DISTINCT COALESCE(root_id, id) FROM shipments "
                  "WHERE event_date >= ? AND event_date <= ? "
                  "EXCEPT "
                  "SELECT DISTINCT COALESCE(root_id, id) FROM shipments "
                  "WHERE status = 'Published'");
        q.addBindValue(dateFrom.toString(Qt::ISODate));
        // Using "..." to cover the whole day correctly compared to ISO date-time strings
        q.addBindValue(dateTo.toString(Qt::ISODate) + "T23:59:59");
        if (q.exec()) {
            while (q.next()) {
                safeRootIds.append(q.value(0).toString());
            }
        }
    }

    if (!safeRootIds.isEmpty()) {
        const int batchSize = 500;
        for (int i = 0; i < safeRootIds.size(); i += batchSize) {
            QStringList batch = safeRootIds.mid(i, batchSize);
            QString placeholders = QString("?,").repeated(batch.size());
            placeholders.chop(1);

            // Delete Invoicing Infos
            {
                QSqlQuery qDelInv(m_db);
                qDelInv.prepare(QString("DELETE FROM invoicing_infos WHERE shipment_root_id IN (%1)").arg(placeholders));
                for (const QString &rid : batch) qDelInv.addBindValue(rid);
                qDelInv.exec();
            }

            // Delete Shipments
            {
                QSqlQuery qDelShip(m_db);
                qDelShip.prepare(QString("DELETE FROM shipments WHERE root_id IN (%1) OR id IN (%1)").arg(placeholders));
                for (const QString &rid : batch) qDelShip.addBindValue(rid);
                for (const QString &rid : batch) qDelShip.addBindValue(rid);
                qDelShip.exec();
            }
        }

        // Clean up orphaned orders
        {
            QSqlQuery qCleanOrd(m_db);
            qCleanOrd.exec("DELETE FROM orders WHERE id NOT IN (SELECT DISTINCT order_id FROM shipments)");
        }
    }

    m_db.commit();
}

void OrderManager::removeShipmentsRefunds(const QDate &dateFromCreated)
{
    m_db.transaction();

    // Find all root_ids inserted >= dateFromCreated that DO NOT have any 'Published' revision
    QStringList safeRootIds;
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT DISTINCT COALESCE(root_id, id) FROM shipments "
                  "WHERE inserted_at >= ? "
                  "EXCEPT "
                  "SELECT DISTINCT COALESCE(root_id, id) FROM shipments "
                  "WHERE status = 'Published'");
        q.addBindValue(dateFromCreated.toString(Qt::ISODate));
        if (q.exec()) {
            while (q.next()) {
                safeRootIds.append(q.value(0).toString());
            }
        }
    }

    if (!safeRootIds.isEmpty()) {
        const int batchSize = 500;
        for (int i = 0; i < safeRootIds.size(); i += batchSize) {
            QStringList batch = safeRootIds.mid(i, batchSize);
            QString placeholders = QString("?,").repeated(batch.size());
            placeholders.chop(1);

            // Delete Invoicing Infos
            {
                QSqlQuery qDelInv(m_db);
                qDelInv.prepare(QString("DELETE FROM invoicing_infos WHERE shipment_root_id IN (%1)").arg(placeholders));
                for (const QString &rid : batch) qDelInv.addBindValue(rid);
                qDelInv.exec();
            }

            // Delete Shipments
            {
                QSqlQuery qDelShip(m_db);
                qDelShip.prepare(QString("DELETE FROM shipments WHERE root_id IN (%1) OR id IN (%1)").arg(placeholders));
                for (const QString &rid : batch) qDelShip.addBindValue(rid);
                for (const QString &rid : batch) qDelShip.addBindValue(rid);
                qDelShip.exec();
            }
        }

        // Clean up orphaned orders
        {
            QSqlQuery qCleanOrd(m_db);
            qCleanOrd.exec("DELETE FROM orders WHERE id NOT IN (SELECT DISTINCT order_id FROM shipments)");
        }
    }

    m_db.commit();
}

void OrderManager::recordAddressesTo(const QHash<QString, Address> &orderId_addressTo)
{
    if (orderId_addressTo.isEmpty())
        return;

    struct Entry { QString id; QString json; };
    QList<Entry> entries;
    entries.reserve(orderId_addressTo.size());
    for (auto it = orderId_addressTo.cbegin(); it != orderId_addressTo.cend(); ++it)
        entries.append({it.key(),
                        QJsonDocument(it.value().toJson()).toJson(QJsonDocument::Compact)});

    const int batchSize = 1000;

    for (int batchStart = 0; batchStart < entries.size(); batchStart += batchSize)
    {
        const int batchEnd = qMin(batchStart + batchSize, entries.size());

        QVariantList ids, jsons;
        ids.reserve(batchEnd - batchStart);
        jsons.reserve(batchEnd - batchStart);
        for (int i = batchStart; i < batchEnd; ++i) {
            ids   << entries[i].id;
            jsons << entries[i].json;
        }

        m_db.transaction();

        // Phase 1: ensure order rows exist
        {
            QSqlQuery q(m_db);
            q.prepare("INSERT OR IGNORE INTO orders (id) VALUES (?)");
            q.addBindValue(ids);
            if (!q.execBatch()) {
                qWarning() << "OrderManager::recordAddressesTo INSERT OR IGNORE failed:" << q.lastError();
                m_db.rollback();
                continue;
            }
        }

        // Phase 2: set address_json
        {
            QSqlQuery q(m_db);
            q.prepare("UPDATE orders SET address_json = ? WHERE id = ?");
            q.addBindValue(jsons);
            q.addBindValue(ids);
            if (!q.execBatch()) {
                qWarning() << "OrderManager::recordAddressesTo UPDATE failed:" << q.lastError();
                m_db.rollback();
                continue;
            }
        }

        m_db.commit();
    }
}

void OrderManager::recordInventoryMove(
        const QHash<int, QHash<int, QHash<QString, QHash<QString, QHash<QString, QHash<QString, int>>>>>> &year_month_countryFrom_countryTo_eventId_sku_units)
{
    if (year_month_countryFrom_countryTo_eventId_sku_units.isEmpty()) {
        return;
    }

    // Flatten nested hash into rows; validate transactionIds eagerly before touching the DB.
    // Also collect unique (year, month, from, to) periods covered by this batch.
    struct Row { int year, month; QString from, to, txnId, sku; int units; };
    struct Period { int year, month; QString from, to; };
    QList<Row> rows;
    QList<Period> periods;
    for (auto it1 = year_month_countryFrom_countryTo_eventId_sku_units.cbegin();
         it1 != year_month_countryFrom_countryTo_eventId_sku_units.cend(); ++it1)
    {
        for (auto it2 = it1.value().cbegin(); it2 != it1.value().cend(); ++it2)
        {
            for (auto it3 = it2.value().cbegin(); it3 != it2.value().cend(); ++it3)
            {
                for (auto it4 = it3.value().cbegin(); it4 != it3.value().cend(); ++it4)
                {
                    periods.append({it1.key(), it2.key(), it3.key(), it4.key()});
                    for (auto it5 = it4.value().cbegin(); it5 != it4.value().cend(); ++it5)
                    {
                        if (it5.key().isEmpty()) {
                            ExceptionWithTitleText ex("Invalid Inventory Move",
                                                      "transactionId cannot be empty");
                            ex.raise();
                        }
                        for (auto it6 = it5.value().cbegin(); it6 != it5.value().cend(); ++it6)
                        {
                            rows.append({it1.key(), it2.key(),
                                         it3.key(), it4.key(),
                                         it5.key(), it6.key(), it6.value()});
                        }
                    }
                }
            }
        }
    }

    if (rows.isEmpty()) {
        return;
    }

    // Delete existing rows for every (year, month, from, to) covered by this batch before
    // inserting new ones. This ensures that re-importing a corrected report — which may have
    // different TRANSACTION_EVENT_IDs for the same period — replaces stale data instead of
    // accumulating duplicates. Each call to recordInventoryMove() is authoritative for the
    // periods it covers.
    {
        m_db.transaction();
        bool ok = true;
        for (const auto &p : std::as_const(periods)) {
            QSqlQuery q(m_db);
            q.prepare("DELETE FROM inventory_moves "
                      "WHERE year = ? AND month = ? AND country_from = ? AND country_to = ?");
            q.addBindValue(p.year);
            q.addBindValue(p.month);
            q.addBindValue(p.from);
            q.addBindValue(p.to);
            if (!q.exec()) {
                qWarning() << "OrderManager::recordInventoryMove DELETE failed:" << q.lastError();
                ok = false;
                break;
            }
        }
        if (ok) {
            m_db.commit();
        } else {
            m_db.rollback();
            return;
        }
    }

    const int batchSize = 500;
    for (int batchStart = 0; batchStart < rows.size(); batchStart += batchSize)
    {
        const int batchEnd = qMin(batchStart + batchSize, rows.size());

        QVariantList ids, years, months, froms, tos, skus, unitsList;
        ids.reserve(batchEnd - batchStart);
        for (int i = batchStart; i < batchEnd; ++i) {
            ids       << rows[i].txnId;
            years     << rows[i].year;
            months    << rows[i].month;
            froms     << rows[i].from;
            tos       << rows[i].to;
            skus      << rows[i].sku;
            unitsList << rows[i].units;
        }

        m_db.transaction();
        QSqlQuery q(m_db);
        q.prepare("INSERT INTO inventory_moves "
                  "(id, year, month, country_from, country_to, sku, units) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?)");
        q.addBindValue(ids);
        q.addBindValue(years);
        q.addBindValue(months);
        q.addBindValue(froms);
        q.addBindValue(tos);
        q.addBindValue(skus);
        q.addBindValue(unitsList);
        if (!q.execBatch()) {
            qWarning() << "OrderManager::recordInventoryMove batch insert failed:" << q.lastError();
            m_db.rollback();
        } else {
            m_db.commit();
        }
    }
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

void OrderManager::recordOrders(const QHash<QString, OrderInfo> &orderId_infos)
{
    if (orderId_infos.isEmpty())
        return;

    struct Entry { QString id; QString store; int isUngrouped; QString customerAccount; };
    QList<Entry> entries;
    entries.reserve(orderId_infos.size());
    for (auto it = orderId_infos.cbegin(); it != orderId_infos.cend(); ++it)
        entries.append({it.key(), it.value().store, it.value().isGrouped ? 0 : 1, it.value().customerAccount});

    const int batchSize = 1000;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    for (int batchStart = 0; batchStart < entries.size(); batchStart += batchSize)
    {
        const int batchEnd = qMin(batchStart + batchSize, entries.size());

        QVariantList ids, stores, isUngroupedList, customerAccounts, nows;
        ids.reserve(batchEnd - batchStart);
        stores.reserve(batchEnd - batchStart);
        isUngroupedList.reserve(batchEnd - batchStart);
        customerAccounts.reserve(batchEnd - batchStart);
        nows.reserve(batchEnd - batchStart);
        for (int i = batchStart; i < batchEnd; ++i) {
            ids              << entries[i].id;
            stores           << entries[i].store;
            isUngroupedList  << entries[i].isUngrouped;
            customerAccounts << entries[i].customerAccount;
            nows             << now;
        }

        m_db.transaction();
        QSqlQuery q(m_db);
        q.prepare("INSERT INTO orders (id, store, inserted_at, is_ungrouped, customer_account) VALUES (?, ?, ?, ?, ?) "
                  "ON CONFLICT(id) DO UPDATE SET store=excluded.store, is_ungrouped=excluded.is_ungrouped, customer_account=excluded.customer_account");
        q.addBindValue(ids);
        q.addBindValue(stores);
        q.addBindValue(nows);
        q.addBindValue(isUngroupedList);
        q.addBindValue(customerAccounts);
        if (!q.execBatch()) {
            qWarning() << "OrderManager::recordOrders batch insert failed:" << q.lastError();
            m_db.rollback();
        } else {
            m_db.commit();
        }
    }
}

QHash<QString, QString> OrderManager::getStores(const QList<QSharedPointer<Shipment>> &shipments) const
{
    if (shipments.isEmpty())
        return {};

    // Collect distinct order IDs (eventIds) from all activities.
    // These are the keys in the orders table — different from the shipment activity ID.
    QStringList orderIds;
    QSet<QString> seen;
    for (const auto &s : shipments) {
        for (const auto &act : s->getActivities()) {
            const QString &id = act.getEventId();
            if (!id.isEmpty() && !seen.contains(id)) {
                seen.insert(id);
                orderIds.append(id);
            }
        }
    }
    if (orderIds.isEmpty())
        return {};

    QStringList placeholders;
    placeholders.reserve(orderIds.size());
    for (int i = 0; i < orderIds.size(); ++i)
        placeholders.append("?");

    QSqlQuery q(m_db);
    q.prepare(QString(
        "SELECT id, store FROM orders "
        "WHERE id IN (%1) AND store IS NOT NULL AND store != ''")
        .arg(placeholders.join(',')));
    for (const QString &id : orderIds)
        q.addBindValue(id);

    if (!q.exec()) {
        qWarning() << "OrderManager::getStores query failed:" << q.lastError();
        return {};
    }

    QHash<QString, QString> result;
    while (q.next())
        result[q.value(0).toString()] = q.value(1).toString();
    return result;
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
    
    // 2. Build JSON, preserving invoiceNumber / invoiceLink from any existing
    //    record when the new data does not supply them.  This prevents a later
    //    FBA-invoicing re-import (which carries items but no invoice fields)
    //    from silently erasing an invoice number stored by an earlier VAT-EU
    //    import.
    QJsonObject newJson = invoicingInfo->toJson();
    if (!newJson.contains("invoiceNumber") || !newJson.contains("invoiceLink")) {
        QSqlQuery qExist(m_db);
        qExist.prepare("SELECT json FROM invoicing_infos WHERE shipment_root_id = ?");
        qExist.addBindValue(rootId);
        if (qExist.exec() && qExist.next()) {
            const QJsonObject existJson =
                QJsonDocument::fromJson(qExist.value(0).toString().toUtf8()).object();
            if (!newJson.contains("invoiceNumber") && existJson.contains("invoiceNumber")) {
                newJson["invoiceNumber"] = existJson["invoiceNumber"];
            }
            if (!newJson.contains("invoiceLink") && existJson.contains("invoiceLink")) {
                newJson["invoiceLink"] = existJson["invoiceLink"];
            }
        }
    }

    // 3. Persist the Info
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO invoicing_infos (shipment_root_id, json) VALUES (?, ?)");
    q.addBindValue(rootId);
    q.addBindValue(QString::fromUtf8(QJsonDocument(newJson).toJson(QJsonDocument::Compact)));
    if (!q.exec()) {
        qWarning() << "Failed to record invoicing info:" << q.lastError();
    }
}

QCoro::Task<QString> OrderManager::tryRecordRefund(
        const QString &orderId, double amount, const QString &currency, const QString &shipmentId, const QDate &refundDate,
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

    // Look up whether this order is grouped and its customer account
    bool orderIsGrouped = true;
    QString orderCustomerAccount;
    {
        QSqlQuery qOrd(m_db);
        qOrd.prepare("SELECT COALESCE(is_ungrouped, 0), COALESCE(customer_account, '') FROM orders WHERE id = ?");
        qOrd.addBindValue(orderId);
        if (qOrd.exec() && qOrd.next()) {
            orderIsGrouped = qOrd.value(0).toInt() == 0;
            orderCustomerAccount = qOrd.value(1).toString();
        }
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
                act.getEventId(),
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

        Refund refund(refundActivities, orderCustomerAccount, orderIsGrouped);

        // Parse source key back to ActivitySource
        ActivitySource source;
        QStringList parts = info.sourceKey.split('|');
        if (parts.size() >= 4) {
            source.type = static_cast<ActivitySourceType>(parts[0].toInt());
            source.channel = parts[1];
            source.subchannel = parts[2];
            source.reportOrMethode = parts[3];
        }

        recordShipmentFromSource(orderId, &source, &refund, refundDate.isValid() ? refundDate : QDate::currentDate(), false);
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
    QString errorText = QObject::tr("Cannot determine which shipment to refund for order %1 (date=%2, amount=%3 %4).\n"
              "Existing shipments:\n%5")
            .arg(orderId, refundDate.isValid() ? refundDate.toString(Qt::ISODate) : QObject::tr("unknown"), QString::number(amount, 'f', 2), currency, details.join("\n"));

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
    bool foundInShipmentsTable = false;
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT COALESCE(root_id, id) FROM shipments WHERE id = ?");
        q.addBindValue(shipmentId);
        if (q.exec() && q.next()) {
            rootId = q.value(0).toString();
            foundInShipmentsTable = true;
        }
    }

    // 2. Retrieve Data by root shipment ID
    QSqlQuery q(m_db);
    q.prepare("SELECT json FROM invoicing_infos WHERE shipment_root_id = ?");
    q.addBindValue(rootId);
    if (q.exec() && q.next()) {
        if (foundInShipmentsTable) {
            // rootId was resolved from the shipments table — it is a proper shipment
            // root, so return the invoicingInfo directly without any guard.
            QJsonObject json = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
            return QSharedPointer<InvoicingInfo>::create(InvoicingInfo::fromJson(json));
        }

        // shipmentId was NOT in the shipments table, so rootId == shipmentId (unchanged).
        // Guard: verify that rootId is not being used as an Amazon order ID.
        // If there are shipments whose order_id equals rootId AND whose id is
        // different from rootId, then rootId is an Amazon order ID (not a proper
        // shipment root).  In that case, the entry was written under the wrong key
        // by a previous broken run of generateInvoice (which used getEventId()
        // instead of getActivityId() for recordInvoicingInfo).
        // Fall through to the order-level fallback to find the correct entry.
        QSqlQuery guardQ(m_db);
        guardQ.prepare("SELECT 1 FROM shipments WHERE order_id = ? AND id != ? LIMIT 1");
        guardQ.addBindValue(rootId);
        guardQ.addBindValue(rootId);
        if (!(guardQ.exec() && guardQ.next())) {
            QJsonObject json = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
            return QSharedPointer<InvoicingInfo>::create(InvoicingInfo::fromJson(json));
        }
        // else: rootId looks like an Amazon order ID → fall through to order-level fallback
    }

    // 3. Order-level fallback: shipmentId may be an Amazon order ID (not a shipment ID).
    // Find the invoice for any shipment belonging to this order (earliest event_date first,
    // so the original sale is preferred over refunds).
    {
        QSqlQuery qOrder(m_db);
        qOrder.prepare(
            "SELECT inv.json FROM invoicing_infos inv "
            "JOIN shipments s ON COALESCE(s.root_id, s.id) = inv.shipment_root_id "
            "WHERE s.order_id = ? "
            "AND inv.json LIKE '%\"invoiceNumber\":%' "
            "AND inv.json NOT LIKE '%\"invoiceNumber\":null%' "
            "ORDER BY s.event_date ASC LIMIT 1");
        qOrder.addBindValue(shipmentId);
        if (qOrder.exec() && qOrder.next()) {
            QJsonObject json = QJsonDocument::fromJson(qOrder.value(0).toString().toUtf8()).object();
            return QSharedPointer<InvoicingInfo>::create(InvoicingInfo::fromJson(json));
        }
    }

    // Return empty pointer if not found
    return nullptr;
}

QSharedPointer<Address> OrderManager::getAddressTo(const QString &orderId) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT address_json FROM orders WHERE id = ?");
    q.addBindValue(orderId);
    if (!q.exec() || !q.next())
        return nullptr;
    const QString addrJson = q.value(0).toString();
    if (addrJson.isEmpty())
        return nullptr;
    const QJsonObject addrObj = QJsonDocument::fromJson(addrJson.toUtf8()).object();
    if (addrObj.isEmpty())
        return nullptr;
    return QSharedPointer<Address>::create(Address::fromJson(addrObj));
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

    QString queryStr = "SELECT s.current_json, s.source_key, s.event_date, s.id, "
                       "COALESCE(o.is_ungrouped, 0) AS is_ungrouped, "
                       "COALESCE(o.customer_account, '') AS customer_account, "
                       "COALESCE(s.is_refund, 0) AS is_refund "
                       "FROM shipments s "
                       "LEFT JOIN orders o ON s.order_id = o.id "
                       "WHERE 1=1";
    if (dateFrom.isValid()) {
        queryStr += QString(" AND s.event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        // Use strict-less-than against the next day so that ISO datetime strings
        // stored as "YYYY-MM-DDThh:mm:ss" on the boundary day are included.
        // ("2025-12-31T00:00:00" <= "2025-12-31" is false; "< 2026-01-01" is true.)
        queryStr += QString(" AND s.event_date < '%1'").arg(dateTo.addDays(1).toString(Qt::ISODate));
    }

    QSqlQuery query(m_db);
    query.exec(queryStr);

    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString dateStr = query.value(2).toString();
        QString id = query.value(3).toString();
        bool isGrouped = query.value(4).toInt() == 0;
        QString customerAccount = query.value(5).toString();
        bool isRefund = query.value(6).toInt() == 1;
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
        QSharedPointer<Shipment> shipment;
        if (isRefund) {
            Shipment temp = Shipment::fromJson(obj);
            auto refund = QSharedPointer<Refund>::create(temp.getActivities());
            refund->setIsWrongIfConflict(temp.isWrongIfConflict());
            shipment = refund;
        } else {
            shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

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
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
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

    QString queryStr = "SELECT s.current_json, s.source_key, s.event_date, s.id, "
                       "COALESCE(o.is_ungrouped, 0) AS is_ungrouped, "
                       "COALESCE(o.customer_account, '') AS customer_account, "
                       "COALESCE(s.is_refund, 0) AS is_refund "
                       "FROM shipments s "
                       "LEFT JOIN orders o ON s.order_id = o.id "
                       "WHERE 1=1";
    if (dateFrom.isValid()) {
        queryStr += QString(" AND s.event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        // Use strict-less-than against the next day so that ISO datetime strings
        // stored as "YYYY-MM-DDThh:mm:ss" on the boundary day are included.
        // ("2025-12-31T00:00:00" <= "2025-12-31" is false; "< 2026-01-01" is true.)
        queryStr += QString(" AND s.event_date < '%1'").arg(dateTo.addDays(1).toString(Qt::ISODate));
    }

    QSqlQuery query(m_db);
    query.exec(queryStr);

    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString dateStr = query.value(2).toString();
        QString id = query.value(3).toString();
        bool isGrouped = query.value(4).toInt() == 0;
        QString customerAccount = query.value(5).toString();
        bool isRefund = query.value(6).toInt() == 1;
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
        QSharedPointer<Shipment> shipment;
        if (isRefund) {
            Shipment temp = Shipment::fromJson(obj);
            auto refund = QSharedPointer<Refund>::create(temp.getActivities());
            refund->setIsWrongIfConflict(temp.isWrongIfConflict());
            shipment = refund;
        } else {
            shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

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
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
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

    QString queryStr = "SELECT s.current_json, s.source_key, s.event_date, s.id, "
                       "COALESCE(o.is_ungrouped, 0) AS is_ungrouped, "
                       "COALESCE(o.customer_account, '') AS customer_account, "
                       "COALESCE(s.is_refund, 0) AS is_refund "
                       "FROM shipments s "
                       "LEFT JOIN orders o ON s.order_id = o.id "
                       "WHERE 1=1";
    if (dateFromInsertedDb.isValid()) {
        queryStr += QString(" AND s.inserted_at >= '%1'").arg(dateFromInsertedDb.toString(Qt::ISODate));
    }
    if (dateToInsertedDb.isValid()) {
        queryStr += QString(" AND s.inserted_at < '%1'").arg(dateToInsertedDb.addDays(1).toString(Qt::ISODate));
    }

    QSqlQuery query(m_db);
    query.exec(queryStr);
    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString id = query.value(3).toString();
        bool isGrouped = query.value(4).toInt() == 0;
        QString customerAccount = query.value(5).toString();
        bool isRefund = query.value(6).toInt() == 1;

        QStringList parts = sourceKey.split('|');
        QString channel = (parts.size() >= 2) ? parts[1] : "Unknown";
        QString subchannel = (parts.size() >= 3) ? parts[2] : "Unknown";

        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment;
        if (isRefund) {
            Shipment temp = Shipment::fromJson(obj);
            auto refund = QSharedPointer<Refund>::create(temp.getActivities());
            refund->setIsWrongIfConflict(temp.isWrongIfConflict());
            shipment = refund;
        } else {
            shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

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
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
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

    QString queryStr = "SELECT s.current_json, s.source_key, s.event_date, s.id, o.store, "
                       "COALESCE(o.is_ungrouped, 0) AS is_ungrouped, "
                       "COALESCE(o.customer_account, '') AS customer_account, "
                       "COALESCE(s.is_refund, 0) AS is_refund "
                       "FROM shipments s "
                       "LEFT JOIN orders o ON s.order_id = o.id "
                       "WHERE 1=1";

    if (dateFrom.isValid()) {
        queryStr += QString(" AND s.event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        // Use strict-less-than against the next day so that ISO datetime strings
        // stored as "YYYY-MM-DDThh:mm:ss" on the boundary day are included.
        // ("2025-12-31T00:00:00" <= "2025-12-31" is false; "< 2026-01-01" is true.)
        queryStr += QString(" AND s.event_date < '%1'").arg(dateTo.addDays(1).toString(Qt::ISODate));
    }

    QSqlQuery query(m_db);
    query.exec(queryStr);

    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString dateStr = query.value(2).toString();
        QString id = query.value(3).toString();
        QString store = query.value(4).toString();
        bool isGrouped = query.value(5).toInt() == 0;
        QString customerAccount = query.value(6).toString();
        bool isRefund = query.value(7).toInt() == 1;
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
        QSharedPointer<Shipment> shipment;
        if (isRefund) {
            Shipment temp = Shipment::fromJson(obj);
            auto refund = QSharedPointer<Refund>::create(temp.getActivities());
            refund->setIsWrongIfConflict(temp.isWrongIfConflict());
            shipment = refund;
        } else {
            shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

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
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
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

    QString queryStr = "SELECT s.current_json, s.source_key, s.event_date, s.id, "
                       "COALESCE(o.is_ungrouped, 0) AS is_ungrouped, "
                       "COALESCE(o.customer_account, '') AS customer_account, "
                       "COALESCE(s.is_refund, 0) AS is_refund "
                       "FROM shipments s "
                       "LEFT JOIN orders o ON s.order_id = o.id "
                       "WHERE 1=1";
    if (dateFrom.isValid()) {
        queryStr += QString(" AND s.event_date >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        queryStr += QString(" AND s.event_date < '%1'").arg(dateTo.addDays(1).toString(Qt::ISODate));
    }

    QSqlQuery query(m_db);
    query.exec(queryStr);
    while (query.next()) {
        QString jsonStr = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString id = query.value(3).toString();
        bool isGrouped = query.value(4).toInt() == 0;
        QString customerAccount = query.value(5).toString();
        bool isRefund = query.value(6).toInt() == 1;

        // Parse Source
        QStringList parts = sourceKey.split('|');
        QString channel = (parts.size() >= 2) ? parts[1] : "Unknown";
        QString subchannel = (parts.size() >= 3) ? parts[2] : "Unknown";

        // Parse Shipment
        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment;
        if (isRefund) {
            Shipment temp = Shipment::fromJson(obj);
            auto refund = QSharedPointer<Refund>::create(temp.getActivities());
            refund->setIsWrongIfConflict(temp.isWrongIfConflict());
            shipment = refund;
        } else {
            shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

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
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
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
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
    QFile::remove(m_filePathDb);
    initDb();
}

QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>>
OrderManager::get_channel_site_ShipmentAndRefundsNoInvoices(const QDate &dateFrom, const QDate &dateTo) const
{
    auto result = QSharedPointer<QHash<QString, QHash<QString, QHash<TaxResolver::TaxContext, OrderManager::ShipmentRefundsWithUpdates>>>>::create();

    // Phase 1: find distinct ORDER IDs that have at least one shipment/refund in the
    // requested date range that still needs an invoice.  We group by order_id so that
    // refunds (which have their own COALESCE(root_id,id) key but share the order_id
    // with the parent sale) are always fetched together with that parent sale in Phase 2.
    QString rootQueryStr = R"(
        SELECT DISTINCT s.order_id
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

    rootQueryStr += R"( AND (
        inv.shipment_root_id IS NULL
        OR inv.json NOT LIKE '%"invoiceNumber":%'
        OR inv.json LIKE '%"invoiceNumber":null%'
        OR s.id LIKE '%-rev-%'
        OR s.id LIKE '%-v-%'
    ))";

    QSqlQuery rootQuery(m_db);
    rootQuery.exec(rootQueryStr);
    QSet<QString> orderIdsToProcess;
    while (rootQuery.next()) {
        orderIdsToProcess.insert(rootQuery.value("order_id").toString());
    }

    if (orderIdsToProcess.isEmpty()) {
        return result;
    }

    // Phase 2: fetch ALL shipments for those orders (including the parent sale that may
    // already have an invoice — so it can be included in the group with invoicesToDo=false).
    QStringList quotedIds;
    for (const QString &id : orderIdsToProcess) {
        QString escaped = id;
        quotedIds << QString("'%1'").arg(escaped.replace('\'', "''"));
    }

    QString queryStr =
        "SELECT s.id, s.order_id, s.current_json, s.source_key, s.event_date, "
        "COALESCE(s.root_id, s.id) as root_id, "
        "inv.json as inv_json, "
        "o.address_json, "
        "COALESCE(o.is_ungrouped, 0) AS is_ungrouped, "
        "COALESCE(o.customer_account, '') AS customer_account "
        "FROM shipments s "
        "LEFT JOIN orders o ON s.order_id = o.id "
        "LEFT JOIN invoicing_infos inv ON COALESCE(s.root_id, s.id) = inv.shipment_root_id "
        "WHERE s.order_id IN ("
        + quotedIds.join(", ") + ") "
        // Sort orders before refunds within the same order_id so that the base
        // invoice number is always assigned to the sale and -R01/-R02 suffixes go
        // to the refunds/credit-notes, regardless of which has an earlier event_date.
        "ORDER BY s.order_id, s.is_refund, s.event_date";

    QSqlQuery query(m_db);
    query.exec(queryStr);

    // Group shipments by (order_id → channel, subchannel, TaxContext).
    // Using order_id as the meta-key ensures refunds and their parent sale always
    // land in the same (channel, store, ctx) bucket.
    struct OrderMeta { QString channel; QString subchannel; TaxResolver::TaxContext ctx; };
    QHash<QString, OrderMeta> orderMeta;
    // Per-order: first invJson that contains an invoice number (from the parent sale).
    QHash<QString, QString>   orderInvJson;
    QHash<QString, QString>   orderAddrJson;

    while (query.next()) {
        const QString id             = query.value("id").toString();
        const QString orderId        = query.value("order_id").toString();
        const QString jsonStr        = query.value("current_json").toString();
        const QString sourceKey      = query.value("source_key").toString();
        const QString invJson        = query.value("inv_json").toString();
        const QString addressJson    = query.value("address_json").toString();
        const bool    isGrouped      = query.value("is_ungrouped").toInt() == 0;
        const QString customerAccount = query.value("customer_account").toString();

        // Parse source → channel / subchannel.
        QStringList parts = sourceKey.split('|');
        const QString channel    = (parts.size() >= 2) ? parts[1] : QString();
        const QString subchannel = (parts.size() >= 3 && !parts[2].isEmpty()) ? parts[2] : QStringLiteral("Unknown");

        // Enforce the requirement: every no-invoice shipment must have a channel.
        if (channel.isEmpty()) {
            ExceptionWithTitleText ex(
                QObject::tr("Missing Channel / Site"),
                QObject::tr("Shipment '%1' has no channel or site information. "
                            "Cannot group no-invoice shipments without a valid channel.")
                    .arg(id));
            ex.raise();
        }

        // Parse and optionally negate (reversal) the shipment.
        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

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
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
        }

        if (shipment->getActivities().isEmpty()) continue;

        // Derive TaxContext from the first activity.
        const auto &act = shipment->getActivities().first();
        TaxResolver::TaxContext ctx;
        ctx.taxDeclaringCountryCode = act.getTaxDeclaringCountryCode();
        ctx.taxScheme               = act.getTaxScheme();
        ctx.taxJurisdictionLevel    = act.getTaxJurisdictionLevel();
        ctx.countryCodeVatPaidTo    = act.getCountryCodeVatPaidTo();

        // On first encounter for this order, record metadata and initialise the entry.
        if (!orderMeta.contains(orderId)) {
            orderMeta[orderId] = OrderMeta{channel, subchannel, ctx};
            orderInvJson[orderId] = QString(); // will be set when we find a sale with invoice
            orderAddrJson[orderId] = addressJson;

            auto &entry = (*result)[channel][subchannel][ctx];
            if (!addressJson.isEmpty()) {
                QJsonObject addrObj = QJsonDocument::fromJson(addressJson.toUtf8()).object();
                entry.addressTo = QSharedPointer<Address>::create(Address::fromJson(addrObj));
            }
        }

        // If this shipment has an invoice number and we haven't cached one yet for this
        // order, use it as the representative invoicingInfo for the group.
        if (!invJson.isEmpty() && orderInvJson[orderId].isEmpty()) {
            QJsonObject invObj = QJsonDocument::fromJson(invJson.toUtf8()).object();
            if (invObj.contains("invoiceNumber") && !invObj.value("invoiceNumber").isNull()) {
                orderInvJson[orderId] = invJson;
                const OrderMeta &meta = orderMeta[orderId];
                auto &entry = (*result)[meta.channel][meta.subchannel][meta.ctx];
                entry.invoicingInfo = QSharedPointer<InvoicingInfo>::create(InvoicingInfo::fromJson(invObj));
            }
        }

        // Determine whether this specific shipment needs an invoice.
        bool hasInvoice = false;
        if (!invJson.isEmpty()) {
            QJsonObject invObj = QJsonDocument::fromJson(invJson.toUtf8()).object();
            hasInvoice = invObj.contains("invoiceNumber") && !invObj.value("invoiceNumber").isNull();
        }
        bool isRevisionOrConflict = id.contains("-rev-") || id.contains("-v-");

        const OrderMeta &meta = orderMeta[orderId];
        auto &entry = (*result)[meta.channel][meta.subchannel][meta.ctx];
        entry.shipmentsRefundsSameActivity.append(shipment);
        entry.invoicesToDo.append(!hasInvoice || isRevisionOrConflict);
    }

    return result;
}


QSharedPointer<QList<OrderManager::ShipmentRefundsWithUpdates>>
OrderManager::getShipmentAndRefundsNoInvoices(const QDate &dateFrom, const QDate &dateTo) const
{
    auto results = QSharedPointer<QList<ShipmentRefundsWithUpdates>>::create();
    
    // Query all shipments for roots that have at least one shipment in the date range
    // that needs an invoice. A shipment needs an invoice if:
    // 1. It's in the date range AND
    // 2. Either: (no invoicing_info exists) OR (it's a revision/conflict: contains -rev- or -v-)
    
    // First, get all distinct order_ids that have at least one shipment in the date range
    // needing invoice work. Group by order_id so refunds and their parent sale are together.
    QString rootQueryStr = R"(
        SELECT DISTINCT s.order_id
        FROM shipments s
        LEFT JOIN invoicing_infos inv ON COALESCE(s.root_id, s.id) = inv.shipment_root_id
        WHERE s.order_id IS NOT NULL AND s.order_id != ''
    )";

    if (dateFrom.isValid()) {
        rootQueryStr += QString(" AND DATE(s.event_date) >= '%1'").arg(dateFrom.toString(Qt::ISODate));
    }
    if (dateTo.isValid()) {
        rootQueryStr += QString(" AND DATE(s.event_date) <= '%1'").arg(dateTo.toString(Qt::ISODate));
    }

    // We want orders where at least one shipment in range needs an invoice:
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
    QSet<QString> orderIdsToProcess;
    while (rootQuery.next()) {
        orderIdsToProcess.insert(rootQuery.value("order_id").toString());
    }

    if (orderIdsToProcess.isEmpty()) {
        return results;
    }

    // Now fetch ALL shipments for those orders (including history outside date range,
    // and including the parent sale that holds the original invoice number)
    // along with their invoicing info and address
    QString queryStr = R"(
        SELECT s.id, s.order_id, s.current_json, s.source_key, s.event_date,
               COALESCE(s.root_id, s.id) as root_id, o.address_json, inv.json as inv_json,
               COALESCE(o.is_ungrouped, 0) AS is_ungrouped,
               COALESCE(o.customer_account, '') AS customer_account,
               COALESCE(s.is_refund, 0) AS is_refund
        FROM shipments s
        LEFT JOIN orders o ON s.order_id = o.id
        LEFT JOIN invoicing_infos inv ON COALESCE(s.root_id, s.id) = inv.shipment_root_id
        WHERE s.order_id IN (
    )";

    // Build IN clause
    QStringList quotedIds;
    for (const QString &id : orderIdsToProcess) {
        quotedIds << QString("'%1'").arg(id);
    }
    queryStr += quotedIds.join(", ") + ")";
    queryStr += " ORDER BY s.order_id, s.is_refund, s.event_date";

    QSqlQuery query(m_db);
    query.exec(queryStr);

    // Group shipments by order_id so parent sale and all refunds are in the same group
    QHash<QString, ShipmentRefundsWithUpdates> groupedResults;
    QHash<QString, bool> orderHasInvJson; // Track if we've set invoicingInfo for this order

    while (query.next()) {
        QString id = query.value("id").toString();
        QString orderId = query.value("order_id").toString();
        QString jsonStr = query.value("current_json").toString();
        QString addressJson = query.value("address_json").toString();
        QString invJson = query.value("inv_json").toString();
        QString eventDateStr = query.value("event_date").toString();
        bool isGrouped = query.value("is_ungrouped").toInt() == 0;
        QString customerAccount = query.value("customer_account").toString();
        bool isRefund = query.value("is_refund").toInt() == 1;
        QDate eventDate = QDate::fromString(eventDateStr.left(10), Qt::ISODate);

        // Parse Shipment
        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment;
        if (isRefund) {
            Shipment temp = Shipment::fromJson(obj);
            auto refund = QSharedPointer<Refund>::create(temp.getActivities());
            refund->setIsWrongIfConflict(temp.isWrongIfConflict());
            shipment = refund;
        } else {
            shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

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
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
        }

        // Initialize group entry on first encounter
        if (!groupedResults.contains(orderId)) {
            ShipmentRefundsWithUpdates item;
            item.activityUpdate = QSharedPointer<ActivityUpdate>::create();

            // Parse address if available
            if (!addressJson.isEmpty()) {
                QJsonObject addrObj = QJsonDocument::fromJson(addressJson.toUtf8()).object();
                item.addressTo = QSharedPointer<Address>::create(Address::fromJson(addrObj));
            }

            groupedResults[orderId] = item;
            orderHasInvJson[orderId] = false;
        }

        // Cache the first invoice number found for this order (parent sale comes first due to ORDER BY event_date)
        if (!orderHasInvJson[orderId] && !invJson.isEmpty()) {
            QJsonObject invObj = QJsonDocument::fromJson(invJson.toUtf8()).object();
            if (invObj.contains("invoiceNumber") && !invObj.value("invoiceNumber").isNull()) {
                groupedResults[orderId].invoicingInfo = QSharedPointer<InvoicingInfo>::create(InvoicingInfo::fromJson(invObj));
                orderHasInvJson[orderId] = true;
            }
        }

        groupedResults[orderId].shipmentsRefundsSameActivity.append(shipment);

        // Determine if THIS specific shipment needs an invoice
        // (check its own inv_json, not the group's)
        bool inDateRange = true;
        if (dateFrom.isValid() && eventDate < dateFrom) {
            inDateRange = false;
        }
        if (dateTo.isValid() && eventDate > dateTo) {
            inDateRange = false;
        }

        bool isRevisionOrConflict = id.contains("-rev-") || id.contains("-v-");

        bool hasInvoice = false;
        if (!invJson.isEmpty()) {
            QJsonObject invObj = QJsonDocument::fromJson(invJson.toUtf8()).object();
            hasInvoice = invObj.contains("invoiceNumber") && !invObj.value("invoiceNumber").isNull();
        }

        // Needs invoice if in range AND (this shipment has no invoice OR it's a revision/conflict)
        bool needsInvoice = inDateRange && (!hasInvoice || isRevisionOrConflict);
        groupedResults[orderId].invoicesToDo.append(needsInvoice);
    }

    // Convert hash to list
    for (auto it = groupedResults.begin(); it != groupedResults.end(); ++it) {
        results->append(it.value());
    }
    
    return results;
}

QMultiMap<QDateTime, QSharedPointer<Shipment>> OrderManager::getShipmentAndRefundsRecentlyAdded(
        const QDate &minDateAdded) const
{
    QMultiMap<QDateTime, QSharedPointer<Shipment>> results;

    QString queryStr = "SELECT s.current_json, s.source_key, s.event_date, s.id, "
                       "COALESCE(o.is_ungrouped, 0) AS is_ungrouped, "
                       "COALESCE(o.customer_account, '') AS customer_account, "
                       "COALESCE(s.is_refund, 0) AS is_refund "
                       "FROM shipments s "
                       "LEFT JOIN orders o ON s.order_id = o.id "
                       "WHERE 1=1";
    if (minDateAdded.isValid())
        queryStr += QString(" AND s.inserted_at >= '%1'").arg(minDateAdded.toString(Qt::ISODate));

    QSqlQuery query(m_db);
    query.exec(queryStr);

    while (query.next()) {
        QString jsonStr   = query.value(0).toString();
        QString sourceKey = query.value(1).toString();
        QString dateStr   = query.value(2).toString();
        QString id        = query.value(3).toString();
        bool isGrouped    = query.value(4).toInt() == 0;
        QString customerAccount = query.value(5).toString();
        bool isRefund = query.value(6).toInt() == 1;
        QDateTime eventDate = QDateTime::fromString(dateStr, Qt::ISODate);

        QJsonObject obj = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
        QSharedPointer<Shipment> shipment;
        if (isRefund) {
            Shipment temp = Shipment::fromJson(obj);
            auto refund = QSharedPointer<Refund>::create(temp.getActivities());
            refund->setIsWrongIfConflict(temp.isWrongIfConflict());
            shipment = refund;
        } else {
            shipment = QSharedPointer<Shipment>::create(Shipment::fromJson(obj));
        }
        shipment->setIsGrouped(isGrouped);
        shipment->setCustomerAccount(customerAccount);

        if (id.contains("-rev-")) {
            QList<Activity> newActs;
            for (const auto &act : shipment->getActivities()) {
                Amount negatedAmount(-act.getAmountTaxed(), -act.getAmountTaxesSource());
                auto res = Activity::create(
                    act.getEventId(), act.getActivityId(), act.getSubActivityId(),
                    act.getDateTime(), act.getDateTimeTax(), act.getCurrency(),
                    act.getCountryCodeFrom(), act.getCountryCodeTo(), act.getIsCompany(),
                    act.getCountryCodeVatPaidTo(), negatedAmount,
                    act.getTaxSource(), act.getTaxDeclaringCountryCode(),
                    act.getTaxScheme(), act.getTaxJurisdictionLevel(),
                    act.getSaleType(), act.getVatTerritoryFrom(), act.getVatTerritoryTo());
                if (res.value) {
                    newActs.append(*res.value);
                }
            }
            shipment = QSharedPointer<Shipment>::create(Shipment(newActs, customerAccount, isGrouped));
        }

        results.insert(eventDate, shipment);
    }

    return results;
}


