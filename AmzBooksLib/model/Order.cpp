#include "Order.h"
#include "Shipment.h"
#include "Refund.h"

Order::Order(QString orderId)
    : m_id(std::move(orderId))
{

}

const QString &Order::id() const noexcept
{
    return m_id;
}

void Order::addShipment(const Shipment *shipment)
{
    if (shipment) {
        m_activities.insert(shipment->getActivity().getDateTime(), shipment);
    }
}

void Order::addRefund(const Refund *refund)
{
    if (refund) {
        m_activities.insert(refund->getActivity().getDateTime(), refund);
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
