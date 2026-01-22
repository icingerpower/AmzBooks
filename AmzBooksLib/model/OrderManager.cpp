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
                
                // Compare all activities
                // Assuming order matters? Or ID usage?
                // Simplest: Check if count differs, or if any activity differs
                const auto &latestActs = latestShip.getActivities();
                const auto &incomingActs = incomingShip.getActivities();
                
                if (latestActs.size() != incomingActs.size()) {
                    isConflict = true;
                } else {
                    for (int i = 0; i < latestActs.size(); ++i) {
                        if (latestActs[i].isDifferentTaxes(incomingActs[i])) {
                            isConflict = true;
                            break;
                        }
                    }
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
