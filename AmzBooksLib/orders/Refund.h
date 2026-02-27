#ifndef REFUND_H
#define REFUND_H

#include "Shipment.h"

class Refund : public Shipment
{
public:
    explicit Refund(QList<Activity> activities, QString customerAccount = QString(), bool isGrouped = true);
};

#endif // REFUND_H
