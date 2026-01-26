#include "Order.h"
#include "Shipment.h"
#include "Refund.h"

Order::Order(QString orderId, QString store)
    : m_id(std::move(orderId))
{

}

const QString &Order::id() const noexcept
{
    return m_id;
}

void Order::addShipment(const Shipment *shipment)
{
    if (shipment && !shipment->getActivities().isEmpty()) {
        m_activities.insert(shipment->getActivities().first().getDateTime(), shipment);
    }
}

void Order::addRefund(const Refund *refund)
{
    if (refund && !refund->getActivities().isEmpty()) {
        m_activities.insert(refund->getActivities().first().getDateTime(), refund);
    }
}

void Order::setAddressTo(Address addressTo) noexcept
{
    m_addressTo = std::move(addressTo);
}

const QMultiMap<QDateTime, const Shipment *> &Order::activities() const noexcept
{
    return m_activities;
}

const std::optional<Address> &Order::addressTo() const noexcept
{
    return m_addressTo;
}
