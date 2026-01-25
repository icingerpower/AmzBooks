#ifndef REFUND_H
#define REFUND_H

#include "Shipment.h"

class Refund : public Shipment
{
public:
    Refund(QList<Activity> activities);
};

#endif // REFUND_H
