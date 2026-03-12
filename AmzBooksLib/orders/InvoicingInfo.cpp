#include "InvoicingInfo.h"
#include "Shipment.h"
#include <QJsonArray>
#include <QJsonObject>

Result<InvoicingInfo> InvoicingInfo::create(
        const Shipment *shipmentOrRefund
        , QList<LineItem> invoiceLineItems
        , std::optional<QString> invoiceNumber
        , std::optional<QString> invoiceLink
        , std::optional<QDate> paymentDate)
{
    // Validation: At least one value needs to be provided to avoid an error
    bool hasItems = !invoiceLineItems.isEmpty();
    bool hasNumber = invoiceNumber.has_value() && !invoiceNumber->isEmpty();
    bool hasLink = invoiceLink.has_value() && !invoiceLink->isEmpty();

    if (!hasItems && !hasNumber && !hasLink) {
        return {std::nullopt, {{"", "InvoicingInfo must have at least one of: invoiceLineItems, invoiceNumber, or invoiceLink."}}};
    }

    return {InvoicingInfo(shipmentOrRefund, invoiceLineItems, invoiceNumber, invoiceLink, paymentDate), {}};
}

InvoicingInfo::InvoicingInfo(
        const Shipment *shipmentOrRefund
        , QList<LineItem> invoiceLineItems
        , std::optional<QString> invoiceNumber
        , std::optional<QString> invoiceLink
        , std::optional<QDate> paymentDate)
    : m_items(invoiceLineItems)
    , m_invoiceNumber(invoiceNumber)
    , m_invoiceLink(invoiceLink)
    , m_paymentDate(paymentDate)
{
    // Perform adjustment
    if (shipmentOrRefund) {
        adjustItemTaxes(shipmentOrRefund->getActivities());
    }
}

bool InvoicingInfo::isInvoiceDone() const
{
    return m_invoiceNumber.has_value() && !m_invoiceNumber->isEmpty();
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

void InvoicingInfo::setInvoiceNumber(const QString &invoiceNumber)
{
    m_invoiceNumber = invoiceNumber;
}

std::optional<QString> InvoicingInfo::getInvoiceLink() const
{
    return m_invoiceLink;
}

void InvoicingInfo::setItemName(int index, const QString &name)
{
    if (index >= 0 && index < m_items.size())
        m_items[index].setName(name);
}

void InvoicingInfo::setPaymentDate(const std::optional<QDate> &date)
{
    m_paymentDate = date;
}

QDate InvoicingInfo::getPaymentDate(const QDate &orderDate) const
{
    return m_paymentDate.value_or(orderDate);
}

QJsonObject InvoicingInfo::toJson() const
{
    QJsonObject json;
    if (m_invoiceNumber) json["invoiceNumber"] = *m_invoiceNumber;
    if (m_invoiceLink) json["invoiceLink"] = *m_invoiceLink;
    if (m_paymentDate) json["paymentDate"] = m_paymentDate->toString(Qt::ISODate);
    if (m_vatOnPayment) json["vatOnPayment"] = true;
    if (!m_reference.isEmpty()) json["reference"] = m_reference;

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
    
    std::optional<QDate> paymentDate;
    if (json.contains("paymentDate")) {
        paymentDate = QDate::fromString(json["paymentDate"].toString(), Qt::ISODate);
    }
    
    bool vatOnPayment = false;
    if (json.contains("vatOnPayment")) vatOnPayment = json["vatOnPayment"].toBool();

    QString reference;
    if (json.contains("reference")) {
        reference = json["reference"].toString();
    }

    // Create with null shipment, we just hold data
    InvoicingInfo info(nullptr, items, number, link, paymentDate);
    info.m_vatOnPayment = vatOnPayment;
    info.m_reference = reference;
    return info;
}
