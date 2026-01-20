#ifndef ORDER_H
#define ORDER_H

#include <optional>
#include <QString>
#include <QMap>
#include <QDateTime>

#include "Address.h"

class Shipment;
class Refund;

class Order
{
public:
    Order(QString orderId);
    const QString &id() const noexcept;

    void addShipment(const Shipment *shipment);
    void addRefund(const Refund *shipment);
    void setAddressTo(Address addressTo) noexcept;

    const QMultiMap<QDateTime, const Shipment *> &activities() const noexcept;
    const std::optional<Address> &addressTo() const noexcept;

protected:
    QMultiMap<QDateTime, const Shipment *> m_activities;
    std::optional<Address> m_addressTo;
    QString m_id;
};

#endif // ORDER_H
