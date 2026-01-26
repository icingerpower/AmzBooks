#include "InvoicingInfo.h"
#include "Shipment.h"
#include <QJsonArray>
#include <QJsonObject>

InvoicingInfo::InvoicingInfo(
        const Shipment *shipmentOrRefund
        , QList<LineItem> invoiceLineItems
        , std::optional<QString> invoiceNumber
        , std::optional<QString> invoiceLink)
    : m_items(invoiceLineItems)
    , m_invoiceNumber(invoiceNumber)
    , m_invoiceLink(invoiceLink)
{
    // Perform adjustment
    if (shipmentOrRefund) {
        adjustItemTaxes(shipmentOrRefund->getActivities());
    }
}

bool InvoicingInfo::isInvoiceDone() const
{
    return !m_invoiceNumber->isEmpty();
}

void InvoicingInfo::adjustItemTaxes(const QList<Activity> &activities)
{
    if (!m_items.isEmpty()) {
        double totalItemTaxes = 0.0;
        for (const auto& item : m_items) {
            totalItemTaxes += item.getTotalTaxes();
        }

        double activityTaxes = 0.0;
        for (const auto &act : activities) {
            activityTaxes += act.getAmountTaxes();
        }
        
        double delta = activityTaxes - totalItemTaxes;

        if (qAbs(delta) > 0.0001) {// Using a small epsilon
            // Check if the adjustment on the first item (per unit) would be too large
            LineItem& firstItem = m_items[0];
            double firstTotalQty = firstItem.getQuantity(); // double conversion implied or checked

            // Avoid division by zero
            if (firstTotalQty == 0) {
                return;
            }

            double deltaPerUnitFirst = delta / firstTotalQty;

            if (qAbs(deltaPerUnitFirst) > 0.015) {
                // Spread evenly across all items based on quantity (per-unit adjustment)
                double totalQty = 0.0;
                for (const auto& item : m_items) {
                    totalQty += item.getQuantity();
                }

                if (totalQty > 0) {
                    double perUnitAdjustment = delta / totalQty;
                    for (auto& item : m_items) {
                        // Each item's unit tax is adjusted by perUnitAdjustment
                        item.adjustTaxes(perUnitAdjustment);
                    }
                }
            } else {
                // Small delta, just dump it on the first item
                firstItem.adjustTaxes(deltaPerUnitFirst);
            }
        }
    }
}

void InvoicingInfo::setItems(const QList<Activity> &activities, const QList<LineItem> &items)
{
    m_items = items;
    adjustItemTaxes(activities);
}

const QList<LineItem> &InvoicingInfo::getItems() const
{
    return m_items;
}

std::optional<QString> InvoicingInfo::getInvoiceNumber() const
{
    return m_invoiceNumber;
}

std::optional<QString> InvoicingInfo::getInvoiceLink() const
{
    return m_invoiceLink;
}

QJsonObject InvoicingInfo::toJson() const
{
    QJsonObject json;
    if (m_invoiceNumber) json["invoiceNumber"] = *m_invoiceNumber;
    if (m_invoiceLink) json["invoiceLink"] = *m_invoiceLink;
    
    QJsonArray itemsArr;
    for (const auto &item : m_items) {
        itemsArr.append(item.toJson());
    }
    json["items"] = itemsArr;
    return json;
}

InvoicingInfo InvoicingInfo::fromJson(const QJsonObject &json)
{
    QList<LineItem> items;
    if (json.contains("items")) {
        QJsonArray arr = json["items"].toArray();
        for (const auto &val : arr) {
            items.append(LineItem::fromJson(val.toObject()));
        }
    }
    
    std::optional<QString> number;
    if (json.contains("invoiceNumber")) number = json["invoiceNumber"].toString();
    
    std::optional<QString> link;
    if (json.contains("invoiceLink")) link = json["invoiceLink"].toString();
    
    // Create with null shipment, we just hold data
    return InvoicingInfo(nullptr, items, number, link);
}
