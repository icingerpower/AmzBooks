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

    void setItems(const Activity *activity,const QList<LineItem> &items);
    const QList<LineItem> &getItems() const;

private:
    void adjustItemTaxes(const Activity *activity);
    QList<LineItem> m_items;
    std::optional<QString> m_invoiceNumber;
    std::optional<QString> m_invoiceLink;
};

#endif // INVOICINGINFO_H
