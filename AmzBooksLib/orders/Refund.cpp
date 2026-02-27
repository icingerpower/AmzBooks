#include "Refund.h"


Refund::Refund(QList<Activity> activities, QString customerAccount, bool isGrouped)
    : Shipment(std::move(activities), std::move(customerAccount), isGrouped)
{
}
