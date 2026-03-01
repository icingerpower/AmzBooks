#ifndef INVOICINGINFO_H
#define INVOICINGINFO_H

#include "LineItem.h"
#include "books/Activity.h"
#include <QDate>

class Shipment;
class Activity;

class InvoicingInfo
{
public:
    static Result<InvoicingInfo> create(const Shipment *shipmentOrRefund
                  , QList<LineItem> invoiceLineItems = {}
                  , std::optional<QString> invoiceNumber = std::nullopt
                  , std::optional<QString> invoiceLink = std::nullopt
                  , std::optional<QDate> paymentDate = std::nullopt);

    bool isInvoiceDone() const;
    void setItems(const QList<Activity> &activities, const QList<LineItem> &items);
    const QList<LineItem> &getItems() const;
    std::optional<QString> getInvoiceNumber() const;
    void setInvoiceNumber(const QString &invoiceNumber);
    std::optional<QString> getInvoiceLink() const;

    bool getVatOnPayment() const { return m_vatOnPayment; }
    void setVatOnPayment(bool vatOnPayment) { m_vatOnPayment = vatOnPayment; }

    /// Renames the line item at position index without touching its amounts.
    void setItemName(int index, const QString &name);

    /// Replaces the optional payment date (nullopt = instant / no deferral).
    void setPaymentDate(const std::optional<QDate> &date);

    /// Returns the payment date. If not set, returns the orderDate as default (instant payment).
    QDate getPaymentDate(const QDate &orderDate) const;

    QJsonObject toJson() const;
    static InvoicingInfo fromJson(const QJsonObject &json);

private:
    InvoicingInfo(const Shipment *shipmentOrRefund
                  , QList<LineItem> invoiceLineItems = {}
                  , std::optional<QString> invoiceNumber = std::nullopt
                  , std::optional<QString> invoiceLink = std::nullopt
                  , std::optional<QDate> paymentDate = std::nullopt);

    void adjustItemTaxes(const QList<Activity> &activities);
    QList<LineItem> m_items;
    std::optional<QString> m_invoiceNumber;
    std::optional<QString> m_invoiceLink;
    std::optional<QDate> m_paymentDate;
    bool m_vatOnPayment = false;
};

#endif // INVOICINGINFO_H

