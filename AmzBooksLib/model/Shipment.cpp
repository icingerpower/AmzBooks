#include "Shipment.h"

Shipment::Shipment(Activity activity)
    : m_activity(std::move(activity))
{
}

const QString &Shipment::getId() const noexcept
{
    return m_activity.getActivityId();
}

const Activity& Shipment::getActivity() const noexcept
{
    return m_activity;
}

