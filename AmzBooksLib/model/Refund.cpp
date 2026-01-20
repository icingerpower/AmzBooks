#include "Refund.h"


Refund::Refund(Activity activity, QList<LineItem> items)
    : Shipment(activity, items)
{

}

Refund::Refund(Activity activity)
    : Shipment(activity)
{

}
