#ifndef ORDERMANAGER_SQL_SCHEMA_H
#define ORDERMANAGER_SQL_SCHEMA_H

#include <QString>

namespace OrderManagerSql {

const QString CREATE_TABLE_ORDERS = R"(
    CREATE TABLE If NOT EXISTS orders (
        id TEXT PRIMARY KEY,
        address_json TEXT,
        store TEXT
    )
)";

const QString CREATE_TABLE_SHIPMENTS = R"(
    CREATE TABLE IF NOT EXISTS shipments (
        id TEXT PRIMARY KEY,
        order_id TEXT NOT NULL,
        status TEXT NOT NULL, -- 'Draft', 'Published'
        original_json TEXT NOT NULL,
        current_json TEXT NOT NULL,
        published_json TEXT, -- NULL if not yet published
        publication_date TEXT, -- ISO8601 string or NULL
        event_date TEXT, -- ISO8601 string (Activity date)
        source_key TEXT, -- For querying history by source
        root_id TEXT,    -- ID of the original/root shipment if this is a revision/correction
        FOREIGN KEY(order_id) REFERENCES orders(id)
    )
)";

const QString CREATE_TABLE_FINANCIAL_EVENTS = R"(
    CREATE TABLE IF NOT EXISTS financial_events (
        id TEXT PRIMARY KEY, -- Invoice Number
        shipment_id TEXT NOT NULL,
        type TEXT NOT NULL, -- 'Invoice' or 'CreditNote'
        event_date TEXT NOT NULL,
        amount REAL NOT NULL,
        currency TEXT NOT NULL,
        content_json TEXT NOT NULL, -- Snapshot of what generated this event
        FOREIGN KEY(shipment_id) REFERENCES shipments(id)
    )
)";

const QString CREATE_TABLE_INVOICING_INFOS = R"(
    CREATE TABLE IF NOT EXISTS invoicing_infos (
        shipment_root_id TEXT PRIMARY KEY,
        json TEXT NOT NULL
    )
)";

} // namespace OrderManagerSql

#endif // ORDERMANAGER_SQL_SCHEMA_H
