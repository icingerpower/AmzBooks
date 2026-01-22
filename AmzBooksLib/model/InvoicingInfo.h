#ifndef INVOICINGINFO_H
#define INVOICINGINFO_H

#include "LineItem.h"
#include "Activity.h"

class Shipment;
class Activity;

class InvoicingInfo
{
public:
    InvoicingInfo(const Shipment *shipmentOrRefund
                  , QList<LineItem> invoiceLineItems = {}
                  , std::optional<QString> invoiceNumber = std::nullopt
                  , std::optional<QString> invoiceLink = std::nullopt);

    void setItems(const QList<Activity> &activities, const QList<LineItem> &items);
    const QList<LineItem> &getItems() const;
    std::optional<QString> getInvoiceNumber() const;
    std::optional<QString> getInvoiceLink() const;

    QJsonObject toJson() const;
    static InvoicingInfo fromJson(const QJsonObject &json);

private:
    void adjustItemTaxes(const QList<Activity> &activities);
    QList<LineItem> m_items;
    std::optional<QString> m_invoiceNumber;
    std::optional<QString> m_invoiceLink;
};

#endif // INVOICINGINFO_H
