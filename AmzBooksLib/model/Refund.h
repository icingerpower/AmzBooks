#ifndef REFUND_H
#define REFUND_H

#include "Shipment.h"

class Refund : public Shipment
{
public:
    Refund(Activity activity);
};

#endif // REFUND_H
